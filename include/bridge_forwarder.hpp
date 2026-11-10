#pragma once

#include "bridge_delivery_revision.hpp"
#include "bridge_delivery_sender.hpp"
#include <boost/asio/awaitable.hpp>
#include <core/actor/actor.hpp>
#include <core/actor/actor_commands.hpp>
#include <core/actor/actor_messages.hpp>

namespace bridge {
enum class DirectForwardDisposition {
  NotForwarded,
  DeliveryFailed,
  NewDelivery,
  AlreadyPersisted,
};

struct BridgeForwardResult {
  // Only complete durable typed evidence may authorize logical publication.
  std::optional<BridgeDeliveryResult> bot_delivery;
  std::optional<BridgeRevisionResult> bot_revision;
  DirectForwardDisposition disposition = DirectForwardDisposition::NotForwarded;
};

class IBridgeForwarder {
public:
  IBridgeForwarder() = default;
  IBridgeForwarder(const IBridgeForwarder &) = delete;
  auto operator=(const IBridgeForwarder &) -> IBridgeForwarder & = delete;
  IBridgeForwarder(IBridgeForwarder &&) = delete;
  auto operator=(IBridgeForwarder &&) -> IBridgeForwarder & = delete;
  virtual ~IBridgeForwarder() = default;

  virtual auto forward_message(
      const obcx::message_store::events::MessageStored &stored,
      const obcx::core::MessageEnvelope &message)
      -> boost::asio::awaitable<BridgeForwardResult> = 0;
  virtual auto handle_command(const obcx::command::CommandInvocation &)
      -> boost::asio::awaitable<bool> {
    co_return false;
  }
  virtual auto handle_notice(const obcx::core::MessageEnvelope &,
                             std::optional<obcx::bot::MessageDeleted>)
      -> boost::asio::awaitable<bool> {
    co_return false;
  }
};
} // namespace bridge
