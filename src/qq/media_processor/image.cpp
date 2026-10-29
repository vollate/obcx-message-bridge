#include "bridge_hash.hpp"
#include "bridge_state_repository.hpp"
#include "config.hpp"
#include "media_processor.hpp"
#include "qq/image_url_validator.hpp"
#include "qq/media_processor.hpp"
#include "telegram/bot/operations.hpp"

#include <algorithm>
#include <common/logger.hpp>
#include <fmt/format.h>
#include <network/image_fetch.hpp>

namespace bridge::qq {
namespace {

auto telegram_utf16_units(const std::string_view text) -> std::size_t {
  std::size_t units = 0;
  for (std::size_t index = 0; index < text.size();) {
    const auto lead = static_cast<unsigned char>(text[index]);
    std::size_t width = 1;
    if ((lead & 0xE0U) == 0xC0U) {
      width = 2;
    } else if ((lead & 0xF0U) == 0xE0U) {
      width = 3;
    } else if ((lead & 0xF8U) == 0xF0U) {
      width = 4;
    }
    if (index + width > text.size()) {
      width = 1;
    }
    units += width == 4 ? 2U : 1U;
    index += width;
  }
  return units;
}

} // namespace

auto QQMediaProcessor::process_image_segment(
    const obcx::common::MessageSegment &segment,
    const obcx::common::MessageEvent &event,
    const std::string &telegram_group_id, int64_t topic_id,
    const std::string &sender_display_name,
    const GroupBridgeConfig *bridge_config,
    std::vector<std::string> &temp_files_to_cleanup)
    -> boost::asio::awaitable<std::optional<obcx::common::MessageSegment>> {

  obcx::common::MessageSegment converted_segment = segment;
  const bool sticker = is_sticker(segment);
  if (sticker &&
      co_await handle_sticker_cache(segment, telegram_group_id, topic_id,
                                    sender_display_name, bridge_config)) {
    co_return std::nullopt;
  }

  std::string file_name = segment.data.value("file", "");
  const auto source_url = segment.data.value("url", "");
  const auto format_cache_key =
      bridge::calculate_hash(source_url.empty() ? file_name : source_url);
  std::string url;
  bool resolution_failed = false;
  try {
    url = co_await operations_->resolve_onebot11_image(segment);
  } catch (const boost::system::system_error &error) {
    if (error.code() == boost::asio::error::operation_aborted) {
      throw;
    }
    resolution_failed = true;
  } catch (const std::exception &) {
    resolution_failed = true;
  }
  if (resolution_failed) {
    // Preserve the slot and existing placeholder policy, never the old URL.
    OBCX_WARN("QQ图片解析失败，使用占位图: category=get_image_failed");
    std::vector<std::string> replaced;
    const std::vector<std::pair<std::string, std::string>> missing{
        {"photo", ""}};
    const auto placeholder =
        co_await ImageUrlValidator::sanitize(*config_, missing, replaced);
    converted_segment.data["url"] = placeholder.front().second;
    converted_segment.data["file"] = placeholder.front().second;
    co_return converted_segment;
  }
  converted_segment.data["url"] = url;
  converted_segment.data["file"] = url;

  bool is_gif = false;
  if (!file_name.empty() && (file_name.find(".gif") != std::string::npos ||
                             file_name.find(".GIF") != std::string::npos)) {
    is_gif = true;
  }
  if (!url.empty() && url.find("gif") != std::string::npos) {
    is_gif = true;
  }

  // subType=1 在 QQ 上是动图表情：扩展名/URL 不一定带 gif，所以先查缓存，
  // 不命中再去下载文件头嗅探。失败时保守地按 GIF 处理。
  if (segment.data.contains("subType") && segment.data.at("subType") == 1 &&
      !url.empty()) {
    try {
      const auto &qq_sticker_hash = format_cache_key;
      const auto source_installation =
          operations_->onebot11_installation().installation_id;
      const auto target_installation =
          operations_->telegram_installation().installation_id;
      std::optional<storage::QQStickerMapping> cached_mapping;
      if (state_repository_) {
        cached_mapping = co_await blocking_executor_->run(
            [repository = state_repository_, source_installation,
             target_installation, qq_sticker_hash] {
              return repository->get_qq_sticker_mapping(
                  source_installation, target_installation, qq_sticker_hash);
            });
      }

      if (cached_mapping && cached_mapping->is_gif.has_value()) {
        is_gif = cached_mapping->is_gif.value();
        OBCX_DEBUG("使用缓存的图片格式检测结果: is_gif={}", is_gif);
      } else {
        is_gif = co_await detect_gif_format(url, format_cache_key);
      }
    } catch (const std::exception &e) {
      is_gif = true;
      OBCX_ERROR("图片格式检测异常，回退到默认行为: {}", e.what());
    }
  }

  if (sticker) {
    if (is_gif) {
      converted_segment.type = "animation";
    } else {
      converted_segment.type = "image";
    }
    OBCX_DEBUG("检测到QQ表情包，使用压缩模式转发");
  } else if (is_gif) {
    converted_segment.type = "animation";
    OBCX_DEBUG("检测到QQ GIF动图，转为Telegram动画");
  } else {
    OBCX_DEBUG("转发QQ图片文件");
  }

  co_return converted_segment;
}

auto QQMediaProcessor::is_sticker(const obcx::common::MessageSegment &segment)
    -> bool {
  std::string file_name = segment.data.value("file", "");
  std::string url = segment.data.value("url", "");

  if (!file_name.empty() && (file_name.find("sticker") != std::string::npos ||
                             file_name.find("emoji") != std::string::npos)) {
    return true;
  }
  // subType=1 在 QQ 表示 GIF 动图表情，也算表情包。
  if (segment.data.contains("subType") && segment.data.at("subType") == 1) {
    return true;
  }
  if (!url.empty() && (url.find("emoticon") != std::string::npos ||
                       url.find("sticker") != std::string::npos ||
                       url.find("emoji") != std::string::npos)) {
    return true;
  }

  return false;
}

auto QQMediaProcessor::handle_sticker_cache(
    const obcx::common::MessageSegment &segment,
    const std::string &telegram_group_id, int64_t topic_id,
    const std::string &sender_display_name,
    const GroupBridgeConfig *bridge_config) -> boost::asio::awaitable<bool> {

  try {
    std::string qq_sticker_hash = bridge::calculate_hash(
        segment.data.value("file", "") + "_" + segment.data.value("url", ""));

    const auto source_installation =
        operations_->onebot11_installation().installation_id;
    const auto target_installation =
        operations_->telegram_installation().installation_id;
    std::optional<storage::QQStickerMapping> cached_mapping;
    if (state_repository_) {
      cached_mapping = co_await blocking_executor_->run(
          [repository = state_repository_, source_installation,
           target_installation, qq_sticker_hash] {
            auto cached = repository->get_qq_sticker_mapping(
                source_installation, target_installation, qq_sticker_hash);
            if (cached.has_value()) {
              (void)repository->update_qq_sticker_last_used(
                  source_installation, target_installation, qq_sticker_hash);
            }
            return cached;
          });
    }
    if (cached_mapping.has_value()) {
      bool show_sender_for_sticker = false;
      if (bridge_config->mode == BridgeMode::GROUP_TO_GROUP) {
        show_sender_for_sticker = bridge_config->show_qq_to_tg_sender;
      } else {
        const TopicBridgeConfig *topic_config = config_->topic_config(
            operations_->pair_id(), telegram_group_id, topic_id);
        show_sender_for_sticker =
            topic_config ? topic_config->show_qq_to_tg_sender : false;
      }

      const std::string sender_label = fmt::format("[{}]", sender_display_name);
      std::string caption_info =
          show_sender_for_sticker ? fmt::format("{}\t", sender_label) : "";
      std::vector<obcx::telegram::bot::TelegramTextEntity> caption_entities;
      if (show_sender_for_sticker) {
        caption_entities.push_back(obcx::telegram::bot::TelegramTextEntity{
            .type = "italic",
            .offset = 0,
            .length = telegram_utf16_units(sender_label)});
      }

      if (topic_id == -1) {
        (void)co_await operations_->send_telegram_photo(
            telegram_group_id, cached_mapping->telegram_file_id, caption_info,
            caption_entities);
      } else {
        obcx::common::Message sticker_message;
        obcx::common::MessageSegment img_segment;
        img_segment.type = "image";
        img_segment.data["file"] = cached_mapping->telegram_file_id;
        if (!caption_info.empty()) {
          sticker_message.push_back(obcx::common::MessageSegment{
              .type = "text",
              .data = {{"text", sender_label}, {"telegram_style", "italic"}}});
          sticker_message.push_back(obcx::common::MessageSegment{
              .type = "text", .data = {{"text", "\t"}}});
        }
        sticker_message.push_back(img_segment);
        (void)co_await operations_->send_telegram_topic(
            telegram_group_id, topic_id, sticker_message);
      }

      OBCX_DEBUG("使用缓存的QQ表情包发送成功: {} -> {}", qq_sticker_hash,
                 cached_mapping->telegram_file_id);
      co_return true;
    }
    OBCX_DEBUG("QQ表情包缓存未命中，将上传并缓存: {}", qq_sticker_hash);
    co_return false;
  } catch (const std::exception &e) {
    OBCX_ERROR("处理QQ表情包缓存时出错: {}", e.what());
    co_return false;
  }
}

auto QQMediaProcessor::detect_gif_format(const std::string &url,
                                         const std::string &format_cache_key)
    -> boost::asio::awaitable<bool> {
  try {
    OBCX_DEBUG("图片缓存未命中，通过 core 获取文件头进行检测");

    // Range: 0-31 covers the image signatures. Core enforces the configured
    // byte limit even when the server ignores Range, without a Telegram proxy.
    auto response = co_await obcx::network::fetch_image(
        {.url = url,
         .timeout =
             std::chrono::milliseconds{config_->image_url_probe_timeout_ms},
         .maximum_bytes = config_->qq_media_download_max_bytes,
         .prefix_bytes = std::min<std::uint64_t>(
             32, config_->qq_media_download_max_bytes)});

    if (response.is_success()) {
      const auto &file_header = response.body;

      if (!file_header.empty()) {
        std::string detected_mime =
            MediaProcessor::detect_mime_type_from_content(file_header);
        bool is_gif = MediaProcessor::is_gif_from_content(file_header);

        OBCX_DEBUG("[图片格式检测] 文件头部MIME检测成功: {} "
                   "(is_gif={}, 读取了{}字节)",
                   detected_mime, is_gif, file_header.size());

        const auto &qq_sticker_hash = format_cache_key;
        storage::QQStickerMapping new_mapping;
        new_mapping.source_installation =
            operations_->onebot11_installation().installation_id;
        new_mapping.target_installation =
            operations_->telegram_installation().installation_id;
        new_mapping.qq_sticker_hash = qq_sticker_hash;
        new_mapping.telegram_file_id = "";
        new_mapping.file_type = is_gif ? "animation" : "photo";
        new_mapping.is_gif = is_gif;
        new_mapping.content_type = detected_mime;
        new_mapping.created_at = std::chrono::system_clock::now();
        new_mapping.last_used_at = std::chrono::system_clock::now();
        new_mapping.last_checked_at = std::chrono::system_clock::now();
        if (state_repository_) {
          (void)co_await blocking_executor_->run(
              [repository = state_repository_, new_mapping] {
                return repository->save_qq_sticker_mapping(new_mapping);
              });
          OBCX_DEBUG("[图片格式检测] 缓存记录已保存");
        }

        co_return is_gif;
      } else {
        OBCX_WARN("[图片格式检测] 文件头部内容为空，回退到默认行为");
        co_return true;
      }
    } else {
      OBCX_WARN("[图片格式检测] Range请求失败，状态码: {}, 回退到默认行为",
                response.status_code);
      co_return true;
    }
  } catch (const std::exception &e) {
    OBCX_ERROR("[图片格式检测] "
               "QQ文件Range请求或检测异常，回退到默认行为: {}",
               e.what());
    co_return true;
  }
}

} // namespace bridge::qq
