#include "received_message_repository.hpp"
#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>
#include <utility>

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
namespace bot = obcx::bot;
using obcx::core::DbBlob;

[[noreturn]] void invalid_payload() {
  throw std::invalid_argument("invalid message_store schema 6 projection");
}

// Store payload v1 has an integer-keyed CBOR map and compact, positional
// records. Read only the facts Bridge needs; never restore reply/media grants.
class Payload {
public:
  explicit Payload(const DbBlob &bytes) : bytes_(bytes) {
    if (bytes.size() > bot::detail::bot_maximum_bytes +
                           bot::detail::bot_maximum_items * 16 ||
        byte() != 1)
      invalid_payload();
  }

  auto facts(const bot::BotInstallationRef &installation,
             const obcx::core::DbValue &sender_column) -> ReceivedMessageFacts {
    const auto head = byte();
    if (head < 0xa1 || head > 0xa7)
      invalid_payload();
    bool image = false;
    unsigned seen = 0;
    for (int i = 0; i < head - 0xa0; ++i) {
      const auto key = byte();
      if (key > 6 || (seen & (1U << key)))
        invalid_payload();
      seen |= 1U << key;
      if (key == 0) {
        image = read_body();
      } else {
        // Sender field, when present, is null (unknown) or a conversation
        // ID array. A known user must come from the validated integer column.
        if (key == 1 && (bytes_.empty() ||
                         (bytes_.front() != 0xf6 && bytes_.front() != 0x82 &&
                          bytes_.front() != 0x83)))
          invalid_payload();
        skip(0);
      }
    }
    const bool has_sender_payload = (seen & (1U << 1)) != 0;
    if (!bytes_.empty() || !(seen & 1U) ||
        has_sender_payload !=
            std::holds_alternative<std::nullptr_t>(sender_column))
      invalid_payload();
    std::optional<bot::UserRef> sender;
    if (!has_sender_payload) {
      const auto id = std::get<std::int64_t>(sender_column);
      sender = bot::UserRef{installation,
                            installation.platform == bot::Platform::OneBot11
                                ? bot::UserId{bot::OneBotUserId{id}}
                                : bot::UserId{bot::TelegramUserId{id}}};
      sender->validate();
    }
    return {std::move(sender), image};
  }

private:
  auto byte() -> std::uint8_t {
    if (bytes_.empty())
      invalid_payload();
    return std::exchange(bytes_, bytes_.subspan(1)).front();
  }

  auto item() -> std::pair<unsigned, std::uint64_t> {
    if (remaining_nodes_ == 0)
      invalid_payload();
    --remaining_nodes_;
    const unsigned head = byte();
    const unsigned arg = head & 31;
    if (arg < 24)
      return {head >> 5, arg};
    if (arg > 27)
      invalid_payload();
    std::uint64_t value = 0;
    for (unsigned n = 1U << (arg - 24); n > 0; --n)
      value = (value << 8) | byte();
    return {head >> 5, value};
  }

  void skip(unsigned depth) {
    if (depth > 40)
      invalid_payload();
    const auto [type, length] = item();
    if (type == 2 || type == 3) {
      if (length > bytes_.size())
        invalid_payload();
      bytes_ = bytes_.subspan(length);
    } else if (type == 4 || type == 5) {
      const auto factor = type == 5 ? 2 : 1;
      if (length > remaining_nodes_ / factor)
        invalid_payload();
      for (std::uint64_t i = 0; i < length * factor; ++i)
        skip(depth + 1);
    } else if (type == 6) {
      if (bytes_.empty() || bytes_.front() >> 5 != 2)
        invalid_payload();
      skip(depth + 1);
    }
  }

  auto read_body() -> bool {
    const auto [type, count] = item();
    if (type != 4 || count > bot::detail::bot_maximum_items)
      invalid_payload();
    bool image = false;
    for (std::uint64_t i = 0; i < count; ++i) {
      if (item() != std::pair<unsigned, std::uint64_t>{4, 2})
        invalid_payload();
      const auto [index_type, index] = item();
      if (index_type != 0 || index >= std::variant_size_v<bot::ReceivedBlock>)
        invalid_payload();
      static_assert(
          std::same_as<std::variant_alternative_t<1, bot::ReceivedBlock>,
                       bot::ReceivedMedia>);
      if (index != 1) {
        skip(0);
        continue;
      }
      // ReceivedMedia v1: [media, kind, metadata, identity, caption].
      if (item() != std::pair<unsigned, std::uint64_t>{4, 5})
        invalid_payload();
      skip(0);
      const auto [kind_type, kind] = item();
      if (kind_type != 0 || kind > static_cast<unsigned>(bot::MediaKind::File))
        invalid_payload();
      image = image || kind == static_cast<unsigned>(bot::MediaKind::Image);
      for (int field = 0; field < 3; ++field)
        skip(0);
    }
    return image;
  }

  std::span<const std::uint8_t> bytes_;
  std::size_t remaining_nodes_ = bot::detail::bot_maximum_items * 3;
};
} // namespace

ReceivedMessageRepository::ReceivedMessageRepository(obcx::core::DbManager &db,
                                                     std::string instance,
                                                     std::string ns)
    : db_manager_(db), db_instance_(std::move(instance)),
      db_namespace_(identifier(ns)) {
  if (db_instance_.empty())
    throw std::invalid_argument("explicit source DB bindings required");
}

auto ReceivedMessageRepository::get_message(const bot::MessageRef &message)
    const -> std::optional<ReceivedMessageFacts> {
  message.validate();
  const auto &conversation = message.conversation;
  const auto &installation = conversation.installation;
  const auto [kind, chat, topic] = std::visit(
      [](const auto &id) {
        using Columns = std::array<std::int64_t, 3>;
        if constexpr (std::same_as<std::decay_t<decltype(id)>,
                                   bot::OneBotConversationId>)
          return Columns{static_cast<std::int64_t>(id.kind), id.id, 0};
        else
          return Columns{static_cast<std::int64_t>(id.kind), id.chat_id,
                         id.topic_id.value_or(0)};
      },
      conversation.id);
  const auto native = std::visit(
      [](const auto &id) -> std::int64_t { return id.id; }, message.id);
  return db_manager_.run_read<std::optional<ReceivedMessageFacts>>(
      db_instance_,
      [&](obcx::core::IDbConnection &db)
          -> std::optional<ReceivedMessageFacts> {
        const auto versions = db.query("SELECT version FROM \"" +
                                       db_namespace_ + "_schema_version\";");
        if (versions.size() != 1 ||
            std::get<std::int64_t>(versions.front().at("version")) != 6)
          throw std::runtime_error(
              "Bridge source requires message_store schema 6");
        const auto rows = db.query(
            "SELECT sender_user_id,payload FROM \"" + db_namespace_ +
                "_messages\" "
                "WHERE platform=? AND group_id=? AND native_message_id=? "
                "AND bot_id=? AND chat_kind=? AND chat_id=? AND topic_id=? "
                "LIMIT 2;",
            {static_cast<std::int64_t>(installation.platform),
             kind == 1 ? 0 : chat, native, installation.installation_id, kind,
             chat, topic});
        if (rows.empty())
          return std::nullopt;
        if (rows.size() != 1)
          throw std::runtime_error("ambiguous typed source message");
        const auto &row = rows.front();
        return Payload(std::get<DbBlob>(row.at("payload")))
            .facts(installation, row.at("sender_user_id"));
      });
}
} // namespace bridge
