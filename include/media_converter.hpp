#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace bridge {

// Actor-owned WebM recipe. Run only within the blocking worker and the
// PathManager private conversion scratch lifetime. No shell/TGS decoder.
class MediaConverter {
public:
  // All settings are explicit. The caller intersects the configured GIF limit
  // (including its existing zero/unlimited spelling) with the read/send budget
  // before entering: maximum_output_bytes must be nonzero.
  // Preserves quality/balanced/compact/minimal tiers under one existing photo
  // normalization timeout; returning true is not playback verification.
  static auto convert_webm_to_gif_with_fallback(
      std::string_view ffmpeg_path, const std::string &webm_path,
      const std::string &output_path, int max_duration,
      std::size_t maximum_output_bytes, int max_width, int max_fps,
      int max_colors) -> bool;
};

} // namespace bridge
