#include "telegram/reaction_emoji.hpp"

#include "emoji_codepoints.hpp"

#include <algorithm>
#include <cstddef>

namespace bridge::telegram {
namespace {
struct ReactionEmoji {
  std::string_view utf8;
  std::string_view id;
};

// Follow CrossGram's QQ reaction catalog: visible face_config.json emoji
// entries map QSid (UTF-8) to QCid, not an inferred first code point or AQLid.
// See docs/telegram-react.md for pinned CrossGram and QQ resource references.
// LLOneBot infers the native reaction type from the decimal ID. Heart is an
// explicit QQ face mapping; presentation aliases never strip modifiers/ZWJ.
constexpr ReactionEmoji emojis[] = {
    {"😊", "128522"},
    {"😌", "128524"},
    {"😚", "128538"},
    {"😓", "128531"},
    {"😰", "128560"},
    {"😝", "128541"},
    {"😁", "128513"},
    {"😜", "128540"},
    {"☺", "9786"},
    {"😍", "128525"},
    {"😔", "128532"},
    {"😄", "128516"},
    {"😏", "128527"},
    {"😒", "128530"},
    {"😳", "128563"},
    {"😘", "128536"},
    {"😭", "128557"},
    {"😱", "128561"},
    {"😂", "128514"},
    {"💪", "128170"},
    {"👊", "128074"},
    {"👍", "128077"},
    {"👏", "128079"},
    {"👎", "128078"},
    {"🙏", "128591"},
    {"👌", "128076"},
    {"👆", "128070"},
    {"👀", "128064"},
    {"🍜", "127836"},
    {"🍧", "127847"},
    {"🍞", "127838"},
    {"🍺", "127866"},
    {"🍻", "127867"},
    {"☕", "9749"},
    {"🍎", "127822"},
    {"🍓", "127827"},
    {"🍉", "127817"},
    {"🚬", "128684"},
    {"🌹", "127801"},
    {"🎉", "127881"},
    {"💝", "128157"},
    {"💣", "128163"},
    {"✨", "10024"},
    {"💨", "128168"},
    {"💦", "128166"},
    {"🔥", "128293"},
    {"💤", "128164"},
    {"💩", "128169"},
    {"💉", "128137"},
    {"📫", "128235"},
    {"🐎", "128014"},
    {"👧", "128103"},
    {"👦", "128102"},
    {"🐵", "128053"},
    {"🐷", "128055"},
    {"🐮", "128046"},
    {"🐔", "128020"},
    {"🐸", "128056"},
    {"👻", "128123"},
    {"🐛", "128027"},
    {"🐶", "128054"},
    {"🐳", "128051"},
    {"👢", "128098"},
    {"☀", "9728"},
    {"❔", "10068"},
    {"🔫", "128299"},
    {"💓", "128147"},
    {"🏪", "127978"},
    // Explicit presentation aliases and QQ-native heart face.
    {"☀️", "9728"},
    {"☺️", "9786"},
    {"❤", "66"},
    {"❤️", "66"},
};

// Consume exactly one scalar, rejecting non-shortest forms, surrogates and
// values outside Unicode. The caller must validate the entire remaining suffix.
auto take_scalar(std::string_view &input) -> std::optional<std::uint32_t> {
  if (input.empty()) {
    return std::nullopt;
  }
  const auto lead = static_cast<unsigned char>(input.front());
  std::size_t length = 1;
  std::uint32_t scalar = lead;
  std::uint32_t minimum = 0;
  if (lead <= 0x7FU) {
    // ASCII is decoded here, but is not eligible for a Unicode reaction ID.
  } else if (lead >= 0xC2U && lead <= 0xDFU) {
    length = 2;
    scalar = lead & 0x1FU;
    minimum = 0x80U;
  } else if (lead >= 0xE0U && lead <= 0xEFU) {
    length = 3;
    scalar = lead & 0x0FU;
    minimum = 0x800U;
  } else if (lead >= 0xF0U && lead <= 0xF4U) {
    length = 4;
    scalar = lead & 0x07U;
    minimum = 0x10000U;
  } else {
    return std::nullopt;
  }
  if (input.size() < length) {
    return std::nullopt;
  }
  for (std::size_t index = 1; index < length; ++index) {
    const auto byte = static_cast<unsigned char>(input[index]);
    if ((byte & 0xC0U) != 0x80U) {
      return std::nullopt;
    }
    scalar = (scalar << 6U) | (byte & 0x3FU);
  }
  if (scalar < minimum || scalar > 0x10FFFFU ||
      (scalar >= 0xD800U && scalar <= 0xDFFFU)) {
    return std::nullopt;
  }
  input.remove_prefix(length);
  return scalar;
}
} // namespace

auto reaction_emoji_id(std::string_view argument)
    -> std::optional<std::string> {
  constexpr std::string_view whitespace = " \t\r\n\f\v";
  const auto first = argument.find_first_not_of(whitespace);
  if (first == std::string_view::npos) {
    return std::nullopt;
  }
  argument.remove_prefix(first);
  argument = argument.substr(0, argument.find_last_not_of(whitespace) + 1);
  auto suffix = argument;
  const auto scalar = take_scalar(suffix);
  // Only presentation selectors may follow the scalar. Never discard a ZWJ,
  // modifier, flag indicator, keycap, tag, second emoji, or malformed suffix.
  if (!scalar || (!suffix.empty() && suffix != "\xEF\xB8\x8E" &&
                  suffix != "\xEF\xB8\x8F")) {
    return std::nullopt;
  }
  const auto base = argument.substr(0, argument.size() - suffix.size());
  for (const auto &emoji : emojis) {
    if (argument == emoji.utf8 || base == emoji.utf8) {
      return std::string{emoji.id};
    }
  }
  // LLOneBot interprets IDs of at most three decimal digits as QQ-native
  // faces. Do not accidentally turn Unicode ©/® into a different reaction.
  if (*scalar < 1000U || !std::ranges::any_of(
                             detail::emoji_scalar_ranges,
                             [scalar](const auto &range) {
                               return *scalar >= range.first &&
                                      *scalar <= range.last;
                             })) {
    return std::nullopt;
  }
  // Client catalogues are not server whitelists. QQ decides whether to accept
  // an eligible single-scalar emoji (e.g. ㊗️ -> 12951), with no retry/fallback.
  return std::to_string(*scalar);
}

} // namespace bridge::telegram
