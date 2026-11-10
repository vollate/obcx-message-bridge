// Included after the delivery/revision ledger. No provider calls or authority
// restoration: this table records observed membership, not sent contribution.
#include <core/bot/bot_codec.hpp>
#include <core/bot/bot_events.hpp>

namespace bridge {
namespace {
namespace album_bot = obcx::bot;
template <class T>
auto album_decode(const ledger::OpaqueValue &value, std::string_view name)
    -> T {
  if (value.codec != "obcx.bot." + std::string(name) + ".cbor" ||
      value.version != 1 || value.bytes.empty() ||
      value.bytes.size() > album_bot::detail::bot_maximum_bytes)
    throw std::runtime_error("invalid album reference encoding");
  const auto document =
      album_bot::Json::from_cbor(value.bytes.begin(), value.bytes.end());
  album_bot::detail::validate_bot_wire(document);
  auto decoded = document.get<T>();
  decoded.validate();
  const auto bytes = album_bot::Json::to_cbor(album_bot::Json(decoded));
  if (std::string(bytes.begin(), bytes.end()) != value.bytes)
    throw std::runtime_error("noncanonical album reference");
  return decoded;
}
void validate_album_observation(const ledger::AlbumObservation &value,
                                const ledger::OpaqueValue &scope) {
  std::size_t remaining = album_bot::detail::bot_maximum_bytes;
  for (const auto *field :
       {&value.event_id, &value.correlation_id, &value.source_bot}) {
    if (field->size() > remaining)
      throw std::runtime_error("album observation string budget exceeded");
    remaining -= field->size();
  }
  const auto source =
      album_decode<album_bot::ConversationRef>(scope, "ConversationRef");
  const auto message =
      album_decode<album_bot::MessageRef>(value.message_ref, "MessageRef");
  const auto group = album_decode<album_bot::MediaGroupRef>(
      value.media_group_ref, "MediaGroupRef");
  if (message.conversation != source || group.conversation != source ||
      source.installation.surface != album_bot::SurfaceId{"telegram.bot_api"} ||
      value.source_bot != source.installation.installation_id ||
      value.event_id.empty())
    throw std::runtime_error("album observation scope or source mismatch");
  album_bot::detail::validate_bot_wire(Json(value));
}
void validate_album_admission_observations(
    const ledger::Admission &admission,
    const std::vector<ledger::AlbumObservation> &observations) {
  const auto &encoded = admission.policy_and_limits;
  if (encoded.codec != "obcx.bot.BridgeDeliveryPolicy.cbor" ||
      encoded.version != 2 || encoded.bytes.empty() ||
      encoded.bytes.size() > album_bot::detail::bot_maximum_bytes ||
      observations.size() != admission.members.size())
    throw std::runtime_error("invalid album admission observation envelope");
  const auto policy =
      Json::from_cbor(encoded.bytes.begin(), encoded.bytes.end());
  album_bot::detail::validate_bot_wire(policy);
  const auto bytes = Json::to_cbor(policy);
  if (std::string(bytes.begin(), bytes.end()) != encoded.bytes ||
      policy.at("album_batch_policy") != "preserve-observed-batch" ||
      !policy.at("album_observations").is_array() ||
      policy.at("album_observations").size() != observations.size())
    throw std::runtime_error(
        "album admission trace is noncanonical or incomplete");
  for (std::size_t i = 0; i < observations.size(); ++i) {
    const auto &observation = observations[i];
    const auto &member = admission.members[i];
    validate_album_observation(observation, admission.source_scope);
    const Json trace = {{"member_id", member.member_id},
                        {"event_id", observation.event_id},
                        {"correlation_id", observation.correlation_id},
                        {"source_bot", observation.source_bot}};
    if (policy.at("album_observations").at(i) != trace ||
        observation.message_ref != member.message_ref ||
        member.media_group_ref != std::optional{observation.media_group_ref})
      throw std::runtime_error(
          "album admission trace does not bind its source member");
    if (i == 0 &&
        (policy.at("publication").at("event_id") != observation.event_id ||
         policy.at("publication").at("correlation_id") !=
             observation.correlation_id ||
         policy.at("publication").at("source_bot") != observation.source_bot))
      throw std::runtime_error("album admission primary trace changed");
  }
}
void create_album_observations(obcx::core::IDbConnection &db) {
  db.execute(R"(
    CREATE TABLE bridge_album_observations (
      pair_id TEXT NOT NULL, source_scope TEXT NOT NULL, target_scope TEXT NOT NULL,
      message_ref TEXT NOT NULL, delivery_id TEXT NOT NULL, anchor_ref TEXT NOT NULL,
      data TEXT NOT NULL,
      PRIMARY KEY(pair_id,source_scope,target_scope,message_ref)
    );
  )");
  db.execute("CREATE INDEX bridge_album_observations_delivery ON "
             "bridge_album_observations(delivery_id);");
}
auto album_observations(obcx::core::IDbConnection &db, const std::string &id)
    -> std::vector<ledger::SuppressedAlbumMember> {
  const auto totals =
      db.query("SELECT COUNT(*) AS count, COALESCE(SUM(length(CAST(data AS "
               "BLOB))),0) AS bytes "
               "FROM bridge_album_observations WHERE delivery_id=?;",
               {id});
  if (totals.size() != 1 || db_int64(totals.front(), "count") < 0 ||
      db_int64(totals.front(), "count") >
          static_cast<std::int64_t>(album_bot::detail::bot_maximum_items) ||
      db_int64(totals.front(), "bytes") < 0 ||
      db_int64(totals.front(), "bytes") >
          static_cast<std::int64_t>(album_bot::detail::bot_maximum_bytes))
    throw std::runtime_error("album association snapshot exceeds bot bounds");
  std::vector<ledger::SuppressedAlbumMember> result;
  if (db_int64(totals.front(), "count") == 0)
    return result;
  const auto admission = admission_from_db(db, id);
  for (const auto &row : db.query("SELECT * FROM bridge_album_observations "
                                  "WHERE delivery_id=? ORDER BY message_ref;",
                                  {id})) {
    const auto serialized = db_string(row, "data");
    const auto document = Json::parse(serialized);
    album_bot::detail::validate_bot_wire(document);
    auto member = document.get<ledger::SuppressedAlbumMember>();
    validate_album_observation(member.observation, admission.source_scope);
    if (Json(member).dump() != serialized ||
        db_string(row, "pair_id") != admission.pair_id ||
        db_string(row, "source_scope") != encode(admission.source_scope) ||
        db_string(row, "target_scope") != encode(admission.target_scope) ||
        db_string(row, "message_ref") !=
            encode(member.observation.message_ref) ||
        db_string(row, "anchor_ref") != encode(member.anchor_member_ref) ||
        member.anchor_member_ref == member.observation.message_ref ||
        member.first_observed_event_id.empty() ||
        member.observed_position < 0 ||
        (member.observed_position == 0 &&
         member.first_observed_event_id != member.observation.event_id) ||
        member.observed_position >=
            static_cast<std::int64_t>(album_bot::detail::bot_maximum_items))
      throw std::runtime_error("inconsistent album association row");
    const auto anchors = db.query(
        "SELECT data FROM bridge_delivery_members WHERE delivery_id=? AND "
        "message_ref=? "
        "AND pair_id=? AND source_scope=? AND target_scope=?;",
        {id, encode(member.anchor_member_ref), admission.pair_id,
         encode(admission.source_scope), encode(admission.target_scope)});
    if (anchors.size() != 1)
      throw std::runtime_error("album association lacks admitted anchor");
    const auto anchor = Json::parse(db_string(anchors.front(), "data"))
                            .get<ledger::SourceMember>();
    if (anchor.message_ref != member.anchor_member_ref ||
        anchor.media_group_ref !=
            std::optional{member.observation.media_group_ref} ||
        album_decode<album_bot::MessageRef>(anchor.message_ref, "MessageRef")
                .conversation !=
            album_decode<album_bot::ConversationRef>(admission.source_scope,
                                                     "ConversationRef") ||
        !db.query("SELECT 1 FROM bridge_delivery_members WHERE pair_id=? AND "
                  "source_scope=? AND target_scope=? AND message_ref=?;",
                  {admission.pair_id, encode(admission.source_scope),
                   encode(admission.target_scope),
                   encode(member.observation.message_ref)})
             .empty())
      throw std::runtime_error(
          "album association conflicts with actual membership");
    result.push_back(std::move(member));
  }
  return result;
}
void validate_album_observations(obcx::core::IDbConnection &db) {
  require_columns(db, "bridge_album_observations",
                  {"pair_id", "source_scope", "target_scope", "message_ref",
                   "delivery_id", "anchor_ref", "data"});
  require_unique_key(
      db, "bridge_album_observations",
      {"pair_id", "source_scope", "target_scope", "message_ref"});
  for (const auto &column :
       db.query("PRAGMA table_info(bridge_album_observations);"))
    if (db_string(column, "type") != "TEXT" || db_int64(column, "notnull") != 1)
      throw std::runtime_error("album observation column constraint mismatch");
  const auto index = db.query("SELECT name FROM "
                              "pragma_index_info('bridge_album_observations_"
                              "delivery') ORDER BY seqno;");
  if (index.size() != 1 || db_string(index.front(), "name") != "delivery_id")
    throw std::runtime_error("album observation lookup index missing");
  for (const auto &row :
       db.query("SELECT DISTINCT delivery_id FROM bridge_album_observations;"))
    (void)album_observations(db, db_string(row, "delivery_id"));
}
auto suppress_album_batch(
    obcx::core::IDbConnection &db, const std::string &pair,
    const ledger::OpaqueValue &source, const ledger::OpaqueValue &target,
    const std::vector<ledger::AlbumObservation> &observations) -> bool {
  if (pair.empty() || observations.empty() ||
      observations.size() > album_bot::detail::bot_maximum_items)
    throw std::invalid_argument("invalid observed album batch");
  (void)album_decode<album_bot::ConversationRef>(target, "ConversationRef");
  std::unordered_set<std::string> seen;
  std::optional<std::pair<std::string, ledger::OpaqueValue>> selected;
  std::vector<std::size_t> unseen;
  for (std::size_t position = 0; position < observations.size(); ++position) {
    const auto &observation = observations[position];
    validate_album_observation(observation, source);
    if (observation.media_group_ref != observations.front().media_group_ref ||
        !seen.insert(encode(observation.message_ref)).second)
      throw std::invalid_argument(
          "album observations mix groups or duplicate a member");
    const auto members = db.query(
        "SELECT delivery_id,data FROM bridge_delivery_members WHERE pair_id=? "
        "AND source_scope=? AND target_scope=? AND message_ref=?;",
        {pair, encode(source), encode(target),
         encode(observation.message_ref)});
    const auto aliases = db.query(
        "SELECT delivery_id FROM bridge_album_observations WHERE pair_id=? AND "
        "source_scope=? AND target_scope=? AND message_ref=?;",
        {pair, encode(source), encode(target),
         encode(observation.message_ref)});
    if (members.size() + aliases.size() > 1)
      throw std::runtime_error("ambiguous album source reservation");
    if (!members.empty()) {
      const auto id = db_string(members.front(), "delivery_id");
      const auto admission = admission_from_db(db, id);
      const auto member = Json::parse(db_string(members.front(), "data"))
                              .get<ledger::SourceMember>();
      if (admission.pair_id != pair || admission.source_scope != source ||
          admission.target_scope != target ||
          member.message_ref != observation.message_ref ||
          member.media_group_ref != std::optional{observation.media_group_ref})
        throw std::runtime_error("album source reservation mismatch");
      if (!selected)
        selected = std::pair{id, member.message_ref};
    } else if (!aliases.empty()) {
      const auto id = db_string(aliases.front(), "delivery_id");
      const auto known = album_observations(db, id);
      const auto found = std::ranges::find_if(known, [&](const auto &entry) {
        return entry.observation.message_ref == observation.message_ref;
      });
      const auto admission = admission_from_db(db, id);
      if (admission.pair_id != pair || admission.source_scope != source ||
          admission.target_scope != target || found == known.end() ||
          found->observation.media_group_ref != observation.media_group_ref)
        throw std::runtime_error("album observation replay changed identity");
      // Preserve the first observation's publication identity. A later ingress
      // envelope does not rewrite it or turn grouping into content
      // contribution.
      if (!selected)
        selected = std::pair{id, found->anchor_member_ref};
    } else
      unseen.push_back(position);
  }
  if (!selected)
    return false;
  for (const auto position : unseen) {
    const auto &observation = observations[position];
    const ledger::SuppressedAlbumMember member{
        observation, selected->second, observations.front().event_id,
        static_cast<std::int64_t>(position)};
    db.execute(
        "INSERT INTO "
        "bridge_album_observations(pair_id,source_scope,target_scope,message_"
        "ref,delivery_id,anchor_ref,data) VALUES(?,?,?,?,?,?,?);",
        {pair, encode(source), encode(target), encode(observation.message_ref),
         selected->first, encode(selected->second), Json(member).dump()});
  }
  // Bound and validate inside the same transaction, before committing aliases.
  (void)album_observations(db, selected->first);
  return true;
}
} // namespace

auto BridgeStateRepository::observe_album_batch(
    const std::string &pair, const ledger::OpaqueValue &source,
    const ledger::OpaqueValue &target,
    const std::vector<ledger::AlbumObservation> &observations) -> bool {
  return db_manager_.run_transaction<bool>(
      db_instance_, [&](obcx::core::IDbConnection &db) {
        require_v4(db);
        return suppress_album_batch(db, pair, source, target, observations);
      });
}
} // namespace bridge
