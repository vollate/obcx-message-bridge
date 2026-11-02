#pragma once

#include "telegram/command_handler.hpp"

#include <boost/asio/awaitable.hpp>
#include <core/actor/blocking_executor.hpp>
#include <memory>

namespace bridge {

class BridgeStateRepository;
struct BridgeConfig;
class ReceivedMessageRepository;

// Commands only. Ordinary messages, revisions and albums have separate typed
// owners; a failed typed operation cannot re-enter a scalar/native sender.
class TelegramHandler final {
public:
  explicit TelegramHandler(
      std::shared_ptr<BridgeCommandOperations> operations,
      std::shared_ptr<const BridgeConfig> config,
      std::shared_ptr<BridgeStateRepository> state_repository,
      std::shared_ptr<ReceivedMessageRepository> received_message_repository,
      std::shared_ptr<obcx::core::BlockingExecutor> blocking_executor);

  auto handle_recall_command(BridgeCommandSource source,
                             std::string_view qq_group_id)
      -> boost::asio::awaitable<void>;
  auto handle_bridge_status_command(obcx::bot::MessageRef source)
      -> boost::asio::awaitable<void>;
  auto handle_userinfo_command(BridgeCommandSource source,
                               std::string_view qq_group_id,
                               std::string arguments)
      -> boost::asio::awaitable<void>;
  auto handle_reaction_command(BridgeCommandSource source,
                               std::string_view qq_group_id,
                               std::string arguments, bool set)
      -> boost::asio::awaitable<void>;
  auto handle_poke_command(BridgeCommandSource source,
                           std::string_view qq_group_id)
      -> boost::asio::awaitable<void>;

private:
  std::unique_ptr<telegram::TelegramCommandHandler> command_handler_;
};

} // namespace bridge
