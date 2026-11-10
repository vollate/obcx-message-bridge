#pragma once

#include <core/bot/bot_client.hpp>
#include <core/bot/bot_events.hpp>
#include <memory>

namespace bridge {

// Copies a received attachment across installations using its original read
// reference only. Source metadata is descriptive, never a locator or authority.
auto read_bridge_attachment(
    std::shared_ptr<obcx::bot::BotOperationGateway> gateway,
    obcx::bot::ReceivedMedia received, std::size_t maximum_bytes)
    -> boost::asio::awaitable<obcx::bot::BytesSource>;

// Existing QQ loop marker policy, applied to leading literal text only. It is
// not sender/mention proof and never grants access to any reference.
[[nodiscard]] auto has_telegram_bridge_prefix(
    const obcx::bot::MessageReceived &message) -> bool;

} // namespace bridge
