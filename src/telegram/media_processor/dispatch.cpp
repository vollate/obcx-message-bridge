#include "telegram/media_processor.hpp"

#include "bridge_bot_failures.hpp"
#include "bridge_state_repository.hpp"

#include <stdexcept>
#include <utility>

namespace bridge::telegram {

TelegramMediaProcessor::TelegramMediaProcessor(
    std::shared_ptr<BridgeCommandOperations> operations,
    std::shared_ptr<const bridge::BridgeConfig> config,
    std::shared_ptr<bridge::BridgeStateRepository> state_repository,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking_executor)
    : operations_(std::move(operations)), config_(std::move(config)),
      path_manager_([&] {
        if (!operations_ || !config_)
          throw std::invalid_argument(
              "TelegramMediaProcessor requires operations and config");
        return PathManager(operations_->onebot11_installation().installation_id,
                           config_->bridge_files_dir,
                           config_->bridge_files_container_dir);
      }()),
      state_repository_(std::move(state_repository)),
      blocking_executor_(std::move(blocking_executor)) {
  if (!blocking_executor_)
    throw std::invalid_argument(
        "TelegramMediaProcessor requires worker ownership");
}

void TelegramMediaProcessor::validate_source(
    const obcx::bot::ReceivedMedia &media) const {
  media.validate();
  if (media.media.installation != operations_->telegram_installation())
    throw std::invalid_argument(
        "Telegram media observation installation mismatch");
  (void)obcx::telegram::bot::file_observation_view(media);
}

void TelegramMediaProcessor::rethrow_if_cancelled(const std::exception &error) {
  if (is_bridge_operation_cancelled(error))
    throw;
}

} // namespace bridge::telegram
