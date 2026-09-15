#include "bridge_bot_operations.hpp"
#include "core/bot/messaging.hpp"
#include "core/bot/typed_operation.hpp"
#include "onebot11/bot/operations.hpp"
#include "telegram/bot/operations.hpp"

#include <utility>

namespace bridge {

auto classify_bridge_operation_failure(const std::exception &error)
    -> BridgeOperationFailureDisposition {
  const auto *typed = dynamic_cast<const BridgeBotOperationFailure *>(&error);
  if (typed == nullptr) {
    return {.outcome_unknown = true, .diagnostic = "unclassified_exception"};
  }
  const auto &failure = typed->error();
  auto diagnostic = std::string{obcx::bot::error_code_id(failure.code)};
  if (failure.provider_code && !failure.provider_code->empty()) {
    diagnostic += " (provider_code=" +
                  obcx::bot::redact_bot_diagnostic(*failure.provider_code) +
                  ")";
  }
  if (!failure.message.empty()) {
    diagnostic += ": " + obcx::bot::redact_bot_diagnostic(failure.message);
  }
  return {
      .retryable = failure.retryable &&
                   failure.submission_safety ==
                       obcx::bot::SubmissionSafety::DefinitelyNotSubmitted,
      .outcome_unknown = failure.submission_safety ==
                         obcx::bot::SubmissionSafety::PossiblySubmitted,
      .diagnostic = std::move(diagnostic),
  };
}

BridgeBotOperations::BridgeBotOperations(
    std::shared_ptr<obcx::bot::BotOperationGateway> client,
    std::string telegram_installation, std::string onebot11_installation,
    std::string pair_id)
    : client_(std::move(client)), pair_id_(std::move(pair_id)),
      telegram_{.installation_id = std::move(telegram_installation),
                .surface = obcx::bot::SurfaceId{"telegram.bot_api"}},
      onebot11_{.installation_id = std::move(onebot11_installation),
                .surface = obcx::bot::SurfaceId{"onebot11.qq"}} {
  if (!client_) {
    throw std::invalid_argument("bridge requires BotOperationGateway");
  }
  if (pair_id_.empty()) {
    throw std::invalid_argument("bridge operations require pair id");
  }
  telegram_.validate();
  onebot11_.validate();
}

auto BridgeBotOperations::pair_id() const noexcept -> const std::string & {
  return pair_id_;
}

auto BridgeBotOperations::telegram_installation() const noexcept
    -> const obcx::bot::BotInstallationRef & {
  return telegram_;
}

auto BridgeBotOperations::onebot11_installation() const noexcept
    -> const obcx::bot::BotInstallationRef & {
  return onebot11_;
}

auto BridgeBotOperations::telegram_group(std::string group_id) const
    -> obcx::bot::GroupTarget {
  return {.installation = telegram_, .native_group_id = std::move(group_id)};
}

auto BridgeBotOperations::onebot11_group(std::string group_id) const
    -> obcx::bot::GroupTarget {
  return {.installation = onebot11_, .native_group_id = std::move(group_id)};
}

auto BridgeBotOperations::send_telegram_group(
    const std::string_view group_id, const obcx::common::Message &message)
    -> boost::asio::awaitable<std::string> {
  auto result = require(co_await obcx::bot::invoke(
      *client_, obcx::bot::SendGroupMessageRequest{
                    .target = telegram_group(std::string{group_id}),
                    .message = message}));
  co_return result.primary().native_message_id;
}

auto BridgeBotOperations::send_telegram_topic(
    const std::string_view group_id, const std::int64_t topic_id,
    const obcx::common::Message &message)
    -> boost::asio::awaitable<std::string> {
  auto result = require(co_await obcx::bot::invoke(
      *client_, obcx::telegram::bot::SendTelegramTopicMessageRequest{
                    .target = {.group = telegram_group(std::string{group_id}),
                               .topic_id = topic_id},
                    .message = message}));
  co_return result.primary().native_message_id;
}

auto BridgeBotOperations::send_onebot11_group(
    const std::string_view group_id, const obcx::common::Message &message)
    -> boost::asio::awaitable<std::string> {
  auto result = require(co_await obcx::bot::invoke(
      *client_, obcx::bot::SendGroupMessageRequest{
                    .target = onebot11_group(std::string{group_id}),
                    .message = message}));
  co_return result.primary().native_message_id;
}

auto BridgeBotOperations::delete_telegram_message(
    const std::string_view group_id, const std::string_view message_id)
    -> boost::asio::awaitable<void> {
  (void)require(co_await obcx::bot::invoke(
      *client_,
      obcx::bot::DeleteMessageRequest{
          .message = {.group = telegram_group(std::string{group_id}),
                      .native_message_id = std::string{message_id}}}));
  co_return;
}

auto BridgeBotOperations::delete_onebot11_message(
    const std::string_view group_id, const std::string_view message_id)
    -> boost::asio::awaitable<void> {
  (void)require(co_await obcx::bot::invoke(
      *client_,
      obcx::bot::DeleteMessageRequest{
          .message = {.group = onebot11_group(std::string{group_id}),
                      .native_message_id = std::string{message_id}}}));
  co_return;
}

auto BridgeBotOperations::edit_telegram_message(
    const std::string_view group_id, const std::string_view message_id,
    const std::string_view text, const std::string_view parse_mode)
    -> boost::asio::awaitable<void> {
  (void)require(co_await obcx::bot::invoke(
      *client_, obcx::telegram::bot::EditTelegramMessageTextRequest{
                    .message = {.group = telegram_group(std::string{group_id}),
                                .native_message_id = std::string{message_id}},
                    .text = std::string{text},
                    .parse_mode = std::string{parse_mode}}));
  co_return;
}

auto BridgeBotOperations::send_telegram_photo(
    const std::string_view group_id, std::string photo, std::string caption,
    std::vector<obcx::telegram::bot::TelegramTextEntity> entities)
    -> boost::asio::awaitable<std::string> {
  auto result = require(co_await obcx::bot::invoke(
      *client_, obcx::telegram::bot::SendTelegramPhotoRequest{
                    .target = telegram_group(std::string{group_id}),
                    .photo = std::move(photo),
                    .caption = std::move(caption),
                    .caption_entities = std::move(entities)}));
  co_return result.primary().native_message_id;
}

auto BridgeBotOperations::send_telegram_media_urls(
    const std::string_view group_id,
    std::vector<obcx::telegram::bot::TelegramMediaSource> media,
    std::string caption, const std::optional<std::int64_t> topic_id,
    std::optional<std::string> reply_to_message_id,
    std::vector<obcx::telegram::bot::TelegramTextEntity> entities)
    -> boost::asio::awaitable<obcx::bot::SendMessageResult> {
  std::optional<obcx::bot::BotMessageRef> reply;
  if (reply_to_message_id.has_value()) {
    reply = {.group = telegram_group(std::string{group_id}),
             .native_message_id = std::move(*reply_to_message_id)};
  }
  co_return require(co_await obcx::bot::invoke(
      *client_, obcx::telegram::bot::SendTelegramMediaGroupUrlsRequest{
                    .target = telegram_group(std::string{group_id}),
                    .media = std::move(media),
                    .caption = std::move(caption),
                    .topic_id = topic_id,
                    .reply_to = std::move(reply),
                    .caption_entities = std::move(entities)}));
}

auto BridgeBotOperations::send_telegram_media_uploads(
    const std::string_view group_id,
    std::vector<obcx::telegram::bot::TelegramMediaUpload> media,
    std::string caption, const std::size_t maximum_bytes,
    const std::optional<std::int64_t> topic_id,
    std::optional<std::string> reply_to_message_id,
    std::vector<obcx::telegram::bot::TelegramTextEntity> entities)
    -> boost::asio::awaitable<obcx::bot::SendMessageResult> {
  std::optional<obcx::bot::BotMessageRef> reply;
  if (reply_to_message_id.has_value()) {
    reply = {.group = telegram_group(std::string{group_id}),
             .native_message_id = std::move(*reply_to_message_id)};
  }
  co_return require(co_await obcx::bot::invoke(
      *client_, obcx::telegram::bot::SendTelegramMediaGroupUploadsRequest{
                    .target = telegram_group(std::string{group_id}),
                    .media = std::move(media),
                    .caption = std::move(caption),
                    .topic_id = topic_id,
                    .reply_to = std::move(reply),
                    .caption_entities = std::move(entities),
                    .maximum_bytes = maximum_bytes}));
}

auto BridgeBotOperations::fetch_telegram_file(
    obcx::telegram::bot::TelegramFileRef file, const std::size_t maximum_bytes)
    -> boost::asio::awaitable<obcx::telegram::bot::FetchedTelegramFile> {
  co_return require(co_await obcx::bot::invoke(
      *client_, obcx::telegram::bot::FetchTelegramFileRequest{
                    .installation = telegram_,
                    .file = std::move(file),
                    .maximum_bytes = maximum_bytes}));
}

auto BridgeBotOperations::get_onebot11_group_member(
    const std::string_view group_id, const std::string_view user_id,
    const bool no_cache)
    -> boost::asio::awaitable<obcx::onebot11::bot::OneBotGroupMember> {
  co_return require(co_await obcx::bot::invoke(
      *client_, obcx::onebot11::bot::GetOneBotGroupMemberRequest{
                    .target = onebot11_group(std::string{group_id}),
                    .user_id = std::string{user_id},
                    .no_cache = no_cache}));
}

auto BridgeBotOperations::get_onebot11_forward_messages(
    const std::string_view forward_id)
    -> boost::asio::awaitable<obcx::bot::Json> {
  auto result = require(co_await obcx::bot::invoke(
      *client_,
      obcx::onebot11::bot::GetOneBotForwardMessageRequest{
          .installation = onebot11_, .forward_id = std::string{forward_id}}));
  co_return std::move(result.messages);
}

auto BridgeBotOperations::resolve_onebot11_group_file(
    const std::string_view group_id, const std::string_view file_id)
    -> boost::asio::awaitable<std::string> {
  auto result = require(co_await obcx::bot::invoke(
      *client_, obcx::onebot11::bot::ResolveOneBotGroupFileRequest{
                    .target = onebot11_group(std::string{group_id}),
                    .file_id = std::string{file_id}}));
  co_return std::move(result.url);
}

auto BridgeBotOperations::resolve_onebot11_private_file(
    const std::string_view user_id, const std::string_view file_id)
    -> boost::asio::awaitable<std::string> {
  auto result = require(co_await obcx::bot::invoke(
      *client_, obcx::onebot11::bot::ResolveOneBotPrivateFileRequest{
                    .installation = onebot11_,
                    .user_id = std::string{user_id},
                    .file_id = std::string{file_id}}));
  co_return std::move(result.url);
}

auto BridgeBotOperations::poke_onebot11_group(const std::string_view group_id,
                                              const std::string_view user_id)
    -> boost::asio::awaitable<void> {
  (void)require(co_await obcx::bot::invoke(
      *client_, obcx::onebot11::bot::PokeOneBotGroupRequest{
                    .target = onebot11_group(std::string{group_id}),
                    .user_id = std::string{user_id}}));
  co_return;
}

} // namespace bridge
