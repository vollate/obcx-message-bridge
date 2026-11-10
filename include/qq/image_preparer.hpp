#pragma once

#include "config.hpp"
#include <core/actor/blocking_executor.hpp>
#include <core/bot/bot_client.hpp>

namespace bridge::qq {

enum class PhotoPreparationStatus {
  Ready,
  Adjusted,
  ReadUnavailable,
  SourceUnavailable,
  UnsupportedFormat,
  InvalidDimensions,
  UnsafeDimensions,
  NormalizationFailed
};
struct PreparedPhoto {
  obcx::bot::BytesSource source;
  PhotoPreparationStatus status;
  std::optional<obcx::bot::BotOperationErrorCode> read_error;
  obcx::bot::MediaKind kind;
  [[nodiscard]] auto replaced() const -> bool {
    return status != PhotoPreparationStatus::Ready &&
           status != PhotoPreparationStatus::Adjusted;
  }
};

// A missing slot explicitly represents a received image with no usable media
// reference. It preserves position without fabricating a reference or I/O.
using PhotoInput = std::optional<obcx::bot::MediaRef>;

// Actor-owned acquisition result, consumed once without a second provider read.
// A supplied result requires its original scoped request reference. This does
// not restore or mint an ingress receipt; it only retains completed read data.
struct PhotoReadInput {
  PhotoInput media;
  std::optional<obcx::bot::BotOperationResult<obcx::bot::MediaBlob>> result;
};
auto prepare_read_photos(
    std::shared_ptr<obcx::bot::BotOperationGateway> gateway,
    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
    std::shared_ptr<const BridgeConfig> config,
    std::vector<PhotoReadInput> media, obcx::bot::SendLimits limits)
    -> boost::asio::awaitable<std::vector<PreparedPhoto>>;

// Only original observed media references enter here. No archive, native URL,
// provider path, send or retry is accepted. The configured placeholder is a
// separate actor-owned public source; it never substitutes read authority.
// Multi-frame GIF bytes remain Animation and are never flattened. Single-frame
// GIFs become static photos; structurally invalid GIFs are unsupported.
auto prepare_photos(std::shared_ptr<obcx::bot::BotOperationGateway> gateway,
                    std::shared_ptr<obcx::core::BlockingExecutor> blocking,
                    std::shared_ptr<const BridgeConfig> config,
                    std::vector<PhotoInput> media, obcx::bot::SendLimits limits)
    -> boost::asio::awaitable<std::vector<PreparedPhoto>>;
} // namespace bridge::qq
