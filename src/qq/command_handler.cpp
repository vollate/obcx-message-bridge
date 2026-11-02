#include "qq/command_handler.hpp"

#include "bridge_state_repository.hpp"
#include "bridge_status.hpp"

#include <common/logger.hpp>

#include <chrono>
#include <optional>
#include <utility>

namespace bridge::qq {

QQCommandHandler::QQCommandHandler(
    std::shared_ptr<BridgeCommandOperations> operations,
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
    obcx::bot::MessageRef source) -> boost::asio::awaitable<void> {
  try {
    source.validate();
    if (source.conversation.installation !=
        operations_->onebot11_installation()) {
      throw std::invalid_argument("QQ status source installation mismatch");
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
    auto outcome =
        co_await operations_->reply_text(std::move(source), response);
    if (auto *failure = std::get_if<obcx::bot::SendFailure>(&outcome)) {
      throw BridgeBotSendFailure(std::move(*failure));
    }
    OBCX_INFO("/bridge_status 命令处理完成");
  } catch (const std::exception &error) {
    OBCX_ERROR("处理 /bridge_status 命令时出错: {}", error.what());
    throw;
  }
}

} // namespace bridge::qq
