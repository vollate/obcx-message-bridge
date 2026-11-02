#pragma once

#include "bridge_delivery_sender.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <core/actor/actor_messages.hpp>
#include <core/bot/domain_events.hpp>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace bridge {
class BridgeCommandOperations;
}

namespace bridge::telegram {

// The actual notification and publication identity. No reconstructed native
// MessageEvent or second copy of its source can diverge across suspension.
struct BufferedTelegramMessage {
  obcx::message_store::events::MessageStored stored;
  BridgeDeliveryPublication publication;
  [[nodiscard]] auto source() const -> const obcx::bot::MessageReceived & {
    return stored.message;
  }
  void validate(const BridgeCommandOperations &) const;
};

// Thread-safe debounce buffer keyed by the whole scoped MediaGroupRef. A flush
// is an observed batch, not proof that all native album members have arrived.
class TGMediaGroupBuffer
    : public std::enable_shared_from_this<TGMediaGroupBuffer> {
public:
  using FlushFn = std::function<void(std::vector<BufferedTelegramMessage>)>;
  static constexpr std::chrono::milliseconds kDebounceWindow{1500};

  explicit TGMediaGroupBuffer(boost::asio::any_io_executor executor);
  ~TGMediaGroupBuffer();
  TGMediaGroupBuffer(const TGMediaGroupBuffer &) = delete;
  auto operator=(const TGMediaGroupBuffer &) -> TGMediaGroupBuffer & = delete;

  // Distinct members retain arrival order; duplicate MessageRefs neither
  // replace the first observation nor postpone its timer indefinitely.
  void add(std::string source_installation, BufferedTelegramMessage message,
           FlushFn flush_fn,
           obcx::core::ActorGenerationLifecycle::WorkLease lease);
  void flush_all_now();

private:
  // SDK serialization of the entire group value; not a new persistence key.
  using GroupKey = std::string;
  struct Group {
    std::vector<BufferedTelegramMessage> events;
    FlushFn flush_fn;
    std::unique_ptr<boost::asio::steady_timer> timer;
    std::shared_ptr<const bool> timer_generation;
  };
  void on_timer_fired_(GroupKey key, std::shared_ptr<const bool> generation);

  boost::asio::any_io_executor executor_;
  mutable std::mutex mutex_;
  std::map<GroupKey, Group> groups_;
};
} // namespace bridge::telegram
