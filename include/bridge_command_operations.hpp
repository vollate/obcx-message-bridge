#pragma once

#include "bridge_bot_failures.hpp"
#include "bridge_delivery_sender.hpp"
#include <core/bot/bot_client.hpp>
#include <core/bot/conversation_resolver.hpp>
#include <onebot11/bot/operations.hpp>
#include <telegram/bot/domain_operations.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bridge {
struct BridgeInstallationPair;

// Original command projection and unchanged configuration-bound mapping key.
// Neither a legacy key nor restoration grants receive/read authority.
struct BridgeCommandSource {
  obcx::bot::MessageReceived received;
  std::string conversation_id;
};

// Installed-domain-SDK-only command boundary. Legacy forwarding derives from
// this owner during migration; command handlers never include its native DTOs.
class BridgeCommandOperations {
public:
  BridgeCommandOperations(
      std::shared_ptr<obcx::bot::BotOperationGateway> client,
      const BridgeInstallationPair &pair,
      std::shared_ptr<const obcx::bot::ConversationResolver> references);
  virtual ~BridgeCommandOperations() = default;

  [[nodiscard]] auto operation_gateway() const noexcept
      -> std::shared_ptr<obcx::bot::BotOperationGateway> {
    return client_;
  }
  auto send_delivery(BridgeDeliveryRequest request,
                     std::shared_ptr<BridgeStateRepository> repository,
                     std::shared_ptr<obcx::core::BlockingExecutor> blocking)
      -> boost::asio::awaitable<BridgeDeliveryResult>;

  [[nodiscard]] auto pair_id() const noexcept -> const std::string &;
  [[nodiscard]] auto telegram_installation() const noexcept
      -> const obcx::bot::BotInstallationRef &;
  [[nodiscard]] auto onebot11_installation() const noexcept
      -> const obcx::bot::BotInstallationRef &;

  // The original received reference remains the only read input. Native file
  // observations are for caller-owned presentation/storage, never authority.
  auto read_telegram_attachment(obcx::bot::ReceivedMedia media,
                                std::size_t maximum_bytes)
      -> boost::asio::awaitable<obcx::bot::BytesSource>;

  auto reply_text(obcx::bot::MessageRef source, std::string text)
      -> boost::asio::awaitable<obcx::bot::SendOutcome>;
  auto reply_image(obcx::bot::MessageRef source, obcx::bot::HttpSource image,
                   std::string caption, std::size_t maximum_bytes)
      -> boost::asio::awaitable<obcx::bot::SendOutcome>;
  [[nodiscard]] auto onebot11_conversation(std::string_view group_id) const
      -> const obcx::bot::ConversationRef &;
  // Match an observed scope to the original configured key. No opaque-ID
  // decoding, raw-notice fallback or new reference authority.
  [[nodiscard]] auto onebot11_group_id(
      const obcx::bot::ConversationRef &conversation) const -> std::string;

  // Explicit configuration/restoration boundaries; never mutation authority.
  [[nodiscard]] auto telegram_conversation(
      std::string_view group_id, std::optional<std::string> topic) const
      -> obcx::bot::ConversationRef;
  void validate_telegram_target(const obcx::bot::ConversationRef &) const;
  auto send_telegram_text(obcx::bot::ConversationRef target, std::string text)
      -> boost::asio::awaitable<obcx::bot::SendOutcome>;
  auto delete_telegram_output(obcx::bot::MessageRef message)
      -> boost::asio::awaitable<
          obcx::bot::BotOperationResult<obcx::bot::DeleteReceipt>>;
  auto get_onebot11_group_member(std::string_view group_id,
                                 std::string_view user_id,
                                 bool no_cache = false)
      -> boost::asio::awaitable<obcx::onebot11::bot::OneBotGroupMember>;

  // Requires an exact, independently proven output MessageRef. Configuration
  // matching is not a substitute for the caller's durable mutation admission.
  // Keeps the full error/submission safety; no retry or legacy-ID
  // reconstruction.
  auto edit_telegram_text(obcx::telegram::bot::EditFormattedTextRequest request)
      -> boost::asio::awaitable<
          obcx::bot::BotOperationResult<obcx::telegram::bot::EditTextReceipt>>;

  auto delete_onebot11_message(obcx::bot::MessageRef message)
      -> boost::asio::awaitable<
          obcx::bot::BotOperationResult<obcx::bot::DeleteReceipt>>;
  auto get_onebot11_user_info(std::string_view user_id, bool no_cache)
      -> boost::asio::awaitable<obcx::onebot11::bot::OneBotUserInfo>;
  auto get_onebot11_user_info(obcx::bot::UserRef user, bool no_cache)
      -> boost::asio::awaitable<obcx::onebot11::bot::OneBotUserInfo>;
  auto set_onebot11_message_emoji_like(
      obcx::onebot11::bot::SetOneBotMessageEmojiLikeRequest request)
      -> boost::asio::awaitable<obcx::bot::BotOperationResult<
          obcx::onebot11::bot::OneBotMessageEmojiLikeResult>>;
  [[nodiscard]] auto prepare_onebot11_poke(std::string_view group_id,
                                           const obcx::bot::UserRef &user) const
      -> obcx::onebot11::bot::PokeOneBotGroupRequest;
  auto poke_onebot11_group(obcx::onebot11::bot::PokeOneBotGroupRequest request)
      -> boost::asio::awaitable<obcx::bot::BotOperationResult<
          obcx::onebot11::bot::OneBotGroupPokeResult>>;

protected:
  template <typename T>
  static auto require(obcx::bot::BotOperationResult<T> result) -> T {
    result.validate();
    if (!result.ok()) {
      throw BridgeBotOperationFailure(std::move(*result.error));
    }
    return std::move(*result.value);
  }

  std::shared_ptr<obcx::bot::BotOperationGateway> client_;
  std::shared_ptr<const obcx::bot::ConversationResolver> references_;
  std::map<std::string, obcx::bot::ConversationRef, std::less<>> onebot_groups_;
  std::vector<obcx::bot::ConversationRef> telegram_scopes_;
  std::string pair_id_;
  obcx::bot::BotInstallationRef telegram_;
  obcx::bot::BotInstallationRef onebot11_;
};
} // namespace bridge
