#pragma once

#include "bridge_command_operations.hpp"

#include <boost/asio.hpp>
#include <common/message_type.hpp>
#include <core/actor/blocking_executor.hpp>
#include <memory>
#include <string_view>

namespace bridge {
class BridgeStateRepository;
class ReceivedMessageRepository;
} // namespace bridge

namespace bridge::telegram {

class TelegramCommandHandler {
public:
  explicit TelegramCommandHandler(
      std::shared_ptr<BridgeCommandOperations> operations,
      std::size_t maximum_image_bytes,
      std::shared_ptr<bridge::BridgeStateRepository> state_repository = nullptr,
      std::shared_ptr<bridge::ReceivedMessageRepository>
          received_message_repository = nullptr,
      std::shared_ptr<obcx::core::BlockingExecutor> blocking_executor =
          nullptr);

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
  void validate_source(const BridgeCommandSource &source) const;
  auto send_reply_message(obcx::bot::MessageRef source, std::string text)
      -> boost::asio::awaitable<void>;

  std::shared_ptr<BridgeCommandOperations> operations_;
  std::size_t maximum_image_bytes_;
  std::shared_ptr<bridge::BridgeStateRepository> state_repository_;
  std::shared_ptr<bridge::ReceivedMessageRepository>
      received_message_repository_;
  std::shared_ptr<obcx::core::BlockingExecutor> blocking_executor_;
};

} // namespace bridge::telegram
