#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace bridge::qq {
class VoicePreparationFailure final : public std::runtime_error {
public:
  explicit VoicePreparationFailure(const char *reason)
      : std::runtime_error(std::string{"QQ voice preparation failed: "} +
                           reason) {}
};
struct NormalizedVoice {
  std::string ogg;
  std::uint64_t duration_ms;
};
// Synchronous worker-only API: all inspection, decoding, I/O and child waits
// must run inside the owned BlockingExecutor, never on the event loop.
auto normalize_voice(std::string_view input, const std::string &ffmpeg_path,
                     std::size_t maximum_bytes) -> NormalizedVoice;
} // namespace bridge::qq
