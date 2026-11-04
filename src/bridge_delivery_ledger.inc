// Included by bridge_state_repository.cpp so the storage seam needs no build
// manifest change. All writes use the process DbManager transaction executor.
namespace bridge::ledger {
// The storage envelope is independent of the SDK codec/version. Opaque bytes
// may contain NUL or invalid UTF-8, so JSON strings must contain canonical hex.
void to_json(nlohmann::json &j, const OpaqueValue &v) {
  if (v.codec.empty() || v.version <= 0 || v.bytes.empty()) {
    throw std::invalid_argument(
        "ledger requires nonempty versioned opaque value");
  }
  std::string hex;
  if (v.bytes.size() > hex.max_size() / 2) {
    throw std::length_error("opaque storage envelope exceeds string limit");
  }
  hex.reserve(v.bytes.size() * 2);
  constexpr char digits[] = "0123456789abcdef";
  for (const unsigned char byte : v.bytes) {
    hex.push_back(digits[byte >> 4]);
    hex.push_back(digits[byte & 15]);
  }
  j = {{"storage_format", 1},
       {"codec", v.codec},
       {"version", v.version},
       {"bytes_hex", std::move(hex)}};
}
void from_json(const nlohmann::json &j, OpaqueValue &v) {
  if (!j.is_object() || j.size() != 4 ||
      !j.at("storage_format").is_number_integer() ||
      j.at("storage_format") != 1 || !j.at("version").is_number_integer() ||
      j.at("version") <= 0 ||
      j.at("version") > std::numeric_limits<std::int64_t>::max()) {
    throw std::invalid_argument("invalid opaque storage envelope");
  }
  auto codec = j.at("codec").get<std::string>();
  const auto &hex = j.at("bytes_hex").get_ref<const std::string &>();
  if (codec.empty() || hex.empty() || hex.size() % 2 != 0) {
    throw std::invalid_argument("invalid opaque storage encoding");
  }
  const auto digit = [](char c) -> unsigned {
    if (c >= '0' && c <= '9') {
      return static_cast<unsigned>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
      return static_cast<unsigned>(c - 'a' + 10);
    }
    throw std::invalid_argument("noncanonical opaque storage hex");
  };
  std::string bytes;
  bytes.reserve(hex.size() / 2);
  for (std::size_t i = 0; i < hex.size(); i += 2) {
    bytes.push_back(
        static_cast<char>((digit(hex[i]) << 4) | digit(hex[i + 1])));
  }
  v = {std::move(codec), j.at("version").get<std::int64_t>(), std::move(bytes)};
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Provenance, member_id, input_block)

template <typename T>
auto optional_json(const std::optional<T> &value) -> nlohmann::json {
  return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}
template <typename T>
auto read_optional(const nlohmann::json &value) -> std::optional<T> {
  return value.is_null() ? std::nullopt : std::optional<T>{value.get<T>()};
}
void to_json(nlohmann::json &j, const SourceMember &v) {
  j = {{"member_id", v.member_id},
       {"message_ref", v.message_ref},
       {"media_group_ref", optional_json(v.media_group_ref)},
       {"input_blocks", v.input_blocks}};
}
void from_json(const nlohmann::json &j, SourceMember &v) {
  j.at("member_id").get_to(v.member_id);
  j.at("message_ref").get_to(v.message_ref);
  v.media_group_ref = read_optional<OpaqueValue>(j.at("media_group_ref"));
  j.at("input_blocks").get_to(v.input_blocks);
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AlbumObservation, message_ref,
                                   media_group_ref, event_id, correlation_id,
                                   source_bot)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SuppressedAlbumMember, observation,
                                   anchor_member_ref, first_observed_event_id,
                                   observed_position)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Admission, delivery_id, pair_id,
                                   source_scope, target_scope,
                                   policy_and_limits, members, admitted_at_ms,
                                   max_attempts)
void to_json(nlohmann::json &j, const PhysicalOutput &v) {
  j = {{"message_ref", v.message_ref},
       {"ordinal", optional_json(v.ordinal)},
       {"provenance", optional_json(v.provenance)},
       {"reusable_media", v.reusable_media},
       {"reverse_primary_member", optional_json(v.reverse_primary_member)},
       {"detail_evidence", v.detail_evidence}};
}
void from_json(const nlohmann::json &j, PhysicalOutput &v) {
  j.at("message_ref").get_to(v.message_ref);
  v.ordinal = read_optional<std::int64_t>(j.at("ordinal"));
  v.provenance = read_optional<std::vector<Provenance>>(j.at("provenance"));
  j.at("reusable_media").get_to(v.reusable_media);
  v.reverse_primary_member =
      read_optional<std::string>(j.at("reverse_primary_member"));
  // Earlier unpublished v4 records had no SDK detail observations.
  v.detail_evidence =
      j.contains("detail_evidence")
          ? j.at("detail_evidence").get<std::vector<OpaqueValue>>()
          : std::vector<OpaqueValue>{};
}
void to_json(nlohmann::json &j, const AttemptResult &v) {
  j = {{"complete", v.complete},
       {"effect", v.effect},
       {"retryable", v.retryable},
       {"outputs", v.outputs},
       {"quote_anchor", optional_json(v.quote_anchor)},
       {"outcome", v.outcome},
       {"finished_at_ms", v.finished_at_ms}};
}
void from_json(const nlohmann::json &j, AttemptResult &v) {
  j.at("complete").get_to(v.complete);
  j.at("effect").get_to(v.effect);
  j.at("retryable").get_to(v.retryable);
  j.at("outputs").get_to(v.outputs);
  v.quote_anchor = read_optional<OpaqueValue>(j.at("quote_anchor"));
  j.at("outcome").get_to(v.outcome);
  j.at("finished_at_ms").get_to(v.finished_at_ms);
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(OutputOperation, operation_id, message_ref,
                                   result)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SourceOperation, operation_id, member_id,
                                   message_ref, anchor_ref, result)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SourceBinding, delivery_id, member_id,
                                   message_ref, anchor_ref)
void to_json(nlohmann::json &j, const CommandOperation &v) {
  j = {{"pair_id", v.pair_id},
       {"operation_id", v.operation_id},
       {"kind", v.kind},
       {"command_ref", v.command_ref},
       {"source_scope", v.source_scope},
       {"target_scope", v.target_scope},
       {"request", v.request},
       {"evidence", v.evidence},
       {"source_binding", optional_json(v.source_binding)},
       {"outcome", optional_json(v.outcome)}};
}
void from_json(const nlohmann::json &j, CommandOperation &v) {
  j.at("pair_id").get_to(v.pair_id);
  j.at("operation_id").get_to(v.operation_id);
  j.at("kind").get_to(v.kind);
  j.at("command_ref").get_to(v.command_ref);
  j.at("source_scope").get_to(v.source_scope);
  j.at("target_scope").get_to(v.target_scope);
  j.at("request").get_to(v.request);
  j.at("evidence").get_to(v.evidence);
  v.source_binding = read_optional<SourceBinding>(j.at("source_binding"));
  v.outcome = read_optional<OpaqueValue>(j.at("outcome"));
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RetryPayload, format_version, body,
                                   target_scope, media_and_leases, total_budget,
                                   next_retry_at_ms, retry_count)
} // namespace bridge::ledger

namespace bridge {
namespace {
using Json = nlohmann::json;

void require_v4(obcx::core::IDbConnection &db) {
  const auto rows = db.query("SELECT version FROM bridge_schema_version;");
  if (rows.size() != 1 || db_int64(rows.front(), "version") !=
                              BridgeStateRepository::current_schema_version) {
    throw std::runtime_error(
        "bridge delivery ledger requires explicit schema 5");
  }
}
void require_blob(const ledger::OpaqueValue &value) {
  if (value.codec.empty() || value.version <= 0 || value.bytes.empty()) {
    throw std::invalid_argument(
        "ledger requires nonempty versioned opaque value");
  }
}
auto encode(const ledger::OpaqueValue &value) -> std::string {
  require_blob(value);
  return Json(value).dump();
}
void create_source_operations(obcx::core::IDbConnection &db) {
  db.execute(R"(
    CREATE TABLE bridge_source_operations (
      delivery_id TEXT NOT NULL, operation_id TEXT NOT NULL,
      member_id TEXT NOT NULL, message_ref TEXT NOT NULL,
      anchor_ref TEXT NOT NULL, data TEXT NOT NULL,
      PRIMARY KEY(delivery_id, operation_id)
    );
  )");
}
void create_command_operations(obcx::core::IDbConnection &db) {
  db.execute(R"(
    CREATE TABLE bridge_command_operations (
      pair_id TEXT NOT NULL, operation_id TEXT NOT NULL,
      kind TEXT NOT NULL, command_ref TEXT NOT NULL,
      source_delivery_id TEXT, source_anchor_ref TEXT, data TEXT NOT NULL,
      PRIMARY KEY(pair_id, operation_id),
      UNIQUE(pair_id, command_ref, kind),
      CHECK((source_delivery_id IS NULL) = (source_anchor_ref IS NULL))
    );
  )");
}
constexpr auto confirmed_output_rows_sql =
    "(SELECT delivery_id,message_ref,data,attempt_id,NULL AS revision_id FROM "
    "bridge_delivery_outputs UNION ALL SELECT delivery_id,message_ref,data,"
    "NULL AS attempt_id,revision_id FROM bridge_revision_outputs)";
auto validation_output_rows(obcx::core::IDbConnection &db) -> std::string {
  // Explicit upgrades validate predecessor schemas before revision tables
  // exist.
  return db.query("SELECT name FROM sqlite_master WHERE type='table' AND "
                  "name='bridge_revision_outputs';")
                 .empty()
             ? "bridge_delivery_outputs"
             : confirmed_output_rows_sql;
}
auto original_delivery_state(obcx::core::IDbConnection &db,
                             const std::string &id, std::size_t output_count)
    -> ledger::DeliveryState {
  auto state = ledger::DeliveryState::Prepared;
  for (const auto &row :
       db.query("SELECT attempt_id,result FROM bridge_delivery_attempts WHERE "
                "delivery_id=? ORDER BY sequence;",
                {id})) {
    const auto result = db_optional_string(row, "result");
    if (!result)
      return ledger::DeliveryState::Uncertain;
    const auto outcome = Json::parse(*result).get<ledger::AttemptResult>();
    const auto confirmed =
        db.query("SELECT message_ref FROM bridge_delivery_outputs WHERE "
                 "delivery_id=? AND attempt_id=?;",
                 {id, db_string(row, "attempt_id")});
    // finish_attempt merges every returned ref, while confirmations may add
    // independently observed refs before or after it. A disagreement must not
    // discard either set or make a partial outcome look fully reconciled.
    if (outcome.effect == ledger::Effect::PossiblySubmitted ||
        outcome.outputs.size() != confirmed.size() ||
        (outcome.complete && outcome.outputs.size() != output_count) ||
        (outcome.effect == ledger::Effect::DefinitelyNotSubmitted &&
         !confirmed.empty()))
      return ledger::DeliveryState::Uncertain;
    state = outcome.complete
                ? ledger::DeliveryState::Complete
                : (output_count == 0 ? ledger::DeliveryState::Failed
                                     : ledger::DeliveryState::Partial);
  }
  return state;
}
void create_album_observations(obcx::core::IDbConnection &db);
void validate_album_observations(obcx::core::IDbConnection &db);
void validate_album_admission_observations(
    const ledger::Admission &admission,
    const std::vector<ledger::AlbumObservation> &observations);
auto album_observations(obcx::core::IDbConnection &db, const std::string &id)
    -> std::vector<ledger::SuppressedAlbumMember>;
auto suppress_album_batch(
    obcx::core::IDbConnection &db, const std::string &pair,
    const ledger::OpaqueValue &source, const ledger::OpaqueValue &target,
    const std::vector<ledger::AlbumObservation> &observations) -> bool;
void create_revision_ledger(obcx::core::IDbConnection &db);
void validate_revision_ledger(obcx::core::IDbConnection &db);
void suppress_unresolved_revision_retries(obcx::core::IDbConnection &db,
                                          const std::string &id);
auto revision_snapshots(obcx::core::IDbConnection &db, const std::string &id)
    -> std::vector<ledger::RevisionSnapshot>;

void create_v4_ledger(obcx::core::IDbConnection &db) {
  create_revision_ledger(db);
  create_source_operations(db);
  create_command_operations(db);
  db.execute(R"(
    CREATE TABLE bridge_deliveries (
      delivery_id TEXT PRIMARY KEY NOT NULL,
      admission TEXT NOT NULL,
      legacy_unknown INTEGER NOT NULL CHECK(legacy_unknown = 0),
      quote_anchor TEXT,
      spent_attempts INTEGER NOT NULL CHECK(spent_attempts >= 0)
    );
  )");
  db.execute(R"(
    CREATE TABLE bridge_delivery_members (
      delivery_id TEXT NOT NULL, member_id TEXT NOT NULL,
      pair_id TEXT NOT NULL, source_scope TEXT NOT NULL,
      target_scope TEXT NOT NULL, message_ref TEXT NOT NULL, data TEXT NOT NULL,
      PRIMARY KEY(delivery_id, member_id),
      UNIQUE(pair_id, source_scope, target_scope, message_ref)
    );
  )");
  db.execute(R"(
    CREATE TABLE bridge_delivery_attempts (
      delivery_id TEXT NOT NULL, attempt_id TEXT NOT NULL,
      sequence INTEGER NOT NULL CHECK(sequence > 0),
      started_at_ms INTEGER NOT NULL, result TEXT,
      PRIMARY KEY(delivery_id, attempt_id), UNIQUE(delivery_id, sequence)
    );
  )");
  db.execute(R"(
    CREATE TABLE bridge_delivery_outputs (
      delivery_id TEXT NOT NULL, attempt_id TEXT NOT NULL,
      message_ref TEXT NOT NULL, ordinal INTEGER CHECK(ordinal >= 0),
      data TEXT NOT NULL,
      PRIMARY KEY(delivery_id, message_ref),
      UNIQUE(delivery_id, attempt_id, ordinal)
    );
  )");
  db.execute(R"(
    CREATE TABLE bridge_output_operations (
      delivery_id TEXT NOT NULL, operation_id TEXT NOT NULL,
      message_ref TEXT NOT NULL, data TEXT NOT NULL,
      PRIMARY KEY(delivery_id, operation_id)
    );
  )");
  db.execute(R"(
    CREATE TABLE bridge_delivery_retries (
      delivery_id TEXT PRIMARY KEY NOT NULL, payload TEXT NOT NULL
    );
  )");
  create_album_observations(db);
}
void require_unique_key(obcx::core::IDbConnection &db, const std::string &table,
                        const std::vector<std::string> &columns) {
  for (const auto &index :
       db.query("SELECT name,\"unique\",\"partial\" FROM pragma_index_list(?);",
                {table})) {
    if (db_int64(index, "unique") != 1 || db_int64(index, "partial") != 0) {
      continue;
    }
    std::vector<std::string> actual;
    for (const auto &column :
         db.query("SELECT name FROM pragma_index_info(?) ORDER BY seqno;",
                  {db_string(index, "name")})) {
      actual.push_back(db_string(column, "name"));
    }
    if (actual == columns) {
      return;
    }
  }
  throw std::runtime_error("ledger unique identity constraint missing in " +
                           table);
}
void validate_v4_base_ledger(obcx::core::IDbConnection &db) {
  require_columns(db, "bridge_deliveries",
                  {"delivery_id", "admission", "legacy_unknown", "quote_anchor",
                   "spent_attempts"});
  require_columns(db, "bridge_delivery_members",
                  {"delivery_id", "member_id", "pair_id", "source_scope",
                   "target_scope", "message_ref", "data"});
  require_columns(
      db, "bridge_delivery_attempts",
      {"delivery_id", "attempt_id", "sequence", "started_at_ms", "result"});
  require_columns(
      db, "bridge_delivery_outputs",
      {"delivery_id", "attempt_id", "message_ref", "ordinal", "data"});
  for (const auto &column :
       db.query("PRAGMA table_info(bridge_delivery_outputs);")) {
    if (db_string(column, "name") == "ordinal" &&
        db_int64(column, "notnull") != 0) {
      throw std::runtime_error(
          "pre-release v4 output ordinal cannot represent unknown order; "
          "offline schema upgrade required");
    }
  }
  require_columns(db, "bridge_output_operations",
                  {"delivery_id", "operation_id", "message_ref", "data"});
  require_columns(db, "bridge_delivery_retries", {"delivery_id", "payload"});
  require_unique_key(db, "bridge_deliveries", {"delivery_id"});
  require_unique_key(db, "bridge_delivery_members",
                     {"delivery_id", "member_id"});
  require_unique_key(
      db, "bridge_delivery_members",
      {"pair_id", "source_scope", "target_scope", "message_ref"});
  require_unique_key(db, "bridge_delivery_attempts",
                     {"delivery_id", "attempt_id"});
  require_unique_key(db, "bridge_delivery_attempts",
                     {"delivery_id", "sequence"});
  require_unique_key(db, "bridge_delivery_outputs",
                     {"delivery_id", "message_ref"});
  require_unique_key(db, "bridge_delivery_outputs",
                     {"delivery_id", "attempt_id", "ordinal"});
  require_unique_key(db, "bridge_output_operations",
                     {"delivery_id", "operation_id"});
  require_unique_key(db, "bridge_delivery_retries", {"delivery_id"});
  for (const auto *table :
       {"bridge_delivery_members", "bridge_delivery_attempts",
        "bridge_delivery_outputs", "bridge_output_operations",
        "bridge_delivery_retries"}) {
    if (!db.query("SELECT t.delivery_id FROM " + std::string(table) +
                  " t LEFT JOIN bridge_deliveries d ON "
                  "d.delivery_id=t.delivery_id WHERE d.delivery_id IS NULL "
                  "LIMIT 1;")
             .empty()) {
      throw std::runtime_error("orphaned delivery ledger evidence");
    }
  }
  if (!db.query("SELECT o.delivery_id FROM bridge_delivery_outputs o LEFT JOIN "
                "bridge_delivery_attempts a ON a.delivery_id=o.delivery_id AND "
                "a.attempt_id=o.attempt_id WHERE a.attempt_id IS NULL LIMIT 1;")
           .empty()) {
    throw std::runtime_error("physical output has no admitted attempt");
  }
}
void validate_v4_source_ledger(obcx::core::IDbConnection &db) {
  validate_v4_base_ledger(db);
  if (db.query("SELECT name FROM sqlite_master WHERE type='table' AND "
               "name='bridge_source_operations';")
          .empty())
    throw std::runtime_error("pre-release v4 lacks source-operation journal; "
                             "explicit offline upgrade required");
  require_columns(db, "bridge_source_operations",
                  {"delivery_id", "operation_id", "member_id", "message_ref",
                   "anchor_ref", "data"});
  require_unique_key(db, "bridge_source_operations",
                     {"delivery_id", "operation_id"});
  if (!db.query(std::string{
                    "SELECT s.delivery_id FROM bridge_source_operations s "} +
                "LEFT JOIN bridge_deliveries d ON d.delivery_id=s.delivery_id "
                "LEFT JOIN bridge_delivery_members m ON "
                "m.delivery_id=s.delivery_id " +
                "AND m.member_id=s.member_id AND m.message_ref=s.message_ref "
                "LEFT JOIN " +
                validation_output_rows(db) +
                " o ON "
                "o.delivery_id=s.delivery_id "
                "AND o.message_ref=s.anchor_ref WHERE d.delivery_id IS NULL OR "
                "m.member_id IS NULL OR o.message_ref IS NULL OR "
                "d.legacy_unknown<>0 OR d.admission IS NULL LIMIT 1;")
           .empty())
    throw std::runtime_error("orphaned source operation evidence");
}
void validate_v4_command_ledger(obcx::core::IDbConnection &db) {
  validate_v4_source_ledger(db);
  if (db.query("SELECT name FROM sqlite_master WHERE type='table' AND "
               "name='bridge_command_operations';")
          .empty())
    throw std::runtime_error("pre-release v4 lacks command-operation journal; "
                             "explicit offline upgrade required");
  require_columns(db, "bridge_command_operations",
                  {"pair_id", "operation_id", "kind", "command_ref",
                   "source_delivery_id", "source_anchor_ref", "data"});
  require_unique_key(db, "bridge_command_operations",
                     {"pair_id", "operation_id"});
  require_unique_key(db, "bridge_command_operations",
                     {"pair_id", "command_ref", "kind"});
  if (!db.query(
             std::string{
                 "SELECT c.operation_id FROM bridge_command_operations c "} +
             "LEFT JOIN bridge_deliveries d ON " +
             "d.delivery_id=c.source_delivery_id LEFT JOIN " +
             validation_output_rows(db) +
             " o ON "
             "o.delivery_id=c.source_delivery_id "
             "AND o.message_ref=c.source_anchor_ref WHERE "
             "(c.source_delivery_id IS NULL)<>(c.source_anchor_ref IS NULL) OR "
             "(c.source_delivery_id IS NOT NULL AND (d.delivery_id IS NULL OR "
             "o.message_ref IS NULL OR d.legacy_unknown<>0 OR d.admission IS "
             "NULL)) LIMIT 1;")
           .empty())
    throw std::runtime_error("orphaned command source binding");
}
void validate_v4_ledger(obcx::core::IDbConnection &db) {
  validate_v4_command_ledger(db);
  validate_revision_ledger(db);
  validate_album_observations(db);
}
auto delivery_row(obcx::core::IDbConnection &db, const std::string &id)
    -> obcx::core::DbRow {
  const auto rows =
      db.query("SELECT * FROM bridge_deliveries WHERE delivery_id=?;", {id});
  if (rows.size() != 1) {
    throw std::runtime_error("missing or corrupt delivery");
  }
  return rows.front();
}
auto admission_from_db(obcx::core::IDbConnection &db, const std::string &id)
    -> ledger::Admission {
  const auto row = delivery_row(db, id);
  if (db_int64(row, "legacy_unknown") != 0) {
    throw std::runtime_error(
        "legacy delivery refs unresolved; no sends or retries permitted");
  }
  return Json::parse(db_string(row, "admission")).get<ledger::Admission>();
}
void add_members(obcx::core::IDbConnection &db,
                 const ledger::Admission &admission,
                 const std::vector<ledger::SourceMember> &members) {
  for (const auto &member : members) {
    if (member.member_id.empty() || member.input_blocks.empty()) {
      throw std::invalid_argument(
          "source member requires identity and input blocks");
    }
    std::unordered_set<std::string> blocks;
    for (const auto &block : member.input_blocks) {
      if (block.empty() || !blocks.insert(block).second) {
        throw std::invalid_argument(
            "source input block IDs must be nonempty and unique");
      }
    }
    if (member.media_group_ref) {
      require_blob(*member.media_group_ref);
    }
    if (!db.query("SELECT 1 FROM bridge_album_observations WHERE pair_id=? AND "
                  "source_scope=? AND target_scope=? AND message_ref=?;",
                  {admission.pair_id, encode(admission.source_scope),
                   encode(admission.target_scope), encode(member.message_ref)})
             .empty())
      throw std::runtime_error(
          "observed album association does not authorize source admission");
    const auto serialized = Json(member).dump();
    const auto existing = db.query("SELECT data FROM bridge_delivery_members "
                                   "WHERE delivery_id=? AND member_id=?;",
                                   {admission.delivery_id, member.member_id});
    if (!existing.empty()) {
      if (db_string(existing.front(), "data") != serialized) {
        throw std::runtime_error("conflicting source member replay");
      }
      continue;
    }
    db.execute("INSERT INTO "
               "bridge_delivery_members(delivery_id,member_id,pair_id,source_"
               "scope,target_scope,message_ref,data) VALUES(?,?,?,?,?,?,?);",
               {admission.delivery_id, member.member_id, admission.pair_id,
                encode(admission.source_scope), encode(admission.target_scope),
                encode(member.message_ref), serialized});
  }
}
auto has_album(obcx::core::IDbConnection &db, const std::string &pair,
               const ledger::OpaqueValue &source,
               const ledger::OpaqueValue &target,
               const ledger::OpaqueValue &group) -> bool {
  // Canonical opaque JSON is stored by this owner, not parsed as a native
  // media_group_id. LIMIT bounds the returned rows; malformed stored JSON
  // fails the query rather than turning corruption into a cache miss.
  return !db.query("SELECT 1 FROM bridge_delivery_members WHERE pair_id=? "
                   "AND source_scope=? AND target_scope=? "
                   "AND json_extract(data,'$.media_group_ref')=? LIMIT 1;",
                   {pair, encode(source), encode(target), encode(group)})
              .empty();
}
auto admit(obcx::core::IDbConnection &db, const ledger::Admission &admission)
    -> ledger::AdmissionResult {
  if (admission.delivery_id.empty() ||
      admission.delivery_id.starts_with("legacy-v3:") ||
      admission.pair_id.empty() || admission.members.empty() ||
      admission.max_attempts <= 0) {
    throw std::invalid_argument("invalid delivery admission");
  }
  require_blob(admission.source_scope);
  require_blob(admission.target_scope);
  require_blob(admission.policy_and_limits);
  std::unordered_set<std::string> member_ids;
  for (const auto &member : admission.members) {
    if (!member_ids.insert(member.member_id).second) {
      throw std::invalid_argument("duplicate source member in admission");
    }
  }
  const auto serialized = Json(admission).dump();
  const auto existing =
      db.query("SELECT admission FROM bridge_deliveries WHERE delivery_id=?;",
               {admission.delivery_id});
  if (!existing.empty()) {
    if (db_optional_string(existing.front(), "admission") != serialized) {
      throw std::runtime_error("conflicting delivery admission replay");
    }
    return ledger::AdmissionResult::AlreadyExists;
  }
  db.execute("INSERT INTO "
             "bridge_deliveries(delivery_id,admission,legacy_unknown,quote_"
             "anchor,spent_attempts) VALUES(?,?,0,NULL,0);",
             {admission.delivery_id, serialized});
  add_members(db, admission, admission.members);
  return ledger::AdmissionResult::Created;
}
auto attempt_rows(obcx::core::IDbConnection &db, const std::string &id)
    -> std::vector<obcx::core::DbRow> {
  return db.query("SELECT * FROM bridge_delivery_attempts WHERE delivery_id=? "
                  "ORDER BY sequence;",
                  {id});
}
auto safe_retry(obcx::core::IDbConnection &db,
                const ledger::Admission &admission) -> bool {
  if (!db.query("SELECT message_ref FROM bridge_delivery_outputs WHERE "
                "delivery_id=? LIMIT 1;",
                {admission.delivery_id})
           .empty()) {
    return false;
  }
  const auto attempts = attempt_rows(db, admission.delivery_id);
  const auto spent =
      db_int64(delivery_row(db, admission.delivery_id), "spent_attempts");
  if (attempts.empty() || spent + static_cast<std::int64_t>(attempts.size()) >=
                              admission.max_attempts) {
    return false;
  }
  for (const auto &attempt : attempts) {
    const auto serialized = db_optional_string(attempt, "result");
    if (!serialized) {
      return false;
    }
    const auto result = Json::parse(*serialized).get<ledger::AttemptResult>();
    if (result.effect != ledger::Effect::DefinitelyNotSubmitted ||
        result.complete || !result.outputs.empty()) {
      return false;
    }
  }
  return Json::parse(db_string(attempts.back(), "result"))
      .get<ledger::AttemptResult>()
      .retryable;
}
auto require_attempt(obcx::core::IDbConnection &db, const std::string &id,
                     const std::string &attempt) -> obcx::core::DbRow {
  const auto rows = db.query("SELECT * FROM bridge_delivery_attempts WHERE "
                             "delivery_id=? AND attempt_id=?;",
                             {id, attempt});
  if (rows.size() != 1) {
    throw std::runtime_error("confirmation/result has no admitted attempt");
  }
  return rows.front();
}
void merge_output(obcx::core::IDbConnection &db, const std::string &id,
                  const std::string &attempt,
                  const ledger::PhysicalOutput &output,
                  const bool attach_proven_member = false) {
  const auto ref = encode(output.message_ref);
  if (output.ordinal && *output.ordinal < 0) {
    throw std::invalid_argument("negative output ordinal");
  }
  for (const auto &media : output.reusable_media) {
    require_blob(media);
  }
  for (const auto &detail : output.detail_evidence) {
    require_blob(detail);
  }
  const auto ordinal_value =
      [](const std::optional<std::int64_t> &ordinal) -> obcx::core::DbValue {
    return ordinal ? obcx::core::DbValue{*ordinal}
                   : obcx::core::DbValue{nullptr};
  };
  const auto member_exists = [&](const std::string &member) {
    return !db.query("SELECT member_id FROM bridge_delivery_members WHERE "
                     "delivery_id=? AND member_id=?;",
                     {id, member})
                .empty();
  };
  if (output.reverse_primary_member &&
      !member_exists(*output.reverse_primary_member)) {
    throw std::invalid_argument(
        "reverse-primary owner is not a delivery member");
  }
  if (output.reverse_primary_member) {
    const auto admission = admission_from_db(db, id);
    for (const auto &row :
         db.query("SELECT o.delivery_id,o.data,d.admission FROM "
                  "bridge_delivery_outputs o JOIN bridge_deliveries d ON "
                  "d.delivery_id=o.delivery_id WHERE o.message_ref=? AND "
                  "o.delivery_id!=?;",
                  {ref, id})) {
      const auto other =
          Json::parse(db_string(row, "admission")).get<ledger::Admission>();
      const auto physical =
          Json::parse(db_string(row, "data")).get<ledger::PhysicalOutput>();
      if (physical.reverse_primary_member &&
          other.pair_id == admission.pair_id &&
          other.source_scope == admission.source_scope &&
          other.target_scope == admission.target_scope) {
        throw std::runtime_error("physical output already has a "
                                 "reverse-primary owner in this scope");
      }
    }
  }
  if (output.provenance) {
    if (output.provenance->empty()) {
      throw std::invalid_argument(
          "empty known provenance; use unknown association");
    }
    for (const auto &origin : *output.provenance) {
      const auto rows = db.query("SELECT data FROM bridge_delivery_members "
                                 "WHERE delivery_id=? AND member_id=?;",
                                 {id, origin.member_id});
      if (rows.empty()) {
        throw std::invalid_argument("provenance member missing");
      }
      const auto member = Json::parse(db_string(rows.front(), "data"))
                              .get<ledger::SourceMember>();
      if (std::find(member.input_blocks.begin(), member.input_blocks.end(),
                    origin.input_block) == member.input_blocks.end()) {
        throw std::invalid_argument("provenance input block missing");
      }
    }
  }
  const auto rows =
      db.query("SELECT attempt_id,data FROM bridge_delivery_outputs WHERE "
               "delivery_id=? AND message_ref=?;",
               {id, ref});
  if (rows.empty()) {
    db.execute(
        "INSERT INTO "
        "bridge_delivery_outputs(delivery_id,attempt_id,message_ref,"
        "ordinal,data) VALUES(?,?,?,?,?);",
        {id, attempt, ref, ordinal_value(output.ordinal), Json(output).dump()});
    return;
  }
  auto merged = Json::parse(db_string(rows.front(), "data"))
                    .get<ledger::PhysicalOutput>();
  const bool incoming_provenance_is_known =
      merged.provenance && output.provenance &&
      std::all_of(output.provenance->begin(), output.provenance->end(),
                  [&](const auto &origin) {
                    return std::find(merged.provenance->begin(),
                                     merged.provenance->end(),
                                     origin) != merged.provenance->end();
                  });
  const bool preserves_provenance =
      attach_proven_member && output.provenance &&
      (!merged.provenance ||
       std::all_of(merged.provenance->begin(), merged.provenance->end(),
                   [&](const auto &origin) {
                     return std::find(output.provenance->begin(),
                                      output.provenance->end(),
                                      origin) != output.provenance->end();
                   }));
  if (db_string(rows.front(), "attempt_id") != attempt ||
      (merged.ordinal && output.ordinal && merged.ordinal != output.ordinal) ||
      (merged.provenance && output.provenance &&
       merged.provenance != output.provenance && !preserves_provenance &&
       !incoming_provenance_is_known) ||
      (merged.reverse_primary_member && output.reverse_primary_member &&
       merged.reverse_primary_member != output.reverse_primary_member)) {
    throw std::runtime_error("conflicting physical output evidence");
  }
  if (output.ordinal) {
    merged.ordinal = output.ordinal;
  }
  if (output.provenance && !incoming_provenance_is_known) {
    merged.provenance = output.provenance;
  }
  if (output.reverse_primary_member) {
    merged.reverse_primary_member = output.reverse_primary_member;
  }
  for (const auto &media : output.reusable_media) {
    if (std::find(merged.reusable_media.begin(), merged.reusable_media.end(),
                  media) == merged.reusable_media.end()) {
      merged.reusable_media.push_back(media);
    }
  }
  for (const auto &detail : output.detail_evidence) {
    if (std::find(merged.detail_evidence.begin(), merged.detail_evidence.end(),
                  detail) == merged.detail_evidence.end()) {
      merged.detail_evidence.push_back(detail);
    }
  }
  db.execute(
      "UPDATE bridge_delivery_outputs SET ordinal=?,data=? WHERE delivery_id=? "
      "AND message_ref=?;",
      {ordinal_value(merged.ordinal), Json(merged).dump(), id, ref});
}
void validate_retry(const ledger::Admission &admission,
                    const ledger::RetryPayload &payload) {
  if (payload.format_version != ledger::RetryPayload::current_format_version) {
    throw std::invalid_argument("unsupported retry payload format version");
  }
  require_blob(payload.body);
  require_blob(payload.target_scope);
  require_blob(payload.media_and_leases);
  require_blob(payload.total_budget);
  if (payload.target_scope != admission.target_scope ||
      payload.retry_count < 0 ||
      payload.retry_count >= admission.max_attempts) {
    throw std::invalid_argument("retry scope/count does not match admission");
  }
}
// Run on the serialized DB writer, after excluding locally owned work. New
// attempts register on that same writer before admission completes. Never hold
// the activity mutex across SQL: releasing ownership must not block the
// reactor.
auto discard_retry_payload(obcx::core::IDbConnection &db, const std::string &id,
                           bool completed_only) -> bool {
  if (db.query("SELECT delivery_id FROM bridge_delivery_retries WHERE "
               "delivery_id=?;",
               {id})
          .empty())
    return false;
  // Never decode the large retry payload. Reconcile metadata and preserve the
  // body only while whole-delivery retry is still permitted. Inactive pending
  // attempts are abandoned/unknown, not permission to invoke the provider.
  try {
    const auto admission = admission_from_db(db, id);
    if (admission.delivery_id != id)
      return false;
    const auto counts = db.query(
        "SELECT (SELECT COUNT(*) FROM bridge_delivery_outputs WHERE "
        "delivery_id=?) + (SELECT COUNT(*) FROM bridge_revision_outputs WHERE "
        "delivery_id=?) AS output_count;",
        {id, id});
    const auto count = db_int64(counts.front(), "output_count");
    const auto state =
        original_delivery_state(db, id, static_cast<std::size_t>(count));
    if (completed_only) {
      if (count <= 0 || state != ledger::DeliveryState::Complete)
        return false;
    } else if (state == ledger::DeliveryState::Failed &&
               safe_retry(db, admission)) {
      return false;
    }
  } catch (const std::exception &) {
    // Missing, legacy or corrupt evidence is not permission to discard data.
    return false;
  }
  db.execute("DELETE FROM bridge_delivery_retries WHERE delivery_id=?;", {id});
  return true;
}

struct AttemptRegistration {
  BridgeStateRepository &repository;
  const std::string &delivery;
  const std::string &attempt;
  bool registered;
  bool committed;
  ~AttemptRegistration() {
    if (registered && !committed)
      repository.release_retry_attempt_ownership(delivery, attempt);
  }
};
} // namespace

auto BridgeStateRepository::admit_delivery(const ledger::Admission &admission)
    -> ledger::AdmissionResult {
  return db_manager_.run_transaction<ledger::AdmissionResult>(
      db_instance_, [&](obcx::core::IDbConnection &db) {
        require_v4(db);
        return admit(db, admission);
      });
}
auto BridgeStateRepository::has_album_delivery(
    const std::string &pair, const ledger::OpaqueValue &source,
    const ledger::OpaqueValue &target, const ledger::OpaqueValue &group) const
    -> bool {
  return db_manager_.run_transaction<bool>(
      db_instance_, [&](obcx::core::IDbConnection &db) {
        require_v4(db);
        return has_album(db, pair, source, target, group);
      });
}
auto BridgeStateRepository::admit_album_delivery(
    const ledger::Admission &admission,
    const std::vector<ledger::AlbumObservation> &observations)
    -> ledger::AdmissionResult {
  return db_manager_.run_transaction<ledger::AdmissionResult>(
      db_instance_, [&](obcx::core::IDbConnection &db) {
        require_v4(db);
        if (admission.members.empty() ||
            !admission.members.front().media_group_ref)
          throw std::invalid_argument(
              "album admission requires group evidence");
        validate_album_admission_observations(admission, observations);
        const auto &group = *admission.members.front().media_group_ref;
        for (const auto &member : admission.members)
          if (member.media_group_ref != std::optional{group})
            throw std::invalid_argument("album admission mixes group evidence");
        // Identical replay remains a no-send result.
        if (!db.query("SELECT 1 FROM bridge_deliveries WHERE delivery_id=?;",
                      {admission.delivery_id})
                 .empty())
          return admit(db, admission);
        if (observations.size() != admission.members.size())
          throw std::invalid_argument(
              "album admission lost original observations");
        for (std::size_t i = 0; i < observations.size(); ++i)
          if (observations[i].message_ref != admission.members[i].message_ref ||
              observations[i].media_group_ref != group)
            throw std::invalid_argument("album admission observation mismatch");
        // Recheck after preparation in the same transaction as source
        // admission.
        if (suppress_album_batch(db, admission.pair_id, admission.source_scope,
                                 admission.target_scope, observations))
          return ledger::AdmissionResult::AlbumBatchSuppressed;
        return admit(db, admission);
      });
}
void BridgeStateRepository::append_delivery_members(
    const std::string &id, const std::vector<ledger::SourceMember> &members) {
  db_manager_.run_transaction<void>(
      db_instance_, [&](obcx::core::IDbConnection &db) {
        require_v4(db);
        if (!attempt_rows(db, id).empty()) {
          for (const auto &member : members) {
            if (db.query("SELECT member_id FROM bridge_delivery_members WHERE "
                         "delivery_id=? AND member_id=?;",
                         {id, member.member_id})
                    .empty()) {
              throw std::runtime_error("attempted delivery membership requires "
                                       "confirmed output provenance");
            }
          }
        }
        add_members(db, admission_from_db(db, id), members);
      });
}
void BridgeStateRepository::attach_proven_member(
    const std::string &id, const std::string &attempt,
    const ledger::SourceMember &member, const ledger::PhysicalOutput &output) {
  db_manager_.run_transaction<void>(
      db_instance_, [&](obcx::core::IDbConnection &db) {
        require_v4(db);
        const auto admission = admission_from_db(db, id);
        (void)require_attempt(db, id, attempt);
        if (!output.provenance ||
            std::none_of(output.provenance->begin(), output.provenance->end(),
                         [&](const auto &origin) {
                           return origin.member_id == member.member_id;
                         })) {
          throw std::invalid_argument(
              "late member needs exact confirmed-output provenance");
        }
        if (db.query("SELECT message_ref FROM bridge_delivery_outputs WHERE "
                     "delivery_id=? AND attempt_id=? AND message_ref=?;",
                     {id, attempt, encode(output.message_ref)})
                .size() != 1) {
          throw std::runtime_error(
              "late member attachment requires existing confirmed output");
        }
        if (!db.query(
                   "SELECT operation_id FROM bridge_output_operations WHERE "
                   "delivery_id=? AND message_ref=? UNION ALL "
                   "SELECT operation_id FROM bridge_source_operations WHERE "
                   "delivery_id=? AND anchor_ref=? UNION ALL "
                   "SELECT operation_id FROM bridge_command_operations WHERE "
                   "source_delivery_id=? AND source_anchor_ref=?;",
                   {id, encode(output.message_ref), id,
                    encode(output.message_ref), id, encode(output.message_ref)})
                 .empty()) {
          const auto rows = db.query("SELECT data FROM bridge_delivery_outputs "
                                     "WHERE delivery_id=? AND message_ref=?;",
                                     {id, encode(output.message_ref)});
          if (rows.size() != 1)
            throw std::runtime_error("mutated output evidence disappeared");
          const auto current = Json::parse(db_string(rows.front(), "data"))
                                   .get<ledger::PhysicalOutput>();
          const auto members =
              db.query("SELECT data FROM bridge_delivery_members WHERE "
                       "delivery_id=? AND member_id=?;",
                       {id, member.member_id});
          if (members.size() != 1 ||
              Json::parse(db_string(members.front(), "data"))
                      .get<ledger::SourceMember>() != member ||
              !current.provenance ||
              std::ranges::any_of(*output.provenance, [&](const auto &origin) {
                return std::ranges::find(*current.provenance, origin) ==
                       current.provenance->end();
              }))
            throw std::runtime_error(
                "mutated output contributor change requires reconciliation");
        }
        add_members(db, admission, {member});
        merge_output(db, id, attempt, output, true);
      });
}
auto BridgeStateRepository::admit_attempt(const std::string &id,
                                          const std::string &attempt,
                                          const std::int64_t started_at_ms)
    -> ledger::AdmissionResult {
  AttemptRegistration registration{*this, id, attempt, false, false};
  const auto written = db_manager_.run_transaction<ledger::AdmissionResult>(
      db_instance_, [&](obcx::core::IDbConnection &db) {
        require_v4(db);
        if (attempt.empty()) {
          throw std::invalid_argument("empty attempt identity");
        }
        const auto admission = admission_from_db(db, id);
        const auto existing =
            db.query("SELECT started_at_ms FROM bridge_delivery_attempts WHERE "
                     "delivery_id=? AND attempt_id=?;",
                     {id, attempt});
        if (!existing.empty()) {
          if (db_int64(existing.front(), "started_at_ms") != started_at_ms) {
            throw std::runtime_error("conflicting attempt admission replay");
          }
          return ledger::AdmissionResult::AlreadyExists;
        }
        const auto attempts = attempt_rows(db, id);
        if (!attempts.empty() && !safe_retry(db, admission)) {
          throw std::runtime_error(
              "delivery is not safe for whole-delivery retry");
        }
        const auto sequence = static_cast<std::int64_t>(attempts.size()) + 1;
        const auto spent = db_int64(delivery_row(db, id), "spent_attempts");
        if (spent + sequence > admission.max_attempts) {
          throw std::runtime_error("delivery attempt budget exhausted");
        }
        db.execute("INSERT INTO "
                   "bridge_delivery_attempts(delivery_id,attempt_id,sequence,"
                   "started_at_ms,result) VALUES(?,?,?,?,NULL);",
                   {id, attempt, sequence, started_at_ms});
        track_retry_attempt(id, attempt);
        registration.registered = true;
        return ledger::AdmissionResult::Created;
      });
  registration.committed = true;
  return written;
}
auto BridgeStateRepository::admit_retry_attempt(
    const ledger::Admission &expected_admission,
    const ledger::RetryPayload &expected_payload, const std::string &attempt,
    std::int64_t started_at_ms) -> ledger::AdmissionResult {
  AttemptRegistration registration{*this, expected_admission.delivery_id,
                                   attempt, false, false};
  const auto written = db_manager_.run_transaction<ledger::AdmissionResult>(
      db_instance_, [&](obcx::core::IDbConnection &db) {
        require_v4(db);
        const auto &id = expected_admission.delivery_id;
        if (attempt.empty() || admission_from_db(db, id) != expected_admission)
          throw std::runtime_error("retry admission changed or missing");
        validate_retry(expected_admission, expected_payload);
        const auto rows = db.query(
            "SELECT payload FROM bridge_delivery_retries WHERE delivery_id=?;",
            {id});
        if (rows.size() != 1 ||
            Json::parse(db_string(rows.front(), "payload"))
                    .get<ledger::RetryPayload>() != expected_payload)
          throw std::runtime_error("retry payload changed or missing");
        if (!db.query(
                   "SELECT 1 FROM bridge_delivery_attempts WHERE delivery_id=? "
                   "AND attempt_id=?;",
                   {id, attempt})
                 .empty())
          return ledger::AdmissionResult::AlreadyExists;
        if (expected_payload.next_retry_at_ms > started_at_ms ||
            !safe_retry(db, expected_admission))
          throw std::runtime_error(
              "retry not due or not safe for whole delivery");
        const auto attempts = attempt_rows(db, id);
        const auto sequence = static_cast<std::int64_t>(attempts.size()) + 1;
        if (expected_payload.retry_count != sequence - 1)
          throw std::runtime_error(
              "retry count disagrees with admitted attempts");
        const auto spent = db_int64(delivery_row(db, id), "spent_attempts");
        if (spent + sequence > expected_admission.max_attempts)
          throw std::runtime_error("delivery attempt budget exhausted");
        db.execute(
            "INSERT INTO bridge_delivery_attempts(delivery_id,attempt_id,"
            "sequence,started_at_ms,result) VALUES(?,?,?,?,NULL);",
            {id, attempt, sequence, started_at_ms});
        track_retry_attempt(id, attempt);
        registration.registered = true;
        return ledger::AdmissionResult::Created;
      });
  registration.committed = true;
  return written;
}
void BridgeStateRepository::record_confirmation(
    const std::string &id, const std::string &attempt,
    const ledger::PhysicalOutput &output) {
  db_manager_.run_transaction<void>(
      db_instance_, [&](obcx::core::IDbConnection &db) {
        require_v4(db);
        (void)admission_from_db(db, id);
        (void)require_attempt(db, id, attempt);
        // Contradictory late confirmations are
        // still physical-effect evidence. Read
        // classification becomes uncertain rather
        // than discarding the ref.
        merge_output(db, id, attempt, output);
        suppress_unresolved_revision_retries(db, id);
      });
}
auto BridgeStateRepository::finish_attempt(const std::string &id,
                                           const std::string &attempt,
                                           const ledger::AttemptResult &result)
    -> ledger::AdmissionResult {
  return finish_attempt(id, attempt, result, std::nullopt);
}
auto BridgeStateRepository::finish_attempt(
    const std::string &id, const std::string &attempt,
    const ledger::AttemptResult &result,
    const std::optional<ledger::RetryPayload> &retry)
    -> ledger::AdmissionResult {
  const auto written = db_manager_.run_transaction<
      ledger::AdmissionResult>(db_instance_, [&](obcx::core::IDbConnection
                                                     &db) {
    require_v4(db);
    const auto admission = admission_from_db(db, id);
    require_blob(result.outcome);
    if ((result.effect == ledger::Effect::ConfirmedOnly &&
         result.outputs.empty()) ||
        (result.effect != ledger::Effect::DefinitelyNotSubmitted &&
         result.effect != ledger::Effect::ConfirmedOnly &&
         result.effect != ledger::Effect::PossiblySubmitted) ||
        (result.complete && (result.effect != ledger::Effect::ConfirmedOnly ||
                             result.outputs.empty() || result.retryable)) ||
        (result.effect == ledger::Effect::DefinitelyNotSubmitted &&
         !result.outputs.empty()) ||
        (result.effect != ledger::Effect::DefinitelyNotSubmitted &&
         result.retryable)) {
      throw std::invalid_argument(
          "inconsistent attempt effect/completeness/retry evidence");
    }
    const auto row = require_attempt(db, id, attempt);
    const auto serialized = Json(result).dump();
    const auto previous = db_optional_string(row, "result");
    if (previous) {
      if (*previous != serialized) {
        throw std::runtime_error("conflicting terminal attempt result");
      }
      return ledger::AdmissionResult::AlreadyExists;
    }
    std::unordered_set<std::string> refs;
    std::int64_t expected_ordinal = 0;
    for (const auto &output : result.outputs) {
      if (result.complete &&
          (!output.ordinal || *output.ordinal != expected_ordinal)) {
        throw std::invalid_argument(
            "complete result requires exact output order");
      }
      ++expected_ordinal;
      if (!refs.insert(encode(output.message_ref)).second) {
        throw std::invalid_argument("duplicate output in result");
      }
      merge_output(db, id, attempt, output);
    }
    // An earlier confirmation may contain a ref absent from this outcome.
    // Retain the returned result AND every physical observation, even for a
    // nominally complete outcome. Rolling back here would lose newly returned
    // refs and make persistence depend on notification order. Snapshot readers
    // classify the mismatch as Uncertain; it authorizes neither success nor
    // whole-delivery retry.
    if (result.quote_anchor) {
      const auto anchor = encode(*result.quote_anchor);
      if (db.query("SELECT message_ref FROM bridge_delivery_outputs WHERE "
                   "delivery_id=? AND message_ref=?;",
                   {id, anchor})
              .empty()) {
        throw std::invalid_argument("quote anchor is not a confirmed output");
      }
      const auto current =
          db_optional_string(delivery_row(db, id), "quote_anchor");
      if (current && *current != anchor) {
        throw std::runtime_error("conflicting explicit quote anchor");
      }
      db.execute(
          "UPDATE bridge_deliveries SET quote_anchor=? WHERE delivery_id=?;",
          {anchor, id});
    }
    db.execute("UPDATE bridge_delivery_attempts SET result=? WHERE "
               "delivery_id=? AND attempt_id=?;",
               {serialized, id, attempt});
    if (retry) {
      validate_retry(admission, *retry);
      const auto rows = db.query(
          "SELECT payload FROM bridge_delivery_retries WHERE delivery_id=?;",
          {id});
      if (rows.size() != 1)
        throw std::runtime_error(
            "retry body was not persisted before submission");
      const auto previous = Json::parse(db_string(rows.front(), "payload"))
                                .get<ledger::RetryPayload>();
      if (previous.body != retry->body ||
          previous.target_scope != retry->target_scope ||
          previous.total_budget != retry->total_budget ||
          previous.media_and_leases != retry->media_and_leases ||
          retry->retry_count != db_int64(row, "sequence") ||
          retry->retry_count != previous.retry_count + 1 ||
          retry->next_retry_at_ms < result.finished_at_ms)
        throw std::runtime_error("unsafe or changed retry scheduling intent");
      // A late physical confirmation can contradict the no-effect result.
      // Keep BOTH observations; do not roll back the outcome merely because
      // this contradiction now forbids retry scheduling.
      if (safe_retry(db, admission))
        db.execute(
            "UPDATE bridge_delivery_retries SET payload=? WHERE delivery_id=?;",
            {Json(*retry).dump(), id});
    }
    return ledger::AdmissionResult::Created;
  });
  release_retry_attempt_ownership(id, attempt);
  try {
    // Outcome commit precedes best-effort reclamation for every terminal
    // result, including nonretryable, exhausted, partial and unknown effects.
    // Another currently owned attempt still prevents reclamation.
    (void)reclaim_terminal_retry_payload(id);
  } catch (...) {
    try {
      OBCX_WARN("Terminal retry payload cleanup deferred; durable outcome "
                "retained");
    } catch (...) {
    }
  }
  return written;
}
auto BridgeStateRepository::load_delivery(const std::string &id) const
    -> std::optional<ledger::DeliverySnapshot> {
  // Serialize the multi-query snapshot with writers; no state is changed.
  return db_manager_.run_transaction<std::optional<ledger::DeliverySnapshot>>(
      db_instance_,
      [&](obcx::core::IDbConnection &db)
          -> std::optional<ledger::DeliverySnapshot> {
        require_v4(db);
        const auto rows = db.query(
            "SELECT * FROM bridge_deliveries WHERE delivery_id=?;", {id});
        if (rows.empty()) {
          return std::nullopt;
        }
        ledger::DeliverySnapshot snapshot{};
        snapshot.delivery_id = id;
        snapshot.completeness_known =
            db_int64(rows.front(), "legacy_unknown") == 0;
        if (!snapshot.completeness_known)
          throw std::runtime_error("unsupported non-typed delivery admission");
        snapshot.admission = admission_from_db(db, id);
        for (const auto &row :
             db.query("SELECT data FROM bridge_delivery_members WHERE "
                      "delivery_id=? ORDER BY member_id;",
                      {id})) {
          snapshot.members.push_back(
              Json::parse(db_string(row, "data")).get<ledger::SourceMember>());
        }
        for (const auto &row :
             db.query("SELECT data FROM bridge_delivery_outputs WHERE "
                      "delivery_id=? ORDER BY attempt_id,ordinal;",
                      {id})) {
          snapshot.outputs.push_back(Json::parse(db_string(row, "data"))
                                         .get<ledger::PhysicalOutput>());
        }
        const auto anchor = db_optional_string(rows.front(), "quote_anchor");
        if (anchor) {
          snapshot.quote_anchor =
              Json::parse(*anchor).get<ledger::OpaqueValue>();
        }
        const auto attempts = attempt_rows(db, id);
        for (const auto &row : attempts) {
          const auto result = db_optional_string(row, "result");
          snapshot.attempts.push_back(
              {db_string(row, "attempt_id"), db_int64(row, "sequence"),
               db_int64(row, "started_at_ms"),
               result
                   ? std::optional<
                         ledger::
                             AttemptResult>{Json::parse(*result)
                                                .get<ledger::AttemptResult>()}
                   : std::nullopt});
        }
        for (const auto &row :
             db.query("SELECT data FROM bridge_output_operations WHERE "
                      "delivery_id=? ORDER BY operation_id;",
                      {id})) {
          snapshot.output_operations.push_back(
              Json::parse(db_string(row, "data"))
                  .get<ledger::OutputOperation>());
        }
        for (const auto &row :
             db.query("SELECT "
                      "operation_id,member_id,message_ref,anchor_ref,data FROM "
                      "bridge_source_operations WHERE delivery_id=? ORDER BY "
                      "operation_id;",
                      {id})) {
          const auto serialized = db_string(row, "data");
          auto operation =
              Json::parse(serialized).get<ledger::SourceOperation>();
          require_blob(operation.result);
          if (operation.operation_id.empty() || operation.member_id.empty() ||
              operation.operation_id != db_string(row, "operation_id") ||
              operation.member_id != db_string(row, "member_id") ||
              encode(operation.message_ref) != db_string(row, "message_ref") ||
              encode(operation.anchor_ref) != db_string(row, "anchor_ref") ||
              Json(operation).dump() != serialized)
            throw std::runtime_error("conflicting source operation snapshot");
          snapshot.source_operations.push_back(std::move(operation));
        }
        snapshot.revisions = revision_snapshots(db, id);
        snapshot.suppressed_album_members = album_observations(db, id);
        // Admitted-without-result remains uncertain after restart; time alone
        // never turns it into permission to submit again.
        snapshot.state =
            original_delivery_state(db, id, snapshot.outputs.size());
        snapshot.completeness_known =
            snapshot.state != ledger::DeliveryState::Uncertain;
        snapshot.safe_to_retry = safe_retry(db, *snapshot.admission);
        const auto retries = db.query(
            "SELECT payload FROM bridge_delivery_retries WHERE delivery_id=?;",
            {id});
        if (!retries.empty()) {
          snapshot.retry = Json::parse(db_string(retries.front(), "payload"))
                               .get<ledger::RetryPayload>();
        }
        return snapshot;
      });
}
auto BridgeStateRepository::resolve_delivery(
    const std::string &pair, const ledger::OpaqueValue &source,
    const ledger::OpaqueValue &target, const ledger::OpaqueValue &message) const
    -> ledger::Resolution {
  return db_manager_.run_read<
      ledger::Resolution>(db_instance_, [&](obcx::core::IDbConnection &db) {
    require_v4(db);
    ledger::Resolution resolution{ledger::ResolutionStatus::Missing, {}, {}};
    try {
      const auto rows = db.query(
          "SELECT m.delivery_id,d.admission,m.association FROM "
          "(SELECT delivery_id,pair_id,source_scope,target_scope,message_ref,0 "
          "AS association FROM bridge_delivery_members "
          "UNION ALL SELECT "
          "delivery_id,pair_id,source_scope,target_scope,message_ref,1 AS "
          "association FROM bridge_album_observations) m "
          "LEFT JOIN bridge_deliveries d ON d.delivery_id=m.delivery_id "
          "WHERE m.pair_id=? AND m.source_scope=? AND m.target_scope=? AND "
          "m.message_ref=?;",
          {pair, encode(source), encode(target), encode(message)});
      for (const auto &row : rows) {
        const auto admission =
            Json::parse(db_string(row, "admission")).get<ledger::Admission>();
        if (admission.delivery_id != db_string(row, "delivery_id") ||
            admission.pair_id != pair || admission.source_scope != source ||
            admission.target_scope != target) {
          throw std::runtime_error("inconsistent member admission");
        }
        if (db_int64(row, "association") != 0) {
          const auto aliases = album_observations(db, admission.delivery_id);
          if (std::ranges::none_of(aliases, [&](const auto &alias) {
                return alias.observation.message_ref == message;
              }))
            throw std::runtime_error("album association disappeared");
        }
        resolution.delivery_ids.push_back(admission.delivery_id);
      }
      resolution.status =
          rows.empty()
              ? ledger::ResolutionStatus::Missing
              : (rows.size() == 1 ? ledger::ResolutionStatus::Unique
                                  : ledger::ResolutionStatus::Ambiguous);
    } catch (const std::exception &error) {
      resolution.status = ledger::ResolutionStatus::Corrupt;
      resolution.diagnostic = error.what();
    }
    return resolution;
  });
}
auto BridgeStateRepository::resolve_source_delivery(
    const std::string &pair, const ledger::OpaqueValue &source,
    const ledger::OpaqueValue &message) const -> ledger::Resolution {
  return db_manager_.run_read<
      ledger::Resolution>(db_instance_, [&](obcx::core::IDbConnection &db) {
    require_v4(db);
    ledger::Resolution result{ledger::ResolutionStatus::Missing, {}, {}};
    try {
      const auto rows = db.query(
          "SELECT m.delivery_id,m.target_scope,d.admission,m.association FROM "
          "(SELECT delivery_id,pair_id,source_scope,target_scope,message_ref,0 "
          "AS association FROM bridge_delivery_members "
          "UNION ALL SELECT "
          "delivery_id,pair_id,source_scope,target_scope,message_ref,1 AS "
          "association FROM bridge_album_observations) m LEFT JOIN "
          "bridge_deliveries d "
          "ON d.delivery_id=m.delivery_id WHERE m.pair_id=? "
          "AND m.source_scope=? AND m.message_ref=? LIMIT 2;",
          {pair, encode(source), encode(message)});
      for (const auto &row : rows) {
        const auto admission =
            Json::parse(db_string(row, "admission")).get<ledger::Admission>();
        if (admission.delivery_id != db_string(row, "delivery_id") ||
            admission.pair_id != pair || admission.source_scope != source ||
            encode(admission.target_scope) != db_string(row, "target_scope"))
          throw std::runtime_error("inconsistent source admission");
        if (db_int64(row, "association") != 0) {
          const auto aliases = album_observations(db, admission.delivery_id);
          if (std::ranges::none_of(aliases, [&](const auto &alias) {
                return alias.observation.message_ref == message;
              }))
            throw std::runtime_error("album source association disappeared");
        }
        result.delivery_ids.push_back(admission.delivery_id);
      }
      result.status = rows.empty()       ? ledger::ResolutionStatus::Missing
                      : rows.size() == 1 ? ledger::ResolutionStatus::Unique
                                         : ledger::ResolutionStatus::Ambiguous;
    } catch (const std::exception &error) {
      result.status = ledger::ResolutionStatus::Corrupt;
      result.diagnostic = error.what();
    }
    return result;
  });
}

void BridgeStateRepository::store_retry_payload(
    const std::string &id, const ledger::RetryPayload &payload) {
  db_manager_.run_transaction<void>(db_instance_, [&](obcx::core::IDbConnection
                                                          &db) {
    require_v4(db);
    const auto admission = admission_from_db(db, id);
    validate_retry(admission, payload);
    // Preparation may persist leases/body before the first attempt; scheduling
    // still requires safe_to_retry and atomic admit_attempt on every retry.
    const auto attempts = attempt_rows(db, id);
    if (!attempts.empty() && !safe_retry(db, admission)) {
      throw std::runtime_error(
          "cannot schedule retry with confirmed/possible effects");
    }
    const auto existing = db.query(
        "SELECT payload FROM bridge_delivery_retries WHERE delivery_id=?;",
        {id});
    if (!existing.empty()) {
      const auto previous = Json::parse(db_string(existing.front(), "payload"))
                                .get<ledger::RetryPayload>();
      if (previous.body != payload.body ||
          previous.target_scope != payload.target_scope ||
          previous.total_budget != payload.total_budget ||
          previous.media_and_leases != payload.media_and_leases ||
          payload.retry_count < previous.retry_count) {
        throw std::runtime_error("retry cannot replace admitted "
                                 "content/leases/budget or reset count");
      }
    }
    db.execute(
        "INSERT INTO bridge_delivery_retries(delivery_id,payload) VALUES(?,?) "
        "ON CONFLICT(delivery_id) DO UPDATE SET payload=excluded.payload;",
        {id, Json(payload).dump()});
  });
}
auto BridgeStateRepository::resolve_observed_output(
    const std::string &pair, const ledger::OpaqueValue &scope,
    const ledger::OpaqueValue &output_ref) const -> ledger::Resolution {
  return db_manager_.run_transaction<
      ledger::Resolution>(db_instance_, [&](obcx::core::IDbConnection &db) {
    require_v4(db);
    require_blob(scope);
    require_blob(output_ref);
    ledger::Resolution resolution{ledger::ResolutionStatus::Missing, {}, {}};
    try {
      for (const auto &row : db.query(
               std::string{"SELECT o.delivery_id,o.data,d.admission FROM "} +
                   confirmed_output_rows_sql +
                   " o LEFT JOIN bridge_deliveries d ON "
                   "d.delivery_id=o.delivery_id WHERE o.message_ref=? ORDER BY "
                   "o.delivery_id;",
               {encode(output_ref)})) {
        const auto id = db_string(row, "delivery_id");
        const auto admission =
            Json::parse(db_string(row, "admission")).get<ledger::Admission>();
        if (admission.delivery_id != id)
          throw std::runtime_error(
              "observed output admission identity mismatch");
        if (admission.pair_id != pair || admission.target_scope != scope)
          continue;
        const auto output =
            Json::parse(db_string(row, "data")).get<ledger::PhysicalOutput>();
        if (output.message_ref != output_ref)
          throw std::runtime_error("observed output row evidence mismatch");
        resolution.delivery_ids.push_back(id);
      }
      if (!resolution.delivery_ids.empty())
        resolution.status = resolution.delivery_ids.size() == 1
                                ? ledger::ResolutionStatus::Unique
                                : ledger::ResolutionStatus::Ambiguous;
    } catch (const std::exception &error) {
      resolution.status = ledger::ResolutionStatus::Corrupt;
      resolution.delivery_ids.clear();
      resolution.diagnostic = error.what();
    }
    return resolution;
  });
}

auto BridgeStateRepository::resolve_reverse_primary(
    const std::string &pair, const ledger::OpaqueValue &source,
    const ledger::OpaqueValue &target,
    const ledger::OpaqueValue &output_ref) const
    -> ledger::OwnershipResolution {
  return db_manager_.run_transaction<
      ledger::OwnershipResolution>(db_instance_, [&](obcx::core::IDbConnection
                                                         &db) {
    require_v4(db);
    require_blob(source);
    require_blob(target);
    require_blob(output_ref);
    ledger::OwnershipResolution resolution{
        ledger::ResolutionStatus::Missing, std::nullopt, {}, false};
    std::size_t owners = 0;
    std::size_t observed = 0;
    try {
      for (const auto &row : db.query(
               std::string{
                   "SELECT o.delivery_id,o.data,o.revision_id,d.admission "
                   "FROM "} +
                   confirmed_output_rows_sql +
                   " o LEFT JOIN bridge_deliveries d ON "
                   "d.delivery_id=o.delivery_id WHERE o.message_ref=?;",
               {encode(output_ref)})) {
        const auto admission =
            Json::parse(db_string(row, "admission")).get<ledger::Admission>();
        if (admission.delivery_id != db_string(row, "delivery_id"))
          throw std::runtime_error(
              "reverse-primary admission identity mismatch");
        if (admission.pair_id != pair || admission.target_scope != target)
          continue;
        ++observed;
        resolution.observed_output = true;
        const auto output =
            Json::parse(db_string(row, "data")).get<ledger::PhysicalOutput>();
        if (output.message_ref != output_ref)
          throw std::runtime_error("reverse-primary output evidence mismatch");
        if (admission.source_scope != source)
          continue;
        if (!output.reverse_primary_member) {
          throw std::runtime_error("reverse-primary ownership unresolved");
        }
        const auto members = db.query(
            "SELECT * FROM bridge_delivery_members WHERE "
            "delivery_id=? AND member_id=?;",
            {db_string(row, "delivery_id"), *output.reverse_primary_member});
        if (members.size() != 1) {
          throw std::runtime_error("reverse-primary member missing or corrupt");
        }
        const auto &member_row = members.front();
        auto member = Json::parse(db_string(member_row, "data"))
                          .get<ledger::SourceMember>();
        const auto original_count = std::ranges::count_if(
            admission.members, [&](const auto &candidate) {
              return candidate.member_id == member.member_id;
            });
        if (member.member_id != *output.reverse_primary_member ||
            db_string(member_row, "member_id") != member.member_id ||
            db_string(member_row, "pair_id") != pair ||
            db_string(member_row, "source_scope") != encode(source) ||
            db_string(member_row, "target_scope") != encode(target) ||
            db_string(member_row, "message_ref") !=
                encode(member.message_ref) ||
            original_count > 1 ||
            (original_count == 1 &&
             std::ranges::count(admission.members, member) != 1) ||
            (original_count == 0 && !output.provenance))
          throw std::runtime_error("reverse-primary member evidence mismatch");
        auto allowed_inputs = member.input_blocks;
        if (const auto revision_id = db_optional_string(row, "revision_id")) {
          const auto revisions = revision_snapshots(db, admission.delivery_id);
          const auto revision =
              std::ranges::find_if(revisions, [&](const auto &entry) {
                return entry.admission.revision_id == *revision_id;
              });
          if (revision == revisions.end() ||
              revision->admission.member_id != member.member_id ||
              revision->admission.source_ref != member.message_ref)
            throw std::runtime_error(
                "edit reverse-primary source evidence mismatch");
          allowed_inputs = revision->admission.input_blocks;
        }
        // Initial audited legacy ownership may lack detailed provenance.
        // Admission is immutable: append/attach_proven_member can add a late
        // member. Such an owner requires exact output provenance instead of
        // being incorrectly rejected merely for absence from initial members.
        // Where provenance exists, it must not contradict a unique owner.
        if (output.provenance) {
          if (output.provenance->empty() ||
              std::ranges::any_of(*output.provenance, [&](const auto &origin) {
                return origin.member_id != member.member_id ||
                       std::ranges::find(allowed_inputs, origin.input_block) ==
                           allowed_inputs.end();
              }))
            throw std::runtime_error("reverse-primary provenance conflict");
        }
        ++owners;
        resolution.primary_member = std::move(member);
      }
      resolution.status = observed > 1  ? ledger::ResolutionStatus::Ambiguous
                          : owners == 1 ? ledger::ResolutionStatus::Unique
                                        : ledger::ResolutionStatus::Missing;
      if (resolution.status != ledger::ResolutionStatus::Unique) {
        resolution.primary_member.reset();
      }
    } catch (const std::exception &error) {
      resolution.status = ledger::ResolutionStatus::Corrupt;
      resolution.primary_member.reset();
      resolution.diagnostic = error.what();
    }
    return resolution;
  });
}
namespace {
auto check_source_binding(obcx::core::IDbConnection &db,
                          const ledger::SourceBinding &operation)
    -> ledger::Admission {
  require_v4(db);
  const auto &id = operation.delivery_id;
  const auto admission = admission_from_db(db, id);
  if (operation.member_id.empty() || admission.delivery_id != id)
    throw std::invalid_argument("source operation identity required");
  const auto members = db.query(
      "SELECT member_id,pair_id,source_scope,target_scope,message_ref,data "
      "FROM "
      "bridge_delivery_members WHERE delivery_id=? AND message_ref=?;",
      {id, encode(operation.message_ref)});
  if (members.size() != 1)
    throw std::runtime_error("source operation member not unique");
  const auto &row = members.front();
  const auto member =
      Json::parse(db_string(row, "data")).get<ledger::SourceMember>();
  if (member.member_id != operation.member_id ||
      member.message_ref != operation.message_ref ||
      db_string(row, "member_id") != member.member_id ||
      db_string(row, "pair_id") != admission.pair_id ||
      db_string(row, "source_scope") != encode(admission.source_scope) ||
      db_string(row, "target_scope") != encode(admission.target_scope) ||
      db_string(row, "message_ref") != encode(member.message_ref))
    throw std::runtime_error("source operation member evidence mismatch");
  const auto outputs = db.query(
      std::string{"SELECT attempt_id,revision_id,data FROM "} +
          confirmed_output_rows_sql + " WHERE delivery_id=? AND message_ref=?;",
      {id, encode(operation.anchor_ref)});
  if (outputs.size() != 1)
    throw std::runtime_error("source operation anchor not confirmed");
  auto allowed_inputs = member.input_blocks;
  if (const auto revision_id =
          db_optional_string(outputs.front(), "revision_id")) {
    const auto revisions = revision_snapshots(db, id);
    const auto revision =
        std::ranges::find_if(revisions, [&](const auto &entry) {
          return entry.admission.revision_id == *revision_id;
        });
    if (revision == revisions.end() ||
        !revision->observations_match_checkpoint ||
        revision->admission.member_id != member.member_id ||
        revision->admission.source_ref != member.message_ref)
      throw std::runtime_error("source operation edit anchor is unresolved");
    allowed_inputs = revision->admission.input_blocks;
  } else {
    (void)require_attempt(db, id, db_string(outputs.front(), "attempt_id"));
  }
  const auto output = Json::parse(db_string(outputs.front(), "data"))
                          .get<ledger::PhysicalOutput>();
  if (output.message_ref != operation.anchor_ref ||
      output.reverse_primary_member !=
          std::optional<std::string>{operation.member_id})
    throw std::runtime_error("source operation anchor ownership changed");
  const auto original_count =
      std::ranges::count_if(admission.members, [&](const auto &original) {
        return original.member_id == member.member_id;
      });
  if (original_count > 1 ||
      (original_count == 1 && std::ranges::find(admission.members, member) ==
                                  admission.members.end()) ||
      (original_count == 0 && !output.provenance))
    throw std::runtime_error("source operation original member conflict");
  if (output.provenance &&
      (output.provenance->empty() ||
       std::ranges::any_of(*output.provenance, [&](const auto &origin) {
         return origin.member_id != member.member_id ||
                std::ranges::find(allowed_inputs, origin.input_block) ==
                    allowed_inputs.end();
       })))
    throw std::runtime_error("source operation provenance conflict");
  std::size_t observed = 0;
  for (const auto &candidate :
       db.query(std::string{"SELECT o.delivery_id,d.admission FROM "} +
                    confirmed_output_rows_sql +
                    " o LEFT JOIN bridge_deliveries d ON "
                    "d.delivery_id=o.delivery_id WHERE "
                    "o.message_ref=?;",
                {encode(operation.anchor_ref)})) {
    const auto other =
        Json::parse(db_string(candidate, "admission")).get<ledger::Admission>();
    if (other.delivery_id != db_string(candidate, "delivery_id"))
      throw std::runtime_error(
          "source operation observation identity mismatch");
    if (other.pair_id == admission.pair_id &&
        other.target_scope == admission.target_scope) {
      ++observed;
      if (other.delivery_id != id)
        throw std::runtime_error("source operation ambiguous anchor");
    }
  }
  if (observed != 1)
    throw std::runtime_error("source operation anchor observation missing");
  return admission;
}
auto checked_source_operation(obcx::core::IDbConnection &db,
                              const std::string &id,
                              const ledger::SourceOperation &operation)
    -> std::string {
  require_blob(operation.result);
  if (operation.operation_id.empty())
    throw std::invalid_argument("source operation identity required");
  (void)check_source_binding(db, {id, operation.member_id,
                                  operation.message_ref, operation.anchor_ref});
  return Json(operation).dump();
}
auto existing_source_operation(obcx::core::IDbConnection &db,
                               const std::string &id,
                               const ledger::SourceOperation &operation)
    -> std::optional<std::string> {
  const auto rows = db.query("SELECT member_id,message_ref,anchor_ref,data "
                             "FROM bridge_source_operations "
                             "WHERE delivery_id=? AND operation_id=?;",
                             {id, operation.operation_id});
  if (rows.empty())
    return std::nullopt;
  if (rows.size() != 1)
    throw std::runtime_error("ambiguous source operation identity");
  const auto serialized = db_string(rows.front(), "data");
  const auto existing = Json::parse(serialized).get<ledger::SourceOperation>();
  require_blob(existing.result);
  if (existing.operation_id != operation.operation_id ||
      existing.member_id != operation.member_id ||
      existing.message_ref != operation.message_ref ||
      existing.anchor_ref != operation.anchor_ref ||
      db_string(rows.front(), "member_id") != operation.member_id ||
      db_string(rows.front(), "message_ref") != encode(operation.message_ref) ||
      db_string(rows.front(), "anchor_ref") != encode(operation.anchor_ref) ||
      existing.result.codec != operation.result.codec ||
      existing.result.version != operation.result.version ||
      Json(existing).dump() != serialized)
    throw std::runtime_error("conflicting source operation evidence");
  return serialized;
}
auto checked_output_operation(obcx::core::IDbConnection &db,
                              const std::string &delivery_id,
                              const ledger::OutputOperation &operation)
    -> std::string {
  require_v4(db);
  const auto admission = admission_from_db(db, delivery_id);
  if (admission.delivery_id != delivery_id)
    throw std::runtime_error("output operation admission identity mismatch");
  require_blob(operation.result);
  if (operation.operation_id.empty())
    throw std::invalid_argument("output operation identity required");
  const auto rows = db.query(
      std::string{"SELECT attempt_id,revision_id,data FROM "} +
          confirmed_output_rows_sql + " WHERE delivery_id=? AND message_ref=?;",
      {delivery_id, encode(operation.message_ref)});
  if (rows.size() != 1 || Json::parse(db_string(rows.front(), "data"))
                                  .get<ledger::PhysicalOutput>()
                                  .message_ref != operation.message_ref)
    throw std::runtime_error("operation output not confirmed or inconsistent");
  if (const auto revision_id =
          db_optional_string(rows.front(), "revision_id")) {
    const auto revisions = revision_snapshots(db, delivery_id);
    const auto revision =
        std::ranges::find_if(revisions, [&](const auto &entry) {
          return entry.admission.revision_id == *revision_id;
        });
    if (revision == revisions.end() || !revision->observations_match_checkpoint)
      throw std::runtime_error("edit output has unresolved effects");
  } else {
    (void)require_attempt(db, delivery_id,
                          db_string(rows.front(), "attempt_id"));
  }
  return Json(operation).dump();
}
auto existing_output_operation(obcx::core::IDbConnection &db,
                               const std::string &delivery_id,
                               const ledger::OutputOperation &operation)
    -> std::optional<std::string> {
  const auto rows =
      db.query("SELECT message_ref,data FROM bridge_output_operations WHERE "
               "delivery_id=? AND operation_id=?;",
               {delivery_id, operation.operation_id});
  if (rows.empty())
    return std::nullopt;
  if (rows.size() != 1)
    throw std::runtime_error("ambiguous output operation identity");
  const auto serialized = db_string(rows.front(), "data");
  const auto existing = Json::parse(serialized).get<ledger::OutputOperation>();
  require_blob(existing.result);
  if (existing.operation_id != operation.operation_id ||
      existing.message_ref != operation.message_ref ||
      db_string(rows.front(), "message_ref") != encode(operation.message_ref) ||
      existing.result.codec != operation.result.codec ||
      existing.result.version != operation.result.version ||
      Json(existing).dump() != serialized)
    throw std::runtime_error("conflicting output operation evidence");
  return serialized;
}
void validate_command_operation(const ledger::CommandOperation &operation) {
  if (operation.pair_id.empty() || operation.operation_id.empty() ||
      operation.kind.empty())
    throw std::invalid_argument("command operation identity required");
  for (const auto *value :
       {&operation.command_ref, &operation.source_scope,
        &operation.target_scope, &operation.request, &operation.evidence})
    require_blob(*value);
  if (operation.source_binding) {
    const auto &binding = *operation.source_binding;
    if (binding.delivery_id.empty() || binding.member_id.empty())
      throw std::invalid_argument("command source binding identity required");
    require_blob(binding.message_ref);
    require_blob(binding.anchor_ref);
  }
  if (operation.outcome)
    require_blob(*operation.outcome);
}
auto read_command_operation(obcx::core::IDbConnection &db,
                            const std::string &pair, const std::string &id)
    -> std::optional<ledger::CommandOperation> {
  require_v4(db);
  const auto rows = db.query("SELECT * FROM bridge_command_operations WHERE "
                             "pair_id=? AND operation_id=?;",
                             {pair, id});
  if (rows.empty())
    return std::nullopt;
  if (rows.size() != 1)
    throw std::runtime_error("ambiguous command operation");
  const auto &row = rows.front();
  const auto serialized = db_string(row, "data");
  auto result = Json::parse(serialized).get<ledger::CommandOperation>();
  validate_command_operation(result);
  if (result.pair_id != pair || result.operation_id != id ||
      db_string(row, "pair_id") != pair ||
      db_string(row, "operation_id") != id ||
      db_string(row, "kind") != result.kind ||
      db_string(row, "command_ref") != encode(result.command_ref) ||
      Json(result).dump() != serialized)
    throw std::runtime_error("command operation storage identity mismatch");
  if (result.source_binding) {
    if (db_string(row, "source_delivery_id") !=
            result.source_binding->delivery_id ||
        db_string(row, "source_anchor_ref") !=
            encode(result.source_binding->anchor_ref))
      throw std::runtime_error("command source binding storage mismatch");
  } else if (!std::holds_alternative<std::nullptr_t>(
                 row.at("source_delivery_id")) ||
             !std::holds_alternative<std::nullptr_t>(
                 row.at("source_anchor_ref"))) {
    throw std::runtime_error("unexpected command source binding columns");
  }
  return result;
}
} // namespace

auto BridgeStateRepository::find_command_operation(const std::string &pair,
                                                   const std::string &id) const
    -> std::optional<ledger::CommandOperation> {
  return db_manager_.run_read<std::optional<ledger::CommandOperation>>(
      db_instance_, [&](obcx::core::IDbConnection &db) {
        return read_command_operation(db, pair, id);
      });
}
auto BridgeStateRepository::admit_command_operation(
    const ledger::CommandOperation &pending) -> ledger::AdmissionResult {
  validate_command_operation(pending);
  if (pending.outcome)
    throw std::invalid_argument("command admission must be pending");
  return db_manager_.run_transaction<ledger::AdmissionResult>(
      db_instance_, [&](obcx::core::IDbConnection &db) {
        require_v4(db);
        if (auto existing = read_command_operation(db, pending.pair_id,
                                                   pending.operation_id)) {
          existing->outcome.reset();
          if (*existing != pending)
            throw std::runtime_error("conflicting command operation replay");
          return ledger::AdmissionResult::AlreadyExists;
        }
        if (pending.source_binding) {
          const auto admission =
              check_source_binding(db, *pending.source_binding);
          if (admission.pair_id != pending.pair_id ||
              admission.source_scope != pending.target_scope ||
              admission.target_scope != pending.source_scope)
            throw std::runtime_error("command source binding scope mismatch");
        }
        db.execute(
            "INSERT INTO "
            "bridge_command_operations(pair_id,operation_id,kind,command_ref,"
            "source_delivery_id,source_anchor_ref,data) VALUES(?,?,?,?,?,?,?);",
            {pending.pair_id, pending.operation_id, pending.kind,
             encode(pending.command_ref),
             pending.source_binding
                 ? obcx::core::DbValue{pending.source_binding->delivery_id}
                 : obcx::core::DbValue{nullptr},
             pending.source_binding ? obcx::core::DbValue{encode(
                                          pending.source_binding->anchor_ref)}
                                    : obcx::core::DbValue{nullptr},
             Json(pending).dump()});
        if (read_command_operation(db, pending.pair_id, pending.operation_id) !=
            std::optional{pending})
          throw std::runtime_error("command admission not persisted");
        return ledger::AdmissionResult::Created;
      });
}
auto BridgeStateRepository::finish_command_operation(
    const ledger::CommandOperation &pending,
    const ledger::CommandOperation &completed) -> ledger::AdmissionResult {
  validate_command_operation(pending);
  validate_command_operation(completed);
  auto intent = completed;
  intent.outcome.reset();
  if (pending.outcome || !completed.outcome || intent != pending)
    throw std::invalid_argument("command terminal identity mismatch");
  return db_manager_.run_transaction<ledger::AdmissionResult>(
      db_instance_, [&](obcx::core::IDbConnection &db) {
        const auto existing =
            read_command_operation(db, pending.pair_id, pending.operation_id);
        if (existing == std::optional{completed})
          return ledger::AdmissionResult::AlreadyExists;
        if (existing != std::optional{pending})
          throw std::runtime_error("command checkpoint changed or missing");
        db.execute("UPDATE bridge_command_operations SET data=? WHERE "
                   "pair_id=? AND operation_id=? AND data=?;",
                   {Json(completed).dump(), pending.pair_id,
                    pending.operation_id, Json(pending).dump()});
        if (read_command_operation(db, pending.pair_id, pending.operation_id) !=
            std::optional{completed})
          throw std::runtime_error("command finalization not persisted");
        return ledger::AdmissionResult::Created;
      });
}

auto BridgeStateRepository::admit_source_operation(
    const std::string &id, const ledger::SourceOperation &pending)
    -> ledger::AdmissionResult {
  return db_manager_.run_transaction<ledger::AdmissionResult>(
      db_instance_, [&](obcx::core::IDbConnection &db) {
        const auto serialized = checked_source_operation(db, id, pending);
        if (existing_source_operation(db, id, pending))
          return ledger::AdmissionResult::AlreadyExists;
        db.execute("INSERT INTO "
                   "bridge_source_operations(delivery_id,operation_id,member_"
                   "id,message_ref,anchor_ref,data) "
                   "VALUES(?,?,?,?,?,?);",
                   {id, pending.operation_id, pending.member_id,
                    encode(pending.message_ref), encode(pending.anchor_ref),
                    serialized});
        if (existing_source_operation(db, id, pending) !=
            std::optional<std::string>{serialized})
          throw std::runtime_error("source operation admission not persisted");
        return ledger::AdmissionResult::Created;
      });
}
auto BridgeStateRepository::finish_source_operation(
    const std::string &id, const ledger::SourceOperation &pending,
    const ledger::SourceOperation &completed) -> ledger::AdmissionResult {
  if (pending.operation_id != completed.operation_id ||
      pending.member_id != completed.member_id ||
      pending.message_ref != completed.message_ref ||
      pending.anchor_ref != completed.anchor_ref ||
      pending.result.codec != completed.result.codec ||
      pending.result.version != completed.result.version ||
      pending.result == completed.result)
    throw std::invalid_argument("source operation terminal identity mismatch");
  require_blob(completed.result);
  return db_manager_.run_transaction<ledger::AdmissionResult>(
      db_instance_, [&](obcx::core::IDbConnection &db) {
        const auto initial = checked_source_operation(db, id, pending);
        const auto terminal = Json(completed).dump();
        const auto existing = existing_source_operation(db, id, pending);
        if (!existing)
          throw std::runtime_error("source operation was not admitted");
        if (*existing == terminal)
          return ledger::AdmissionResult::AlreadyExists;
        if (*existing != initial)
          throw std::runtime_error("conflicting source operation finalization");
        db.execute(
            "UPDATE bridge_source_operations SET data=? WHERE delivery_id=? "
            "AND operation_id=? "
            "AND member_id=? AND message_ref=? AND anchor_ref=? AND data=?;",
            {terminal, id, pending.operation_id, pending.member_id,
             encode(pending.message_ref), encode(pending.anchor_ref), initial});
        if (existing_source_operation(db, id, completed) !=
            std::optional<std::string>{terminal})
          throw std::runtime_error(
              "source operation finalization not persisted");
        return ledger::AdmissionResult::Created;
      });
}
auto BridgeStateRepository::admit_output_operation(
    const std::string &id, const ledger::OutputOperation &pending,
    const std::vector<ledger::Provenance> &expected_provenance)
    -> ledger::AdmissionResult {
  return db_manager_.run_transaction<ledger::AdmissionResult>(
      db_instance_, [&](obcx::core::IDbConnection &db) {
        const auto serialized = checked_output_operation(db, id, pending);
        const auto outputs = db.query(
            std::string{"SELECT data FROM "} + confirmed_output_rows_sql +
                " WHERE delivery_id=? AND message_ref=?;",
            {id, encode(pending.message_ref)});
        if (outputs.size() != 1)
          throw std::runtime_error("mutation admission output disappeared");
        const auto output = Json::parse(db_string(outputs.front(), "data"))
                                .get<ledger::PhysicalOutput>();
        if (expected_provenance.empty() || !output.provenance ||
            *output.provenance != expected_provenance)
          throw std::runtime_error(
              "output provenance changed before mutation admission");
        if (existing_output_operation(db, id, pending))
          return ledger::AdmissionResult::AlreadyExists;
        const auto claims =
            db.query("SELECT revision_id FROM bridge_revision_output_claims "
                     "WHERE delivery_id=? AND message_ref=?;",
                     {id, encode(pending.message_ref)});
        if (!claims.empty()) {
          const auto revisions = revision_snapshots(db, id);
          const auto owner =
              std::ranges::find_if(revisions, [&](const auto &entry) {
                return entry.admission.revision_id ==
                       db_string(claims.front(), "revision_id");
              });
          if (claims.size() != 1 || owner == revisions.end() ||
              !owner->observations_match_checkpoint || !owner->checkpoint ||
              std::ranges::find(owner->admission.expected_outputs,
                                pending.message_ref) ==
                  owner->admission.expected_outputs.end() ||
              std::ranges::find(owner->checkpoint->deleted_outputs,
                                pending.message_ref) !=
                  owner->checkpoint->deleted_outputs.end())
            throw std::runtime_error(
                "output is retired or owned by an unresolved edit");
          // A settled definite rejection retained this old-version output.
          // A NEW explicit command may mutate it; the old edit cannot replay.
        }
        db.execute("INSERT INTO "
                   "bridge_output_operations(delivery_id,operation_id,message_"
                   "ref,data) VALUES(?,?,?,?);",
                   {id, pending.operation_id, encode(pending.message_ref),
                    serialized});
        if (existing_output_operation(db, id, pending) !=
            std::optional<std::string>{serialized})
          throw std::runtime_error("output operation admission not persisted");
        return ledger::AdmissionResult::Created;
      });
}

auto BridgeStateRepository::finish_output_operation(
    const std::string &id, const ledger::OutputOperation &pending,
    const ledger::OutputOperation &completed) -> ledger::AdmissionResult {
  if (pending.operation_id != completed.operation_id ||
      pending.message_ref != completed.message_ref ||
      pending.result.codec != completed.result.codec ||
      pending.result.version != completed.result.version ||
      pending.result == completed.result)
    throw std::invalid_argument("output operation terminal identity mismatch");
  require_blob(completed.result);
  return db_manager_.run_transaction<ledger::AdmissionResult>(
      db_instance_, [&](obcx::core::IDbConnection &db) {
        const auto initial = checked_output_operation(db, id, pending);
        const auto terminal = Json(completed).dump();
        const auto existing = existing_output_operation(db, id, pending);
        if (!existing)
          throw std::runtime_error("output operation was not admitted");
        if (*existing == terminal)
          return ledger::AdmissionResult::AlreadyExists;
        if (*existing != initial)
          throw std::runtime_error("conflicting output operation finalization");
        db.execute(
            "UPDATE bridge_output_operations SET data=? WHERE delivery_id=? "
            "AND operation_id=? AND message_ref=? AND data=?;",
            {terminal, id, pending.operation_id, encode(pending.message_ref),
             initial});
        if (existing_output_operation(db, id, completed) !=
            std::optional<std::string>{terminal})
          throw std::runtime_error(
              "output operation finalization not persisted");
        return ledger::AdmissionResult::Created;
      });
}

void BridgeStateRepository::record_output_operation(
    const std::string &id, const ledger::OutputOperation &operation) {
  db_manager_.run_transaction<void>(
      db_instance_, [&](obcx::core::IDbConnection &db) {
        require_v4(db);
        require_blob(operation.result);
        if (operation.operation_id.empty()) {
          throw std::invalid_argument("output operation identity required");
        }
        const auto ref = encode(operation.message_ref);
        if (db.query(std::string{"SELECT message_ref FROM "} +
                         confirmed_output_rows_sql +
                         " WHERE delivery_id=? AND message_ref=?;",
                     {id, ref})
                .size() != 1) {
          throw std::runtime_error("operation output not confirmed");
        }
        const auto existing =
            db.query("SELECT data FROM bridge_output_operations WHERE "
                     "delivery_id=? AND operation_id=?;",
                     {id, operation.operation_id});
        const auto serialized = Json(operation).dump();
        if (!existing.empty()) {
          if (db_string(existing.front(), "data") != serialized) {
            throw std::runtime_error("conflicting output operation replay");
          }
          return;
        }
        db.execute("INSERT INTO "
                   "bridge_output_operations(delivery_id,operation_id,message_"
                   "ref,data) VALUES(?,?,?,?);",
                   {id, operation.operation_id, ref, serialized});
      });
}
auto BridgeStateRepository::prune_completed_retry_payloads(
    const std::int64_t limit) -> std::int64_t {
  if (limit <= 0)
    throw std::invalid_argument(
        "retry cleanup requires positive explicit limit");
  return db_manager_.run_transaction<
      std::int64_t>(db_instance_, [&](obcx::core::IDbConnection &db) {
    require_v4(db);
    // A cheap completion hint selects IDs only. The full reconciliation
    // above remains authoritative before any payload is deleted.
    constexpr auto completed_ids =
        "SELECT r.delivery_id FROM bridge_delivery_retries r JOIN "
        "bridge_deliveries d ON d.delivery_id=r.delivery_id WHERE "
        "d.legacy_unknown=0 AND d.admission IS NOT NULL AND EXISTS(SELECT "
        "1 FROM bridge_delivery_attempts a WHERE "
        "a.delivery_id=r.delivery_id AND a.sequence=(SELECT MAX(sequence) "
        "FROM bridge_delivery_attempts WHERE delivery_id=r.delivery_id) "
        "AND CASE WHEN json_valid(a.result) THEN "
        "json_extract(a.result,'$.complete') END=1)";
    std::optional<std::string> after;
    std::int64_t removed = 0;
    for (;;) {
      const auto rows =
          after
              ? db.query(
                    std::string{completed_ids} +
                        " AND r.delivery_id>? ORDER BY r.delivery_id LIMIT ?;",
                    {*after, limit})
              : db.query(std::string{completed_ids} +
                             " ORDER BY r.delivery_id LIMIT ?;",
                         {limit});
      if (rows.empty())
        return removed;
      for (const auto &row : rows) {
        const auto id = db_string(row, "delivery_id");
        after = id;
        {
          std::scoped_lock lock(retry_activity_->mutex);
          if (retry_activity_->busy(id))
            continue;
        }
        if (discard_retry_payload(db, id, true) && ++removed == limit)
          return removed;
      }
    }
  });
}

auto BridgeStateRepository::reclaim_terminal_retry_payload(
    const std::string &id) -> bool {
  return db_manager_.run_transaction<bool>(
      db_instance_, [&](obcx::core::IDbConnection &db) {
        require_v4(db);
        {
          std::scoped_lock lock(retry_activity_->mutex);
          if (retry_activity_->busy(id))
            return false;
        }
        return discard_retry_payload(db, id, false);
      });
}

auto BridgeStateRepository::prune_terminal_retry_payloads(
    const std::int64_t limit) -> std::int64_t {
  if (limit <= 0)
    throw std::invalid_argument(
        "retry cleanup requires positive explicit limit");
  return db_manager_.run_transaction<std::int64_t>(
      db_instance_, [&](obcx::core::IDbConnection &db) {
        require_v4(db);
        constexpr auto ids =
            "SELECT r.delivery_id FROM bridge_delivery_retries r JOIN "
            "bridge_deliveries d ON d.delivery_id=r.delivery_id WHERE "
            "d.legacy_unknown=0 AND d.admission IS NOT NULL";
        std::optional<std::string> after;
        {
          std::scoped_lock lock(retry_activity_->mutex);
          after = retry_activity_->sweep_after;
        }
        auto rows =
            after ? db.query(std::string{ids} + " AND r.delivery_id>? ORDER BY "
                                                "r.delivery_id LIMIT ?;",
                             {*after, limit})
                  : db.query(std::string{ids} +
                                 " ORDER BY r.delivery_id LIMIT ?;",
                             {limit});
        if (rows.empty() && after) {
          {
            std::scoped_lock lock(retry_activity_->mutex);
            retry_activity_->sweep_after.reset();
          }
          rows = db.query(std::string{ids} + " ORDER BY r.delivery_id LIMIT ?;",
                          {limit});
        }
        std::int64_t removed = 0;
        for (const auto &row : rows) {
          const auto id = db_string(row, "delivery_id");
          {
            std::scoped_lock lock(retry_activity_->mutex);
            // Advance even past active, corrupt or runnable work.
            retry_activity_->sweep_after = id;
            if (retry_activity_->busy(id))
              continue;
          }
          if (discard_retry_payload(db, id, false))
            ++removed;
        }
        return removed;
      });
}

auto BridgeStateRepository::list_ready_retry_deliveries(
    const std::int64_t ready_at_ms, const std::int64_t limit) const
    -> std::vector<std::string> {
  if (limit <= 0) {
    throw std::invalid_argument("retry list requires positive explicit limit");
  }
  return db_manager_.run_transaction<std::vector<
      std::string>>(db_instance_, [&](obcx::core::IDbConnection &db) {
    require_v4(db);
    // Retry payloads contain opaque bodies and can be hundreds of MiB.
    // Never materialize them for the whole queue, or parse completed work
    // just to discover that it cannot be retried. Page only IDs using the
    // existing identity indexes; safe_retry remains the authoritative gate.
    constexpr auto eligible_ids =
        "SELECT r.delivery_id FROM bridge_delivery_retries r WHERE "
        "EXISTS(SELECT 1 FROM bridge_delivery_attempts a WHERE "
        "a.delivery_id=r.delivery_id) AND NOT EXISTS(SELECT 1 FROM "
        "bridge_delivery_outputs o WHERE o.delivery_id=r.delivery_id)";
    std::optional<std::string> after;
    std::vector<std::pair<std::int64_t, std::string>> candidates;
    for (;;) {
      const auto rows =
          after
              ? db.query(
                    std::string{eligible_ids} +
                        " AND r.delivery_id>? ORDER BY r.delivery_id LIMIT ?;",
                    {*after, limit})
              : db.query(std::string{eligible_ids} +
                             " ORDER BY r.delivery_id LIMIT ?;",
                         {limit});
      if (rows.empty())
        break;
      for (const auto &row : rows) {
        const auto id = db_string(row, "delivery_id");
        after = id;
        const auto admission = admission_from_db(db, id);
        if (!safe_retry(db, admission))
          continue;
        // Project only the scalar schedule into C++. SQLite may inspect
        // one large JSON value, but no queue-wide payload copies survive.
        const auto schedule =
            db.query("SELECT CASE WHEN "
                     "json_type(payload,'$.next_retry_at_ms')='integer' THEN "
                     "json_extract(payload,'$.next_retry_at_ms') END AS "
                     "next_retry_at_ms FROM bridge_delivery_retries WHERE "
                     "delivery_id=?;",
                     {id});
        if (schedule.size() != 1)
          throw std::runtime_error("retry schedule disappeared");
        const auto timestamp =
            std::get_if<std::int64_t>(&schedule.front().at("next_retry_at_ms"));
        if (timestamp == nullptr)
          throw std::invalid_argument(
              "retry schedule must be an integer timestamp");
        const auto time = *timestamp;
        if (time > ready_at_ms)
          continue;
        const std::pair candidate{time, id};
        const auto position =
            std::lower_bound(candidates.begin(), candidates.end(), candidate);
        // Keep the earliest safe IDs globally, not merely the first page.
        if (position == candidates.end() &&
            static_cast<std::int64_t>(candidates.size()) == limit)
          continue;
        candidates.insert(position, candidate);
        if (static_cast<std::int64_t>(candidates.size()) > limit)
          candidates.pop_back();
      }
    }
    std::vector<std::string> result;
    for (const auto &[time, id] : candidates) {
      const auto rows = db.query(
          "SELECT payload FROM bridge_delivery_retries WHERE delivery_id=?;",
          {id});
      if (rows.size() != 1)
        throw std::runtime_error("retry payload disappeared");
      // Parse by reference: db_string would copy the entire opaque body.
      const auto payload =
          Json::parse(std::get<std::string>(rows.front().at("payload")))
              .get<ledger::RetryPayload>();
      const auto admission = admission_from_db(db, id);
      validate_retry(admission, payload);
      if (payload.next_retry_at_ms != time || !safe_retry(db, admission))
        throw std::runtime_error("retry candidate changed during selection");
      result.push_back(id);
    }
    return result;
  });
}
} // namespace bridge

#include "bridge_album_observations.inc"
#include "bridge_delivery_revisions.inc"
