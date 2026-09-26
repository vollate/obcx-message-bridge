#include "telegram/bot/operations.hpp"
#include "telegram/media_processor.hpp"

#include <common/logger.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace bridge::telegram {

auto TelegramMediaProcessor::process_downloaded_file(
    const obcx::telegram::bot::FetchedTelegramFile &file,
    std::string output_type, const std::string &filename,
    std::vector<std::string> &temp_files_to_cleanup)
    -> boost::asio::awaitable<obcx::common::MessageSegment> {
  const auto safe_name = std::filesystem::path{filename}.filename().string();
  if (safe_name.empty()) {
    throw std::runtime_error("Telegram media filename is empty");
  }
  const auto relative =
      "temp/" +
      std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()) +
      "_" + safe_name;
  const auto local_path = co_await blocking_executor_->run(
      [path_manager = path_manager_, relative, bytes = file.bytes] {
        if (!path_manager.ensure_directory("temp")) {
          throw std::runtime_error("cannot create Telegram media directory");
        }
        return path_manager.write_new_file(relative,
                                           std::as_bytes(std::span(bytes)));
      });
  temp_files_to_cleanup.push_back(local_path);
  const auto file_uri = co_await blocking_executor_->run(
      [path_manager = path_manager_, local_path] {
        return path_manager.file_uri(local_path);
      });

  obcx::common::MessageSegment segment;
  segment.type = std::move(output_type);
  segment.data["file"] = file_uri;
  if (segment.type == "image") {
    segment.data["proxy"] = 1;
  }
  if (segment.type == "file") {
    segment.data["name"] = safe_name;
  }
  co_return segment;
}

} // namespace bridge::telegram
