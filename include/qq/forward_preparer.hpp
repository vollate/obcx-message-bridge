#pragma once

#include "qq/image_preparer.hpp"
#include <onebot11/bot/group_forward.hpp>

namespace bridge::qq {
// An explicit forward lookup produces the node media observations. Archive
// restoration alone never authorizes a media read. No send occurs here.
auto prepare_forward(std::shared_ptr<obcx::bot::BotOperationGateway> gateway,
                     std::shared_ptr<obcx::core::BlockingExecutor> blocking,
                     std::shared_ptr<const BridgeConfig> config,
                     obcx::onebot11::bot::QqForwardRef forward,
                     obcx::bot::SendLimits limits)
    -> boost::asio::awaitable<obcx::bot::MessageBody>;
} // namespace bridge::qq
