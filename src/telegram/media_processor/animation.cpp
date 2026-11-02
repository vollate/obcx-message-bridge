#include "media_processor.hpp"
#include "telegram/media_processor.hpp"

#include <algorithm>
#include <common/logger.hpp>
#include <core/bot/domain_codec.hpp>
#include <filesystem>

namespace bridge::telegram {

auto TelegramMediaProcessor::prepare_animation(obcx::bot::ReceivedMedia media,
                                               obcx::bot::RichText caption,
                                               std::size_t maximum_bytes)
    -> boost::asio::awaitable<PreparedTelegramMedia> {
  validate_source(media);
  if (media.kind != obcx::bot::MediaKind::Animation ||
      obcx::telegram::bot::sticker_view(media))
    throw std::invalid_argument("typed animation observation required");
  if (!maximum_bytes || maximum_bytes > config_->qq_media_download_max_bytes ||
      maximum_bytes > obcx::bot::detail::domain_maximum_bytes)
    throw std::invalid_argument("invalid Telegram animation byte budget");
  std::size_t prepared_bytes = 0;
  std::string fallback = "[动画暂时无法获取]";
  try {
    auto cached = co_await read_cached_media(media, maximum_bytes, {});
    prepared_bytes = cached.prepared_bytes;
    if (cached.error)
      std::rethrow_exception(cached.error);
    auto bytes = std::move(cached.bytes);
    const std::string signature(
        bytes.begin(), bytes.begin() + std::min(bytes.size(), std::size_t{32}));
    auto metadata = media.metadata;
    metadata.size_bytes = bytes.size();
    if (MediaProcessor::detect_mime_type_from_content(signature) ==
        "image/gif") {
      metadata.mime_type = "image/gif";
      metadata.width.reset();
      metadata.height.reset();
      metadata.duration_ms.reset();
      if (metadata.filename)
        metadata.filename = std::filesystem::path(*metadata.filename)
                                .replace_extension(".gif")
                                .string();
      co_return PreparedTelegramMedia{
          obcx::bot::Animation{
              obcx::bot::BytesSource{std::move(bytes), std::move(metadata)},
              std::move(caption)},
          prepared_bytes};
    }
    // Explicit MP4 animation -> ordinary QQ Video presentation, not a claim
    // that QQ supports MP4 Animation or that the container was played.
    if (signature.size() >= 12 && signature.substr(4, 4) == "ftyp" &&
        (signature.substr(8, 4) == "isom" || signature.substr(8, 4) == "iso2" ||
         signature.substr(8, 4) == "mp41" || signature.substr(8, 4) == "mp42" ||
         signature.substr(8, 4) == "avc1" || signature.substr(8, 4) == "M4V " ||
         signature.substr(8, 4) == "dash")) {
      metadata.mime_type = "video/mp4";
      co_return PreparedTelegramMedia{
          obcx::bot::Video{
              obcx::bot::BytesSource{std::move(bytes), std::move(metadata)},
              std::move(caption)},
          prepared_bytes};
    }
    fallback = "[动画格式暂不支持]";
  } catch (const TelegramCacheScopeMismatch &) {
    throw;
  } catch (const std::exception &error) {
    rethrow_if_cancelled(error);
    OBCX_WARN("动画缓存字节准备失败: {}", error.what());
  }
  obcx::bot::RichText text{obcx::bot::Text{std::move(fallback), {}}};
  for (auto &span : caption)
    text.push_back(std::move(span));
  co_return PreparedTelegramMedia{std::move(text), prepared_bytes};
}
} // namespace bridge::telegram
