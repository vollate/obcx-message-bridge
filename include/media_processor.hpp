#pragma once

#include <string>
#include <utility>

namespace bridge {

/**
 * @brief 媒体文件处理器
 *
 * 处理QQ和Telegram之间的媒体文件转换和下载
 */
class MediaProcessor {
public:
  /**
   * @brief 获取QQ文件信息（文件名和URL）
   * @param file_url 文件URL
   * @param file_type 文件类型
   * @return {URL, 文件名} 的pair
   */
  static auto get_qq_file_info(const std::string &file_url,
                               const std::string &file_type = "image")
      -> std::pair<std::string, std::string>;

  /**
   * @brief 清理临时媒体文件
   * @param file_path 要删除的文件路径
   */
  static auto cleanup_media_file(const std::string &file_path) -> void;

  /**
   * @brief 根据Content-Type判断是否为GIF格式
   * @param content_type MIME类型字符串
   * @return 是否为GIF格式
   */
  static auto is_gif_content_type(const std::string &content_type) -> bool;

  /**
   * @brief 从文件内容检测MIME类型
   * @param content 文件内容的二进制数据
   * @return MIME类型字符串，如 "image/gif", "image/jpeg" 等
   */
  static auto detect_mime_type_from_content(const std::string &content)
      -> std::string;

  /**
   * @brief 从文件内容判断是否为GIF格式
   * @param content 文件内容的二进制数据
   * @return 是否为GIF格式
   */
  static auto is_gif_from_content(const std::string &content) -> bool;
};

} // namespace bridge
