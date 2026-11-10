#include "qq/forward_preparer.hpp"
#include "bridge_bot_failures.hpp"
#include "bridge_media_reader.hpp"
#include "qq/voice_preparer.hpp"

#include <algorithm>
#include <array>
#include <onebot11/bot/client.hpp>
#include <onebot11/bot/user_target.hpp>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace bridge::qq {
namespace {
namespace d = obcx::bot;
namespace qq = obcx::onebot11::bot;

void append(d::RichText &text, std::string value) {
  if (!value.empty())
    text.emplace_back(d::Text{std::move(value), {}});
}
auto display_marker(std::string_view category) -> std::optional<std::string> {
  // The predecessor deliberately displays these node-only kinds as [type],
  // rather than recursively looking up forwards or reading nested locators.
  // Keep that explicit Bridge presentation; malformed text/face/at/media are
  // not accepted here and cannot silently become a successful text-only copy.
  constexpr std::array kinds{
      "json",    "app",      "ark", "miniapp",   "share", "music",
      "node",    "forward",  "rps", "dice",      "shake", "poke",
      "contact", "location", "xml", "anonymous", "mface"};
  // The SDK keeps a node's reply as this explicit unsupported marker because
  // the node has no standalone MessageRef. Display it, never resolve/quote it.
  if (category == "onebot11.forward_reply")
    return "[reply]";
  constexpr std::string_view prefix = "onebot11.";
  if (!category.starts_with(prefix))
    return std::nullopt;
  const auto kind = category.substr(prefix.size());
  if (std::ranges::find(kinds, kind) == kinds.end())
    return std::nullopt;
  return "[" + std::string(kind) + "]";
}
auto display_text(const d::ReceivedRichText &source) -> d::RichText {
  d::RichText result;
  for (const auto &span : source) {
    if (const auto *text = std::get_if<d::Text>(&span)) {
      if (!text->content.empty())
        result.emplace_back(*text);
    } else if (const auto *mention = std::get_if<d::Mention>(&span)) {
      const auto view = qq::user_identity_view(mention->user);
      append(result, "[@" + std::to_string(view.user_id) + "]");
    } else if (std::holds_alternative<d::MentionEveryone>(span)) {
      append(result, "[@all]");
    } else {
      // Do not lose a source mention and report a complete text-only copy.
      throw std::runtime_error("forward node has an unresolved mention");
    }
  }
  return result;
}
} // namespace

auto prepare_forward(std::shared_ptr<d::BotOperationGateway> gateway,
                     std::shared_ptr<obcx::core::BlockingExecutor> blocking,
                     std::shared_ptr<const BridgeConfig> config,
                     qq::QqForwardRef forward, d::SendLimits limits)
    -> boost::asio::awaitable<d::MessageBody> {
  forward.validate();
  limits.validate();
  if (!gateway || !blocking || !config)
    throw std::invalid_argument("forward preparation requires owned services");
  auto read = co_await qq::Client{*gateway}.execute(
      qq::ObserveOneBotForwardMessageRequest{forward});
  read.validate();
  if (!read.ok())
    throw BridgeBotOperationFailure(std::move(*read.error));
  read.value->validate();
  if (read.value->message.forward != forward)
    throw std::runtime_error("forward lookup result scope mismatch");

  d::RichText transcript;
  append(transcript, "\n📋 合并转发消息:\n");
  std::vector<PhotoInput> images;
  std::vector<d::RichText> captions;
  std::vector<std::variant<std::size_t, d::ReceivedMedia>> attachments;
  std::size_t face_cursor = 0;
  for (std::size_t node_index = 0;
       node_index < read.value->message.nodes.size(); ++node_index) {
    const auto &node = read.value->message.nodes[node_index];
    // Report the observed display name, never manufacture an author UserRef.
    append(transcript, "👤 " + node.display_name.value_or("未知用户") + ": ");
    for (std::size_t block_index = 0; block_index < node.body.size();
         ++block_index) {
      const auto &block = node.body[block_index];
      if (const auto *text = std::get_if<d::ReceivedRichText>(&block)) {
        auto rendered = display_text(*text);
        for (auto &span : rendered)
          transcript.push_back(std::move(span));
      } else if (const auto *media = std::get_if<d::ReceivedMedia>(&block)) {
        if (media->kind == d::MediaKind::Image) {
          attachments.emplace_back(images.size());
          images.emplace_back(media->media);
          captions.push_back(display_text(media->caption));
          append(transcript, "[图片" + std::to_string(images.size()) + "]");
        } else {
          attachments.emplace_back(*media);
          append(transcript,
                 "[附件" + std::to_string(attachments.size()) + "]");
        }
      } else {
        const auto &unsupported = std::get<d::UnsupportedContent>(block);
        if (unsupported.category == "onebot11.face") {
          if (face_cursor >= read.value->faces.size() ||
              read.value->faces[face_cursor].node_index != node_index ||
              read.value->faces[face_cursor].normalized_block_index !=
                  block_index)
            throw std::runtime_error(
                "forward face lacks its original observation");
          append(transcript,
                 "[表情:" + read.value->faces[face_cursor++].face.id + "]");
        } else if (unsupported.category == "onebot11.image") {
          attachments.emplace_back(images.size());
          images.emplace_back(std::nullopt);
          captions.emplace_back();
          append(transcript, "[图片" + std::to_string(images.size()) + "]");
        } else if (auto marker = display_marker(unsupported.category)) {
          append(transcript, std::move(*marker));
        } else {
          // Do not turn unmodeled nodes into a successful text-only copy.
          // The diagnostic identifies the unsupported category; no send has
          // occurred and the caller cannot fall back to the predecessor.
          throw std::runtime_error("forward node has unsupported content: " +
                                   unsupported.category);
        }
      }
    }
    append(transcript, "\n");
  }
  if (face_cursor != read.value->faces.size())
    throw std::runtime_error("forward face observations were not consumed");
  if (read.value->message.nodes.empty())
    append(transcript, "[空合并转发消息]\n");
  if (attachments.size() >= limits.maximum_messages)
    throw std::runtime_error("forward expansion exceeds output budget");
  d::validate_rich_text(transcript, forward.source.conversation.installation);
  d::MessageBody body;
  body.emplace_back(std::move(transcript));
  if (attachments.empty())
    co_return body;
  std::vector<PreparedPhoto> prepared;
  if (!images.empty())
    prepared = co_await prepare_photos(gateway, blocking, config,
                                       std::move(images), limits);
  std::vector<d::MessageBlock> prepared_images;
  std::size_t bytes = 0;
  for (const auto &photo : prepared)
    bytes += photo.source.bytes.size();
  // Within this forward, use the destination's adjacent static-photo grouping
  // rule for recovery captions. Non-image attachments and animations break an
  // album; grouping the flat image list would count unrelated images together.
  // The transcript is always the first output, so any reply belongs to it,
  // not to the first photo batch.
  std::vector<std::pair<std::size_t, std::size_t>> image_batches;
  for (std::size_t position = 0; position < attachments.size();) {
    const auto *image = std::get_if<std::size_t>(&attachments[position]);
    if (!image) {
      ++position;
      continue;
    }
    std::size_t run = 1;
    if (prepared.at(*image).kind == d::MediaKind::Image) {
      while (position + run < attachments.size()) {
        const auto *next =
            std::get_if<std::size_t>(&attachments[position + run]);
        if (!next || prepared.at(*next).kind != d::MediaKind::Image)
          break;
        ++run;
      }
    }
    for (std::size_t consumed = 0; consumed < run;) {
      const auto remaining = run - consumed;
      auto count = std::min<std::size_t>(10, remaining);
      if (remaining > 10 && remaining - count == 1)
        --count;
      image_batches.emplace_back(*image + consumed, count);
      consumed += count;
    }
    position += run;
  }
  const auto batches = image_batches.size();
  for (std::size_t batch = 0; batch < batches; ++batch) {
    const auto [start, count] = image_batches[batch];
    std::size_t adjusted = 0, replaced = 0, unsupported = 0;
    for (std::size_t i = start; i < start + count; ++i) {
      adjusted += prepared[i].status == PhotoPreparationStatus::Adjusted;
      replaced += prepared[i].replaced();
      unsupported +=
          prepared[i].status == PhotoPreparationStatus::UnsupportedFormat;
    }
    for (std::size_t i = start; i < start + count; ++i) {
      auto caption = std::move(captions[i]);
      if (i == start) {
        if (!caption.empty())
          append(caption, "\n");
        append(caption, "📸 合并转发消息中的图片 (" +
                            std::to_string(batch + 1) + "/" +
                            std::to_string(batches) + ")");
        if (adjusted)
          append(caption,
                 "\nℹ️ " + std::to_string(adjusted) + " 张图片尺寸已自动调整");
        if (replaced)
          append(caption, "\n⚠️ " + std::to_string(replaced) +
                              " 张图片暂时无法获取，已用占位图替换");
        if (unsupported)
          append(caption,
                 "\nℹ️ " + std::to_string(unsupported) + " 张图片格式不受支持");
      }
      auto &photo = prepared[i];
      if (photo.kind == d::MediaKind::Animation)
        prepared_images.emplace_back(
            d::Animation{std::move(photo.source), std::move(caption)});
      else
        prepared_images.emplace_back(
            d::Image{std::move(photo.source), std::move(caption)});
    }
  }
  for (std::size_t index = 0; index < attachments.size(); ++index) {
    if (const auto *image = std::get_if<std::size_t>(&attachments[index])) {
      body.push_back(std::move(prepared_images.at(*image)));
      continue;
    }
    const auto &media = std::get<d::ReceivedMedia>(attachments[index]);
    const auto remaining = limits.maximum_media_total_bytes - bytes;
    if (!remaining)
      throw std::runtime_error("forward attachment byte budget exhausted");
    auto source = co_await read_bridge_attachment(
        gateway, media, std::min(limits.maximum_media_item_bytes, remaining));
    if (media.kind == d::MediaKind::Voice)
      source = co_await prepare_voice(
          blocking, config, std::move(source),
          std::min(limits.maximum_media_item_bytes, remaining));
    bytes += source.bytes.size();
    auto caption = display_text(media.caption);
    if (!caption.empty())
      append(caption, "\n");
    append(caption, "📎 合并转发消息附件" + std::to_string(index + 1));
    switch (media.kind) {
    case d::MediaKind::Animation:
      body.emplace_back(d::Animation{std::move(source), std::move(caption)});
      break;
    case d::MediaKind::Video:
      body.emplace_back(d::Video{std::move(source), std::move(caption)});
      break;
    case d::MediaKind::Audio:
    case d::MediaKind::Voice:
      body.emplace_back(d::Audio{std::move(source),
                                 media.kind == d::MediaKind::Voice
                                     ? d::AudioKind::Voice
                                     : d::AudioKind::Audio,
                                 std::move(caption)});
      break;
    case d::MediaKind::File: {
      if (!source.metadata.filename)
        throw std::runtime_error("forward file lacks a display filename");
      auto filename = *source.metadata.filename;
      body.emplace_back(
          d::File{std::move(source), std::move(filename), std::move(caption)});
      break;
    }
    default:
      throw std::runtime_error("unsupported forward attachment kind");
    }
  }
  co_return body;
}
} // namespace bridge::qq
