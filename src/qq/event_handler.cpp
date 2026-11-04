#include "qq/event_handler.hpp"

#include "bridge_bot_failures.hpp"
#include "bridge_delivery_sender.hpp"
#include "bridge_state_repository.hpp"
#include "config.hpp"
#include "received_message_repository.hpp"

#include <common/logger.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>

namespace bridge::qq {
namespace {

auto json_id(const nlohmann::json &data, const std::string_view key)
    -> std::string {
  const auto field = std::string{key};
  if (!data.contains(field)) {
    return {};
  }
  const auto &value = data.at(field);
  if (value.is_string()) {
    return value.get<std::string>();
  }
  if (value.is_number_integer()) {
    return std::to_string(value.get<std::int64_t>());
  }
  return {};
}

auto sanitize_generated_text(std::string text) -> std::string {
  for (auto &value : text) {
    const auto byte = static_cast<unsigned char>(value);
    if (byte < 0x20U && value != '\n') {
      value = ' ';
    }
  }
  return text;
}

} // namespace

QQEventHandler::QQEventHandler(
    std::shared_ptr<BridgeCommandOperations> operations,
    std::shared_ptr<const bridge::BridgeConfig> config,
    std::shared_ptr<bridge::BridgeStateRepository> state_repository,
    std::shared_ptr<bridge::ReceivedMessageRepository>
        received_message_repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking_executor)
    : operations_(std::move(operations)), config_(std::move(config)),
      state_repository_(std::move(state_repository)),
      received_message_repository_(std::move(received_message_repository)),
      blocking_executor_(std::move(blocking_executor)) {
  if (!operations_) {
    throw std::invalid_argument("QQEventHandler requires bot operations");
  }
}

auto QQEventHandler::handle_recall_event(obcx::bot::MessageDeleted deleted)
    -> boost::asio::awaitable<void> {
  try {
    deleted.validate();
    // The typed observation is authoritative. Recover only the original
    // configured spelling, not a group ID guessed from the archive payload.
    const auto qq_group_id =
        operations_->onebot11_group_id(deleted.message.conversation);
    // Notice ingress includes conversations that this pair does not bridge.
    // Ignore them before archive reads or provider operations.
    if (!qq_group_id)
      co_return;
    const auto [telegram_group_id, topic_id] =
        config_->tg_group_and_topic_id(operations_->pair_id(), *qq_group_id);
    const auto *route =
        config_->bridge_config(operations_->pair_id(), telegram_group_id);
    if (telegram_group_id.empty() || route == nullptr) {
      co_return;
    }

    if (!state_repository_ || !blocking_executor_)
      throw std::runtime_error("recall propagation storage unavailable");
    std::optional<storage::MessageInfo> original;
    if (received_message_repository_) {
      original = co_await blocking_executor_->run(
          [received = received_message_repository_, message = deleted.message] {
            return received->get_message("qq", message);
          });
    }
    bool picture = false;
    std::optional<obcx::bot::MessageReceived> source;
    if (original) {
      if (!original->message || original->message->message != deleted.message)
        throw std::runtime_error("recall typed source identity mismatch");
      source = *original->message;
      picture = std::ranges::any_of(source->body, [](const auto &block) {
        const auto *media = std::get_if<obcx::bot::ReceivedMedia>(&block);
        return media && media->kind == obcx::bot::MediaKind::Image;
      });
    }
    bool show_sender = route->show_qq_to_tg_sender;
    if (route->mode == BridgeMode::TOPIC_TO_GROUP) {
      const auto *topic = config_->topic_config(operations_->pair_id(),
                                                telegram_group_id, topic_id);
      show_sender = topic != nullptr && topic->show_qq_to_tg_sender;
    }
    obcx::telegram::bot::FormattedText text;
    if (!picture && show_sender && original && source) {
      std::string sender = "Unknown";
      if (const auto *known =
              std::get_if<obcx::bot::KnownUser>(&source->sender)) {
        const auto member =
            co_await obcx::bot::BotClient{*operations_->operation_gateway()}
                .get_member({source->message.conversation, known->user});
        if (!member.ok() &&
            member.error->code == obcx::bot::BotOperationErrorCode::Cancelled)
          throw BridgeBotOperationFailure(*member.error);
        sender = member.ok() && member.value->display_name
                     ? *member.value->display_name
                     : known->user.id.serialized();
      }
      text.emplace_back(obcx::bot::Text{
          fmt::format("[{}] ", sanitize_generated_text(sender)), {}});
    }
    if (!picture)
      text.emplace_back(
          obcx::telegram::bot::StrikethroughText{"Message has been recalled"});
    auto result = co_await propagate_bridge_recall(
        operations_, state_repository_, blocking_executor_, deleted, picture,
        std::move(text));
    if (!result) {
      throw BridgeRecallPropagationFailure(
          "QQ recall unavailable: historical target binding missing",
          std::nullopt);
    }
    if (result->interrupted || !result->completeness_known)
      throw BridgeRecallPropagationFailure(
          "QQ recall incomplete or uncertain; no automatic retry",
          std::move(result));
    if (result->replayed)
      OBCX_DEBUG("QQ recall already admitted; no provider replay");
  } catch (const BridgeRecallPropagationFailure &) {
    throw;
  } catch (const std::exception &error) {
    throw BridgeRecallPropagationFailure(
        fmt::format("QQ recall preparation failed; no automatic retry: {}",
                    error.what()),
        std::nullopt);
  }
}

auto QQEventHandler::handle_poke_event(const obcx::common::NoticeEvent &event)
    -> boost::asio::awaitable<void> {
  try {
    if (!event.group_id.has_value()) {
      co_return;
    }
    const auto qq_group_id = *event.group_id;
    const auto user_id = json_id(event.data, "user_id");
    const auto target_id = json_id(event.data, "target_id");
    if (user_id.empty() || target_id.empty()) {
      co_return;
    }
    const int poke_type =
        event.data.value("poke_type", event.data.value("type", 1));
    const int poke_id = event.data.value("poke_id", event.data.value("id", -1));
    auto action = get_poke_action_name(poke_type, poke_id);
    if (event.data.contains("action_text") &&
        event.data["action_text"].is_string()) {
      action = event.data["action_text"].get<std::string>();
    } else if (event.data.contains("action") &&
               event.data["action"].is_string()) {
      action = event.data["action"].get<std::string>();
    }
    const auto suffix = event.data.value("suffix", std::string{});

    const auto [telegram_group_id, topic_id] =
        config_->tg_group_and_topic_id(operations_->pair_id(), qq_group_id);
    const auto *route =
        config_->bridge_config(operations_->pair_id(), telegram_group_id);
    if (telegram_group_id.empty() || route == nullptr) {
      co_return;
    }
    bool enabled = route->enable_qq_to_tg;
    bool show_names = route->show_qq_to_tg_sender;
    if (route->mode == BridgeMode::TOPIC_TO_GROUP) {
      const auto *topic = config_->topic_config(operations_->pair_id(),
                                                telegram_group_id, topic_id);
      enabled = topic != nullptr && topic->enable_qq_to_tg;
      show_names = topic != nullptr && topic->show_qq_to_tg_sender;
    }
    if (!enabled) {
      co_return;
    }

    std::string user = " ";
    std::string target = " ";
    if (show_names) {
      user = co_await fetch_user_display_name(user_id, qq_group_id);
      target = co_await fetch_user_display_name(target_id, qq_group_id);
    }
    const auto text =
        suffix.empty()
            ? fmt::format("[{}] {} [{}]", user, action, target)
            : fmt::format("[{}] {} [{}]{}", user, action, target, suffix);
    auto target_scope = operations_->telegram_conversation(
        telegram_group_id,
        route->mode == BridgeMode::TOPIC_TO_GROUP && topic_id > 0
            ? std::optional<std::string>{std::to_string(topic_id)}
            : std::nullopt);
    auto outcome =
        co_await operations_->send_telegram_text(std::move(target_scope), text);
    if (auto *failure = std::get_if<obcx::bot::SendFailure>(&outcome))
      throw BridgeBotSendFailure(std::move(*failure));
  } catch (const std::exception &error) {
    OBCX_ERROR("处理戳一戳事件时出错: {}", error.what());
  }
}

auto QQEventHandler::get_poke_action_name(const int poke_type,
                                          const int poke_id) -> std::string {
  if (poke_id == -1 || poke_id == 0) {
    switch (poke_type) {
    case 1:
      return "戳了戳";
    case 2:
      return "比了个心";
    case 3:
      return "点了个赞";
    case 4:
      return "心碎了";
    case 5:
      return "给了一个666";
    case 6:
      return "放了个大招给";
    default:
      break;
    }
  }
  if (poke_type == 126) {
    switch (poke_id) {
    case 2001:
      return "抓了一下";
    case 2002:
      return "碎了屏给";
    case 2003:
      return "勾引了";
    case 2004:
      return "扔了个手雷给";
    case 2005:
      return "结了个印给";
    case 2006:
      return "使用了召唤术召唤";
    case 2007:
      return "送了朵玫瑰花给";
    case 2009:
      return "让你皮了";
    case 2011:
      return "扔了个宝贝球给";
    default:
      break;
    }
  }
  return "戳了戳";
}

auto QQEventHandler::fetch_user_display_name(const std::string &user_id,
                                             const std::string &group_id)
    -> boost::asio::awaitable<std::string> {
  std::optional<std::string> display_name;
  if (state_repository_) {
    const auto installation =
        operations_->onebot11_installation().installation_id;
    display_name = co_await blocking_executor_->run(
        [repository = state_repository_, installation, user_id, group_id] {
          return repository->query_user_display_name(installation, "qq",
                                                     user_id, group_id);
        });
  }
  if (!display_name.has_value()) {
    co_await fetch_user_info(user_id, group_id);
    if (state_repository_) {
      const auto installation =
          operations_->onebot11_installation().installation_id;
      display_name = co_await blocking_executor_->run(
          [repository = state_repository_, installation, user_id, group_id] {
            return repository->query_user_display_name(installation, "qq",
                                                       user_id, group_id);
          });
    }
  }
  co_return display_name.value_or(user_id);
}

auto QQEventHandler::fetch_user_info(const std::string &user_id,
                                     const std::string &group_id)
    -> boost::asio::awaitable<void> {
  try {
    const auto member = co_await operations_->get_onebot11_group_member(
        group_id, user_id, false);
    storage::UserInfo info;
    info.installation_id = operations_->onebot11_installation().installation_id;
    info.platform = "qq";
    info.user_id = user_id;
    info.group_id = group_id;
    info.nickname = !member.card.empty()    ? member.card
                    : !member.title.empty() ? member.title
                                            : member.nickname;
    info.title = member.title;
    info.last_updated = std::chrono::system_clock::now();
    if (state_repository_) {
      (void)co_await blocking_executor_->run(
          [repository = state_repository_, info] {
            return repository->save_or_update_user(info, true);
          });
    }
  } catch (const std::exception &error) {
    // A typed SDK cancellation need not set this coroutine's Asio flag. Do
    // not turn it into a display-name fallback followed by recall/poke I/O.
    if (is_bridge_operation_cancelled(error))
      throw;
    OBCX_DEBUG("获取QQ用户信息失败：{}", error.what());
  }
}

} // namespace bridge::qq
