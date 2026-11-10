#include "telegram/album_delivery_worker.hpp"
#include "bridge_bot_failures.hpp"
#include "bridge_command_operations.hpp"
#include <common/logger.hpp>
#include <exception>

namespace bridge::telegram {
namespace {
void warn_retained(const char *message) noexcept {
  try {
    OBCX_WARN("{}", message);
  } catch (...) {
  }
}
} // namespace
AlbumDeliveryWorker::AlbumDeliveryWorker(
    boost::asio::any_io_executor executor,
    std::shared_ptr<OrdinaryForwarder> forwarder,
    std::shared_ptr<BridgeCommandOperations> operations,
    std::shared_ptr<const obcx::bot::ConversationResolver> references,
    std::shared_ptr<Lifecycle> lifecycle,
    std::shared_ptr<obcx::core::ActorEventPublisher> publisher)
    : executor_(Lifecycle::make_serial_executor(std::move(executor))),
      buffer_(std::make_shared<TGMediaGroupBuffer>(executor_)),
      forwarder_(std::move(forwarder)), operations_(std::move(operations)),
      references_(std::move(references)), lifecycle_(std::move(lifecycle)),
      publisher_(std::move(publisher)) {
  if (!forwarder_ || !operations_ || !references_ || !lifecycle_ || !publisher_)
    throw std::invalid_argument("album owner requires generation services");
}
auto AlbumDeliveryWorker::prepare(
    boost::asio::any_io_executor executor,
    std::shared_ptr<OrdinaryForwarder> forwarder,
    std::shared_ptr<BridgeCommandOperations> operations,
    std::shared_ptr<const obcx::bot::ConversationResolver> references,
    std::shared_ptr<Lifecycle> lifecycle,
    std::shared_ptr<obcx::core::ActorEventPublisher> publisher)
    -> std::shared_ptr<AlbumDeliveryWorker> {
  auto self = std::shared_ptr<AlbumDeliveryWorker>(new AlbumDeliveryWorker(
      std::move(executor), std::move(forwarder), std::move(operations),
      std::move(references), lifecycle, std::move(publisher)));
  std::weak_ptr<AlbumDeliveryWorker> weak = self;
  lifecycle->subscribe(
      [weak](Lifecycle::TokenPtr token) noexcept {
        if (auto current = weak.lock())
          current->start(std::move(token));
      },
      [weak]() noexcept {
        if (auto current = weak.lock())
          current->stop();
      });
  return self;
}
void AlbumDeliveryWorker::start(Lifecycle::TokenPtr token) noexcept {
  std::scoped_lock lock(mutex_);
  token_ = std::move(token);
}
void AlbumDeliveryWorker::enqueue(BufferedTelegramMessage message) {
  message.validate(*operations_);
  if (!message.source().media_group)
    throw std::invalid_argument(
        "typed album requires original stored source and publication");
  std::scoped_lock lock(mutex_);
  if (!token_ || !token_->valid())
    throw std::runtime_error("album generation is not accepting members");
  auto lease = lifecycle_->acquire_work(token_);
  if (!lease)
    throw std::runtime_error("album generation admission is closed");
  auto self = shared_from_this();
  auto token = token_;
  buffer_->add(
      operations_->telegram_installation().installation_id, std::move(message),
      [self, token, lease](std::vector<BufferedTelegramMessage> messages) {
        // This is already-admitted logical work, including a shutdown flush.
        // Its existing lease covers coroutine and completion destruction; do
        // not acquire a new generation route after invalidation.
        Lifecycle::spawn_owned(
            self->executor_, lease, flush(self, std::move(messages), token),
            [self](std::exception_ptr error) {
              if (!error)
                return;
              try {
                std::rethrow_exception(error);
              } catch (const std::exception &cause) {
                try {
                  // Never emit arbitrary exception/provider prose: it may
                  // contain locators or credentials. Acquisition omissions
                  // have their own bounded member-level diagnostic.
                  const auto *typed =
                      dynamic_cast<const BridgeBotOperationFailure *>(&cause);
                  OBCX_WARN("Telegram album forwarding failed: error={}; "
                            "batch will not be automatically replayed",
                            typed
                                ? obcx::bot::error_code_id(typed->error().code)
                                : "unclassified_exception");
                } catch (...) {
                  // Diagnostics must not escape the completion handler.
                }
              } catch (...) {
                warn_retained("Telegram album forwarding failed: unknown "
                              "exception; batch will not be automatically "
                              "replayed");
              }
            },
            boost::asio::cancellation_slot{});
      },
      lease);
}
void AlbumDeliveryWorker::stop() noexcept {
  {
    std::scoped_lock lock(mutex_);
    token_.reset();
  }
  try {
    buffer_->flush_all_now();
  } catch (...) {
    try {
      OBCX_ERROR("Typed album stop could not flush accepted work");
    } catch (...) {
      // A diagnostic failure must not escape this noexcept lifecycle callback.
    }
  }
}
auto AlbumDeliveryWorker::flush(std::shared_ptr<AlbumDeliveryWorker> self,
                                std::vector<BufferedTelegramMessage> messages,
                                Lifecycle::TokenPtr token)
    -> boost::asio::awaitable<void> {
  std::vector<obcx::message_store::events::MessageStored> stored;
  std::vector<BridgeDeliveryPublication> publications;
  stored.reserve(messages.size());
  publications.reserve(messages.size());
  for (auto &message : messages) {
    message.validate(*self->operations_);
    stored.push_back(std::move(message.stored));
    publications.push_back(std::move(message.publication));
  }
  BridgeForwardResult result;
  std::optional<obcx::core::MessageEnvelope> failure_notification;
  try {
    result = co_await self->forwarder_->forward_album(std::move(stored),
                                                      std::move(publications));
  } catch (const BridgeDeliveryPersistenceFailure &error) {
    failure_notification = bridge_delivery_failure_notification(error);
  }
  if (failure_notification) {
    // Preserve returned output evidence on the existing failure channel. A
    // refused/throwing publication never grants another provider invocation.
    if (!co_await self->publisher_->publish(std::move(token),
                                            std::move(*failure_notification)))
      warn_retained("Album failure notification was not accepted; no resubmit");
    co_return;
  }
  if (!result.bot_delivery)
    co_return;
  if (result.bot_delivery->newly_completed) {
    // A retired route can refuse publication. Complete stays durable and must
    // never be re-sent to recover a notification.
    const auto published = co_await publish_bridge_delivery(
        self->publisher_, std::move(token), std::move(*result.bot_delivery));
    if (!published)
      warn_retained(
          "Album completed without notification acceptance; no resubmit");
  } else if (result.bot_delivery->persisted.state !=
             ledger::DeliveryState::Complete) {
    warn_retained(
        "Typed album is incomplete or uncertain; durable delivery retained");
  }
}
} // namespace bridge::telegram
