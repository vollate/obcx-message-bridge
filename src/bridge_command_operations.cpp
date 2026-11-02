#include "bridge_command_operations.hpp"
#include "bridge_media_reader.hpp"
#include "bridge_message_identity.hpp"
#include "config.hpp"
#include <algorithm>
#include <charconv>
#include <core/bot/typed_operation.hpp>
#include <onebot11/bot/user_target.hpp>
#include <telegram/bot/client.hpp>

#include <utility>

namespace bridge {
namespace {
auto query_user_target(const obcx::bot::BotInstallationRef &installation,
                       std::string_view text)
    -> obcx::onebot11::bot::NativeUserTarget {
  std::int64_t id;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), id);
  if (error != std::errc{} || end != text.data() + text.size() || id <= 0 ||
      std::to_string(id) != text)
    throw std::invalid_argument("invalid current QQ query target");
  obcx::onebot11::bot::NativeUserTarget target{installation, id};
  target.validate();
  return target;
}
// Coroutine parameters own everything needed after the facade's synchronous
// entry returns. In particular, no borrowed string_view or facade `this` is
// consulted after initial suspension.
template <obcx::bot::TypedOperation Request>
auto invoke_owned(std::shared_ptr<obcx::bot::BotOperationGateway> gateway,
                  Request request)
    -> boost::asio::awaitable<obcx::bot::BotOperationResult<
        typename obcx::bot::OperationTraits<Request>::result_type>> {
  co_return co_await obcx::bot::invoke(*gateway, std::move(request));
}
template <class T>
auto required_result(
    boost::asio::awaitable<obcx::bot::BotOperationResult<T>> operation)
    -> boost::asio::awaitable<T> {
  auto result = co_await std::move(operation);
  result.validate();
  if (!result.ok())
    throw BridgeBotOperationFailure(std::move(*result.error));
  co_return std::move(*result.value);
}
auto send_owned(std::shared_ptr<obcx::bot::BotOperationGateway> gateway,
                obcx::bot::SendMessageRequest request)
    -> boost::asio::awaitable<obcx::bot::SendOutcome> {
  co_return co_await obcx::bot::BotClient{*gateway}.send(std::move(request));
}
auto ready_outcome(obcx::bot::SendOutcome outcome)
    -> boost::asio::awaitable<obcx::bot::SendOutcome> {
  co_return std::move(outcome);
}
auto edit_telegram_owned(
    std::shared_ptr<obcx::bot::BotOperationGateway> gateway,
    obcx::telegram::bot::EditFormattedTextRequest request)
    -> boost::asio::awaitable<
        obcx::bot::BotOperationResult<obcx::telegram::bot::EditTextReceipt>> {
  co_return co_await obcx::telegram::bot::Client{*gateway}.execute(
      std::move(request));
}
} // namespace

auto BridgeCommandOperations::send_delivery(
    BridgeDeliveryRequest request,
    std::shared_ptr<BridgeStateRepository> repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking)
    -> boost::asio::awaitable<BridgeDeliveryResult> {
  if (request.pair_id != pair_id_ || request.members.empty())
    throw std::invalid_argument("delivery does not belong to this bridge pair");
  const auto &source =
      request.members.front().message.conversation.installation;
  const auto &target = request.send.target.installation;
  if (!((source == onebot11_ && target == telegram_) ||
        (source == telegram_ && target == onebot11_)))
    throw std::invalid_argument("delivery installation direction mismatch");
  // Own the services and request before returning the deferred operation.
  return send_bridge_delivery(client_, std::move(repository),
                              std::move(blocking), references_,
                              std::move(request));
}

auto BridgeCommandOperations::read_telegram_attachment(
    obcx::bot::ReceivedMedia media, std::size_t maximum_bytes)
    -> boost::asio::awaitable<obcx::bot::BytesSource> {
  media.validate();
  if (media.media.installation != telegram_)
    throw std::invalid_argument("bridge Telegram media installation mismatch");
  // Non-coroutine wrapper: copy the shared gateway and own the media before
  // returning an awaitable; no suspended reference to this facade is retained.
  return read_bridge_attachment(client_, std::move(media), maximum_bytes);
}

BridgeCommandOperations::BridgeCommandOperations(
    std::shared_ptr<obcx::bot::BotOperationGateway> client,
    const BridgeInstallationPair &pair,
    std::shared_ptr<const obcx::bot::ConversationResolver> references)
    : client_(std::move(client)), references_(std::move(references)),
      pair_id_(pair.id),
      telegram_{.installation_id = pair.telegram_installation,
                .surface = obcx::bot::SurfaceId{"telegram.bot_api"}},
      onebot11_{.installation_id = pair.onebot11_installation,
                .surface = obcx::bot::SurfaceId{"onebot11.qq"}} {
  if (!client_ || !references_) {
    throw std::invalid_argument(
        "bridge requires gateway and reference resolver");
  }
  if (pair_id_.empty()) {
    throw std::invalid_argument("bridge operations require pair id");
  }
  telegram_.validate();
  onebot11_.validate();
  // Pure configuration boundary. Keep original pair/group keys, resolve each
  // QQ conversation once, and never decode opaque IDs inside a command.
  const auto add_group = [&](const std::string &group_id) {
    if (onebot_groups_.contains(group_id))
      return;
    auto conversation = require(references_->resolve(
        onebot11_, configured_group(group_id, std::nullopt)));
    onebot_groups_.emplace(group_id, std::move(conversation));
  };
  const auto add_telegram = [&](const std::string &group_id,
                                std::optional<std::string> topic) {
    auto scope = require(
        references_->resolve(telegram_, configured_group(group_id, topic)));
    if (std::ranges::find(telegram_scopes_, scope) == telegram_scopes_.end())
      telegram_scopes_.push_back(std::move(scope));
  };
  for (const auto &[_, group] : pair.group_map) {
    if (group.mode == BridgeMode::GROUP_TO_GROUP) {
      add_group(group.qq_group_id);
      add_telegram(group.telegram_group_id, std::nullopt);
    } else if (group.mode == BridgeMode::TOPIC_TO_GROUP) {
      for (const auto &topic : group.topics) {
        add_group(topic.qq_group_id);
        add_telegram(group.telegram_group_id,
                     std::to_string(topic.telegram_topic_id));
      }
    } else {
      throw std::invalid_argument("bridge conversation mode is invalid");
    }
  }
}

auto BridgeCommandOperations::pair_id() const noexcept -> const std::string & {
  return pair_id_;
}

auto BridgeCommandOperations::telegram_installation() const noexcept
    -> const obcx::bot::BotInstallationRef & {
  return telegram_;
}

auto BridgeCommandOperations::onebot11_installation() const noexcept
    -> const obcx::bot::BotInstallationRef & {
  return onebot11_;
}

auto BridgeCommandOperations::reply_text(obcx::bot::MessageRef source,
                                         std::string text)
    -> boost::asio::awaitable<obcx::bot::SendOutcome> {
  if (source.conversation.installation != telegram_ &&
      source.conversation.installation != onebot11_) {
    return ready_outcome(obcx::bot::send_failure(
        {.code = obcx::bot::BotOperationErrorCode::InvalidRequest,
         .message = "bridge reply source does not belong to this pair",
         .retryable = false,
         .submission_safety =
             obcx::bot::SubmissionSafety::DefinitelyNotSubmitted},
        {}, false));
  }
  obcx::bot::SendMessageRequest request{
      .target = source.conversation,
      .body = {obcx::bot::RichText{obcx::bot::Text{std::move(text), {}}}},
      .reply_to = std::move(source),
      .delivery = obcx::bot::DeliveryPolicy::SingleMessageOnly,
      .limits = {1, 0, 0}};
  return send_owned(client_, std::move(request));
}

auto BridgeCommandOperations::reply_image(obcx::bot::MessageRef source,
                                          obcx::bot::HttpSource image,
                                          std::string caption,
                                          std::size_t maximum_bytes)
    -> boost::asio::awaitable<obcx::bot::SendOutcome> {
  if (source.conversation.installation != telegram_) {
    return ready_outcome(obcx::bot::send_failure(
        {.code = obcx::bot::BotOperationErrorCode::InvalidRequest,
         .message =
             "bridge profile reply requires the pair Telegram installation",
         .retryable = false,
         .submission_safety =
             obcx::bot::SubmissionSafety::DefinitelyNotSubmitted},
        {}, false));
  }
  obcx::bot::SendMessageRequest request{
      .target = source.conversation,
      .body = {obcx::bot::Image{std::move(image),
                                {obcx::bot::Text{std::move(caption), {}}}}},
      .reply_to = std::move(source),
      .delivery = obcx::bot::DeliveryPolicy::SingleMessageOnly,
      .limits = {1, maximum_bytes, maximum_bytes}};
  return send_owned(client_, std::move(request));
}

auto BridgeCommandOperations::onebot11_conversation(
    std::string_view group_id) const -> const obcx::bot::ConversationRef & {
  const auto found = onebot_groups_.find(group_id);
  if (found == onebot_groups_.end())
    throw std::invalid_argument(
        "QQ target is not configured for this bridge pair");
  return found->second;
}

auto BridgeCommandOperations::onebot11_group_id(
    const obcx::bot::ConversationRef &conversation) const -> std::string {
  conversation.validate();
  if (conversation.installation != onebot11_)
    throw std::invalid_argument("QQ observed scope installation mismatch");
  const std::string *group = nullptr;
  for (const auto &[key, configured] : onebot_groups_) {
    if (configured != conversation)
      continue;
    if (group)
      throw std::invalid_argument(
          "QQ observed scope has ambiguous configured keys");
    group = &key;
  }
  if (!group)
    throw std::invalid_argument(
        "QQ observed scope is not configured for this bridge pair");
  return *group;
}

auto BridgeCommandOperations::telegram_conversation(
    std::string_view group_id, std::optional<std::string> topic) const
    -> obcx::bot::ConversationRef {
  auto scope = require(
      references_->resolve(telegram_, configured_group(group_id, topic)));
  validate_telegram_target(scope);
  return scope;
}

void BridgeCommandOperations::validate_telegram_target(
    const obcx::bot::ConversationRef &scope) const {
  scope.validate();
  if (scope.installation != telegram_ ||
      std::ranges::find(telegram_scopes_, scope) == telegram_scopes_.end())
    throw std::invalid_argument(
        "Telegram target is not configured for this bridge pair");
}

auto BridgeCommandOperations::send_telegram_text(
    obcx::bot::ConversationRef target, std::string text)
    -> boost::asio::awaitable<obcx::bot::SendOutcome> {
  validate_telegram_target(target);
  obcx::bot::SendMessageRequest request{
      .target = std::move(target),
      .body = {obcx::bot::RichText{obcx::bot::Text{std::move(text), {}}}},
      .reply_to = std::nullopt,
      .delivery = obcx::bot::DeliveryPolicy::SingleMessageOnly,
      .limits = {1, 0, 0}};
  return send_owned(client_, std::move(request));
}

auto BridgeCommandOperations::delete_telegram_output(
    obcx::bot::MessageRef message)
    -> boost::asio::awaitable<
        obcx::bot::BotOperationResult<obcx::bot::DeleteReceipt>> {
  message.validate();
  validate_telegram_target(message.conversation);
  return invoke_owned(client_,
                      obcx::bot::DeleteMessageRequest{std::move(message)});
}

auto BridgeCommandOperations::get_onebot11_group_member(
    const std::string_view group_id, const std::string_view user_id,
    const bool no_cache)
    -> boost::asio::awaitable<obcx::onebot11::bot::OneBotGroupMember> {
  return required_result(
      invoke_owned(client_, obcx::onebot11::bot::GetOneBotGroupMemberRequest{
                                .target = onebot11_conversation(group_id),
                                .user = query_user_target(onebot11_, user_id),
                                .no_cache = no_cache}));
}

auto BridgeCommandOperations::edit_telegram_text(
    obcx::telegram::bot::EditFormattedTextRequest request)
    -> boost::asio::awaitable<
        obcx::bot::BotOperationResult<obcx::telegram::bot::EditTextReceipt>> {
  request.validate();
  if (request.message.conversation.installation != telegram_ ||
      std::ranges::find(telegram_scopes_, request.message.conversation) ==
          telegram_scopes_.end())
    throw std::invalid_argument(
        "Telegram edit target is not configured for this bridge pair");
  return edit_telegram_owned(client_, std::move(request));
}

auto BridgeCommandOperations::delete_onebot11_message(
    obcx::bot::MessageRef message)
    -> boost::asio::awaitable<
        obcx::bot::BotOperationResult<obcx::bot::DeleteReceipt>> {
  message.validate();
  bool configured = false;
  for (const auto &entry : onebot_groups_)
    configured |= entry.second == message.conversation;
  if (!configured || message.conversation.installation != onebot11_)
    throw std::invalid_argument(
        "QQ delete target is not configured for this bridge pair");
  return invoke_owned(client_,
                      obcx::bot::DeleteMessageRequest{std::move(message)});
}

auto BridgeCommandOperations::get_onebot11_user_info(
    const std::string_view user_id, const bool no_cache)
    -> boost::asio::awaitable<obcx::onebot11::bot::OneBotUserInfo> {
  return required_result(
      invoke_owned(client_, obcx::onebot11::bot::GetOneBotUserInfoRequest{
                                .user = query_user_target(onebot11_, user_id),
                                .no_cache = no_cache}));
}

auto BridgeCommandOperations::get_onebot11_user_info(obcx::bot::UserRef user,
                                                     const bool no_cache)
    -> boost::asio::awaitable<obcx::onebot11::bot::OneBotUserInfo> {
  user.validate();
  if (user.installation != onebot11_)
    throw std::invalid_argument("QQ user installation mismatch");
  return required_result(
      invoke_owned(client_, obcx::onebot11::bot::GetOneBotUserInfoRequest{
                                std::move(user), no_cache}));
}

auto BridgeCommandOperations::set_onebot11_message_emoji_like(
    obcx::onebot11::bot::SetOneBotMessageEmojiLikeRequest request)
    -> boost::asio::awaitable<obcx::bot::BotOperationResult<
        obcx::onebot11::bot::OneBotMessageEmojiLikeResult>> {
  request.validate();
  bool configured = false;
  for (const auto &[key, scope] : onebot_groups_)
    configured = configured || scope == request.message.conversation;
  if (!configured)
    throw std::invalid_argument(
        "reaction target is not configured for this bridge pair");
  return invoke_owned(client_, std::move(request));
}
auto BridgeCommandOperations::prepare_onebot11_poke(
    const std::string_view group_id, const obcx::bot::UserRef &user) const
    -> obcx::onebot11::bot::PokeOneBotGroupRequest {
  user.validate();
  if (user.installation != onebot11_)
    throw std::invalid_argument("QQ poke user installation mismatch");
  obcx::onebot11::bot::PokeOneBotGroupRequest request{
      onebot11_conversation(group_id), user};
  request.validate();
  return request;
}

auto BridgeCommandOperations::poke_onebot11_group(
    obcx::onebot11::bot::PokeOneBotGroupRequest request)
    -> boost::asio::awaitable<obcx::bot::BotOperationResult<
        obcx::onebot11::bot::OneBotGroupPokeResult>> {
  request.validate();
  bool configured = false;
  for (const auto &[key, scope] : onebot_groups_)
    configured = configured || scope == request.target;
  if (!configured || request.user.installation != onebot11_)
    throw std::invalid_argument(
        "poke target is not configured for this bridge pair");
  return invoke_owned(client_, std::move(request));
}
} // namespace bridge
