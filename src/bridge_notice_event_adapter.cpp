#include "bridge_notice_event_adapter.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace bridge {
namespace {

auto json_string(const obcx::common::json &value, const char *key)
    -> std::string {
  if (!value.is_object() || !value.contains(key) || value.at(key).is_null()) {
    return {};
  }
  const auto &item = value.at(key);
  if (item.is_string()) {
    return item.get<std::string>();
  }
  if (item.is_number_integer()) {
    return std::to_string(item.get<std::int64_t>());
  }
  if (item.is_number_unsigned()) {
    return std::to_string(item.get<std::uint64_t>());
  }
  return {};
}

auto payload_object(const obcx::core::MessageEnvelope &message)
    -> obcx::common::json {
  if (message.payload.is_object() && message.payload.contains("payload") &&
      message.payload.at("payload").is_object()) {
    return message.payload.at("payload");
  }
  return obcx::common::json::object();
}

auto unix_seconds(const std::chrono::system_clock::time_point timestamp)
    -> double {
  const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                          timestamp.time_since_epoch())
                          .count();
  return static_cast<double>(millis) / 1000.0;
}

void set_if_missing(obcx::common::json &raw, const char *key,
                    const std::string &value) {
  if (!value.empty() && (!raw.contains(key) || raw.at(key).is_null())) {
    raw[key] = value;
  }
}

} // namespace

auto notice_event_from_raw_notice(const obcx::core::MessageEnvelope &message)
    -> std::optional<obcx::common::NoticeEvent> {
  if (message.type != "obcx::core::events::RawNoticeEvent" ||
      !message.payload.is_object()) {
    return std::nullopt;
  }

  auto raw =
      message.raw.is_object() ? message.raw : obcx::common::json::object();
  const auto notice_payload = payload_object(message);
  for (const auto &[key, value] : notice_payload.items()) {
    if (!raw.contains(key)) {
      raw[key] = value;
    }
  }

  if (!raw.contains("time")) {
    raw["time"] = unix_seconds(message.timestamp);
  }
  set_if_missing(raw, "self_id", message.source_bot);
  set_if_missing(raw, "post_type", "notice");
  set_if_missing(raw, "notice_type",
                 json_string(message.payload, "notice_type"));
  set_if_missing(raw, "user_id", json_string(message.payload, "sender"));
  set_if_missing(raw, "group_id", json_string(message.payload, "group_id"));
  if (json_string(raw, "notice_type").empty()) {
    return std::nullopt;
  }

  obcx::common::NoticeEvent event;
  event.from_json(raw);
  event.type = obcx::common::EventType::notice;
  event.data = raw;
  return event;
}

} // namespace bridge
