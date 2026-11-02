#include "bridge_media_reader.hpp"
#include "bridge_bot_failures.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace bridge {
namespace domain = obcx::bot;
namespace {
// Match the existing Telegram upload signature checks, not an implicit decoder
// or transcoder. Native kind remains authoritative; unknown encodings stay
// unknown and the destination adapter still validates the complete send.
auto attachment_encoding(const domain::BytesSource &source,
                         domain::MediaKind kind) -> std::optional<std::string> {
  const std::string_view bytes{
      reinterpret_cast<const char *>(source.bytes.data()), source.bytes.size()};
  const bool audio =
      kind == domain::MediaKind::Audio || kind == domain::MediaKind::Voice;
  if (bytes.size() >= 12 && bytes.substr(4, 4) == "ftyp") {
    if (audio)
      return "audio/mp4";
    if (kind == domain::MediaKind::Video ||
        kind == domain::MediaKind::Animation)
      return "video/mp4";
  }
  if (audio) {
    if (bytes.starts_with("OggS") && bytes.contains("OpusHead"))
      return "audio/ogg";
    if (bytes.starts_with("ID3"))
      return "audio/mpeg";
    if (bytes.size() >= 4) {
      const auto second = static_cast<unsigned char>(bytes[1]);
      const auto third = static_cast<unsigned char>(bytes[2]);
      // Do not mistake AAC ADTS sync (reserved MPEG-audio layer 00) for MP3.
      if (static_cast<unsigned char>(bytes[0]) == 0xff &&
          (second & 0xe0) == 0xe0 && (second & 0x18) != 0x08 &&
          (second & 0x06) != 0 && (third & 0xf0) != 0 &&
          (third & 0xf0) != 0xf0 && (third & 0x0c) != 0x0c)
        return "audio/mpeg";
    }
  }
  return std::nullopt;
}
} // namespace

auto read_bridge_attachment(
    std::shared_ptr<domain::BotOperationGateway> gateway,
    domain::ReceivedMedia received, std::size_t maximum_bytes)
    -> boost::asio::awaitable<domain::BytesSource> {
  received.validate();
  if (!gateway || !maximum_bytes ||
      maximum_bytes > domain::detail::domain_maximum_bytes)
    throw std::invalid_argument(
        "attachment read requires service and byte budget");
  if (received.kind == domain::MediaKind::File &&
      received.metadata.size_bytes &&
      *received.metadata.size_bytes > maximum_bytes)
    throw BridgeBotOperationFailure(
        {.code = domain::BotOperationErrorCode::MediaTooLarge,
         .message = "file exceeds bridge byte budget",
         .retryable = false,
         .submission_safety =
             domain::SubmissionSafety::DefinitelyNotSubmitted});
  auto read = co_await domain::BotClient{*gateway}.read_media(
      domain::ReadMediaRequest{received.media, maximum_bytes});
  read.validate();
  if (!read.ok())
    throw BridgeBotOperationFailure(std::move(*read.error));
  auto blob = std::move(*read.value);
  if (blob.source.bytes.size() > maximum_bytes ||
      (blob.kind && *blob.kind != received.kind) ||
      (blob.identity &&
       blob.identity->installation != received.media.installation) ||
      (blob.identity && received.identity &&
       *blob.identity != *received.identity))
    throw std::runtime_error(
        "attachment read result violates kind, identity or budget");
  if (received.kind == domain::MediaKind::File &&
      received.metadata.size_bytes &&
      *received.metadata.size_bytes != blob.source.bytes.size())
    throw BridgeBotOperationFailure(
        {.code = domain::BotOperationErrorCode::MalformedResponse,
         .message = "file read size differs from received metadata",
         .retryable = false,
         .submission_safety =
             domain::SubmissionSafety::DefinitelyNotSubmitted});
  // Preserve available typed metadata without replacing the reader's MIME or
  // copying a reported size into verified bytes. A native MIME is only a hint:
  // the destination adapter still checks the actual supported byte signature.
  if (!blob.source.metadata.mime_type)
    blob.source.metadata.mime_type = received.metadata.mime_type;
  if (!blob.source.metadata.mime_type)
    blob.source.metadata.mime_type =
        attachment_encoding(blob.source, received.kind);
  if (received.metadata.filename)
    blob.source.metadata.filename = received.metadata.filename;
  if (!blob.source.metadata.width)
    blob.source.metadata.width = received.metadata.width;
  if (!blob.source.metadata.height)
    blob.source.metadata.height = received.metadata.height;
  if (!blob.source.metadata.duration_ms)
    blob.source.metadata.duration_ms = received.metadata.duration_ms;
  blob.source.metadata.size_bytes = blob.source.bytes.size();
  blob.source.validate();
  co_return std::move(blob.source);
}

auto has_telegram_bridge_prefix(const domain::MessageReceived &message)
    -> bool {
  constexpr std::string_view marker = "[Telegram] ";
  std::size_t matched = 0;
  for (const auto &block : message.body) {
    const auto *text = std::get_if<domain::ReceivedRichText>(&block);
    if (!text)
      return false;
    for (const auto &span : *text) {
      const auto *literal = std::get_if<domain::Text>(&span);
      if (!literal)
        return false;
      const auto count =
          std::min(marker.size() - matched, literal->content.size());
      if (std::string_view(literal->content).substr(0, count) !=
          marker.substr(matched, count))
        return false;
      matched += count;
      if (matched == marker.size())
        return true;
    }
  }
  return false;
}
} // namespace bridge
