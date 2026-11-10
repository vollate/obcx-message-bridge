#include "telegram/media_processor.hpp"

#include "bridge_bot_failures.hpp"
#include "bridge_state_repository.hpp"
#include "media_converter.hpp"
#include "media_processor.hpp"
#include "qq/photo_normalizer.hpp"

#include <algorithm>
#include <chrono>
#include <common/logger.hpp>
#include <core/bot/bot_codec.hpp>
#include <filesystem>
#include <fmt/format.h>
#include <type_traits>
#include <utility>

namespace bridge::telegram {
namespace {
using Bytes = std::span<const std::uint8_t>;
using Transform = std::function<std::optional<PathManager::CreatedFile>(
    const PathManager &, const std::string &, Bytes, std::size_t)>;

struct CachePlan {
  std::string installation;
  std::string platform;
  std::string folder;
  std::string prefix;
  std::string file_id;
  std::string unique_id;
  std::string mime;
  std::string extension;
};

auto mime_of(Bytes bytes) -> std::string {
  const auto size = std::min(bytes.size(), std::size_t{32});
  return MediaProcessor::detect_mime_type_from_content(
      std::string(bytes.begin(), bytes.begin() + size));
}

auto valid_gif(Bytes bytes) -> bool {
  if (bytes.size() < 14 || bytes.back() != 0x3b)
    return false;
  const std::string_view encoded(reinterpret_cast<const char *>(bytes.data()),
                                 bytes.size());
  if (!encoded.starts_with("GIF87a") && !encoded.starts_with("GIF89a"))
    return false;
  const auto status = qq::inspect_photo_dimensions(encoded).status;
  return status != qq::PhotoDimensionStatus::InvalidDimensions &&
         status != qq::PhotoDimensionStatus::UnsafeDimensions;
}

auto same_cache_path(const PathManager &mapper, const std::string &left,
                     const std::string &right) -> bool {
  try {
    return mapper.host_to_container_absolute(left) ==
           mapper.host_to_container_absolute(right);
  } catch (const std::runtime_error &) {
    return false; // An unmappable historical path cannot name this entry.
  }
}

auto check_cache_claim(BridgeStateRepository *repository,
                       const PathManager &mapper,
                       const storage::StickerCacheInfo &row,
                       const std::string &path) -> bool {
  if (!repository)
    return false;
  bool referenced = false;
  for (const auto &ref : repository->sticker_cache_file_references(
           std::filesystem::path(path).filename().string())) {
    if (!same_cache_path(mapper, ref.host_path, path))
      continue;
    if (ref.installation_id != row.installation_id ||
        ref.platform != row.platform || ref.sticker_hash != row.sticker_hash)
      throw TelegramCacheScopeMismatch(
          "cache filename belongs to another namespace or key");
    referenced = true;
  }
  return referenced;
}

void retain_referenced(BridgeStateRepository *repository,
                       const PathManager &mapper,
                       PathManager::CreatedFile &created) {
  if (!repository)
    return;
  try {
    for (const auto &ref : repository->sticker_cache_file_references(
             std::filesystem::path(created.path()).filename().string())) {
      if (same_cache_path(mapper, ref.host_path, created.path())) {
        created.retain();
        return;
      }
    }
  } catch (const std::exception &error) {
    // A failed read cannot prove that a concurrent/uncertain cache commit did
    // not reference our inode. Keep it for explicit recovery, never unlink it
    // on an assumed rollback. This does not authorize any provider operation.
    created.retain();
    if (is_bridge_operation_cancelled(error))
      throw;
    OBCX_WARN("Cache file reference state unavailable; cleanup deferred");
  } catch (...) {
    created.retain();
    throw;
  }
}

// CreatedFile itself only knows inode ownership, not whether a cache commit
// succeeded. On exceptional exits, consult references BEFORE its destructor
// can unlink a newly published inode. Keep files when that consultation fails.
struct CacheReferenceGuard {
  BridgeStateRepository *repository;
  const PathManager *mapper;
  PathManager::CreatedFile *original;
  std::optional<PathManager::CreatedFile> *converted;
  bool settled = false;

  ~CacheReferenceGuard() noexcept {
    if (settled)
      return;
    const auto protect = [&](PathManager::CreatedFile &file) noexcept {
      try {
        retain_referenced(repository, *mapper, file);
      } catch (...) {
        // Preserve the original preparation/cancellation exception. A second
        // error (including logging/allocation) cannot authorize file removal.
        file.retain();
      }
    };
    if (original)
      protect(*original);
    if (converted && *converted)
      protect(**converted);
  }
};

auto persist_cache(BridgeStateRepository *repository,
                   std::optional<storage::StickerCacheInfo> &expected,
                   const storage::StickerCacheInfo &row) -> bool {
  if (!repository || row.sticker_hash.empty())
    return false;
  try {
    // Allocate the next snapshot before committing. After commit, advancing
    // it and retaining file guards must not allocate or throw.
    static_assert(std::is_nothrow_move_assignable_v<
                  std::optional<storage::StickerCacheInfo>>);
    std::optional<storage::StickerCacheInfo> next{row};
    if (!repository->save_sticker_cache_if_unchanged(expected, row)) {
      OBCX_DEBUG("Telegram cache changed concurrently; newer row preserved");
      return false;
    }
    expected = std::move(next);
    return true;
  } catch (const std::exception &error) {
    if (is_bridge_operation_cancelled(error))
      throw;
    OBCX_WARN("Telegram cache persistence failed; prepared bytes retained");
    return false;
  }
}

// Synchronous worker-only extent. No descriptor/publication guard is returned
// through BlockingExecutor; every exit settles ownership before completion.
// Cleanup can be explicitly deferred when reference state is unknown.
auto prepare_cache_file(const PathManager &mapper,
                        storage::StickerCacheInfo row,
                        std::string selected_path,
                        std::vector<std::uint8_t> prepared, bool convert_webm,
                        const Transform &transform, const BridgeConfig &config,
                        std::size_t maximum_bytes,
                        BridgeStateRepository *repository,
                        PathManager::CreatedFile *original,
                        std::optional<storage::StickerCacheInfo> expected)
    -> std::vector<std::uint8_t> {
  std::optional<PathManager::CreatedFile> converted;
  // Declared after converted so reference protection precedes inode cleanup
  // during unwinding, including a committed-but-reported-failed static save.
  CacheReferenceGuard references{repository, &mapper, original, &converted};
  if (convert_webm) {
    auto filename = std::filesystem::path(row.original_file_path).filename();
    filename.replace_extension(".gif");
    const auto folder = row.platform == "telegram" ? "stickers" : "animations";
    const auto destination = mapper.to_host_path(
        std::string(folder) + "/converted/" + filename.string());
    const auto limit = config.gif_max_file_size == 0
                           ? maximum_bytes
                           : std::min(maximum_bytes, config.gif_max_file_size);
    const auto referenced =
        check_cache_claim(repository, mapper, row, destination);
    converted = mapper.convert_owned_file(
        selected_path, destination, prepared, limit, valid_gif,
        [&](const std::string &input, const std::string &output) {
          return MediaConverter::convert_webm_to_gif_with_fallback(
              config.ffmpeg_path, input, output, config.gif_max_duration, limit,
              config.gif_max_width, config.gif_max_fps, config.gif_max_colors);
        });
    if (converted) {
      if (referenced)
        converted->retain();
      prepared = converted->read(maximum_bytes);
      selected_path = converted->path();
      row.converted_file_path = selected_path;
      row.conversion_status = "success";
    } else {
      row.conversion_status = "failed";
      // Preserve the original cache file/status. Presentation decides whether
      // its bytes are supported; failed WebM never becomes an Image/GIF.
    }
  }
  if (transform) {
    if (mime_of(prepared) == "image/webp" &&
        selected_path != row.original_file_path)
      throw TelegramCacheScopeMismatch("sticker conversion cache row mismatch");
    // The predecessor caches a newly acquired static original before trying
    // PNG normalization. Preserve that reusable original on conversion failure,
    // but only if its real cache row was saved. Missing-ID/failed-save files
    // remain worker-owned and are removed on every exit.
    if (original && mime_of(prepared) == "image/webp") {
      row.container_path = mapper.host_to_container_absolute(selected_path);
      row.last_used_at = std::chrono::system_clock::now();
      if (persist_cache(repository, expected, row))
        original->retain();
    }
    bool referenced = false;
    if (mime_of(prepared) == "image/webp") {
      auto filename = std::filesystem::path(selected_path).filename();
      filename.replace_extension(".png");
      referenced = check_cache_claim(
          repository, mapper, row,
          mapper.to_host_path("stickers/converted/" + filename.string()));
    }
    auto output = transform(mapper, selected_path, prepared, maximum_bytes);
    if (output) {
      // At most one conversion recipe applies to a source observation.
      if (converted)
        throw std::logic_error("multiple cache conversion owners");
      converted = std::move(output);
      if (referenced)
        converted->retain();
      prepared = converted->read(maximum_bytes);
      selected_path = converted->path();
      row.converted_file_path = selected_path;
      row.conversion_status = "success";
    }
  }
  row.container_path = mapper.host_to_container_absolute(selected_path);
  row.last_used_at = std::chrono::system_clock::now();
  if (persist_cache(repository, expected, row)) {
    if (original)
      original->retain();
    if (converted)
      converted->retain();
  } else {
    if (original)
      retain_referenced(repository, mapper, *original);
    if (converted)
      retain_referenced(repository, mapper, *converted);
  }
  // Owned bytes survive. Only files proven unreferenced may be removed;
  // uncertain reference state intentionally defers cleanup to offline review.
  references.settled = true;
  return prepared;
}
} // namespace

auto TelegramMediaProcessor::read_cached_media(obcx::bot::ReceivedMedia media,
                                               std::size_t maximum_bytes,
                                               CacheTransform transform)
    -> boost::asio::awaitable<CacheRead> {
  validate_source(media);
  if (!maximum_bytes || maximum_bytes > config_->qq_media_download_max_bytes ||
      maximum_bytes > obcx::bot::detail::bot_maximum_bytes)
    throw std::invalid_argument("invalid Telegram cache byte budget");
  const auto info = obcx::telegram::bot::file_observation_view(media);
  const auto sticker = obcx::telegram::bot::sticker_view(media);
  CachePlan plan;
  plan.installation = operations_->telegram_installation().installation_id;
  plan.file_id = info.file_id;
  plan.unique_id = info.file_unique_id.value_or("");
  if (sticker) {
    if (sticker->format == obcx::telegram::bot::StickerFormat::AnimatedTgs)
      throw std::invalid_argument(
          "TGS has no verified cache conversion recipe");
    plan.platform = "telegram";
    plan.folder = "stickers";
    plan.prefix = "sticker";
    const bool webm =
        sticker->format == obcx::telegram::bot::StickerFormat::VideoWebm;
    plan.mime = webm ? "video/webm" : "image/webp";
    plan.extension = webm ? ".webm" : ".webp";
  } else {
    if (media.kind != obcx::bot::MediaKind::Animation)
      throw std::invalid_argument(
          "animation or sticker cache observation required");
    plan.platform = "telegram_animation";
    plan.folder = "animations";
    plan.prefix = "animation";
    plan.mime = media.metadata.mime_type.value_or("video/mp4");
    plan.extension = plan.mime == "video/webm"  ? ".webm"
                     : plan.mime == "image/gif" ? ".gif"
                                                : ".mp4";
  }

  struct Lookup {
    std::optional<CacheRead> result;
    std::optional<storage::StickerCacheInfo> expected;
  };
  std::optional<storage::StickerCacheInfo> expected;
  if (!plan.unique_id.empty()) {
    auto cached = co_await blocking_executor_->run(
        [repository = state_repository_, mapper = path_manager_,
         config = config_, plan, transform, maximum_bytes]() -> Lookup {
          if (!repository)
            return {};
          auto row = repository->get_sticker_cache(
              plan.installation, plan.platform, plan.unique_id);
          if (!row)
            return {};
          if (row->installation_id != plan.installation ||
              row->platform != plan.platform ||
              row->sticker_hash != plan.unique_id)
            throw TelegramCacheScopeMismatch(
                "Telegram cache row scope mismatch");
          const bool converted_selected = row->conversion_status == "success" &&
                                          row->converted_file_path &&
                                          !row->converted_file_path->empty();
          auto selected = converted_selected ? *row->converted_file_path
                                             : row->original_file_path;
          if (selected.empty() || row->container_path.empty())
            return {{}, std::move(row)};
          bool regenerate_webm = false;
          try {
            (void)mapper.file_uri(selected);
          } catch (const std::runtime_error &) {
            if (!converted_selected || row->original_file_path.empty())
              return {{}, std::move(row)};
            // A lost derived cache is not a lost original. Reuse the same
            // scoped row's independently checked original instead of fetching
            // again and attempting to overwrite its immutable filename.
            try {
              (void)mapper.file_uri(row->original_file_path);
            } catch (const std::runtime_error &) {
              return {{}, std::move(row)};
            }
            selected = row->original_file_path;
            regenerate_webm = plan.mime == "video/webm";
          }
          auto bytes = mapper.read_file_bounded(selected, maximum_bytes);
          const auto acquired = bytes.size();
          try {
            // Regeneration is only for a previously successful derived file
            // that became unavailable. Existing failed WebM entries are still
            // not retried. Publication remains exclusive and the full row CAS
            // protects concurrent cache changes.
            auto prior = row;
            auto prepared = prepare_cache_file(
                mapper, std::move(*row), selected, std::move(bytes),
                regenerate_webm, transform, *config, maximum_bytes,
                repository.get(), nullptr, std::move(prior));
            const auto size = prepared.size();
            return {CacheRead{std::move(prepared), size, {}}, {}};
          } catch (...) {
            return {CacheRead{{}, acquired, std::current_exception()}, {}};
          }
        });
    if (cached.result)
      co_return std::move(*cached.result);
    expected = std::move(cached.expected);
  }

  // Preserve the existing unique/file prefixes and missing-unique-ID timestamp
  // recipe. This timestamp is a local filename fact, not a provider identity.
  const auto identity =
      !plan.unique_id.empty()
          ? plan.unique_id.substr(0, 12)
          : std::to_string(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count());
  const auto prefix =
      fmt::format("{}_{}_{}", plan.prefix, identity, plan.file_id.substr(0, 8));
  if (std::filesystem::path(prefix).filename() != prefix)
    throw std::runtime_error("invalid Telegram cache filename");
  auto fetched = co_await operations_->read_telegram_attachment(
      std::move(media), maximum_bytes);
  const auto acquired = fetched.bytes.size();
  try {
    co_return co_await blocking_executor_->run(
        [repository = state_repository_, mapper = path_manager_,
         config = config_, plan = std::move(plan), prefix,
         transform = std::move(transform), maximum_bytes,
         fetched = std::move(fetched),
         expected = std::move(expected)]() mutable {
          const auto directory = plan.folder + "/original";
          if (!mapper.ensure_directory(directory))
            throw std::runtime_error("cannot create Telegram cache directory");
          storage::StickerCacheInfo row;
          row.installation_id = plan.installation;
          row.platform = plan.platform;
          row.sticker_id = plan.file_id;
          row.sticker_hash = plan.unique_id;
          // Preserve original_name absence and the original empty file_type.
          row.original_file_path =
              mapper.to_host_path(directory + "/" + prefix + plan.extension);
          row.file_size = static_cast<std::int64_t>(fetched.bytes.size());
          row.mime_type = plan.mime;
          row.conversion_status = "success";
          row.created_at = std::chrono::system_clock::now();
          const auto referenced = check_cache_claim(
              repository.get(), mapper, row, row.original_file_path);
          auto original =
              mapper.write_owned_file(directory + "/" + prefix + plan.extension,
                                      std::as_bytes(std::span(fetched.bytes)));
          CacheReferenceGuard references{repository.get(), &mapper, &original,
                                         nullptr};
          if (referenced)
            original.retain();
          auto prepared = prepare_cache_file(
              mapper, std::move(row), original.path(),
              original.read(maximum_bytes), plan.mime == "video/webm",
              transform, *config, maximum_bytes, repository.get(), &original,
              std::move(expected));
          references.settled = true;
          const auto size = prepared.size();
          return CacheRead{std::move(prepared), size, {}};
        });
  } catch (...) {
    // Conversion/file/publication failure still accounts for the original
    // accepted media bytes, even when presentation becomes a placeholder.
    co_return CacheRead{{}, acquired, std::current_exception()};
  }
}
} // namespace bridge::telegram
