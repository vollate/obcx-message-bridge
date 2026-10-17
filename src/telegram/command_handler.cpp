#include "telegram/command_handler.hpp"

#include "bridge_state_repository.hpp"
#include "bridge_status.hpp"
#include "received_message_repository.hpp"
#include "telegram/reaction_emoji.hpp"

#include <common/logger.hpp>

#include <charconv>
#include <chrono>
#include <limits>
#include <optional>
#include <utility>

namespace bridge::telegram {

TelegramCommandHandler::TelegramCommandHandler(
    std::shared_ptr<BridgeBotOperations> operations,
    std::shared_ptr<bridge::BridgeStateRepository> state_repository,
    std::shared_ptr<bridge::ReceivedMessageRepository>
        received_message_repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking_executor)
    : operations_(std::move(operations)),
      state_repository_(std::move(state_repository)),
      received_message_repository_(std::move(received_message_repository)),
      blocking_executor_(std::move(blocking_executor)) {
  if (!operations_) {
    throw std::invalid_argument(
        "TelegramCommandHandler requires bot operations");
  }
}

auto TelegramCommandHandler::handle_recall_command(
    obcx::common::MessageEvent event, const std::string_view qq_group_id)
    -> boost::asio::awaitable<void> {
  try {
    const auto telegram_group_id = event.group_id.value();
    const auto telegram_installation =
        operations_->telegram_installation().installation_id;
    const auto onebot_installation =
        operations_->onebot11_installation().installation_id;
    if (!event.data.contains("reply_to_message") ||
        !event.data["reply_to_message"].contains("message_id")) {
      co_await send_reply_message(
          telegram_group_id, event.message_id,
          "⚠️ 请回复一条消息后使用 /recall 命令来撤回对应的QQ消息");
      co_return;
    }
    const auto replied_message_id = std::to_string(
        event.data["reply_to_message"]["message_id"].get<std::int64_t>());
    const auto telegram_conversation =
        telegram_conversation_id(telegram_group_id);
    const auto qq_conversation = qq_conversation_id(std::string{qq_group_id});
    MessageMappingResolution selected;
    if (state_repository_) {
      selected = co_await blocking_executor_->run(
          [repository = state_repository_, telegram_installation,
           onebot_installation, telegram_conversation, qq_conversation,
           replied_message_id] {
            auto direct = repository->resolve_target_mapping(
                {.installation_id = telegram_installation,
                 .platform = "telegram",
                 .conversation_id = telegram_conversation,
                 .message_id = replied_message_id},
                {.installation_id = onebot_installation,
                 .platform = "qq",
                 .conversation_id = qq_conversation});
            if (!direct.missing()) {
              return direct;
            }
            return repository->resolve_source_mapping(
                {.installation_id = telegram_installation,
                 .platform = "telegram",
                 .conversation_id = telegram_conversation,
                 .message_id = replied_message_id},
                {.installation_id = onebot_installation,
                 .platform = "qq",
                 .conversation_id = qq_conversation});
          });
    }
    if (selected.missing()) {
      co_await send_reply_message(
          telegram_group_id, event.message_id,
          "❌ 未找到该消息对应的QQ消息，可能不是转发消息或已过期");
      co_return;
    }
    if (!selected.unique()) {
      OBCX_WARN("/recall mapping resolution failed: {}",
                selected.diagnostic.empty() ? "ambiguous_message_mapping"
                                            : selected.diagnostic);
      co_return;
    }

    const auto target_message_id =
        selected.mapping->source_platform == "telegram"
            ? selected.mapping->target_message_id
            : selected.mapping->source_message_id;
    std::string reply;
    try {
      co_await operations_->delete_onebot11_message(qq_group_id,
                                                    target_message_id);
      reply = "✅ 撤回成功";
      if (state_repository_) {
        const auto mapping = *selected.mapping;
        (void)co_await blocking_executor_->run(
            [repository = state_repository_, mapping] {
              return repository->delete_message_mapping(
                  {.installation_id = mapping.source_installation,
                   .platform = mapping.source_platform,
                   .conversation_id = mapping.source_conversation_id,
                   .message_id = mapping.source_message_id},
                  {.installation_id = mapping.target_installation,
                   .platform = mapping.target_platform,
                   .conversation_id = mapping.target_conversation_id});
            });
      }
    } catch (const std::exception &error) {
      OBCX_WARN("/recall typed QQ delete failed: {}", error.what());
      reply = "❌ 撤回操作失败，请稍后重试";
    }
    co_await send_reply_message(telegram_group_id, event.message_id, reply);
  } catch (const std::exception &error) {
    OBCX_ERROR("处理 /recall 命令时出错: {}", error.what());
  }
}

auto TelegramCommandHandler::send_reply_message(
    const std::string &telegram_group_id,
    const std::string &reply_to_message_id, const std::string &text)
    -> boost::asio::awaitable<void> {
  obcx::common::Message message{
      {.type = "reply", .data = {{"id", reply_to_message_id}}},
      {.type = "text", .data = {{"text", text}}},
  };
  (void)co_await operations_->send_telegram_group(telegram_group_id, message);
}

auto TelegramCommandHandler::handle_bridge_status_command(
    obcx::common::MessageEvent event, const std::string_view qq_group_id)
    -> boost::asio::awaitable<void> {
  (void)qq_group_id;
  try {
    const auto telegram_group_id = event.group_id.value();
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
    co_await send_reply_message(telegram_group_id, event.message_id, response);
  } catch (const std::exception &error) {
    OBCX_ERROR("处理 /bridge_status 命令时出错: {}", error.what());
  }
}

auto TelegramCommandHandler::handle_userinfo_command(
    obcx::common::MessageEvent event, const std::string_view qq_group_id,
    std::string arguments) -> boost::asio::awaitable<void> {
  std::string avatar_url;
  const auto query = [&]() -> boost::asio::awaitable<std::string> {
    try {
      constexpr std::string_view usage =
          "⚠️ 请回复一条从QQ转发的消息并使用 /userinfo，或使用 /userinfo <QQ号>";
      std::string target_user_id;
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
        if (!event.data.contains("reply_to_message") ||
            !event.data["reply_to_message"].is_object() ||
            !event.data["reply_to_message"].contains("message_id") ||
            !event.data["reply_to_message"]["message_id"].is_number_integer()) {
          co_return std::string{usage};
        }
        if (!state_repository_ || !received_message_repository_ ||
            !blocking_executor_) {
          co_return "❌ 消息记录不可用，请使用 /userinfo <QQ号>";
        }
        const auto replied_message_id = std::to_string(
            event.data["reply_to_message"]["message_id"].get<std::int64_t>());
        const auto telegram_installation =
            operations_->telegram_installation().installation_id;
        const auto onebot_installation =
            operations_->onebot11_installation().installation_id;
        const auto telegram_conversation =
            telegram_conversation_id(*event.group_id);
        const auto qq_conversation =
            qq_conversation_id(std::string{qq_group_id});
        bool ambiguous = false;
        std::tie(target_user_id, ambiguous) = co_await blocking_executor_->run(
            [state = state_repository_, received = received_message_repository_,
             telegram_installation, onebot_installation, telegram_conversation,
             qq_conversation, replied_message_id] {
              const auto resolution = state->resolve_source_mapping(
                  {.installation_id = telegram_installation,
                   .platform = "telegram",
                   .conversation_id = telegram_conversation,
                   .message_id = replied_message_id},
                  {.installation_id = onebot_installation,
                   .platform = "qq",
                   .conversation_id = qq_conversation});
              if (!resolution.unique()) {
                return std::pair{std::string{}, !resolution.missing()};
              }
              if (auto source = received->get_message(
                      "qq", onebot_installation, qq_conversation,
                      resolution.mapping->source_message_id)) {
                return std::pair{source->user_id, false};
              }
              return std::pair{std::string{}, false};
            });
        if (ambiguous) {
          co_return "❌ 消息映射不唯一或已损坏，请使用 /userinfo <QQ号>";
        }
        if (target_user_id.empty()) {
          co_return "❌ "
                    "未找到原始QQ用户，消息可能不是从QQ转发或记录已过期，请使用"
                    " /userinfo <QQ号>";
        }
      }
      const auto info =
          co_await operations_->get_onebot11_user_info(target_user_id, true);
      avatar_url = "https://q1.qlogo.cn/g?b=qq&nk=" + info.user_id + "&s=640";
      const auto sex = info.sex == "male"     ? "男"
                       : info.sex == "female" ? "女"
                                              : "未知";
      co_return "QQ: " + info.user_id + "\n昵称: " + info.nickname +
          "\n性别: " + sex + "\n年龄: " + std::to_string(info.age);
    } catch (const std::exception &) {
      avatar_url.clear();
      OBCX_WARN("/userinfo lookup failed");
      co_return "❌ QQ用户信息查询失败，请稍后重试";
    }
  };
  const auto text = co_await query();
  const auto topic_id =
      event.data.contains("message_thread_id")
          ? std::optional{event.data["message_thread_id"].get<std::int64_t>()}
          : std::nullopt;
  if (!avatar_url.empty()) {
    try {
      (void)co_await operations_->send_telegram_photo(
          event.group_id.value(), std::move(avatar_url), text, {}, topic_id,
          event.message_id);
      co_return;
    } catch (const std::exception &) {
      // Do not retry the photo: a timeout can leave its submission uncertain.
      // Preserve the queried profile even when Telegram cannot fetch the
      // avatar.
      OBCX_WARN("/userinfo avatar send failed; falling back to text");
    }
  }
  obcx::common::Message reply{
      {.type = "reply", .data = {{"id", event.message_id}}},
      {.type = "text", .data = {{"text", text}}},
  };
  if (topic_id) {
    (void)co_await operations_->send_telegram_topic(event.group_id.value(),
                                                    *topic_id, reply);
  } else {
    (void)co_await operations_->send_telegram_group(event.group_id.value(),
                                                    reply);
  }
}

auto TelegramCommandHandler::handle_reaction_command(
    obcx::common::MessageEvent event, const std::string_view qq_group_id,
    std::string arguments, const bool set) -> boost::asio::awaitable<void> {
  const std::string_view command = set ? "react" : "unreact";
  const std::string_view verb = set ? "添加" : "移除";
  // Keep all failures inside the command: actor retries must not repeat a
  // possibly-submitted mutation, including when Telegram feedback fails.
  try {
    if (!event.group_id || event.group_id->empty()) {
      co_return;
    }
    const auto positive_integer =
        [](const obcx::common::json &value) -> std::optional<std::int64_t> {
      if (!value.is_number_integer() ||
          (value.is_number_unsigned() &&
           value.get<std::uint64_t>() >
               static_cast<std::uint64_t>(
                   std::numeric_limits<std::int64_t>::max()))) {
        return std::nullopt;
      }
      const auto id = value.get<std::int64_t>();
      return id > 0 ? std::optional{id} : std::nullopt;
    };
    std::optional<std::int64_t> topic_id;
    if (event.data.contains("message_thread_id")) {
      topic_id = positive_integer(event.data.at("message_thread_id"));
      if (!topic_id) {
        OBCX_WARN("/{} received invalid Telegram topic identity", command);
        co_return;
      }
    }

    const auto mutate = [&]() -> boost::asio::awaitable<std::string> {
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
        if (!event.data.contains("reply_to_message") ||
            !event.data.at("reply_to_message").is_object() ||
            !event.data.at("reply_to_message").contains("message_id")) {
          co_return std::string{usage};
        }
        const auto replied_id = positive_integer(
            event.data.at("reply_to_message").at("message_id"));
        if (!replied_id) {
          co_return std::string{usage};
        }
        if (!state_repository_ || !blocking_executor_) {
          co_return std::string{"❌ 消息映射不可用，无法"} + std::string{verb} +
              "QQ表情回应";
        }
        const auto telegram_installation =
            operations_->telegram_installation().installation_id;
        const auto onebot_installation =
            operations_->onebot11_installation().installation_id;
        const auto telegram_conversation =
            telegram_conversation_id(*event.group_id);
        const auto qq_conversation =
            qq_conversation_id(std::string{qq_group_id});
        const auto replied_message_id = std::to_string(*replied_id);
        const auto selected = co_await blocking_executor_->run(
            [repository = state_repository_, telegram_installation,
             onebot_installation, telegram_conversation, qq_conversation,
             replied_message_id] {
              auto direct = repository->resolve_target_mapping(
                  {.installation_id = telegram_installation,
                   .platform = "telegram",
                   .conversation_id = telegram_conversation,
                   .message_id = replied_message_id},
                  {.installation_id = onebot_installation,
                   .platform = "qq",
                   .conversation_id = qq_conversation});
              if (!direct.missing()) {
                return direct;
              }
              return repository->resolve_source_mapping(
                  {.installation_id = telegram_installation,
                   .platform = "telegram",
                   .conversation_id = telegram_conversation,
                   .message_id = replied_message_id},
                  {.installation_id = onebot_installation,
                   .platform = "qq",
                   .conversation_id = qq_conversation});
            });
        if (selected.missing()) {
          co_return "❌ 未找到对应的QQ消息，可能不是桥接消息或映射已过期";
        }
        if (!selected.unique()) {
          co_return std::string{"❌ 消息映射不唯一或已损坏，未"} +
              std::string{verb} + "表情回应";
        }
        const auto &mapping = *selected.mapping;
        const auto &qq_message_id = mapping.source_platform == "telegram"
                                        ? mapping.target_message_id
                                        : mapping.source_message_id;
        mutation_started = true;
        co_await operations_->set_onebot11_message_emoji_like(
            qq_group_id, qq_message_id, *emoji_id, set);
        co_return std::string{"✅ 已"} + std::string{verb} + "QQ表情回应";
      } catch (const std::exception &error) {
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
    obcx::common::Message reply{
        {.type = "reply", .data = {{"id", event.message_id}}},
        {.type = "text", .data = {{"text", text}}},
    };
    if (topic_id) {
      (void)co_await operations_->send_telegram_topic(*event.group_id,
                                                      *topic_id, reply);
    } else {
      (void)co_await operations_->send_telegram_group(*event.group_id, reply);
    }
  } catch (...) {
    OBCX_WARN(
        "/{} could not complete feedback; the mutation will not be retried",
        command);
  }
}

auto TelegramCommandHandler::handle_poke_command(
    obcx::common::MessageEvent event, const std::string_view qq_group_id)
    -> boost::asio::awaitable<void> {
  try {
    const auto telegram_group_id = event.group_id.value();
    const auto telegram_installation =
        operations_->telegram_installation().installation_id;
    const auto onebot_installation =
        operations_->onebot11_installation().installation_id;
    if (!event.data.contains("reply_to_message") ||
        !event.data["reply_to_message"].contains("message_id")) {
      co_await send_reply_message(
          telegram_group_id, event.message_id,
          "⚠️ 请回复一条消息后使用 /poke 命令来戳对应的QQ用户");
      co_return;
    }
    const auto replied_message_id = std::to_string(
        event.data["reply_to_message"]["message_id"].get<std::int64_t>());
    std::string target_user_id;
    bool mapping_ambiguous = false;
    if (state_repository_ || received_message_repository_) {
      const auto telegram_conversation =
          telegram_conversation_id(telegram_group_id);
      const auto qq_conversation = qq_conversation_id(std::string{qq_group_id});
      std::tie(target_user_id, mapping_ambiguous) =
          co_await blocking_executor_->run(
              [state = state_repository_,
               received = received_message_repository_, telegram_installation,
               onebot_installation, telegram_conversation, qq_conversation,
               replied_message_id] {
                if (received) {
                  if (auto source = received->get_message(
                          "telegram", telegram_installation,
                          telegram_conversation, replied_message_id)) {
                    return std::pair{source->user_id, false};
                  }
                }
                if (!state || !received) {
                  return std::pair{std::string{}, false};
                }
                auto resolution = state->resolve_target_mapping(
                    {.installation_id = telegram_installation,
                     .platform = "telegram",
                     .conversation_id = telegram_conversation,
                     .message_id = replied_message_id},
                    {.installation_id = onebot_installation,
                     .platform = "qq",
                     .conversation_id = qq_conversation});
                if (resolution.unique()) {
                  if (auto message = received->get_message(
                          "qq", onebot_installation, qq_conversation,
                          resolution.mapping->target_message_id)) {
                    return std::pair{message->user_id, false};
                  }
                } else if (!resolution.missing()) {
                  return std::pair{std::string{}, true};
                }
                resolution = state->resolve_source_mapping(
                    {.installation_id = telegram_installation,
                     .platform = "telegram",
                     .conversation_id = telegram_conversation,
                     .message_id = replied_message_id},
                    {.installation_id = onebot_installation,
                     .platform = "qq",
                     .conversation_id = qq_conversation});
                if (resolution.unique()) {
                  if (auto message = received->get_message(
                          "qq", onebot_installation, qq_conversation,
                          resolution.mapping->source_message_id)) {
                    return std::pair{message->user_id, false};
                  }
                } else if (!resolution.missing()) {
                  return std::pair{std::string{}, true};
                }
                return std::pair{std::string{}, false};
              });
    }
    if (mapping_ambiguous) {
      OBCX_WARN("/poke mapping resolution failed: ambiguous_message_mapping");
      co_return;
    }
    if (target_user_id.empty()) {
      co_await send_reply_message(
          telegram_group_id, event.message_id,
          "❌ 未找到该消息对应的QQ用户，可能不是从QQ转发的消息或已过期");
      co_return;
    }
    bool poke_failed = false;
    try {
      co_await operations_->poke_onebot11_group(qq_group_id, target_user_id);
    } catch (const std::exception &error) {
      OBCX_WARN("/poke typed OneBot operation failed: {}", error.what());
      poke_failed = true;
    }
    if (poke_failed) {
      co_await send_reply_message(telegram_group_id, event.message_id,
                                  "❌ 戳一戳操作失败，请稍后重试");
    }
  } catch (const std::exception &error) {
    OBCX_ERROR("处理 /poke 命令时出错: {}", error.what());
  }
}

} // namespace bridge::telegram
