// QQ 的 OneBot 11 上报里 "json" / "app" / "ark" / "miniapp" 都是结构化卡片，
// 顶层 "app" 字段决定具体类型，常见的有：
//   "com.tencent.miniapp_01"  小程序卡片（meta.detail_1.qqdocurl）
//   "com.tencent.structmsg"   分享卡片/文章/音乐（meta.news.jumpUrl）
//   "com.tencent.map"         位置分享
//   "com.tencent.multimsg"    合并转发
//
// 实现参考 koishi-plugin-adapter-onebot / NoneBot：把卡片解析成
// MiniAppParseResult，再统一渲染成文本消息。

#include "qq/card_formatter.hpp"

#include "config.hpp"

#include <algorithm>
#include <array>
#include <common/logger.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <regex>
#include <string_view>

namespace bridge::qq {

namespace {
auto ready_card_text(std::string text) -> boost::asio::awaitable<std::string> {
  co_return std::move(text);
}

// 反转义 HTML 实体（QQ 的小程序 JSON 里 URL 常含 &amp; 等）。
auto unescape_html_entities(std::string s) -> std::string {
  // 按更长的实体优先替换，避免错误地连锁替换
  struct Pair {
    std::string_view from;
    std::string_view to;
  };
  static constexpr std::array<Pair, 9> kEntities = {{
      {.from = "&amp;", .to = "&"},
      {.from = "&lt;", .to = "<"},
      {.from = "&gt;", .to = ">"},
      {.from = "&quot;", .to = "\""},
      {.from = "&#39;", .to = "'"},
      {.from = "&apos;", .to = "'"},
      {.from = "&#x2F;", .to = "/"},
      {.from = "&#47;", .to = "/"},
      {.from = "&nbsp;", .to = " "},
  }};
  for (const auto &p : kEntities) {
    size_t pos = 0;
    while ((pos = s.find(p.from, pos)) != std::string::npos) {
      s.replace(pos, p.from.size(), p.to);
      pos += p.to.size();
    }
  }
  return s;
}

// 把 JSON 字段安全地转成字符串（兼容字段被报为整数等情况）。
auto json_to_string(const nlohmann::json &v) -> std::string {
  if (v.is_string()) {
    return v.get<std::string>();
  }
  if (v.is_number_integer()) {
    return std::to_string(v.get<int64_t>());
  }
  if (v.is_number_unsigned()) {
    return std::to_string(v.get<uint64_t>());
  }
  if (v.is_number_float()) {
    return std::to_string(v.get<double>());
  }
  if (v.is_boolean()) {
    return v.get<bool>() ? "true" : "false";
  }
  return "";
}

// 在 JSON 对象里按已知 key 收集 URL。各 key 来自 QQ 小程序/分享卡片的实测格式。
auto pick_urls_from_object(const nlohmann::json &obj)
    -> std::vector<std::string> {
  static constexpr std::array<std::string_view, 10> kUrlKeys = {
      "qqdocurl",  "jumpUrl",    "jump_url", "musicUrl",   "music_url",
      "sourceUrl", "source_url", "url",      "contentUrl", "dataUrl"};
  std::vector<std::string> urls;
  if (!obj.is_object()) {
    return urls;
  }
  for (const auto key : kUrlKeys) {
    auto it = obj.find(std::string(key));
    if (it != obj.end() && it->is_string()) {
      auto v = it->get<std::string>();
      if (!v.empty()) {
        urls.push_back(unescape_html_entities(std::move(v)));
      }
    }
  }
  return urls;
}

auto pick_title_from_object(const nlohmann::json &obj) -> std::string {
  static constexpr std::array<std::string_view, 5> kTitleKeys = {
      "title", "tag", "brief", "name", "desc"};
  if (!obj.is_object()) {
    return "";
  }
  for (const auto key : kTitleKeys) {
    auto it = obj.find(std::string(key));
    if (it != obj.end() && it->is_string()) {
      auto v = it->get<std::string>();
      if (!v.empty()) {
        return v;
      }
    }
  }
  return "";
}

auto pick_desc_from_object(const nlohmann::json &obj) -> std::string {
  static constexpr std::array<std::string_view, 5> kDescKeys = {
      "desc", "summary", "digest", "abstract", "brief"};
  if (!obj.is_object()) {
    return "";
  }
  for (const auto key : kDescKeys) {
    auto it = obj.find(std::string(key));
    if (it != obj.end() && it->is_string()) {
      auto v = it->get<std::string>();
      if (!v.empty()) {
        return v;
      }
    }
  }
  return "";
}

} // namespace

auto CardFormatter::render_json(const obcx::bot::Json &metadata) const
    -> std::string {
  try {
    std::string json_data = metadata.value("data", "");
    if (!json_data.empty()) {
      auto parse_result = parse_miniapp_json(json_data);
      auto text = format_miniapp_message(parse_result);
      OBCX_DEBUG("转换QQ小程序JSON: success={}, title={}", parse_result.success,
                 parse_result.title);
      return text;
    }
    OBCX_DEBUG("QQ小程序JSON消息无数据");
    return "📱 [小程序-无数据]";
  } catch (const std::exception &e) {
    OBCX_ERROR("处理QQ小程序JSON时出错: {}", e.what());
    return "📱 [小程序解析错误]";
  }
}

auto CardFormatter::render_app(const obcx::bot::Json &metadata) const
    -> std::string {
  try {
    auto parse_result = parse_miniapp_json(metadata.dump());
    if (!parse_result.success) {
      // Preserve the original top-level app fallback and absent/empty values.
      parse_result.title = metadata.value("title", "应用分享");
      parse_result.description = metadata.value("content", "");
      parse_result.app_name = metadata.value("name", "");
      if (metadata.contains("url")) {
        parse_result.urls.push_back(metadata.value("url", ""));
        parse_result.success = true;
      }
    }
    auto text = format_miniapp_message(parse_result);
    OBCX_DEBUG("转换QQ应用分享: success={}, title={}", parse_result.success,
               parse_result.title);
    return text;
  } catch (const std::exception &e) {
    OBCX_ERROR("处理QQ应用分享时出错: {}", e.what());
    return "📱 [应用分享解析错误]";
  }
}

auto CardFormatter::render_ark(const obcx::bot::Json &metadata) const
    -> std::string {
  try {
    auto parse_result = parse_miniapp_json(metadata.dump());
    if (!parse_result.success) {
      parse_result.title = metadata.value("prompt", "ARK卡片");
      parse_result.description = metadata.value("desc", "");
      if (metadata.contains("kv") && metadata.at("kv").is_array()) {
        for (const auto &kv : metadata.at("kv")) {
          if (kv.contains("key") && kv.contains("value")) {
            std::string key = kv["key"];
            if (key.find("URL") != std::string::npos ||
                key.find("url") != std::string::npos)
              parse_result.urls.push_back(kv["value"]);
          }
        }
      }
      parse_result.success =
          !parse_result.urls.empty() || !parse_result.title.empty();
    }
    auto text = format_miniapp_message(parse_result);
    OBCX_DEBUG("转换QQ ARK卡片: success={}, title={}", parse_result.success,
               parse_result.title);
    return text;
  } catch (const std::exception &e) {
    OBCX_ERROR("处理QQ ARK卡片时出错: {}", e.what());
    return "📱 [ARK卡片解析错误]";
  }
}

auto CardFormatter::render_miniapp(const obcx::bot::Json &metadata) const
    -> std::string {
  try {
    auto parse_result = parse_miniapp_json(metadata.dump());
    if (!parse_result.success) {
      parse_result.title = metadata.value("title", "小程序");
      parse_result.description = metadata.value("desc", "");
      parse_result.app_name = metadata.value("appid", "");
      if (metadata.contains("url")) {
        parse_result.urls.push_back(metadata.value("url", ""));
        parse_result.success = true;
      }
    }
    auto text = format_miniapp_message(parse_result);
    OBCX_DEBUG("转换QQ小程序: success={}, title={}", parse_result.success,
               parse_result.title);
    return text;
  } catch (const std::exception &e) {
    OBCX_ERROR("处理QQ小程序时出错: {}", e.what());
    return "📱 [小程序解析错误]";
  }
}

auto CardFormatter::parse_miniapp_json(const std::string &json_data) const
    -> MiniAppParseResult {
  MiniAppParseResult result;
  result.raw_json = json_data;

  if (!config_->enable_miniapp_parsing) {
    return result;
  }

  try {
    nlohmann::json j = nlohmann::json::parse(json_data);

    if (j.contains("app")) {
      result.app_name = json_to_string(j["app"]);
    }

    std::string prompt_title;
    if (j.contains("prompt")) {
      prompt_title = json_to_string(j["prompt"]);
    }

    std::vector<std::string> found_urls;
    std::string detail_title;
    std::string detail_desc;

    // meta 下子对象的 key 不固定（detail_1 / news / music / detail / contact /
    // notification ...），所以遍历所有对象类型的子节点统一抽取字段。
    if (j.contains("meta") && j["meta"].is_object()) {
      const auto &meta = j["meta"];

      // 少数实现把字段直接放在 meta 顶层。
      auto meta_urls = pick_urls_from_object(meta);
      found_urls.insert(found_urls.end(), meta_urls.begin(), meta_urls.end());

      for (auto it = meta.begin(); it != meta.end(); ++it) {
        if (!it.value().is_object()) {
          continue;
        }
        const auto &child = it.value();

        auto child_urls = pick_urls_from_object(child);
        found_urls.insert(found_urls.end(), child_urls.begin(),
                          child_urls.end());

        if (detail_title.empty()) {
          detail_title = pick_title_from_object(child);
        }

        if (detail_desc.empty()) {
          detail_desc = pick_desc_from_object(child);
          // desc 与 title 来自同一个 "desc" 字段时避免重复显示。
          if (detail_desc == detail_title) {
            detail_desc.clear();
          }
        }

        // child 内若再嵌套一层（如 host 子对象）就不再下钻——一层遍历已
        // 覆盖绝大多数 OneBot 11 上报格式。
      }
    }

    if (j.contains("url") && j["url"].is_string()) {
      auto v = j["url"].get<std::string>();
      if (!v.empty()) {
        found_urls.push_back(unescape_html_entities(std::move(v)));
      }
    }

    if (detail_desc.empty() && j.contains("desc")) {
      detail_desc = json_to_string(j["desc"]);
    }

    // prompt 形如 "[QQ小程序] 哔哩哔哩"，更友好；但 meta 里能拿到具体标题时
    // 优先用具体标题。
    if (!detail_title.empty()) {
      result.title = detail_title;
    } else {
      result.title = prompt_title;
    }
    result.description = detail_desc;

    // 结构化字段都没拿到 URL 时，用正则从原始 JSON 兜底。
    if (found_urls.empty()) {
      auto regex_urls = extract_urls_from_json(json_data);
      for (auto &u : regex_urls) {
        found_urls.push_back(unescape_html_entities(std::move(u)));
      }
    }

    // 清理尾部多余字符 + 去重，保留首次出现顺序。
    std::vector<std::string> deduped_urls;
    deduped_urls.reserve(found_urls.size());
    for (auto &u : found_urls) {
      while (!u.empty() &&
             (u.back() == '"' || u.back() == ',' || u.back() == '}' ||
              u.back() == ' ' || u.back() == '\\')) {
        u.pop_back();
      }
      if (u.empty()) {
        continue;
      }
      if (std::ranges::find(deduped_urls, u) == deduped_urls.end()) {
        deduped_urls.push_back(std::move(u));
      }
    }
    result.urls = std::move(deduped_urls);

    result.success = !result.urls.empty() || !result.title.empty();

    OBCX_DEBUG("解析小程序: app={}, title={}, desc_len={}, urls_count={}",
               result.app_name, result.title, result.description.size(),
               result.urls.size());

  } catch (const std::exception &e) {
    OBCX_DEBUG("小程序JSON解析失败: {}", e.what());
    // 解析失败时仍然尝试用正则提取URL
    auto regex_urls = extract_urls_from_json(json_data);
    std::vector<std::string> deduped_urls;
    deduped_urls.reserve(regex_urls.size());
    for (auto &u : regex_urls) {
      auto cleaned = unescape_html_entities(std::move(u));
      if (std::ranges::find(deduped_urls, cleaned) == deduped_urls.end()) {
        deduped_urls.push_back(std::move(cleaned));
      }
    }
    result.urls = std::move(deduped_urls);
    result.success = !result.urls.empty();
  }

  return result;
}

auto CardFormatter::format_miniapp_message(
    const MiniAppParseResult &parse_result) const -> std::string {
  std::string message_text;

  if (parse_result.success) {
    message_text = "📱 ";

    if (!parse_result.title.empty()) {
      message_text += fmt::format("[{}]", parse_result.title);
    } else {
      message_text += "[小程序]";
    }

    if (!parse_result.description.empty() &&
        parse_result.description != parse_result.title) {
      message_text += fmt::format("\nTitle: {}", parse_result.description);
    }

    if (!parse_result.urls.empty()) {
      message_text += "\n🔗 链接:";
      for (const auto &url : parse_result.urls) {
        if (url.starts_with("m.q.qq.com")) {
          continue;
        }
        message_text += fmt::format("\n{}", url);
      }
    }

  } else {
    message_text = "📱 [无法解析的小程序]";

    if (config_->show_raw_json_on_parse_fail) {
      std::string json_to_show = parse_result.raw_json;
      if (json_to_show.length() >
          static_cast<std::size_t>(config_->max_json_display_length)) {
        json_to_show =
            json_to_show.substr(0, config_->max_json_display_length) + "...";
      }
      message_text +=
          fmt::format("\n原始数据:\n```json\n{}\n```", json_to_show);
    }
  }

  return message_text;
}

auto CardFormatter::extract_urls_from_json(const std::string &json_str)
    -> std::vector<std::string> {
  std::vector<std::string> urls;

  // 用正则匹配URL：排除引号、转义符、空白与JSON结构字符。
  // 注意：QQ 小程序 JSON 中常出现 &amp; 等 HTML 实体，调用方需要做反转义。
  std::regex url_regex(R"((https?://[^\s\"',}\]\\<>]+))");
  std::sregex_iterator url_iter(json_str.begin(), json_str.end(), url_regex);
  std::sregex_iterator url_end;

  for (; url_iter != url_end; ++url_iter) {
    urls.push_back(url_iter->str());
  }

  return urls;
}

auto CardFormatter::render_share(const obcx::bot::Json &metadata)
    -> std::string {
  std::string url = metadata.value("url", "");
  std::string title = metadata.value("title", "链接分享");
  auto text = fmt::format("[{}]\t{}", title, url);
  OBCX_DEBUG("转换QQ链接分享为文本: title={}, url={}", title, url);
  return text;
}

auto CardFormatter::render(obcx::onebot11::bot::RawCardView card)
    -> boost::asio::awaitable<std::string> {
  card.validate();
  // All parsing is synchronous and actor-owned. Only the resulting owned
  // string survives suspension; neither card metadata nor this is borrowed
  // by the returned awaitable. Existing formatting/configuration stays here.
  using obcx::onebot11::bot::CardKind;
  switch (card.kind) {
  case CardKind::Json:
    return ready_card_text(render_json(card.metadata));
  case CardKind::App:
    return ready_card_text(render_app(card.metadata));
  case CardKind::Ark:
    return ready_card_text(render_ark(card.metadata));
  case CardKind::MiniApp:
    return ready_card_text(render_miniapp(card.metadata));
  case CardKind::Share:
    return ready_card_text(render_share(card.metadata));
  }
  throw std::invalid_argument("invalid QQ card kind");
}

} // namespace bridge::qq
