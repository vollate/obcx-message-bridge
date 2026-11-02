#include "config.hpp"
#include "telegram/handler.hpp"

#include <stdexcept>
#include <utility>

namespace bridge {

TelegramHandler::TelegramHandler(
    std::shared_ptr<BridgeCommandOperations> operations,
    std::shared_ptr<const BridgeConfig> config,
    std::shared_ptr<BridgeStateRepository> state_repository,
    std::shared_ptr<ReceivedMessageRepository> received_message_repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking_executor) {
  if (!operations || !config || !state_repository ||
      !received_message_repository || !blocking_executor)
    throw std::invalid_argument(
        "Telegram commands require configured services");
  command_handler_ = std::make_unique<telegram::TelegramCommandHandler>(
      std::move(operations), config->qq_media_download_max_bytes,
      std::move(state_repository), std::move(received_message_repository),
      std::move(blocking_executor));
}

auto TelegramHandler::handle_recall_command(BridgeCommandSource source,
                                            std::string_view qq_group_id)
    -> boost::asio::awaitable<void> {
  co_await command_handler_->handle_recall_command(std::move(source),
                                                   qq_group_id);
}

auto TelegramHandler::handle_bridge_status_command(obcx::bot::MessageRef source)
    -> boost::asio::awaitable<void> {
  co_await command_handler_->handle_bridge_status_command(std::move(source));
}

auto TelegramHandler::handle_userinfo_command(BridgeCommandSource source,
                                              std::string_view qq_group_id,
                                              std::string arguments)
    -> boost::asio::awaitable<void> {
  co_await command_handler_->handle_userinfo_command(
      std::move(source), qq_group_id, std::move(arguments));
}

auto TelegramHandler::handle_reaction_command(BridgeCommandSource source,
                                              std::string_view qq_group_id,
                                              std::string arguments, bool set)
    -> boost::asio::awaitable<void> {
  co_await command_handler_->handle_reaction_command(
      std::move(source), qq_group_id, std::move(arguments), set);
}

auto TelegramHandler::handle_poke_command(BridgeCommandSource source,
                                          std::string_view qq_group_id)
    -> boost::asio::awaitable<void> {
  co_await command_handler_->handle_poke_command(std::move(source),
                                                 qq_group_id);
}

} // namespace bridge
