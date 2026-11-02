#pragma once

#include "bridge_forwarder.hpp"
#include "config.hpp"

namespace bridge::qq {

// Migrated QQ text/media and recognized specialized content, including forward
// nodes. Known and unknown senders are distinct; display keys do not create
// users. Unsupported content/albums stay separate, selected before effects.
class TextForwarder final {
public:
  TextForwarder(
      std::shared_ptr<obcx::bot::BotOperationGateway> gateway,
      std::shared_ptr<const obcx::bot::ConversationResolver> references,
      std::shared_ptr<BridgeStateRepository> repository,
      std::shared_ptr<obcx::core::BlockingExecutor> blocking,
      const BridgeInstallationPair &pair, std::int64_t max_attempts,
      std::shared_ptr<const BridgeConfig> config);

  [[nodiscard]] static auto accepts(const obcx::bot::MessageReceived &source)
      -> bool;
  auto forward(obcx::message_store::events::MessageStored stored,
               BridgeDeliveryPublication publication)
      -> boost::asio::awaitable<BridgeForwardResult>;

private:
  struct Route {
    obcx::bot::ConversationRef source;
    obcx::bot::ConversationRef target;
    std::string source_group;
    bool show_sender;
  };
  auto render_text(obcx::bot::ReceivedRichText text, Route route)
      -> boost::asio::awaitable<obcx::bot::RichText>;
  auto display_name(obcx::bot::UserRef user, Route route)
      -> boost::asio::awaitable<std::optional<std::string>>;

  std::shared_ptr<obcx::bot::BotOperationGateway> gateway_;
  std::shared_ptr<const obcx::bot::ConversationResolver> references_;
  std::shared_ptr<BridgeStateRepository> repository_;
  std::shared_ptr<obcx::core::BlockingExecutor> blocking_;
  std::shared_ptr<const BridgeConfig> config_;
  std::string pair_id_;
  std::int64_t max_attempts_;
  std::vector<Route> routes_;
};
} // namespace bridge::qq
