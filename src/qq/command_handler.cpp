#include "qq/command_handler.hpp"

#include "bridge_state_repository.hpp"
#include "bridge_status.hpp"

#include <common/logger.hpp>

#include <chrono>
#include <optional>
#include <utility>

namespace bridge::qq {

QQCommandHandler::QQCommandHandler(
    std::shared_ptr<BridgeBotOperations> operations,
    std::shared_ptr<bridge::BridgeStateRepository> state_repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking_executor)
    : operations_(std::move(operations)),
      state_repository_(std::move(state_repository)),
      blocking_executor_(std::move(blocking_executor)) {
  if (!operations_) {
    throw std::invalid_argument("QQCommandHandler requires bot operations");
  }
}

auto QQCommandHandler::handle_bridge_status_command(
    obcx::common::MessageEvent event, const std::string &telegram_group_id)
    -> boost::asio::awaitable<void> {
  (void)telegram_group_id;
  try {
    const std::string qq_group_id = event.group_id.value();
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
            (void)repository->update_platform_heartbeat(onebot_installation,
                                                        "qq", now);
            return std::pair{
                repository->get_platform_heartbeat(onebot_installation),
                repository->get_platform_heartbeat(telegram_installation)};
          });
    }

    const auto response =
        render_bridge_status(onebot_installation, qq_activity,
                             telegram_installation, telegram_activity, now);
    co_await send_reply_message(qq_group_id, event.message_id, response);
    OBCX_INFO("/bridge_status 命令处理完成");
  } catch (const std::exception &error) {
    OBCX_ERROR("处理 /bridge_status 命令时出错: {}", error.what());
  }
}

auto QQCommandHandler::send_reply_message(
    const std::string &qq_group_id, const std::string &reply_to_message_id,
    const std::string &text) -> boost::asio::awaitable<void> {
  try {
    obcx::common::Message reply_message;

    obcx::common::MessageSegment reply_segment;
    reply_segment.type = "reply";
    reply_segment.data["id"] = reply_to_message_id;
    reply_message.push_back(reply_segment);

    obcx::common::MessageSegment text_segment;
    text_segment.type = "text";
    text_segment.data["text"] = text;
    reply_message.push_back(text_segment);

    (void)co_await operations_->send_onebot11_group(qq_group_id, reply_message);

  } catch (const std::exception &e) {
    OBCX_ERROR("发送回复消息失败: {}", e.what());
  }
}

} // namespace bridge::qq
