#pragma once
#include "core/bot/typed_operation.hpp"

#include "bridge_forwarder.hpp"
#include "bridge_state_repository.hpp"
#include "received_message_repository.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <core/actor/blocking_executor.hpp>
#include <core/actor/command_scope.hpp>

#include <atomic>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace bridge {

class QQHandler;
class TelegramHandler;
struct BridgeConfig;

class BridgeRetryUnavailable final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class BridgeForwardingRuntime final : public IBridgeForwarder {
public:
  BridgeForwardingRuntime(
      std::shared_ptr<obcx::bot::BotOperationGateway> operation_client,
      std::shared_ptr<const BridgeConfig> config,
      std::shared_ptr<BridgeStateRepository> state_repository,
      std::shared_ptr<ReceivedMessageRepository> received_message_repository,
      boost::asio::any_io_executor buffer_executor,
      std::shared_ptr<obcx::core::BlockingExecutor> blocking_executor,
      std::shared_ptr<const obcx::command::CommandScopeResolver> command_scopes,
      std::shared_ptr<const obcx::bot::ConversationResolver> references,
      std::shared_ptr<obcx::core::ActorGenerationLifecycle> lifecycle,
      std::shared_ptr<obcx::core::ActorEventPublisher> publisher);
  ~BridgeForwardingRuntime() override;

  auto forward_message(const obcx::message_store::events::MessageStored &stored,
                       const obcx::core::MessageEnvelope &message)
      -> boost::asio::awaitable<BridgeForwardResult> override;
  auto handle_command(const obcx::command::CommandInvocation &invocation)
      -> boost::asio::awaitable<bool> override;
  auto handle_notice(const obcx::core::MessageEnvelope &message,
                     std::optional<obcx::bot::MessageDeleted> deleted)
      -> boost::asio::awaitable<bool> override;

  void shutdown() noexcept;

private:
  struct PairRuntime;

  std::shared_ptr<const BridgeConfig> config_;
  std::shared_ptr<BridgeStateRepository> state_repository_;
  std::shared_ptr<ReceivedMessageRepository> received_message_repository_;
  std::shared_ptr<obcx::core::BlockingExecutor> blocking_executor_;
  std::shared_ptr<const obcx::command::CommandScopeResolver> command_scopes_;
  std::unordered_map<std::string, std::unique_ptr<PairRuntime>> pair_runtimes_;
  std::atomic_bool shutting_down_{false};
};

} // namespace bridge
