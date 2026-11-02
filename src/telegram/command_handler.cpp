#include "telegram/command_handler.hpp"

#include "bridge_bot_failures.hpp"
#include "bridge_delivery_sender.hpp"
#include "bridge_hash.hpp"
#include "bridge_message_identity.hpp"
#include "bridge_state_repository.hpp"
#include "bridge_status.hpp"
#include "received_message_repository.hpp"
#include "telegram/reaction_emoji.hpp"
#include <boost/asio/error.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/system/system_error.hpp>

#include <common/logger.hpp>
#include <onebot11/bot/user_target.hpp>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <optional>
#include <utility>

namespace bridge::telegram {
namespace {
template <typename T>
auto command_value(std::string_view type, const T &value)
    -> ledger::OpaqueValue {
  const obcx::bot::Json document = value;
  obcx::bot::detail::validate_domain_wire(document);
  const auto bytes = obcx::bot::Json::to_cbor(document);
  return {"obcx.bot." + std::string{type} + ".cbor", 1,
          std::string(bytes.begin(), bytes.end())};
}
// Call only after retaining/settling mutation results. Cancellation must not
// become permission to send command feedback, even without an Asio flag.
void check_command_cancellation(
    bool cancelled, const std::optional<obcx::bot::BotOperationError> &error,
    std::exception_ptr interruption) {
  if (error && error->code == obcx::bot::BotOperationErrorCode::Cancelled)
    throw BridgeBotOperationFailure(*error);
  if (interruption) {
    try {
      std::rethrow_exception(interruption);
    } catch (const std::exception &failure) {
      if (is_bridge_operation_cancelled(failure))
        throw;
    } catch (...) {
      // Other failures keep the existing uncertain-result feedback policy.
    }
  }
  if (cancelled)
    throw boost::system::system_error(boost::asio::error::operation_aborted);
}
template <typename Result>
void check_mutation_cancellation(const Result &result) {
  for (const auto &target : result.targets)
    if (target.outcome)
      check_command_cancellation(false, target.outcome->error, {});
  check_command_cancellation(result.cancelled, std::nullopt, {});
}
struct PokeSelection {
  std::optional<obcx::onebot11::bot::PokeOneBotGroupRequest> request;
  std::optional<ledger::SourceBinding> binding;
  obcx::bot::Json evidence;
  bool replayed = false;
};
} // namespace

TelegramCommandHandler::TelegramCommandHandler(
    std::shared_ptr<BridgeCommandOperations> operations,
    std::size_t maximum_image_bytes,
    std::shared_ptr<bridge::BridgeStateRepository> state_repository,
    std::shared_ptr<bridge::ReceivedMessageRepository>
        received_message_repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking_executor)
    : operations_(std::move(operations)),
      maximum_image_bytes_(maximum_image_bytes),
      state_repository_(std::move(state_repository)),
      received_message_repository_(std::move(received_message_repository)),
      blocking_executor_(std::move(blocking_executor)) {
  if (!operations_) {
    throw std::invalid_argument(
        "TelegramCommandHandler requires bot operations");
  }
  obcx::bot::SendLimits{1, maximum_image_bytes_, maximum_image_bytes_}
      .validate();
  if (maximum_image_bytes_ == 0)
    throw std::invalid_argument(
        "profile image byte budget must be explicit and positive");
}

void TelegramCommandHandler::validate_source(
    const BridgeCommandSource &source) const {
  source.received.validate();
  if (source.received.message.conversation.installation !=
      operations_->telegram_installation())
    throw std::invalid_argument(
        "Telegram command source installation mismatch");
  validate_message_scope({operations_->telegram_installation().installation_id,
                          "telegram", source.conversation_id},
                         "command source");
}

auto TelegramCommandHandler::handle_recall_command(
    BridgeCommandSource source, const std::string_view qq_group_id)
    -> boost::asio::awaitable<void> {
  try {
    validate_source(source);
    if (!source.received.reply || !source.received.reply->reference()) {
      co_await send_reply_message(
          source.received.message,
          "⚠️ 请回复一条消息后使用 /recall 命令来撤回对应的QQ消息");
      co_return;
    }
    if (state_repository_) {
      if (!blocking_executor_)
        throw std::logic_error("recall storage requires blocking executor");
      std::optional<BridgeRecallResult> recalled;
      bool unresolved = false;
      try {
        recalled = co_await recall_bridge_delivery(
            operations_, state_repository_, blocking_executor_,
            *source.received.reply, std::string{qq_group_id});
      } catch (const std::exception &error) {
        if (is_bridge_operation_cancelled(error))
          throw;
        unresolved = true;
      }
      if (unresolved) {
        co_await send_reply_message(source.received.message,
                                    "❌ 撤回未完成或结果不确定；不会自动重试");
        co_return;
      }
      if (recalled) {
        check_mutation_cancellation(*recalled);
        // Re-delivery of the same command cannot repeat deletions, continue
        // an unattempted suffix or emit another success/feedback message.
        if (recalled->replayed)
          co_return;
        std::string feedback;
        if (recalled->targets.empty()) {
          feedback = "❌ 没有可证实、可单独撤回的QQ消息";
        } else {
          std::size_t confirmed = 0, failed = 0, unknown = 0, uncalled = 0;
          bool persisted = true;
          for (const auto &output : recalled->targets) {
            if (!output.outcome) {
              ++uncalled;
              continue;
            }
            persisted &= output.persisted;
            const auto &outcome = *output.outcome;
            if (outcome.ok() && outcome.value->message == output.message)
              ++confirmed;
            else if (!outcome.value && outcome.error &&
                     outcome.error->submission_safety ==
                         obcx::bot::SubmissionSafety::DefinitelyNotSubmitted)
              ++failed;
            else
              ++unknown;
          }
          if (!recalled->interrupted && persisted &&
              confirmed == recalled->targets.size()) {
            feedback = recalled->original_source
                           ? "✅ 已撤回QQ原消息；转发副本可能仍存在"
                           : "✅ 已撤回 " + std::to_string(confirmed) +
                                 " 条已知QQ输出";
          } else {
            feedback = "⚠️ 本次撤回：已确认 " + std::to_string(confirmed) +
                       "，失败 " + std::to_string(failed) + "，未知 " +
                       std::to_string(unknown) + "，未调用 " +
                       std::to_string(uncalled) +
                       "。未自动重试；未落库的结果不能视为安全重试。";
          }
        }
        co_await send_reply_message(source.received.message,
                                    std::move(feedback));
        co_return;
      }
    }
    co_await send_reply_message(
        source.received.message,
        "❌ 未找到该消息的当前投递记录，不能安全撤回QQ消息");
  } catch (const std::exception &error) {
    OBCX_ERROR("处理 /recall 命令时出错: {}", error.what());
    throw;
  }
}

auto TelegramCommandHandler::send_reply_message(obcx::bot::MessageRef source,
                                                std::string text)
    -> boost::asio::awaitable<void> {
  auto outcome =
      co_await operations_->reply_text(std::move(source), std::move(text));
  if (auto *failure = std::get_if<obcx::bot::SendFailure>(&outcome))
    throw BridgeBotSendFailure(std::move(*failure));
}

auto TelegramCommandHandler::handle_bridge_status_command(
    obcx::bot::MessageRef source) -> boost::asio::awaitable<void> {
  try {
    source.validate();
    if (source.conversation.installation !=
        operations_->telegram_installation()) {
      throw std::invalid_argument(
          "Telegram status source installation mismatch");
    }
    const auto now = std::chrono::system_clock::now();
    const auto telegram_installation =
        operations_->telegram_installation().installation_id;
    const auto onebot_installation =
        operations_->onebot11_installation().installation_id;

    std::optional<storage::PlatformHeartbeatInfo> qq_activity;
    std::optional<storage::PlatformHeartbeatInfo> telegram_activity;
    if (state_repository_) {
      std::tie(qq_activity, telegram_activity) =
          co_await blocking_executor_->run([repository = state_repository_,
                                            telegram_installation,
                                            onebot_installation, now] {
            (void)repository->update_platform_heartbeat(telegram_installation,
                                                        "telegram", now);
            return std::pair{
                repository->get_platform_heartbeat(onebot_installation),
                repository->get_platform_heartbeat(telegram_installation)};
          });
    }

    const auto response =
        render_bridge_status(onebot_installation, qq_activity,
                             telegram_installation, telegram_activity, now);
    auto outcome =
        co_await operations_->reply_text(std::move(source), response);
    if (auto *failure = std::get_if<obcx::bot::SendFailure>(&outcome)) {
      throw BridgeBotSendFailure(std::move(*failure));
    }
  } catch (const std::exception &error) {
    OBCX_ERROR("处理 /bridge_status 命令时出错: {}", error.what());
    throw;
  }
}

auto TelegramCommandHandler::handle_userinfo_command(
    BridgeCommandSource source, const std::string_view qq_group_id,
    std::string arguments) -> boost::asio::awaitable<void> {
  validate_source(source);
  std::string avatar_url;
  const auto query = [&]() -> boost::asio::awaitable<std::string> {
    try {
      constexpr std::string_view usage =
          "⚠️ 请回复一条从QQ转发的消息并使用 /userinfo，或使用 /userinfo <QQ号>";
      std::string target_user_id;
      std::optional<obcx::bot::UserRef> observed_user;
      auto argument = std::string_view{arguments};
      const auto first = argument.find_first_not_of(" \t\r\n");
      if (first != std::string_view::npos) {
        argument = argument.substr(first);
        argument = argument.substr(0, argument.find_last_not_of(" \t\r\n") + 1);
        std::int64_t id = 0;
        const auto [end, error] = std::from_chars(
            argument.data(), argument.data() + argument.size(), id);
        if (error != std::errc{} || end != argument.data() + argument.size() ||
            id <= 0) {
          co_return std::string{usage};
        }
        target_user_id = std::to_string(id);
      } else {
        if (!source.received.reply || !source.received.reply->reference())
          co_return std::string{usage};
        if (!state_repository_ || !received_message_repository_ ||
            !blocking_executor_) {
          co_return "❌ 消息记录不可用，请使用 /userinfo <QQ号>";
        }
        observed_user = co_await blocking_executor_->run(
            [state = state_repository_, received = received_message_repository_,
             pair_id = operations_->pair_id(), reply = *source.received.reply,
             original_scope = operations_->onebot11_conversation(
                 qq_group_id)] -> std::optional<obcx::bot::UserRef> {
              const auto original = resolve_bridge_delivery_source(
                  *state, pair_id, original_scope, reply);
              if (!original)
                return std::nullopt;
              const auto record = received->get_message("qq", *original);
              if (!record || !record->message)
                return std::nullopt;
              if (const auto *known = std::get_if<obcx::bot::KnownUser>(
                      &record->message->sender))
                return known->user;
              return std::nullopt;
            });
        if (!observed_user) {
          co_return "❌ "
                    "未找到原始QQ用户，消息可能不是从QQ转发或记录已过期，请使用"
                    " /userinfo <QQ号>";
        }
      }
      const auto info = observed_user
                            ? co_await operations_->get_onebot11_user_info(
                                  *observed_user, true)
                            : co_await operations_->get_onebot11_user_info(
                                  target_user_id, true);
      const auto user_id = std::to_string(
          obcx::onebot11::bot::user_identity_view(info.user).user_id);
      avatar_url = "https://q1.qlogo.cn/g?b=qq&nk=" + user_id + "&s=640";
      const auto sex = info.sex == "male"     ? "男"
                       : info.sex == "female" ? "女"
                                              : "未知";
      co_return "QQ: " + user_id + "\n昵称: " + info.nickname +
          "\n性别: " + sex + "\n年龄: " + std::to_string(info.age);
    } catch (const std::exception &error) {
      if (is_bridge_operation_cancelled(error))
        throw;
      avatar_url.clear();
      OBCX_WARN("/userinfo lookup failed");
      co_return "❌ QQ用户信息查询失败，请稍后重试";
    }
  };
  const auto text = co_await query();
  if (!avatar_url.empty()) {
    auto outcome = co_await operations_->reply_image(
        source.received.message,
        obcx::bot::HttpSource{std::move(avatar_url), {}}, text,
        maximum_image_bytes_);
    auto *failure = std::get_if<obcx::bot::SendFailure>(&outcome);
    if (!failure)
      co_return;
    // The existing text fallback is allowed only after proven zero effects,
    // never after cancellation (including a typed result without an Asio flag).
    // Never add a second message after a confirmed or uncertain photo send.
    if (failure->error.code == obcx::bot::BotOperationErrorCode::Cancelled ||
        !failure->confirmed_messages.empty() ||
        failure->may_have_unconfirmed_messages ||
        failure->error.submission_safety !=
            obcx::bot::SubmissionSafety::DefinitelyNotSubmitted)
      throw BridgeBotSendFailure(std::move(*failure));
    OBCX_WARN("/userinfo avatar was not submitted; falling back to text");
  }
  co_await send_reply_message(source.received.message, text);
}

auto TelegramCommandHandler::handle_reaction_command(
    BridgeCommandSource source, const std::string_view qq_group_id,
    std::string arguments, const bool set) -> boost::asio::awaitable<void> {
  const std::string_view command = set ? "react" : "unreact";
  const std::string_view verb = set ? "添加" : "移除";
  // Mutation failures produce feedback, but feedback failures propagate.
  // The actor boundary forbids whole-command retries for these mutations.
  try {
    validate_source(source);

    // Keep every returned receipt in the handler frame through feedback,
    // including when journal finalization failed.
    std::optional<BridgeReactionResult> changed;
    const auto mutate =
        [&]() -> boost::asio::awaitable<std::optional<std::string>> {
      bool mutation_started = false;
      try {
        const auto usage = std::string{"⚠️ 请回复一条桥接消息并使用 /"} +
                           std::string{command} + " <emoji>，例如 /" +
                           std::string{command} + " 👍";
        const auto emoji_id = reaction_emoji_id(arguments);
        if (!emoji_id) {
          co_return std::string{usage} +
              "\n请输入单个Unicode表情（如 👍 ❤️ ㊗️），可带显示选择符；"
              "不支持数字ID、组合表情或额外文字";
        }
        if (!source.received.reply || !source.received.reply->reference()) {
          co_return std::string{usage};
        }
        if (!state_repository_ || !blocking_executor_) {
          co_return std::string{"❌ 消息映射不可用，无法"} + std::string{verb} +
              "QQ表情回应";
        }
        changed = co_await react_bridge_delivery(
            operations_, state_repository_, blocking_executor_,
            *source.received.reply, std::string{qq_group_id}, *emoji_id, set);
        if (!changed)
          co_return "❌ 未找到对应的QQ消息，可能不是桥接消息或映射已过期";
        check_mutation_cancellation(*changed);
        if (changed->replayed)
          co_return std::nullopt;
        mutation_started = true;
        std::size_t confirmed = 0;
        bool recorded = changed->command_persisted;
        for (const auto &item : changed->targets) {
          if (item.matched_receipt)
            ++confirmed;
          recorded = recorded && item.persisted;
        }
        if (!changed->interrupted && recorded && !changed->targets.empty() &&
            confirmed == changed->targets.size()) {
          if (changed->original_source)
            co_return std::string{"✅ 已"} + std::string{verb} +
                "QQ原消息的表情回应";
          co_return std::string{"✅ 已对"} + std::to_string(confirmed) +
              "条已确认QQ输出" + std::string{verb} + "表情回应";
        }
        co_return std::string{"⚠️ QQ表情回应未全部完成或记录不完整（确认"} +
            std::to_string(confirmed) + "/" +
            std::to_string(changed->targets.size()) +
            "）；请核对QQ状态，不会自动重试";
      } catch (const std::exception &error) {
        if (is_bridge_operation_cancelled(error))
          throw;
        if (!mutation_started) {
          OBCX_WARN("/{} message lookup failed", command);
          co_return std::string{"❌ 消息映射查询失败，未"} + std::string{verb} +
              "表情回应";
        }
        const auto failure = classify_bridge_operation_failure(error);
        OBCX_WARN("/{} typed OneBot operation failed: {}", command,
                  failure.diagnostic);
        co_return failure.outcome_unknown
            ? std::string{"⚠️ "} + std::string{verb} +
                  "QQ表情回应结果未知，请先在QQ确认；未自动重试"
            : std::string{"❌ "} + std::string{verb} +
                  "QQ表情回应失败，消息可能已过期或服务不支持该表情";
      }
    };
    const auto text = co_await mutate();
    if (text)
      co_await send_reply_message(source.received.message, *text);
  } catch (...) {
    OBCX_WARN(
        "/{} could not complete feedback; the mutation will not be retried",
        command);
    throw;
  }
}

auto TelegramCommandHandler::handle_poke_command(
    BridgeCommandSource source, const std::string_view qq_group_id)
    -> boost::asio::awaitable<void> {
  try {
    validate_source(source);
    const auto onebot_installation =
        operations_->onebot11_installation().installation_id;
    if (!source.received.reply || !source.received.reply->reference()) {
      co_await send_reply_message(
          source.received.message,
          "⚠️ 请回复一条从QQ转发的消息后使用 /poke 命令");
      co_return;
    }
    if (!state_repository_ || !received_message_repository_ ||
        !blocking_executor_)
      throw std::logic_error(
          "poke requires owned storage and blocking executor");
    const auto reply = *source.received.reply;
    if (reply.source() != source.received.message)
      throw std::invalid_argument("poke requires original command reply");
    const auto pair = operations_->pair_id();
    const auto command = command_value("MessageRef", source.received.message);
    const auto digest = calculate_hash(
        obcx::bot::Json::array({pair, "qq-group-poke", source.received.message})
            .dump());
    if (digest.empty())
      throw std::runtime_error("poke command identity hashing failed");
    const auto operation_id = "bridge-command-v1:" + digest;
    const auto original_scope = operations_->onebot11_conversation(qq_group_id);
    const auto selection = co_await blocking_executor_->run(
        [state = state_repository_, received = received_message_repository_,
         operations = operations_, pair, operation_id, command, reply,
         onebot_installation, original_scope,
         qq_conversation = qq_conversation_id(std::string{qq_group_id}),
         group_id = std::string{qq_group_id}] {
          PokeSelection selected;
          if (auto existing =
                  state->find_command_operation(pair, operation_id)) {
            if (existing->command_ref != command ||
                existing->kind != "qq-group-poke")
              throw std::runtime_error("poke command fence identity conflict");
            selected.replayed = true;
            return selected;
          }
          const auto original = resolve_bridge_delivery_source(
              *state, pair, original_scope, reply);
          if (original) {
            const auto from = command_value("ConversationRef", original_scope);
            const auto to =
                command_value("ConversationRef", reply.source().conversation);
            const auto quoted =
                command_value("MessageRef", reply.reference()->target());
            const auto source_ref = command_value("MessageRef", *original);
            const auto owner =
                state->resolve_reverse_primary(pair, from, to, quoted);
            const auto delivery =
                state->resolve_delivery(pair, from, to, source_ref);
            if (owner.status != ledger::ResolutionStatus::Unique ||
                !owner.primary_member ||
                owner.primary_member->message_ref != source_ref ||
                delivery.status != ledger::ResolutionStatus::Unique ||
                delivery.delivery_ids.size() != 1)
              throw std::runtime_error("poke source ownership changed");
            selected.binding = ledger::SourceBinding{
                delivery.delivery_ids.front(), owner.primary_member->member_id,
                source_ref, quoted};
          } else {
            return selected;
          }
          const auto record = received->get_message("qq", *original);
          if (!record || !record->message)
            return selected;
          if (record->platform != "qq" ||
              record->source_bot != onebot_installation ||
              record->conversation_id != qq_conversation ||
              record->message->message != *original ||
              record->group_id != group_id)
            throw std::runtime_error("poke Store source identity mismatch");
          const auto *known =
              std::get_if<obcx::bot::KnownUser>(&record->message->sender);
          if (!known)
            return selected;
          selected.request =
              operations->prepare_onebot11_poke(group_id, known->user);
          selected.evidence = {{"reply", reply},
                               {"source_platform", "qq"},
                               {"source_bot", onebot_installation},
                               {"source_conversation", qq_conversation},
                               {"source_message_id", record->message_id},
                               {"source_user_id", record->user_id},
                               {"v4_binding", selected.binding.has_value()}};
          return selected;
        });
    if (selection.replayed)
      co_return;
    if (!selection.request) {
      co_await send_reply_message(
          source.received.message,
          "❌ 未找到该消息对应的QQ用户，可能不是从QQ转发的消息或已过期");
      co_return;
    }
    const auto request = *selection.request;
    const ledger::CommandOperation pending{
        .pair_id = pair,
        .operation_id = operation_id,
        .kind = "qq-group-poke",
        .command_ref = command,
        .source_scope =
            command_value("ConversationRef", reply.source().conversation),
        .target_scope = command_value("ConversationRef", request.target),
        .request = command_value("PokeOneBotGroupRequest", request),
        .evidence = command_value("BridgePokeEvidence", selection.evidence),
        .source_binding = selection.binding,
        .outcome = std::nullopt};
    // Own uncertainty before any call. Once admitted, no catch/replay path can
    // invoke again, even if the actual receipt cannot be serialized or stored.
    auto outcome =
        obcx::bot::failed_operation<obcx::onebot11::bot::OneBotGroupPokeResult>(
            obcx::bot::BotOperationErrorCode::OutcomeUnknown,
            "poke invocation outcome unavailable", false,
            obcx::bot::SubmissionSafety::PossiblySubmitted);
    std::exception_ptr interruption;
    bool persisted = false;
    bool matched = false;
    bool replayed = false;
    const auto cancellation =
        co_await boost::asio::this_coro::cancellation_state;
    const auto previous = co_await boost::asio::this_coro::throw_if_cancelled();
    co_await boost::asio::this_coro::throw_if_cancelled(false);
    try {
      if (cancellation.cancelled() == boost::asio::cancellation_type::none) {
        const auto admitted = co_await blocking_executor_->run(
            [state = state_repository_, pending] {
              return state->admit_command_operation(pending);
            });
        replayed = admitted != ledger::AdmissionResult::Created;
        if (!replayed) {
          if (cancellation.cancelled() != boost::asio::cancellation_type::none)
            outcome = obcx::bot::failed_operation<
                obcx::onebot11::bot::OneBotGroupPokeResult>(
                obcx::bot::BotOperationErrorCode::Cancelled,
                "poke cancelled before invocation", false,
                obcx::bot::SubmissionSafety::DefinitelyNotSubmitted);
          else
            outcome = co_await operations_->poke_onebot11_group(request);
          outcome.validate();
          matched = outcome.ok() && outcome.value->target == request.target &&
                    outcome.value->user == request.user;
          auto completed = pending;
          completed.outcome = command_value(
              "BridgePokeOutcome", obcx::bot::Json{{"matched_receipt", matched},
                                                   {"outcome", outcome}});
          (void)co_await blocking_executor_->run(
              [state = state_repository_, pending, completed] {
                return state->finish_command_operation(pending, completed);
              });
          persisted = true;
        }
      }
    } catch (...) {
      interruption = std::current_exception();
      // Keep the full owned result until feedback completes. The durable
      // pending entry remains possibly invoked; no timeout authorizes replay.
      try {
        OBCX_WARN(
            "/poke incomplete or result not persisted; no retry authorized");
      } catch (...) {
        // Diagnostics cannot bypass restoration of the caller's cancellation
        // checks or change the retained operation outcome.
      }
    }
    co_await boost::asio::this_coro::throw_if_cancelled(previous);
    check_command_cancellation(cancellation.cancelled() !=
                                   boost::asio::cancellation_type::none,
                               outcome.error, interruption);
    if (replayed)
      co_return;
    if (!persisted || !matched) {
      const bool unknown =
          !persisted || !outcome.error ||
          outcome.error->submission_safety !=
              obcx::bot::SubmissionSafety::DefinitelyNotSubmitted;
      co_await send_reply_message(
          source.received.message,
          unknown ? "⚠️ 戳一戳未完成或结果未知，请先在QQ确认；未自动重试"
                  : "❌ 戳一戳操作失败；未自动重试");
    }
  } catch (const std::exception &error) {
    OBCX_ERROR("处理 /poke 命令时出错: {}", error.what());
    throw;
  }
}

} // namespace bridge::telegram
