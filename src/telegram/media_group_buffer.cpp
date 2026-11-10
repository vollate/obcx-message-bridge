#include "telegram/media_group_buffer.hpp"
#include "bridge_command_operations.hpp"

#include <algorithm>
#include <boost/system/error_code.hpp>
#include <common/logger.hpp>
#include <core/bot/bot_codec.hpp>
#include <utility>

namespace bridge::telegram {
namespace {
void report_flush_failure(const char *message) noexcept {
  try {
    OBCX_ERROR("{}", message);
  } catch (...) {
    // Diagnostics must not discard another accepted batch or escape teardown.
  }
}
} // namespace

void BufferedTelegramMessage::validate(
    const BridgeCommandOperations &operations) const {
  source().validate();
  stored.source.validate(stored.message);
  if (source().message.conversation.installation !=
          operations.telegram_installation() ||
      stored.source.source_platform != "telegram" ||
      stored.source.source_bot !=
          operations.telegram_installation().installation_id)
    throw std::invalid_argument("Telegram original source carrier mismatch");
  if (publication.event_id.empty() ||
      publication.source_bot != stored.source.source_bot)
    throw std::invalid_argument("Telegram publication carrier mismatch");
}

TGMediaGroupBuffer::TGMediaGroupBuffer(boost::asio::any_io_executor executor)
    : executor_(std::move(executor)) {}

TGMediaGroupBuffer::~TGMediaGroupBuffer() {
  try {
    // Owners explicitly flush accepted batches during stop. Destruction only
    // cancels waits; it must never create provider work.
    std::scoped_lock lock(mutex_);
    for (auto &[key, group] : groups_) {
      (void)key;
      if (group.timer)
        group.timer->cancel();
    }
    groups_.clear();
  } catch (...) {
    // Destructors cannot propagate failures.
  }
}

void TGMediaGroupBuffer::add(
    std::string source_installation, BufferedTelegramMessage message,
    FlushFn flush_fn, obcx::core::ActorGenerationLifecycle::WorkLease lease) {
  if (!lease)
    throw std::invalid_argument(
        "album timer requires a generation drain lease");
  message.source().validate();
  const auto &installation = message.source().message.conversation.installation;
  if (installation.installation_id != source_installation ||
      installation.surface != obcx::bot::SurfaceId{"telegram.bot_api"})
    throw std::invalid_argument("Telegram album source installation mismatch");
  if (!message.source().media_group) {
    if (flush_fn) {
      std::vector<BufferedTelegramMessage> single;
      single.push_back(std::move(message));
      flush_fn(std::move(single));
    }
    return;
  }
  GroupKey key = obcx::bot::Json(*message.source().media_group).dump();
  std::scoped_lock lock(mutex_);
  auto &group = groups_[key];
  if (std::ranges::any_of(group.events, [&](const auto &existing) {
        return existing.source().message == message.source().message;
      }))
    return;
  group.events.push_back(std::move(message));
  group.flush_fn = std::move(flush_fn);
  if (!group.timer)
    group.timer = std::make_unique<boost::asio::steady_timer>(executor_);
  group.timer->expires_after(kDebounceWindow);
  // A success handler may already be queued when expires_after cancels its
  // predecessor. A unique arm token also fences erase/recreate of the same key.
  auto generation = std::make_shared<const bool>(false);
  group.timer_generation = generation;
  std::weak_ptr<TGMediaGroupBuffer> weak_self = weak_from_this();
  obcx::core::ActorGenerationLifecycle::wait_owned(
      *group.timer, std::move(lease),
      [weak_self, key, generation](const boost::system::error_code &ec) {
        // The core-owned outer handler retains the drain pin through
        // destruction of this DSO-owned callback, including cancelled arms.
        if (ec == boost::asio::error::operation_aborted)
          return;
        auto self = weak_self.lock();
        if (self)
          self->on_timer_fired_(key, generation);
      });
}

void TGMediaGroupBuffer::on_timer_fired_(
    GroupKey key, std::shared_ptr<const bool> generation) {
  std::vector<BufferedTelegramMessage> events;
  FlushFn fn;
  {
    std::scoped_lock lock(mutex_);
    auto it = groups_.find(key);
    if (it == groups_.end() || it->second.timer_generation != generation)
      return;
    events = std::move(it->second.events);
    fn = std::move(it->second.flush_fn);
    groups_.erase(it);
  }
  if (fn && !events.empty()) {
    try {
      fn(std::move(events));
    } catch (...) {
      report_flush_failure("Media group flush callback failed; no replay");
    }
  }
}

void TGMediaGroupBuffer::flush_all_now() {
  // Transfer complete groups without allocating a second batch collection.
  // Removing their keys also fences already-queued timer completions. Each
  // group's callback retains its original drain lease through its own flush.
  decltype(groups_) pending;
  {
    std::scoped_lock lock(mutex_);
    pending.swap(groups_);
  }
  if (!pending.empty()) {
    try {
      OBCX_INFO("Flushing {} pending Telegram media groups synchronously",
                pending.size());
    } catch (...) {
      // Logging cannot prevent accepted work from reaching its flush callback.
    }
  }
  for (auto &[key, group] : pending) {
    (void)key;
    if (group.timer) {
      try {
        group.timer->cancel();
      } catch (...) {
        report_flush_failure("Media group timer cancellation failed");
      }
    }
    if (group.flush_fn && !group.events.empty()) {
      try {
        group.flush_fn(std::move(group.events));
      } catch (...) {
        report_flush_failure(
            "Media group sync flush callback failed; no replay");
      }
    }
  }
}
} // namespace bridge::telegram
