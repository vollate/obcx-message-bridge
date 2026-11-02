#pragma once

#include "bridge_command_operations.hpp"

#include <boost/asio.hpp>
#include <common/message_type.hpp>
#include <core/actor/blocking_executor.hpp>
#include <memory>

namespace bridge {
class BridgeStateRepository;
}

namespace bridge::qq {

class QQCommandHandler {
public:
  explicit QQCommandHandler(
      std::shared_ptr<BridgeCommandOperations> operations,
      std::shared_ptr<bridge::BridgeStateRepository> state_repository,
      std::shared_ptr<obcx::core::BlockingExecutor> blocking_executor);

  auto handle_bridge_status_command(obcx::bot::MessageRef source)
      -> boost::asio::awaitable<void>;

private:
  std::shared_ptr<BridgeCommandOperations> operations_;
  std::shared_ptr<bridge::BridgeStateRepository> state_repository_;
  std::shared_ptr<obcx::core::BlockingExecutor> blocking_executor_;
};

} // namespace bridge::qq
