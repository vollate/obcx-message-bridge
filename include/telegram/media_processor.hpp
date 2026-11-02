#pragma once

#include "bridge_command_operations.hpp"
#include "config.hpp"
#include "path_manager.hpp"

#include <boost/asio.hpp>
#include <core/actor/blocking_executor.hpp>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <telegram/bot/domain_views.hpp>
#include <vector>

namespace bridge {
class BridgeStateRepository;
}

namespace bridge::telegram {

class TelegramCacheScopeMismatch final : public std::logic_error {
public:
  using std::logic_error::logic_error;
};

struct PreparedTelegramMedia {
  obcx::bot::MessageBlock block;
  std::size_t prepared_bytes;
};

class TelegramMediaProcessor {
public:
  explicit TelegramMediaProcessor(
      std::shared_ptr<BridgeCommandOperations> operations,
      std::shared_ptr<const bridge::BridgeConfig> config,
      std::shared_ptr<bridge::BridgeStateRepository> state_repository,
      std::shared_ptr<obcx::core::BlockingExecutor> blocking_executor);

  // Preparation owns bounded bytes before delivery admission. Cache paths
  // never become outgoing native segments or provider-side file references.
  auto prepare_sticker(obcx::bot::ReceivedMedia media,
                       obcx::bot::RichText caption, std::size_t maximum_bytes)
      -> boost::asio::awaitable<PreparedTelegramMedia>;
  // Uses the same actor cache/conversion recipe, then owns bounded bytes.
  // GIF stays Animation; MP4 is explicitly presented as Video on QQ. Failed
  // WebM/TGS/unknown data is never mislabeled as a playable image or GIF.
  auto prepare_animation(obcx::bot::ReceivedMedia media,
                         obcx::bot::RichText caption, std::size_t maximum_bytes)
      -> boost::asio::awaitable<PreparedTelegramMedia>;

private:
  struct CacheRead {
    std::vector<std::uint8_t> bytes;
    std::size_t prepared_bytes;
    std::exception_ptr error;
  };
  using CacheTransform = std::function<std::optional<PathManager::CreatedFile>(
      const PathManager &, const std::string &, std::span<const std::uint8_t>,
      std::size_t)>;
  // Only owned bytes cross suspension. File descriptors, conversion scratch,
  // publication guards and cache persistence stay in the worker callable.
  auto read_cached_media(obcx::bot::ReceivedMedia media,
                         std::size_t maximum_bytes, CacheTransform transform)
      -> boost::asio::awaitable<CacheRead>;
  void validate_source(const obcx::bot::ReceivedMedia &) const;
  static void rethrow_if_cancelled(const std::exception &);

  std::shared_ptr<BridgeCommandOperations> operations_;
  std::shared_ptr<const bridge::BridgeConfig> config_;
  PathManager path_manager_;
  std::shared_ptr<bridge::BridgeStateRepository> state_repository_;
  std::shared_ptr<obcx::core::BlockingExecutor> blocking_executor_;
};

} // namespace bridge::telegram
