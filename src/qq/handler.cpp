#include "qq/handler.hpp"
#include "qq/command_handler.hpp"
#include "qq/event_handler.hpp"

namespace bridge {

QQHandler::QQHandler(
    std::shared_ptr<BridgeCommandOperations> operations,
    std::shared_ptr<const BridgeConfig> config,
    std::shared_ptr<BridgeStateRepository> state_repository,
    std::shared_ptr<ReceivedMessageRepository> received_message_repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking_executor)
    : command_handler_(std::make_unique<qq::QQCommandHandler>(
          operations, state_repository, blocking_executor)),
      event_handler_(std::make_unique<qq::QQEventHandler>(
          operations, std::move(config), std::move(state_repository),
          std::move(received_message_repository),
          std::move(blocking_executor))) {
  if (!operations)
    throw std::invalid_argument("QQHandler requires bot operations");
}

QQHandler::~QQHandler() = default;

auto QQHandler::handle_recall_event(obcx::bot::MessageDeleted deleted)
    -> boost::asio::awaitable<void> {
  co_await event_handler_->handle_recall_event(std::move(deleted));
}

auto QQHandler::handle_bridge_status_command(obcx::bot::MessageRef source)
    -> boost::asio::awaitable<void> {
  co_await command_handler_->handle_bridge_status_command(std::move(source));
}

auto QQHandler::handle_poke_event(const obcx::common::NoticeEvent &event)
    -> boost::asio::awaitable<void> {
  co_await event_handler_->handle_poke_event(event);
}

} // namespace bridge
