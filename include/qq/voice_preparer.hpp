#pragma once

#include "config.hpp"
#include <core/actor/blocking_executor.hpp>
#include <core/bot/domain_media.hpp>

namespace bridge::qq {
// Only typed QQ Voice attachments enter this path. Never sends or falls back to
// the original bytes. The caller charges the returned size to its total budget.
auto prepare_voice(std::shared_ptr<obcx::core::BlockingExecutor> blocking,
                   std::shared_ptr<const BridgeConfig> config,
                   obcx::bot::BytesSource source, std::size_t maximum_bytes)
    -> boost::asio::awaitable<obcx::bot::BytesSource>;
} // namespace bridge::qq
