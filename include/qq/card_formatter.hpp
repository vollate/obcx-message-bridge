#pragma once

#include "config.hpp"
#include <boost/asio/awaitable.hpp>
#include <memory>
#include <onebot11/bot/special_content.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace bridge::qq {

struct MiniAppParseResult {
  bool success = false;
  std::string title;
  std::string description;
  std::vector<std::string> urls;
  std::string app_name;
  std::string raw_json;
};

// Bridge owns every parsing/rendering/configuration decision. The SDK supplies
// only the card kind and original metadata. Rendering produces text, never
// reconstructs a native message segment, and performs no provider operation.
class CardFormatter {
public:
  explicit CardFormatter(std::shared_ptr<const BridgeConfig> config)
      : config_(std::move(config)) {
    if (!config_)
      throw std::invalid_argument("QQ card formatter requires configuration");
  }
  auto render(obcx::onebot11::bot::RawCardView card)
      -> boost::asio::awaitable<std::string>;

private:
  auto render_json(const obcx::bot::Json &metadata) const -> std::string;
  auto render_app(const obcx::bot::Json &metadata) const -> std::string;
  auto render_ark(const obcx::bot::Json &metadata) const -> std::string;
  auto render_miniapp(const obcx::bot::Json &metadata) const -> std::string;
  static auto render_share(const obcx::bot::Json &metadata) -> std::string;
  auto parse_miniapp_json(const std::string &json_data) const
      -> MiniAppParseResult;
  auto format_miniapp_message(const MiniAppParseResult &parse_result) const
      -> std::string;
  static auto extract_urls_from_json(const std::string &json_str)
      -> std::vector<std::string>;
  std::shared_ptr<const BridgeConfig> config_;
};

} // namespace bridge::qq
