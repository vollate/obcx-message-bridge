#include "qq/image_preparer.hpp"
#include "bridge_bot_failures.hpp"
#include "media_processor.hpp"
#include "qq/photo_normalizer.hpp"
#include "qq/photo_placeholder.hpp"

#include <algorithm>
#include <boost/asio/error.hpp>
#include <boost/system/system_error.hpp>
#include <chrono>
#include <limits>
#include <network/image_fetch.hpp>
#include <stdexcept>
#include <system_error>

namespace bridge::qq {
namespace {
namespace bot = obcx::bot;

// These are the installed Telegram photo formats, not an implicit conversion
// of animation/WebP/BMP into a static frame. PNG chunks are checked before the
// existing dimension normalizer can invoke its single-frame decoder.
auto static_photo_mime(const std::string &bytes) -> std::optional<std::string> {
  const auto mime = MediaProcessor::detect_mime_type_from_content(bytes);
  if (mime == "image/jpeg")
    return mime;
  if (mime != "image/png")
    return std::nullopt;
  std::size_t cursor = 8;
  bool header = false;
  while (cursor <= bytes.size() && bytes.size() - cursor >= 12) {
    std::uint32_t size = 0;
    for (std::size_t i = 0; i < 4; ++i)
      size = (size << 8U) | static_cast<unsigned char>(bytes[cursor + i]);
    if (size > bytes.size() - cursor - 12)
      return std::nullopt;
    const std::string_view type{bytes.data() + cursor + 4, 4};
    if (!header) {
      if (type != "IHDR" || size != 13)
        return std::nullopt;
      header = true;
    } else if (type == "IHDR") {
      return std::nullopt;
    }
    if (type == "acTL" || type == "fcTL" || type == "fdAT")
      return std::nullopt;
    cursor += static_cast<std::size_t>(size) + 12;
    if (type == "IEND")
      return size == 0 && cursor == bytes.size() ? std::optional{mime}
                                                 : std::nullopt;
  }
  return std::nullopt;
}
// This is deliberately not a general URL reader: only explicit generation
// configuration can supply a placeholder. Message locators never enter it.
auto read_configured_placeholder(std::shared_ptr<const BridgeConfig> config,
                                 std::size_t maximum_bytes)
    -> boost::asio::awaitable<std::optional<DownloadedImage>> {
  if (config->image_placeholder_url.empty())
    co_return std::nullopt;
  try {
    // Preserve the predecessor timeout floor and cap the configured read bound
    // by this slot's remaining budget, before fetching rather than afterwards.
    // No probing, backoff, redirects or source-URL fallback is needed.
    auto response = co_await obcx::network::fetch_image(
        {.url = config->image_placeholder_url,
         .timeout = std::chrono::milliseconds(
             std::max(30000, config->image_url_probe_timeout_ms)),
         .maximum_bytes =
             std::min(config->qq_media_download_max_bytes, maximum_bytes),
         .prefix_bytes = std::nullopt});
    if (response.status_code != 200 || response.body.empty())
      co_return std::nullopt;
    auto mime = static_photo_mime(response.body);
    if (!mime)
      co_return std::nullopt;
    co_return DownloadedImage{
        .type = "photo",
        .original_url = config->image_placeholder_url,
        .filename = *mime == "image/png" ? "qq-media-0.png" : "qq-media-0.jpg",
        .mime_type = std::move(*mime),
        .data = std::move(response.body)};
  } catch (const boost::system::system_error &error) {
    if (error.code() == boost::asio::error::operation_aborted)
      throw;
  } catch (const std::system_error &error) {
    if (error.code() == std::errc::operation_canceled)
      throw;
  } catch (const std::exception &) {
    // A failed configured placeholder keeps the existing offline fallback.
    // Never expose its locator or transport exception in diagnostics.
  }
  co_return std::nullopt;
}

auto owned_photo(DownloadedImage image, std::size_t maximum_bytes)
    -> bot::BytesSource {
  if (image.data.empty() || image.data.size() > maximum_bytes)
    throw std::runtime_error("prepared photo exceeds explicit byte budget");
  const auto mime = static_photo_mime(image.data);
  if (!mime || inspect_photo_dimensions(image.data).status !=
                   PhotoDimensionStatus::Compliant)
    throw std::runtime_error("prepared photo is not a supported static photo");
  bot::MediaMetadata metadata;
  metadata.mime_type = *mime;
  metadata.filename = image.filename;
  metadata.size_bytes = image.data.size();
  const auto dimensions = inspect_photo_dimensions(image.data).dimensions;
  metadata.width = dimensions.width;
  metadata.height = dimensions.height;
  bot::BytesSource result{{image.data.begin(), image.data.end()}, metadata};
  result.validate();
  return result;
}
auto normalization_status(PhotoNormalizationFailure failure)
    -> PhotoPreparationStatus {
  switch (failure) {
  case PhotoNormalizationFailure::InvalidDimensions:
    return PhotoPreparationStatus::InvalidDimensions;
  case PhotoNormalizationFailure::UnsafeDimensions:
    return PhotoPreparationStatus::UnsafeDimensions;
  default:
    return PhotoPreparationStatus::NormalizationFailed;
  }
}
} // namespace

auto prepare_photos(std::shared_ptr<bot::BotOperationGateway> gateway,
                    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
                    std::shared_ptr<const BridgeConfig> config,
                    std::vector<PhotoInput> media, bot::SendLimits limits)
    -> boost::asio::awaitable<std::vector<PreparedPhoto>> {
  std::vector<PhotoReadInput> inputs;
  inputs.reserve(media.size());
  for (auto &ref : media)
    inputs.push_back({std::move(ref), std::nullopt});
  return prepare_read_photos(std::move(gateway), std::move(blocking),
                             std::move(config), std::move(inputs), limits);
}

auto prepare_read_photos(std::shared_ptr<bot::BotOperationGateway> gateway,
                         std::shared_ptr<obcx::core::BlockingExecutor> blocking,
                         std::shared_ptr<const BridgeConfig> config,
                         std::vector<PhotoReadInput> media,
                         bot::SendLimits limits)
    -> boost::asio::awaitable<std::vector<PreparedPhoto>> {
  limits.validate();
  if (!gateway || !blocking || !config || media.empty() ||
      media.size() > limits.maximum_messages ||
      limits.maximum_media_item_bytes == 0 ||
      limits.maximum_media_total_bytes == 0 ||
      limits.maximum_media_item_bytes > config->qq_media_download_max_bytes)
    throw std::invalid_argument(
        "photo preparation requires owned services and budgets");
  for (const auto &input : media) {
    if (input.media)
      input.media->validate();
    if (input.result && !input.media)
      throw std::invalid_argument("photo read requires its request reference");
  }
  std::vector<PreparedPhoto> prepared;
  prepared.reserve(media.size());
  std::optional<bot::BytesSource> placeholder;
  std::size_t total = 0;
  for (auto &input : media) {
    const auto &ref = input.media;
    const auto remaining = limits.maximum_media_total_bytes - total;
    const auto maximum = std::min(limits.maximum_media_item_bytes, remaining);
    if (maximum == 0)
      throw std::runtime_error("photo preparation total byte budget exhausted");
    auto read = std::move(input.result);
    if (ref && !read)
      read = co_await bot::BotClient{*gateway}.read_media(
          bot::ReadMediaRequest{*ref, maximum});
    if (read)
      read->validate();
    auto status = ref ? PhotoPreparationStatus::ReadUnavailable
                      : PhotoPreparationStatus::SourceUnavailable;
    auto kind = bot::MediaKind::Image;
    std::optional<bot::BotOperationErrorCode> read_error;
    std::optional<bot::BytesSource> source;
    if (read && !read->ok()) {
      read_error = read->error->code;
      if (*read_error == bot::BotOperationErrorCode::Cancelled)
        // Preserve cancellation classification and the original operation
        // evidence. It must not become a generic preparation failure.
        throw BridgeBotOperationFailure(std::move(*read->error));
    } else if (read) {
      auto blob = std::move(*read->value);
      if (blob.source.bytes.size() > maximum ||
          (blob.identity && blob.identity->installation != ref->installation))
        throw std::runtime_error("photo read result violates scope or budget");
      DownloadedImage image{
          .type = "photo",
          .filename = "qq-media.jpg",
          .data = {blob.source.bytes.begin(), blob.source.bytes.end()}};
      const auto mime = static_photo_mime(image.data);
      const bool image_kind = !blob.kind || *blob.kind == bot::MediaKind::Image;
      std::optional<PhotoNormalizationResult> normalized;
      if ((image_kind || blob.kind == bot::MediaKind::Animation) &&
          MediaProcessor::detect_mime_type_from_content(image.data) ==
              "image/gif") {
        const auto dimensions = inspect_photo_dimensions(image.data);
        const auto frames = inspect_gif_frames(image.data);
        if (dimensions.status == PhotoDimensionStatus::InvalidDimensions)
          status = PhotoPreparationStatus::InvalidDimensions;
        else if (dimensions.status == PhotoDimensionStatus::UnsafeDimensions)
          status = PhotoPreparationStatus::UnsafeDimensions;
        else if (frames == GifFrameCount::Malformed)
          status = PhotoPreparationStatus::UnsupportedFormat;
        else if (frames == GifFrameCount::Single)
          normalized = co_await blocking->run(
              [config, maximum, image = std::move(image)]() mutable {
                return PhotoNormalizer(config->ffmpeg_path, maximum)
                    .flatten_gif(std::move(image));
              });
        else {
          // Static-photo aspect/size normalization must not flatten an
          // animated GIF. Preserve its bytes for the animation recipe.
          blob.source.metadata.mime_type = "image/gif";
          blob.source.metadata.filename = "qq-media.gif";
          blob.source.metadata.size_bytes = blob.source.bytes.size();
          blob.source.metadata.width = dimensions.dimensions.width;
          blob.source.metadata.height = dimensions.dimensions.height;
          blob.source.validate();
          source = std::move(blob.source);
          kind = bot::MediaKind::Animation;
          status = PhotoPreparationStatus::Ready;
        }
      } else if (!image_kind || !mime) {
        status = PhotoPreparationStatus::UnsupportedFormat;
      } else {
        image.mime_type = *mime;
        image.filename = *mime == "image/png" ? "qq-media.png" : "qq-media.jpg";
        normalized = co_await blocking->run(
            [config, maximum, image = std::move(image)]() mutable {
              return PhotoNormalizer(config->ffmpeg_path, maximum)
                  .normalize(std::move(image));
            });
      }
      if (normalized) {
        if (normalized->succeeded()) {
          source = owned_photo(std::move(*normalized->image), maximum);
          status = normalized->normalized ? PhotoPreparationStatus::Adjusted
                                          : PhotoPreparationStatus::Ready;
        } else {
          status = normalization_status(normalized->failure);
        }
      }
    }
    if (!source) {
      if (!placeholder) {
        auto selected = embedded_placeholder();
        if (!config->image_placeholder_url.empty()) {
          auto downloaded =
              co_await read_configured_placeholder(config, maximum);
          if (downloaded) {
            auto normalized = co_await blocking->run(
                [config, maximum, image = std::move(*downloaded)]() mutable {
                  return PhotoNormalizer(config->ffmpeg_path, maximum)
                      .normalize(std::move(image));
                });
            if (normalized.succeeded() &&
                normalized.image->data.size() <= maximum)
              selected = std::move(*normalized.image);
          }
        }
        placeholder = owned_photo(std::move(selected), maximum);
      }
      if (placeholder->bytes.size() > maximum)
        throw std::runtime_error(
            "photo placeholder exceeds remaining byte budget");
      source = *placeholder;
    }
    total += source->bytes.size();
    prepared.push_back({std::move(*source), status, read_error, kind});
  }
  co_return prepared;
}
} // namespace bridge::qq
