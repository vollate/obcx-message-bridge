#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace bridge::telegram {

// Parse one complete UTF-8 emoji with an optional presentation selector.
// Prefer mapped IDs, otherwise return an owning decimal Unicode scalar ID.
// Reject numeric input and compound sequences rather than truncating them.
[[nodiscard]] auto reaction_emoji_id(std::string_view argument)
    -> std::optional<std::string>;

} // namespace bridge::telegram
