#pragma once
#include "qq/downloaded_image.hpp"

#include <cstdint>
#include <string_view>

namespace bridge::qq {
// Shared, unchanged final offline fallback for ordinary and forward-node
// photos.
inline auto embedded_placeholder() -> DownloadedImage {
  static const std::string kPng = [] {
    static constexpr std::string_view kBase64 =
        "iVBORw0KGgoAAAANSUhEUgAAAQAAAABgCAYAAADy8ayIAAAByklEQVR42u3cMQ7CMAxA"
        "0V6ImWswMXFWjoSE1B1mWBhwXCd+w5ugAUXmD1Hpdj+dX0BPm00AAQAEAGgdgMdzBxYl"
        "ACAAAgACIAAgAAIAAiAAIAA2CQQAEABAAAABAAQAEABAAAABAAQAEABAAAABAAQAEABAA"
        "AABAAQAEABAAAABAAQAEABAAIDqAbhcbx9GX//9/l+i1/v386Jlf//R60XPT/b6VeZFA"
        "ARAAARAAARAAARAAARAAAQgIwDRA5AdmOjrswNQbf3qP9DZ51MABEAABEAABEAABEAABE"
        "AABKDSIeDo1wVAAI6cPwEQAAEQAAEQAAEQAAEQAAEQAAGIOzQUgLE3AnUPwOzzKQACIA"
        "ACIAACIAACIAACIAACsNKfgQTAIWDn9QVAAARAAARAAARAAARAAARAAARAAARAAARAAA"
        "RAAARAAARAAARAAARAADwUNHZDuwcge/9nv5HJU4EFQAAEQAAEQAAEQAAEQAAEYOUAAP"
        "UIAAiAAIAACAAIgACAAAgACIBNAgEABAAQAEAAAAEABAAQAEAAAAEABAAQAEAAAAEABAA"
        "QAEAAAAEABAAQAEAAAAEABAAQAGBUAIA+BAAEABAAoJU3yWqRE/JFpjwAAAAASUVORK5C"
        "YII=";
    const auto value = [](unsigned char byte) -> unsigned int {
      if (byte >= 'A' && byte <= 'Z')
        return byte - 'A';
      if (byte >= 'a' && byte <= 'z')
        return byte - 'a' + 26U;
      if (byte >= '0' && byte <= '9')
        return byte - '0' + 52U;
      return byte == '+' ? 62U : 63U;
    };
    std::string decoded;
    decoded.reserve(kBase64.size() * 3U / 4U);
    std::uint32_t buffer = 0;
    unsigned int bits = 0;
    for (const unsigned char byte : kBase64) {
      if (byte == '=')
        break;
      buffer = (buffer << 6U) | value(byte);
      bits += 6U;
      if (bits < 8U)
        continue;
      bits -= 8U;
      decoded.push_back(static_cast<char>((buffer >> bits) & 0xffU));
      buffer &= bits == 0U ? 0U : (1U << bits) - 1U;
    }
    return decoded;
  }();
  return DownloadedImage{.type = "photo",
                         .filename = "qq-media-placeholder.png",
                         .mime_type = "image/png",
                         .data = kPng};
}
} // namespace bridge::qq
