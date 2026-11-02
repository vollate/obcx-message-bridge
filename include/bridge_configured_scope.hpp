#pragma once
#include <charconv>
#include <core/bot/conversation_resolver.hpp>

namespace bridge {
// Current configuration tokens only; never accepts chat:/group: storage keys.
inline auto configured_group(std::string_view id,
                             const std::optional<std::string> &topic)
    -> obcx::bot::ConfiguredConversation {
  std::optional<std::int64_t> number;
  if (topic) {
    std::int64_t value;
    const auto [end, error] =
        std::from_chars(topic->data(), topic->data() + topic->size(), value);
    if (error != std::errc{} || end != topic->data() + topic->size() ||
        value <= 0 || std::to_string(value) != *topic)
      throw std::invalid_argument("invalid current configured topic");
    number = value;
  }
  obcx::bot::ConfiguredConversation result{
      obcx::bot::ConfiguredConversationKind::Group, std::string(id), number};
  result.validate();
  return result;
}
} // namespace bridge
