#pragma once

#include <core/bot/bot_events.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace storage {

struct UserInfo {
  std::string installation_id;
  std::string platform;
  std::string user_id;
  std::string group_id;
  std::string username;
  std::string nickname;
  std::string title;
  std::string first_name;
  std::string last_name;
  std::chrono::system_clock::time_point last_updated;
};

struct MessageInfo {
  std::string platform;
  std::string source_bot;
  std::string conversation_id;
  std::string message_id;
  std::string group_id;
  std::string user_id;
  std::string content;
  std::string message_type;
  std::chrono::system_clock::time_point timestamp;
  std::optional<std::string> reply_to_message_id;
  std::optional<std::string> forwarded_to_platform;
  std::optional<std::string> forwarded_message_id;
  std::chrono::system_clock::time_point created_at;
  std::optional<obcx::bot::MessageReceived> message;
};

struct StickerCacheInfo {
  std::string installation_id;
  std::string platform;
  std::string sticker_id;
  std::string sticker_hash;
  std::optional<std::string> original_name;
  std::string file_type;
  std::optional<std::string> mime_type;
  std::string original_file_path;
  std::optional<std::string> converted_file_path;
  std::string container_path;
  std::optional<std::int64_t> file_size;
  std::string conversion_status;
  std::chrono::system_clock::time_point created_at;
  std::chrono::system_clock::time_point last_used_at;
};

// Cache ownership only: never a provider reference or message contribution.
struct StickerCacheFileReference {
  std::string installation_id;
  std::string platform;
  std::string sticker_hash;
  std::string host_path;
};

struct QQStickerMapping {
  std::string source_installation;
  std::string target_installation;
  std::string qq_sticker_hash;
  std::string telegram_file_id;
  std::string file_type;
  std::chrono::system_clock::time_point created_at;
  std::chrono::system_clock::time_point last_used_at;
  std::optional<bool> is_gif;
  std::optional<std::string> content_type;
  std::optional<std::chrono::system_clock::time_point> last_checked_at;
};

struct PlatformHeartbeatInfo {
  std::string installation_id;
  std::string platform;
  std::chrono::system_clock::time_point last_heartbeat_at;
  std::chrono::system_clock::time_point updated_at;
};

} // namespace storage
