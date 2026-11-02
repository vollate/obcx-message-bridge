#pragma once

#include "bridge_delivery_sender.hpp"
#include "config.hpp"
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <mutex>

namespace bridge {

// Constructed without I/O during preparation; starts only with the published
// generation token. All asynchronous frames/callbacks retain the drain lease.
class BridgeDeliveryRetryWorker final
    : public std::enable_shared_from_this<BridgeDeliveryRetryWorker> {
public:
  BridgeDeliveryRetryWorker(
      boost::asio::any_io_executor executor,
      std::shared_ptr<obcx::bot::BotOperationGateway> gateway,
      std::shared_ptr<BridgeStateRepository> repository,
      std::shared_ptr<obcx::core::BlockingExecutor> blocking,
      std::shared_ptr<const obcx::bot::ConversationResolver> references,
      std::shared_ptr<obcx::core::ActorGenerationLifecycle> lifecycle,
      std::shared_ptr<obcx::core::ActorEventPublisher> publisher,
      const BridgeConfig &config, std::int64_t maximum_per_poll);
  void start(obcx::core::ActorGenerationLifecycle::TokenPtr token) noexcept;
  void stop() noexcept;

private:
  struct Route {
    std::string pair_id;
    obcx::bot::BotInstallationRef source_installation;
    obcx::bot::ConfiguredConversation source_address;
    bool all_topics;
    obcx::bot::ConversationRef target;
  };
  struct Active {
    obcx::core::ActorGenerationLifecycle::TokenPtr token;
    obcx::core::ActorGenerationLifecycle::WorkLease lease;
    std::shared_ptr<boost::asio::cancellation_signal> cancel;
  };
  static auto run(std::shared_ptr<BridgeDeliveryRetryWorker> self,
                  obcx::core::ActorGenerationLifecycle::TokenPtr token)
      -> boost::asio::awaitable<void>;
  auto select_route(const ledger::DeliverySnapshot &snapshot) const
      -> std::optional<BridgeRetryRoute>;
  void finished(obcx::core::ActorGenerationLifecycle::TokenPtr token,
                std::exception_ptr error) noexcept;

  boost::asio::any_io_executor executor_; // Core-created serial executor.
  boost::asio::steady_timer timer_;
  std::shared_ptr<obcx::bot::BotOperationGateway> gateway_;
  std::shared_ptr<BridgeStateRepository> repository_;
  std::shared_ptr<obcx::core::BlockingExecutor> blocking_;
  std::shared_ptr<const obcx::bot::ConversationResolver> references_;
  std::shared_ptr<obcx::core::ActorGenerationLifecycle> lifecycle_;
  std::shared_ptr<obcx::core::ActorEventPublisher> publisher_;
  std::chrono::seconds interval_;
  std::int64_t maximum_per_poll_;
  std::vector<Route> routes_;
  std::mutex mutex_;
  obcx::core::ActorGenerationLifecycle::TokenPtr desired_;
  std::optional<Active> active_;
};
} // namespace bridge
