#include "bridge_state_repository.hpp"

#include <common/logger.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <variant>

namespace bridge {
namespace {

constexpr std::string_view kSchemaTable = "bridge_schema_version";
constexpr std::string_view kUsersTable = "bridge_users";
constexpr std::string_view kStickerCacheTable = "bridge_sticker_cache";
constexpr std::string_view kQqStickerTable = "bridge_qq_sticker_mappings";
constexpr std::string_view kHeartbeatsTable = "bridge_platform_heartbeats";

void create_v4_ledger(obcx::core::IDbConnection &connection);
void validate_v4_ledger(obcx::core::IDbConnection &connection);

auto timestamp_ms(const std::chrono::system_clock::time_point &time)
    -> std::int64_t {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             time.time_since_epoch())
      .count();
}

auto time_point_from_ms(const std::int64_t timestamp)
    -> std::chrono::system_clock::time_point {
  return std::chrono::system_clock::time_point{
      std::chrono::milliseconds{timestamp}};
}

auto db_string(const obcx::core::DbRow &row, const std::string &column)
    -> std::string {
  return std::get<std::string>(row.at(column));
}

auto db_int64(const obcx::core::DbRow &row, const std::string &column)
    -> std::int64_t {
  return std::get<std::int64_t>(row.at(column));
}

auto optional_string(const std::optional<std::string> &value)
    -> obcx::core::DbValue {
  if (!value.has_value()) {
    return nullptr;
  }
  return *value;
}

auto optional_int64(const std::optional<std::int64_t> &value)
    -> obcx::core::DbValue {
  if (!value.has_value()) {
    return nullptr;
  }
  return *value;
}

auto optional_bool(const std::optional<bool> &value) -> obcx::core::DbValue {
  if (!value.has_value()) {
    return nullptr;
  }
  return static_cast<std::int64_t>(*value ? 1 : 0);
}

auto optional_time_ms(
    const std::optional<std::chrono::system_clock::time_point> &value)
    -> obcx::core::DbValue {
  if (!value.has_value()) {
    return nullptr;
  }
  return timestamp_ms(*value);
}

auto db_optional_string(const obcx::core::DbRow &row, const std::string &column)
    -> std::optional<std::string> {
  const auto &value = row.at(column);
  if (std::holds_alternative<std::nullptr_t>(value)) {
    return std::nullopt;
  }
  return std::get<std::string>(value);
}

auto db_optional_int64(const obcx::core::DbRow &row, const std::string &column)
    -> std::optional<std::int64_t> {
  const auto &value = row.at(column);
  if (std::holds_alternative<std::nullptr_t>(value)) {
    return std::nullopt;
  }
  return std::get<std::int64_t>(value);
}

auto db_optional_bool(const obcx::core::DbRow &row, const std::string &column)
    -> std::optional<bool> {
  const auto value = db_optional_int64(row, column);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return *value != 0;
}

auto db_optional_time(const obcx::core::DbRow &row, const std::string &column)
    -> std::optional<std::chrono::system_clock::time_point> {
  const auto value = db_optional_int64(row, column);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return time_point_from_ms(*value);
}

auto table_exists(obcx::core::IDbConnection &connection,
                  const std::string_view table) -> bool {
  return !connection
              .query("SELECT name FROM sqlite_master WHERE type = 'table' "
                     "AND name = ?;",
                     {std::string{table}})
              .empty();
}

void require_columns(obcx::core::IDbConnection &connection,
                     const std::string &table,
                     const std::vector<std::string> &required) {
  if (!table_exists(connection, table)) {
    throw std::runtime_error("bridge schema is missing table " + table);
  }
  const auto rows = connection.query("PRAGMA table_info(\"" + table + "\");");
  std::unordered_set<std::string> columns;
  for (const auto &row : rows) {
    columns.insert(db_string(row, "name"));
  }
  for (const auto &column : required) {
    if (!columns.contains(column)) {
      throw std::runtime_error("bridge schema table " + table +
                               " is missing column " + column);
    }
  }
}

void create_auxiliary_tables(obcx::core::IDbConnection &connection) {
  connection.execute(R"(
    CREATE TABLE IF NOT EXISTS bridge_users (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      installation_id TEXT NOT NULL,
      platform TEXT NOT NULL,
      user_id TEXT NOT NULL,
      group_id TEXT NOT NULL DEFAULT '',
      username TEXT NOT NULL DEFAULT '',
      nickname TEXT NOT NULL DEFAULT '',
      title TEXT NOT NULL DEFAULT '',
      first_name TEXT NOT NULL DEFAULT '',
      last_name TEXT NOT NULL DEFAULT '',
      last_updated INTEGER NOT NULL,
      UNIQUE(installation_id, platform, user_id, group_id)
    );
  )");

  connection.execute(R"(
    CREATE TABLE IF NOT EXISTS bridge_sticker_cache (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      installation_id TEXT NOT NULL,
      platform TEXT NOT NULL,
      sticker_id TEXT NOT NULL,
      sticker_hash TEXT NOT NULL,
      original_name TEXT,
      file_type TEXT NOT NULL,
      mime_type TEXT,
      original_file_path TEXT NOT NULL,
      converted_file_path TEXT,
      container_path TEXT NOT NULL,
      file_size INTEGER,
      conversion_status TEXT NOT NULL,
      created_at INTEGER NOT NULL,
      last_used_at INTEGER NOT NULL,
      UNIQUE(installation_id, platform, sticker_hash)
    );
  )");

  connection.execute(R"(
    CREATE TABLE IF NOT EXISTS bridge_qq_sticker_mappings (
      source_installation TEXT NOT NULL,
      target_installation TEXT NOT NULL,
      qq_sticker_hash TEXT NOT NULL,
      telegram_file_id TEXT NOT NULL,
      file_type TEXT NOT NULL,
      created_at INTEGER NOT NULL,
      last_used_at INTEGER NOT NULL,
      is_gif INTEGER,
      content_type TEXT,
      last_checked_at INTEGER,
      PRIMARY KEY(source_installation, target_installation, qq_sticker_hash)
    );
  )");

  connection.execute(R"(
    CREATE TABLE IF NOT EXISTS bridge_platform_heartbeats (
      installation_id TEXT PRIMARY KEY NOT NULL,
      platform TEXT NOT NULL,
      last_heartbeat_at INTEGER NOT NULL,
      updated_at INTEGER NOT NULL
    );
  )");
}

void validate_auxiliary_columns(obcx::core::IDbConnection &connection) {
  require_columns(connection, std::string{kUsersTable},
                  {"installation_id", "platform", "user_id"});
  require_columns(connection, std::string{kStickerCacheTable},
                  {"installation_id", "platform", "sticker_hash"});
  require_columns(
      connection, std::string{kQqStickerTable},
      {"source_installation", "target_installation", "qq_sticker_hash"});
  require_columns(connection, std::string{kHeartbeatsTable},
                  {"installation_id", "platform", "last_heartbeat_at"});
}

void require_installation(const std::string &installation,
                          const std::string_view field) {
  if (installation.empty()) {
    throw std::invalid_argument("bridge state requires " + std::string{field});
  }
}

} // namespace

BridgeStateRepository::BridgeStateRepository(obcx::core::DbManager &db_manager,
                                             std::string db_instance,
                                             std::string db_namespace)
    : db_manager_(db_manager), db_instance_(std::move(db_instance)),
      db_namespace_(std::move(db_namespace)) {
  if (db_instance_.empty()) {
    throw std::invalid_argument("Bridge DB instance cannot be empty");
  }
  if (db_namespace_.empty()) {
    throw std::invalid_argument("Bridge DB namespace cannot be empty");
  }
}

void BridgeStateRepository::initialize_schema() {
  db_manager_.with_migration_lock(
      db_instance_, db_namespace_, [](obcx::core::IDbConnection &connection) {
        if (table_exists(connection, kSchemaTable)) {
          const auto rows =
              connection.query("SELECT version FROM bridge_schema_version "
                               ";");
          if (rows.size() != 1 ||
              db_int64(rows.front(), "version") != current_schema_version) {
            throw std::runtime_error("bridge requires current typed schema 5");
          }
          validate_auxiliary_columns(connection);
          validate_v4_ledger(connection);
          return;
        }
        if (!connection
                 .query("SELECT name FROM sqlite_master WHERE type = 'table' "
                        "AND name GLOB 'bridge_*';")
                 .empty()) {
          throw std::runtime_error(
              "bridge refuses unversioned or partial state");
        }
        create_auxiliary_tables(connection);
        create_v4_ledger(connection);
        connection.execute(
            "CREATE TABLE bridge_schema_version (version INTEGER NOT NULL);");
        connection.execute(
            "INSERT INTO bridge_schema_version(version) VALUES (?);",
            {current_schema_version});
        validate_auxiliary_columns(connection);
        validate_v4_ledger(connection);
      });
}

void BridgeStateRepository::validate_schema() const {
  db_manager_.run_read<void>(
      db_instance_, [](obcx::core::IDbConnection &connection) {
        if (!table_exists(connection, kSchemaTable)) {
          throw std::runtime_error("bridge requires initialized schema 5");
        }
        const auto rows =
            connection.query("SELECT version FROM bridge_schema_version;");
        if (rows.size() != 1 ||
            db_int64(rows.front(), "version") != current_schema_version) {
          throw std::runtime_error("bridge requires current typed schema 5");
        }
        validate_auxiliary_columns(connection);
        validate_v4_ledger(connection);
      });
}

auto BridgeStateRepository::schema_version() const -> std::int64_t {
  return db_manager_.run_read<std::int64_t>(
      db_instance_, [](obcx::core::IDbConnection &connection) {
        if (table_exists(connection, kSchemaTable)) {
          const auto rows = connection.query(
              "SELECT MAX(version) AS version FROM bridge_schema_version;");
          return rows.empty() ? 0 : db_int64(rows.front(), "version");
        }
        return std::int64_t{0};
      });
}

auto BridgeStateRepository::save_or_update_user(
    const storage::UserInfo &user_info, const bool force_update) -> bool {
  require_installation(user_info.installation_id, "user installation");
  return db_manager_.run_write<bool>(
      db_instance_, [&](obcx::core::IDbConnection &connection) {
        const auto update_guard = force_update ? "" : R"(
          WHERE
            IFNULL(bridge_users.username, '') != IFNULL(excluded.username, '') OR
            IFNULL(bridge_users.nickname, '') != IFNULL(excluded.nickname, '') OR
            IFNULL(bridge_users.title, '') != IFNULL(excluded.title, '') OR
            IFNULL(bridge_users.first_name, '') != IFNULL(excluded.first_name, '') OR
            IFNULL(bridge_users.last_name, '') != IFNULL(excluded.last_name, '')
        )";
        connection.execute(std::string{R"(
          INSERT INTO bridge_users
            (installation_id, platform, user_id, group_id, username, nickname,
             title, first_name, last_name, last_updated)
          VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
          ON CONFLICT(installation_id, platform, user_id, group_id)
          DO UPDATE SET
            username = excluded.username,
            nickname = excluded.nickname,
            title = excluded.title,
            first_name = excluded.first_name,
            last_name = excluded.last_name,
            last_updated = excluded.last_updated
        )"} + update_guard + ";",
                           {user_info.installation_id, user_info.platform,
                            user_info.user_id, user_info.group_id,
                            user_info.username, user_info.nickname,
                            user_info.title, user_info.first_name,
                            user_info.last_name,
                            timestamp_ms(user_info.last_updated)});
        return true;
      });
}

auto BridgeStateRepository::get_user(const std::string &installation_id,
                                     const std::string &platform,
                                     const std::string &user_id,
                                     const std::string &group_id)
    -> std::optional<storage::UserInfo> {
  return db_manager_.run_read<std::optional<storage::UserInfo>>(
      db_instance_, [&](obcx::core::IDbConnection &connection) {
        const auto rows =
            connection.query(R"(
          SELECT installation_id, platform, user_id, group_id, username,
                 nickname, title, first_name, last_name, last_updated
          FROM bridge_users
          WHERE installation_id = ? AND platform = ? AND user_id = ?
            AND group_id = ?
          LIMIT 1;
        )",
                             {installation_id, platform, user_id, group_id});
        if (rows.empty()) {
          return std::optional<storage::UserInfo>{};
        }
        const auto &row = rows.front();
        return std::optional<storage::UserInfo>{storage::UserInfo{
            .installation_id = db_string(row, "installation_id"),
            .platform = db_string(row, "platform"),
            .user_id = db_string(row, "user_id"),
            .group_id = db_string(row, "group_id"),
            .username = db_string(row, "username"),
            .nickname = db_string(row, "nickname"),
            .title = db_string(row, "title"),
            .first_name = db_string(row, "first_name"),
            .last_name = db_string(row, "last_name"),
            .last_updated = time_point_from_ms(db_int64(row, "last_updated")),
        }};
      });
}

auto BridgeStateRepository::query_user_display_name(
    const std::string &installation_id, const std::string &platform,
    const std::string &user_id, const std::string &group_id)
    -> std::optional<std::string> {
  constexpr auto kUserInfoExpiration = std::chrono::minutes{10};
  const auto query_group_id = platform == "telegram" ? std::string{} : group_id;
  const auto user_info =
      get_user(installation_id, platform, user_id, query_group_id);
  if (!user_info.has_value() ||
      std::chrono::system_clock::now() - user_info->last_updated >
          kUserInfoExpiration) {
    return std::nullopt;
  }
  if (platform == "telegram") {
    if (!user_info->nickname.empty()) {
      return user_info->nickname;
    }
    if (!user_info->first_name.empty()) {
      auto display_name = user_info->first_name;
      if (!user_info->last_name.empty()) {
        display_name += " " + user_info->last_name;
      }
      return display_name;
    }
    if (!user_info->username.empty()) {
      return user_info->username;
    }
    return std::nullopt;
  }
  if (!user_info->nickname.empty()) {
    return user_info->nickname;
  }
  if (!user_info->username.empty()) {
    return user_info->username;
  }
  if (!user_info->first_name.empty()) {
    auto display_name = user_info->first_name;
    if (!user_info->last_name.empty()) {
      display_name += " " + user_info->last_name;
    }
    return display_name;
  }
  return std::nullopt;
}

auto BridgeStateRepository::save_sticker_cache(
    const storage::StickerCacheInfo &cache_info) -> bool {
  require_installation(cache_info.installation_id, "sticker installation");
  return db_manager_.run_write<bool>(
      db_instance_, [&](obcx::core::IDbConnection &connection) {
        connection.execute(
            R"(
          INSERT INTO bridge_sticker_cache
            (installation_id, platform, sticker_id, sticker_hash,
             original_name, file_type, mime_type, original_file_path,
             converted_file_path, container_path, file_size, conversion_status,
             created_at, last_used_at)
          VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
          ON CONFLICT(installation_id, platform, sticker_hash) DO UPDATE SET
            sticker_id = excluded.sticker_id,
            original_name = excluded.original_name,
            file_type = excluded.file_type,
            mime_type = excluded.mime_type,
            original_file_path = excluded.original_file_path,
            converted_file_path = excluded.converted_file_path,
            container_path = excluded.container_path,
            file_size = excluded.file_size,
            conversion_status = excluded.conversion_status,
            created_at = excluded.created_at,
            last_used_at = excluded.last_used_at;
        )",
            {cache_info.installation_id, cache_info.platform,
             cache_info.sticker_id, cache_info.sticker_hash,
             optional_string(cache_info.original_name), cache_info.file_type,
             optional_string(cache_info.mime_type),
             cache_info.original_file_path,
             optional_string(cache_info.converted_file_path),
             cache_info.container_path, optional_int64(cache_info.file_size),
             cache_info.conversion_status, timestamp_ms(cache_info.created_at),
             timestamp_ms(cache_info.last_used_at)});
        return true;
      });
}

auto BridgeStateRepository::save_sticker_cache_if_unchanged(
    const std::optional<storage::StickerCacheInfo> &expected,
    const storage::StickerCacheInfo &cache_info) -> bool {
  require_installation(cache_info.installation_id, "sticker installation");
  if (expected && (expected->installation_id != cache_info.installation_id ||
                   expected->platform != cache_info.platform ||
                   expected->sticker_hash != cache_info.sticker_hash))
    throw std::invalid_argument("cache replacement changes scope or key");
  const auto parameters = [](const storage::StickerCacheInfo &row) {
    return obcx::core::DbParams{row.installation_id,
                                row.platform,
                                row.sticker_id,
                                row.sticker_hash,
                                optional_string(row.original_name),
                                row.file_type,
                                optional_string(row.mime_type),
                                row.original_file_path,
                                optional_string(row.converted_file_path),
                                row.container_path,
                                optional_int64(row.file_size),
                                row.conversion_status,
                                timestamp_ms(row.created_at),
                                timestamp_ms(row.last_used_at)};
  };
  return db_manager_.run_transaction<bool>(
      db_instance_, [&](obcx::core::IDbConnection &connection) {
        auto values = parameters(cache_info);
        std::string sql;
        if (!expected) {
          sql = R"(
            INSERT INTO bridge_sticker_cache
              (installation_id, platform, sticker_id, sticker_hash,
               original_name, file_type, mime_type, original_file_path,
               converted_file_path, container_path, file_size, conversion_status,
               created_at, last_used_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(installation_id, platform, sticker_hash) DO NOTHING
            RETURNING sticker_hash;
          )";
        } else {
          sql = R"(
            UPDATE bridge_sticker_cache SET
              installation_id=?, platform=?, sticker_id=?, sticker_hash=?,
              original_name=?, file_type=?, mime_type=?, original_file_path=?,
              converted_file_path=?, container_path=?, file_size=?, conversion_status=?,
              created_at=?, last_used_at=?
            WHERE installation_id IS ? AND platform IS ? AND sticker_id IS ?
              AND sticker_hash IS ? AND original_name IS ? AND file_type IS ?
              AND mime_type IS ? AND original_file_path IS ?
              AND converted_file_path IS ? AND container_path IS ?
              AND file_size IS ? AND conversion_status IS ?
              AND created_at IS ? AND last_used_at IS ?
            RETURNING sticker_hash;
          )";
          auto prior = parameters(*expected);
          values.insert(values.end(), prior.begin(), prior.end());
        }
        const auto rows = connection.query(sql, values);
        if (rows.size() > 1 ||
            (!rows.empty() && db_string(rows.front(), "sticker_hash") !=
                                  cache_info.sticker_hash))
          throw std::runtime_error("invalid cache replacement result");
        return !rows.empty();
      });
}

auto BridgeStateRepository::sticker_cache_file_references(
    const std::string &filename)
    -> std::vector<storage::StickerCacheFileReference> {
  if (filename.empty() || filename.find('/') != std::string::npos ||
      filename.find('\0') != std::string::npos)
    throw std::invalid_argument("cache reference lookup requires a filename");
  const auto suffix = "/" + filename;
  return db_manager_.run_read<std::vector<storage::StickerCacheFileReference>>(
      db_instance_, [&](obcx::core::IDbConnection &connection) {
        // Select lexical aliases too (e.g. a historical doubled separator).
        // The filesystem owner maps each returned host path before comparison.
        const auto rows = connection.query(R"(
          SELECT installation_id, platform, sticker_hash,
                 original_file_path AS host_path FROM bridge_sticker_cache
            WHERE substr(original_file_path, -length(?)) = ?
          UNION
          SELECT installation_id, platform, sticker_hash,
                 converted_file_path AS host_path FROM bridge_sticker_cache
            WHERE substr(converted_file_path, -length(?)) = ?;
        )",
                                           {suffix, suffix, suffix, suffix});
        std::vector<storage::StickerCacheFileReference> result;
        result.reserve(rows.size());
        for (const auto &row : rows)
          result.push_back(
              {db_string(row, "installation_id"), db_string(row, "platform"),
               db_string(row, "sticker_hash"), db_string(row, "host_path")});
        return result;
      });
}

auto BridgeStateRepository::get_sticker_cache(
    const std::string &installation_id, const std::string &platform,
    const std::string &sticker_hash)
    -> std::optional<storage::StickerCacheInfo> {
  return db_manager_.run_read<std::optional<storage::StickerCacheInfo>>(
      db_instance_, [&](obcx::core::IDbConnection &connection) {
        const auto rows =
            connection.query(R"(
          SELECT installation_id, platform, sticker_id, sticker_hash,
                 original_name, file_type, mime_type, original_file_path,
                 converted_file_path, container_path, file_size,
                 conversion_status, created_at, last_used_at
          FROM bridge_sticker_cache
          WHERE installation_id = ? AND platform = ? AND sticker_hash = ?
          LIMIT 1;
        )",
                             {installation_id, platform, sticker_hash});
        if (rows.empty()) {
          return std::optional<storage::StickerCacheInfo>{};
        }
        const auto &row = rows.front();
        storage::StickerCacheInfo result;
        result.installation_id = db_string(row, "installation_id");
        result.platform = db_string(row, "platform");
        result.sticker_id = db_string(row, "sticker_id");
        result.sticker_hash = db_string(row, "sticker_hash");
        result.original_name = db_optional_string(row, "original_name");
        result.file_type = db_string(row, "file_type");
        result.mime_type = db_optional_string(row, "mime_type");
        result.original_file_path = db_string(row, "original_file_path");
        result.converted_file_path =
            db_optional_string(row, "converted_file_path");
        result.container_path = db_string(row, "container_path");
        result.file_size = db_optional_int64(row, "file_size");
        result.conversion_status = db_string(row, "conversion_status");
        result.created_at = time_point_from_ms(db_int64(row, "created_at"));
        result.last_used_at = time_point_from_ms(db_int64(row, "last_used_at"));
        return std::optional<storage::StickerCacheInfo>{std::move(result)};
      });
}

auto BridgeStateRepository::update_sticker_last_used(
    const std::string &installation_id, const std::string &platform,
    const std::string &sticker_hash) -> bool {
  return db_manager_.run_write<bool>(
      db_instance_, [&](obcx::core::IDbConnection &connection) {
        connection.execute(R"(
          UPDATE bridge_sticker_cache SET last_used_at = ?
          WHERE installation_id = ? AND platform = ? AND sticker_hash = ?;
        )",
                           {timestamp_ms(std::chrono::system_clock::now()),
                            installation_id, platform, sticker_hash});
        return true;
      });
}

auto BridgeStateRepository::update_sticker_conversion(
    const std::string &installation_id, const std::string &platform,
    const std::string &sticker_hash, const std::string &conversion_status,
    const std::optional<std::string> &converted_file_path) -> bool {
  return db_manager_.run_write<bool>(
      db_instance_, [&](obcx::core::IDbConnection &connection) {
        connection.execute(R"(
          UPDATE bridge_sticker_cache
          SET conversion_status = ?, converted_file_path = ?
          WHERE installation_id = ? AND platform = ? AND sticker_hash = ?;
        )",
                           {conversion_status,
                            optional_string(converted_file_path),
                            installation_id, platform, sticker_hash});
        return true;
      });
}

auto BridgeStateRepository::save_qq_sticker_mapping(
    const storage::QQStickerMapping &mapping) -> bool {
  require_installation(mapping.source_installation,
                       "QQ sticker source installation");
  require_installation(mapping.target_installation,
                       "QQ sticker target installation");
  return db_manager_.run_write<bool>(
      db_instance_, [&](obcx::core::IDbConnection &connection) {
        connection.execute(
            R"(
          INSERT INTO bridge_qq_sticker_mappings
            (source_installation, target_installation, qq_sticker_hash,
             telegram_file_id, file_type, created_at, last_used_at, is_gif,
             content_type, last_checked_at)
          VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
          ON CONFLICT(source_installation, target_installation, qq_sticker_hash)
          DO UPDATE SET
            telegram_file_id = excluded.telegram_file_id,
            file_type = excluded.file_type,
            created_at = excluded.created_at,
            last_used_at = excluded.last_used_at,
            is_gif = excluded.is_gif,
            content_type = excluded.content_type,
            last_checked_at = excluded.last_checked_at;
        )",
            {mapping.source_installation, mapping.target_installation,
             mapping.qq_sticker_hash, mapping.telegram_file_id,
             mapping.file_type, timestamp_ms(mapping.created_at),
             timestamp_ms(mapping.last_used_at), optional_bool(mapping.is_gif),
             optional_string(mapping.content_type),
             optional_time_ms(mapping.last_checked_at)});
        return true;
      });
}

auto BridgeStateRepository::get_qq_sticker_mapping(
    const std::string &source_installation,
    const std::string &target_installation, const std::string &qq_sticker_hash)
    -> std::optional<storage::QQStickerMapping> {
  return db_manager_.run_read<std::optional<storage::QQStickerMapping>>(
      db_instance_, [&](obcx::core::IDbConnection &connection) {
        const auto rows = connection.query(
            R"(
          SELECT source_installation, target_installation, qq_sticker_hash,
                 telegram_file_id, file_type, created_at, last_used_at, is_gif,
                 content_type, last_checked_at
          FROM bridge_qq_sticker_mappings
          WHERE source_installation = ? AND target_installation = ?
            AND qq_sticker_hash = ?
          LIMIT 1;
        )",
            {source_installation, target_installation, qq_sticker_hash});
        if (rows.empty()) {
          return std::optional<storage::QQStickerMapping>{};
        }
        const auto &row = rows.front();
        return std::optional<storage::QQStickerMapping>{
            storage::QQStickerMapping{
                .source_installation = db_string(row, "source_installation"),
                .target_installation = db_string(row, "target_installation"),
                .qq_sticker_hash = db_string(row, "qq_sticker_hash"),
                .telegram_file_id = db_string(row, "telegram_file_id"),
                .file_type = db_string(row, "file_type"),
                .created_at = time_point_from_ms(db_int64(row, "created_at")),
                .last_used_at =
                    time_point_from_ms(db_int64(row, "last_used_at")),
                .is_gif = db_optional_bool(row, "is_gif"),
                .content_type = db_optional_string(row, "content_type"),
                .last_checked_at = db_optional_time(row, "last_checked_at"),
            }};
      });
}

auto BridgeStateRepository::update_qq_sticker_last_used(
    const std::string &source_installation,
    const std::string &target_installation, const std::string &qq_sticker_hash)
    -> bool {
  return db_manager_.run_write<bool>(
      db_instance_, [&](obcx::core::IDbConnection &connection) {
        connection.execute(R"(
          UPDATE bridge_qq_sticker_mappings SET last_used_at = ?
          WHERE source_installation = ? AND target_installation = ?
            AND qq_sticker_hash = ?;
        )",
                           {timestamp_ms(std::chrono::system_clock::now()),
                            source_installation, target_installation,
                            qq_sticker_hash});
        return true;
      });
}

auto BridgeStateRepository::update_platform_heartbeat(
    const std::string &installation_id, const std::string &platform,
    const std::chrono::system_clock::time_point &heartbeat_time) -> bool {
  require_installation(installation_id, "heartbeat installation");
  return db_manager_.run_write<bool>(
      db_instance_, [&](obcx::core::IDbConnection &connection) {
        connection.execute(R"(
          INSERT INTO bridge_platform_heartbeats
            (installation_id, platform, last_heartbeat_at, updated_at)
          VALUES (?, ?, ?, ?)
          ON CONFLICT(installation_id) DO UPDATE SET
            platform = excluded.platform,
            last_heartbeat_at = excluded.last_heartbeat_at,
            updated_at = excluded.updated_at;
        )",
                           {installation_id, platform,
                            timestamp_ms(heartbeat_time),
                            timestamp_ms(std::chrono::system_clock::now())});
        return true;
      });
}

auto BridgeStateRepository::get_platform_heartbeat(
    const std::string &installation_id)
    -> std::optional<storage::PlatformHeartbeatInfo> {
  return db_manager_.run_read<std::optional<storage::PlatformHeartbeatInfo>>(
      db_instance_, [&](obcx::core::IDbConnection &connection) {
        const auto rows = connection.query(R"(
          SELECT installation_id, platform, last_heartbeat_at, updated_at
          FROM bridge_platform_heartbeats
          WHERE installation_id = ? LIMIT 1;
        )",
                                           {installation_id});
        if (rows.empty()) {
          return std::optional<storage::PlatformHeartbeatInfo>{};
        }
        const auto &row = rows.front();
        return std::optional<storage::PlatformHeartbeatInfo>{
            storage::PlatformHeartbeatInfo{
                .installation_id = db_string(row, "installation_id"),
                .platform = db_string(row, "platform"),
                .last_heartbeat_at =
                    time_point_from_ms(db_int64(row, "last_heartbeat_at")),
                .updated_at = time_point_from_ms(db_int64(row, "updated_at")),
            }};
      });
}

} // namespace bridge

#include "bridge_delivery_ledger.inc"
