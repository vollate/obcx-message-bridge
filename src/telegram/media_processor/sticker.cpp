#include "media_processor.hpp"
#include "qq/photo_normalizer.hpp"
#include "telegram/media_processor.hpp"

#include <algorithm>
#include <array>
#include <common/logger.hpp>
#include <core/bot/domain_codec.hpp>
#include <filesystem>
#include <span>

namespace bridge::telegram {
namespace {
namespace domain = obcx::bot;
using Bytes = std::span<const std::uint8_t>;

auto le32(Bytes bytes, std::size_t at) -> std::uint32_t {
  return std::uint32_t{bytes[at]} | (std::uint32_t{bytes[at + 1]} << 8) |
         (std::uint32_t{bytes[at + 2]} << 16) |
         (std::uint32_t{bytes[at + 3]} << 24);
}
auto tag(Bytes bytes, std::size_t at) -> std::string_view {
  return {reinterpret_cast<const char *>(bytes.data() + at), 4};
}

// Container/geometry preflight, not an image decoder. Do not flatten animation.
auto static_webp_dimensions(Bytes bytes) -> std::optional<qq::PhotoDimensions> {
  if (bytes.size() < 20 || tag(bytes, 0) != "RIFF" || tag(bytes, 8) != "WEBP" ||
      std::uint64_t{le32(bytes, 4)} + 8 != bytes.size())
    return std::nullopt;
  std::optional<qq::PhotoDimensions> canvas, frame;
  unsigned seen = 0;
  std::size_t cursor = 12;
  while (cursor < bytes.size()) {
    if (bytes.size() - cursor < 8)
      return std::nullopt;
    const auto kind = tag(bytes, cursor);
    const std::size_t length = le32(bytes, cursor + 4);
    cursor += 8;
    if (length > bytes.size() - cursor)
      return std::nullopt;
    const auto data = bytes.subspan(cursor, length);
    unsigned bit = 0;
    if (kind == "VP8X") {
      bit = 1;
      if (cursor != 20 || length != 10 || (data[0] & 0xc3) || data[1] ||
          data[2] || data[3])
        return std::nullopt;
      canvas =
          qq::PhotoDimensions{1U + data[4] + (std::uint32_t{data[5]} << 8) +
                                  (std::uint32_t{data[6]} << 16),
                              1U + data[7] + (std::uint32_t{data[8]} << 8) +
                                  (std::uint32_t{data[9]} << 16)};
    } else if (kind == "VP8 ") {
      bit = 2;
      if (frame || length < 10 || (data[0] & 1) || data[3] != 0x9d ||
          data[4] != 1 || data[5] != 0x2a)
        return std::nullopt;
      frame = qq::PhotoDimensions{
          (std::uint32_t{data[6]} | (std::uint32_t{data[7]} << 8)) & 0x3fffU,
          (std::uint32_t{data[8]} | (std::uint32_t{data[9]} << 8)) & 0x3fffU};
    } else if (kind == "VP8L") {
      bit = 4;
      if (frame || length < 5 || data[0] != 0x2f || (data[4] & 0xe0) ||
          (seen & 8))
        return std::nullopt;
      frame = qq::PhotoDimensions{1U + data[1] +
                                      ((std::uint32_t{data[2]} & 0x3fU) << 8),
                                  1U + ((std::uint32_t{data[2]} & 0xc0U) >> 6) +
                                      (std::uint32_t{data[3]} << 2) +
                                      ((std::uint32_t{data[4]} & 15U) << 10)};
    } else if (kind == "ALPH") {
      bit = 8;
      if (!canvas || frame)
        return std::nullopt;
    } else if (kind == "ICCP") {
      bit = 16;
    } else if (kind == "EXIF") {
      bit = 32;
    } else if (kind == "XMP ") {
      bit = 64;
    } else {
      return std::nullopt;
    }
    if (seen & bit)
      return std::nullopt;
    seen |= bit;
    cursor += length;
    if (length & 1) {
      if (cursor == bytes.size() || bytes[cursor] != 0)
        return std::nullopt;
      ++cursor;
    }
  }
  if (!frame || !frame->width || !frame->height ||
      (canvas && canvas != frame) ||
      std::uint64_t{frame->width} * frame->height >
          qq::kTelegramPhotoDecodePixelLimit)
    return std::nullopt;
  return frame;
}

auto static_png_dimensions(Bytes bytes) -> std::optional<qq::PhotoDimensions> {
  constexpr std::array<std::uint8_t, 8> signature{0x89, 'P', 'N', 'G',
                                                  13,   10,  26,  10};
  if (bytes.size() < 33 ||
      !std::equal(signature.begin(), signature.end(), bytes.begin()))
    return std::nullopt;
  const auto be32 = [&](std::size_t at) {
    return (std::uint32_t{bytes[at]} << 24) |
           (std::uint32_t{bytes[at + 1]} << 16) |
           (std::uint32_t{bytes[at + 2]} << 8) | bytes[at + 3];
  };
  std::size_t cursor = 8, chunks = 0;
  bool header = false, data = false;
  qq::PhotoDimensions dimensions{};
  while (bytes.size() - cursor >= 12 &&
         ++chunks <= domain::detail::domain_maximum_items) {
    const std::size_t length = be32(cursor);
    const auto kind = tag(bytes, cursor + 4);
    if (length > bytes.size() - cursor - 12 || kind == "acTL" ||
        kind == "fcTL" || kind == "fdAT")
      return std::nullopt;
    if (!header) {
      if (kind != "IHDR" || length != 13)
        return std::nullopt;
      dimensions = {be32(cursor + 8), be32(cursor + 12)};
      if (!dimensions.width || !dimensions.height ||
          std::uint64_t{dimensions.width} * dimensions.height >
              qq::kTelegramPhotoDecodePixelLimit)
        return std::nullopt;
      header = true;
    } else if (kind == "IHDR") {
      return std::nullopt;
    } else if (kind == "IDAT") {
      data = true;
    } else if (kind == "IEND") {
      return length == 0 && data && cursor + 12 == bytes.size()
                 ? std::optional{dimensions}
                 : std::nullopt;
    }
    cursor += length + 12;
  }
  return std::nullopt;
}

auto sticker_label(const obcx::telegram::bot::StickerView &sticker,
                   bool description) -> std::string {
  std::string label = "[贴纸";
  if (sticker.emoji)
    label += " " + *sticker.emoji;
  if (description &&
      sticker.format == obcx::telegram::bot::StickerFormat::VideoWebm)
    label += " 视频";
  if (description &&
      sticker.format == obcx::telegram::bot::StickerFormat::AnimatedTgs)
    label += " 动画暂不支持";
  return label + "]";
}

auto prepare_static_png(const PathManager &mapper, const std::string &original,
                        Bytes input, std::size_t maximum_bytes,
                        const BridgeConfig &config)
    -> std::optional<PathManager::CreatedFile> {
  const auto mime = MediaProcessor::detect_mime_type_from_content(std::string(
      input.begin(), input.begin() + std::min(input.size(), std::size_t{32})));
  if (mime != "image/webp")
    return std::nullopt;
  const auto dimensions = static_webp_dimensions(input);
  if (!dimensions)
    throw std::runtime_error("sticker WebP is not a safe static image");
  auto filename = std::filesystem::path(original).filename();
  filename.replace_extension(".png");
  auto converted = mapper.convert_owned_file(
      original, mapper.to_host_path("stickers/converted/" + filename.string()),
      input, maximum_bytes,
      [dimensions](Bytes encoded) {
        return static_png_dimensions(encoded) == dimensions;
      },
      [&](const std::string &source_fd, const std::string &output_fd) {
        const auto result = qq::PhotoNormalizer::run_process(
            {config.ffmpeg_path,
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
             "webp_pipe",
             "-i",
             source_fd,
             "-map_metadata",
             "-1",
             "-frames:v",
             "1",
             "-an",
             "-c:v",
             "png",
             "-threads",
             "1",
             "-f",
             "image2",
             "-update",
             "1",
             "-fs",
             std::to_string(maximum_bytes),
             "-y",
             output_fd},
            std::chrono::duration_cast<std::chrono::milliseconds>(
                qq::kTelegramPhotoNormalizationTimeout));
        return !result.timed_out && result.exit_code == 0;
      });
  if (!converted)
    throw std::runtime_error("static sticker PNG conversion failed");
  return converted;
}
} // namespace

auto TelegramMediaProcessor::prepare_sticker(domain::ReceivedMedia media,
                                             domain::RichText caption,
                                             std::size_t maximum_bytes)
    -> boost::asio::awaitable<PreparedTelegramMedia> {
  validate_source(media);
  const auto sticker = obcx::telegram::bot::sticker_view(media);
  if (!sticker)
    throw std::invalid_argument("typed sticker observation required");
  const bool tgs =
      sticker->format == obcx::telegram::bot::StickerFormat::AnimatedTgs;
  // TGS is an explicit text-only representation, not an acquisition. An
  // exhausted media budget must not suppress its unsupported-format notice.
  if ((!maximum_bytes && !tgs) ||
      maximum_bytes > config_->qq_media_download_max_bytes ||
      maximum_bytes > domain::detail::domain_maximum_bytes)
    throw std::invalid_argument("invalid Telegram sticker byte budget");
  std::size_t prepared_bytes = 0;
  try {
    // No TGS decoder: do not acquire merely to mislabel compressed JSON.
    if (!tgs) {
      CacheTransform transform;
      if (sticker->format == obcx::telegram::bot::StickerFormat::StaticWebp)
        transform = [config = config_](const PathManager &mapper,
                                       const std::string &source, Bytes input,
                                       std::size_t maximum) {
          return prepare_static_png(mapper, source, input, maximum, *config);
        };
      auto cached = co_await read_cached_media(media, maximum_bytes,
                                               std::move(transform));
      prepared_bytes = cached.prepared_bytes;
      if (cached.error)
        std::rethrow_exception(cached.error);
      auto bytes = std::move(cached.bytes);
      const auto mime = MediaProcessor::detect_mime_type_from_content(
          std::string(bytes.begin(),
                      bytes.begin() + std::min(bytes.size(), std::size_t{32})));
      // Successful media keeps only the source caption; the generated sticker
      // label is reserved for the text-only fallback below.
      auto metadata = media.metadata;
      metadata.mime_type = mime;
      metadata.size_bytes = bytes.size();
      metadata.width.reset();
      metadata.height.reset();
      metadata.duration_ms.reset();
      if (sticker->format == obcx::telegram::bot::StickerFormat::VideoWebm &&
          mime == "image/gif") {
        if (metadata.filename)
          metadata.filename = std::filesystem::path(*metadata.filename)
                                  .replace_extension(".gif")
                                  .string();
        co_return PreparedTelegramMedia{
            domain::Animation{
                domain::BytesSource{std::move(bytes), std::move(metadata)},
                std::move(caption)},
            prepared_bytes};
      }
      if (sticker->format == obcx::telegram::bot::StickerFormat::StaticWebp &&
          (mime == "image/png" || mime == "image/jpeg")) {
        const std::string_view encoded(
            reinterpret_cast<const char *>(bytes.data()), bytes.size());
        const auto inspected = qq::inspect_photo_dimensions(encoded);
        if ((mime == "image/png" && !static_png_dimensions(bytes)) ||
            inspected.status == qq::PhotoDimensionStatus::UnsafeDimensions ||
            inspected.status == qq::PhotoDimensionStatus::InvalidDimensions)
          throw std::runtime_error("sticker cache is not a safe static image");
        if (metadata.filename)
          metadata.filename =
              std::filesystem::path(*metadata.filename)
                  .replace_extension(mime == "image/png" ? ".png" : ".jpg")
                  .string();
        co_return PreparedTelegramMedia{
            domain::Image{
                domain::BytesSource{std::move(bytes), std::move(metadata)},
                std::move(caption)},
            prepared_bytes};
      }
      OBCX_WARN("贴纸缓存格式不受目标支持，保留文字提示");
    }
  } catch (const TelegramCacheScopeMismatch &) {
    throw;
  } catch (const std::exception &error) {
    rethrow_if_cancelled(error);
    OBCX_WARN("贴纸字节准备失败，保留文字提示: {}", error.what());
  }
  domain::RichText fallback{domain::Text{sticker_label(*sticker, tgs), {}}};
  for (auto &span : caption)
    fallback.push_back(std::move(span));
  co_return PreparedTelegramMedia{std::move(fallback), prepared_bytes};
}
} // namespace bridge::telegram
