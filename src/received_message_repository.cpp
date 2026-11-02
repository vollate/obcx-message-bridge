#include "received_message_repository.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace bridge {
namespace {
auto identifier(const std::string &value) -> std::string {
  if (value.empty() || value.size() > 200)
    throw std::invalid_argument("invalid message_store namespace");
  if (!value.starts_with("x__") && std::ranges::all_of(value, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
      }))
    return value;
  constexpr char hex[] = "0123456789abcdef";
  std::string result = "x__";
  for (unsigned char c : value) {
    result += hex[c >> 4];
    result += hex[c & 15];
  }
  return result;
}
auto text(const obcx::core::DbRow &row, const std::string &key)
    -> const std::string & {
  return std::get<std::string>(row.at(key));
}
auto number(const obcx::core::DbRow &row, const std::string &key)
    -> std::int64_t {
  return std::get<std::int64_t>(row.at(key));
}
} // namespace
ReceivedMessageRepository::ReceivedMessageRepository(obcx::core::DbManager &db,
                                                     std::string instance,
                                                     std::string ns)
    : db_manager_(db), db_instance_(std::move(instance)),
      db_namespace_(std::move(ns)) {
  if (db_instance_.empty() || db_namespace_.empty())
    throw std::invalid_argument("explicit source DB bindings required");
}
auto ReceivedMessageRepository::read(
    const std::string &platform, const std::string &filter,
    const obcx::core::DbParams &parameters) const
    -> std::optional<storage::MessageInfo> {
  if (platform != "qq" && platform != "telegram")
    throw std::invalid_argument("unsupported source platform");
  return db_manager_.run_read<std::optional<storage::MessageInfo>>(
      db_instance_,
      [&](obcx::core::IDbConnection &c) -> std::optional<storage::MessageInfo> {
        const auto prefix = identifier(db_namespace_);
        const auto versions =
            c.query("SELECT version FROM \"" + prefix + "_schema_version\"");
        if (versions.size() != 1 || number(versions.front(), "version") != 5)
          throw std::runtime_error(
              "Bridge source requires typed-only message_store schema 5");
        const auto table = prefix + "_" + platform + "_messages";
        if (c.query(
                 "SELECT name FROM sqlite_master WHERE type='table' AND name=?",
                 {table})
                .empty())
          return std::nullopt;
        const auto rows = c.query(
            "SELECT * FROM \"" + table + "\" WHERE " + filter, parameters);
        if (rows.empty())
          return std::nullopt;
        if (rows.size() != 1)
          throw std::runtime_error("ambiguous typed source message");
        const auto &row = rows.front();
        auto message = nlohmann::json::parse(text(row, "message"))
                           .get<obcx::bot::MessageReceived>();
        const auto *known = std::get_if<obcx::bot::KnownUser>(&message.sender);
        if (message.message.id.serialized() != text(row, "domain_message_id") ||
            message.message.conversation.installation.installation_id !=
                text(row, "source_bot") ||
            message.context.kind != text(row, "message_type") ||
            message.context.group_id != text(row, "group_id") ||
            (known ? known->user.id.serialized() : std::string{}) !=
                text(row, "sender"))
          throw std::runtime_error("typed source snapshot/index mismatch");
        storage::MessageInfo result;
        result.platform = text(row, "source_platform");
        result.source_bot = text(row, "source_bot");
        result.conversation_id = text(row, "conversation_id");
        result.message_id = text(row, "message_id");
        result.group_id = message.context.group_id;
        result.user_id = known ? known->user.id.serialized() : std::string{};
        result.message_type = message.context.kind;
        result.timestamp = std::chrono::system_clock::time_point{
            std::chrono::milliseconds{number(row, "timestamp")}};
        result.created_at = std::chrono::system_clock::time_point{
            std::chrono::milliseconds{number(row, "created_at")}};
        const auto append = [&](const obcx::bot::ReceivedRichText &spans) {
          for (const auto &span : spans)
            if (const auto *literal = std::get_if<obcx::bot::Text>(&span))
              result.content += literal->content;
        };
        for (const auto &block : message.body)
          if (const auto *spans =
                  std::get_if<obcx::bot::ReceivedRichText>(&block))
            append(*spans);
          else if (const auto *media =
                       std::get_if<obcx::bot::ReceivedMedia>(&block))
            append(media->caption);
        if (message.reply && message.reply->reference())
          result.reply_to_message_id =
              message.reply->reference()->target().id.serialized();
        result.message = std::move(message);
        return result;
      });
}
auto ReceivedMessageRepository::get_message(const std::string &platform,
                                            const std::string &bot,
                                            const std::string &conversation,
                                            const std::string &message) const
    -> std::optional<storage::MessageInfo> {
  return read(platform,
              "source_platform=? AND source_bot=? AND conversation_id=? AND "
              "message_id=?",
              {platform, bot, conversation, message});
}
auto ReceivedMessageRepository::get_message(
    const std::string &platform, const obcx::bot::MessageRef &message) const
    -> std::optional<storage::MessageInfo> {
  message.validate();
  auto result = read(
      platform, "source_platform=? AND source_bot=? AND domain_message_id=?",
      {platform, message.conversation.installation.installation_id,
       message.id.serialized()});
  if (result && (!result->message || result->message->message != message))
    throw std::runtime_error("typed source conversation mismatch");
  return result;
}
} // namespace bridge
