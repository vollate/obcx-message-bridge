#include "media_converter.hpp"
#include "qq/photo_normalizer.hpp"

#include <algorithm>
#include <chrono>
#include <common/logger.hpp>
#include <filesystem>
#include <format>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace bridge {

auto MediaConverter::convert_webm_to_gif_with_fallback(
    std::string_view ffmpeg_path, const std::string &webm_path,
    const std::string &output_path, int max_duration,
    std::size_t maximum_output_bytes, int max_width, int max_fps,
    int max_colors) -> bool {
  if (ffmpeg_path.empty() || maximum_output_bytes == 0 ||
      maximum_output_bytes == std::numeric_limits<std::size_t>::max() ||
      max_duration <= 0 || max_width < 0 || max_fps < 0)
    throw std::invalid_argument(
        "WebM conversion requires explicit bounded settings");

  struct Tier {
    const char *name;
    int width;
    int fps;
    int colors;
  };
  const Tier tiers[] = {
      {"quality", max_width, max_fps, std::clamp(max_colors, 2, 256)},
      {"balanced", 320, 0, 256},
      {"compact", 200, 8, 64},
      {"minimal", 160, 5, 32},
  };
  const auto deadline =
      std::chrono::steady_clock::now() + qq::kTelegramPhotoNormalizationTimeout;
  // output_path names the worker's private scratch, not a cache destination.
  // Every unsuccessful tier is discarded before a later tier starts.
  const auto discard = [&] {
    std::error_code error;
    std::filesystem::remove(output_path, error);
    if (error)
      throw std::runtime_error("cannot discard WebM conversion scratch");
  };
  // PathManager owns final scratch cleanup if any process or file operation
  // throws; no filesystem resource escapes this worker invocation.
  for (const auto &tier : tiers) {
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
    if (remaining <= std::chrono::milliseconds::zero())
      break;
    std::string filter;
    if (tier.fps > 0)
      filter += std::format("fps=fps={},", tier.fps);
    if (tier.width > 0)
      filter += std::format(
          "scale={}:-1:flags=lanczos:force_original_aspect_ratio=decrease,",
          tier.width);
    filter += std::format(
        "split[s0][s1];[s0]palettegen=reserve_transparent=on"
        ":max_colors={}:stats_mode=full[p];"
        "[s1][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle",
        tier.colors);
    const auto result = qq::PhotoNormalizer::run_process(
        {std::string(ffmpeg_path),
         "-nostdin",
         "-hide_banner",
         "-loglevel",
         "error",
         "-threads",
         "1",
         "-filter_threads",
         "1",
         "-protocol_whitelist",
         "file,pipe",
         "-f",
         "matroska",
         "-i",
         webm_path,
         "-map_metadata",
         "-1",
         "-an",
         "-t",
         std::to_string(max_duration),
         "-vf",
         std::move(filter),
         "-loop",
         "0",
         "-threads",
         "1",
         "-f",
         "gif",
         "-fs",
         std::to_string(maximum_output_bytes + 1),
         "-y",
         output_path},
        remaining);
    if (result.timed_out) {
      discard();
      break;
    }
    std::error_code error;
    const auto size = std::filesystem::file_size(output_path, error);
    if (result.exit_code == 0 && !error && size != 0 &&
        size <= maximum_output_bytes) {
      // PathManager validates the bounded GIF bytes before publication.
      OBCX_DEBUG("WebM conversion tier {} prepared {} bytes", tier.name, size);
      return true;
    }
    discard();
  }
  return false;
}

} // namespace bridge
