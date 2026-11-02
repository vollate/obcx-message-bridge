#pragma once

#include "bridge_command_operations.hpp"

#include <boost/asio.hpp>
#include <common/message_type.hpp>
#include <core/actor/blocking_executor.hpp>
#include <memory>

namespace bridge {

class BridgeStateRepository;
struct BridgeConfig;
class ReceivedMessageRepository;

namespace qq {
class QQCommandHandler;
class QQEventHandler;
} // namespace qq

// QQ ordinary messages are handled exclusively by TextForwarder from the
// owning MessageStored projection. This boundary handles commands/notices;
// it cannot re-enter a native segment sender after a typed refusal or failure.
class QQHandler {
public:
  explicit QQHandler(
      std::shared_ptr<BridgeCommandOperations> operations,
      std::shared_ptr<const BridgeConfig> config,
      std::shared_ptr<BridgeStateRepository> state_repository,
      std::shared_ptr<ReceivedMessageRepository> received_message_repository,
      std::shared_ptr<obcx::core::BlockingExecutor> blocking_executor);
  ~QQHandler();

  auto handle_recall_event(obcx::bot::MessageDeleted deleted)
      -> boost::asio::awaitable<void>;
  auto handle_bridge_status_command(obcx::bot::MessageRef source)
      -> boost::asio::awaitable<void>;
  auto handle_poke_event(const obcx::common::NoticeEvent &event)
      -> boost::asio::awaitable<void>;

private:
  std::unique_ptr<qq::QQCommandHandler> command_handler_;
  std::unique_ptr<qq::QQEventHandler> event_handler_;
};

} // namespace bridge
