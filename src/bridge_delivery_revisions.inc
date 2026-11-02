// Included after bridge_delivery_ledger.inc. No online schema upgrade caller.
#include <map>
namespace bridge::ledger {
void to_json(nlohmann::json &j, const RevisionAdmission &v) {
  j = {{"delivery_id", v.delivery_id},
       {"pair_id", v.pair_id},
       {"source_scope", v.source_scope},
       {"target_scope", v.target_scope},
       {"revision_id", v.revision_id},
       {"member_id", v.member_id},
       {"source_ref", v.source_ref},
       {"native_edited_at_ms", v.native_edited_at_ms},
       {"content_fingerprint", v.content_fingerprint},
       {"previous_revision_id", optional_json(v.previous_revision_id)},
       {"expected_outputs", v.expected_outputs},
       {"input_blocks", v.input_blocks},
       {"expected_output_operations", v.expected_output_operations},
       {"intent", v.intent}};
}
void from_json(const nlohmann::json &j, RevisionAdmission &v) {
  j.at("delivery_id").get_to(v.delivery_id);
  j.at("pair_id").get_to(v.pair_id);
  j.at("source_scope").get_to(v.source_scope);
  j.at("target_scope").get_to(v.target_scope);
  j.at("revision_id").get_to(v.revision_id);
  j.at("member_id").get_to(v.member_id);
  j.at("source_ref").get_to(v.source_ref);
  j.at("native_edited_at_ms").get_to(v.native_edited_at_ms);
  j.at("content_fingerprint").get_to(v.content_fingerprint);
  v.previous_revision_id =
      read_optional<std::string>(j.at("previous_revision_id"));
  j.at("expected_outputs").get_to(v.expected_outputs);
  j.at("input_blocks").get_to(v.input_blocks);
  j.at("expected_output_operations").get_to(v.expected_output_operations);
  j.at("intent").get_to(v.intent);
}
void to_json(nlohmann::json &j, const RevisionCheckpoint &v) {
  j = {{"sequence", v.sequence},
       {"phase", v.phase},
       {"evidence", v.evidence},
       {"outputs", v.outputs},
       {"deleted_outputs", v.deleted_outputs},
       {"retained_outputs", v.retained_outputs},
       {"settled", v.settled},
       {"complete", v.complete},
       {"send_attempt_ids", v.send_attempt_ids},
       {"send_results", v.send_results},
       {"retry_due_at_ms", optional_json(v.retry_due_at_ms)}};
}
void from_json(const nlohmann::json &j, RevisionCheckpoint &v) {
  j.at("sequence").get_to(v.sequence);
  j.at("phase").get_to(v.phase);
  j.at("evidence").get_to(v.evidence);
  j.at("outputs").get_to(v.outputs);
  j.at("deleted_outputs").get_to(v.deleted_outputs);
  j.at("retained_outputs").get_to(v.retained_outputs);
  j.at("settled").get_to(v.settled);
  j.at("complete").get_to(v.complete);
  j.at("send_attempt_ids").get_to(v.send_attempt_ids);
  j.at("send_results").get_to(v.send_results);
  v.retry_due_at_ms = read_optional<std::int64_t>(j.at("retry_due_at_ms"));
}
} // namespace bridge::ledger

namespace bridge {
namespace {
void create_revision_ledger(obcx::core::IDbConnection &db) {
  db.execute(R"(
    CREATE TABLE bridge_delivery_revisions (
      delivery_id TEXT NOT NULL, revision_id TEXT NOT NULL,
      member_id TEXT NOT NULL, native_edited_at_ms INTEGER NOT NULL,
      admission TEXT NOT NULL, checkpoint TEXT,
      PRIMARY KEY(delivery_id,revision_id),
      UNIQUE(delivery_id,member_id,native_edited_at_ms)
    );
  )");
  db.execute(R"(
    CREATE TABLE bridge_revision_outputs (
      delivery_id TEXT NOT NULL, revision_id TEXT NOT NULL,
      message_ref TEXT NOT NULL, data TEXT NOT NULL,
      confirmation_attempts TEXT NOT NULL,
      PRIMARY KEY(delivery_id,message_ref)
    );
  )");
  db.execute(R"(
    CREATE TABLE bridge_revision_output_claims (
      delivery_id TEXT NOT NULL, message_ref TEXT NOT NULL,
      revision_id TEXT NOT NULL,
      PRIMARY KEY(delivery_id,message_ref)
    );
  )");
}
void validate_revision_ledger(obcx::core::IDbConnection &db) {
  require_columns(db, "bridge_delivery_revisions",
                  {"delivery_id", "revision_id", "member_id",
                   "native_edited_at_ms", "admission", "checkpoint"});
  require_columns(db, "bridge_revision_outputs",
                  {"delivery_id", "revision_id", "message_ref", "data",
                   "confirmation_attempts"});
  require_columns(db, "bridge_revision_output_claims",
                  {"delivery_id", "message_ref", "revision_id"});
  require_unique_key(db, "bridge_delivery_revisions",
                     {"delivery_id", "revision_id"});
  require_unique_key(db, "bridge_delivery_revisions",
                     {"delivery_id", "member_id", "native_edited_at_ms"});
  for (const auto *table :
       {"bridge_revision_outputs", "bridge_revision_output_claims"}) {
    require_unique_key(db, table, {"delivery_id", "message_ref"});
    if (!db.query("SELECT o.delivery_id FROM " + std::string(table) +
                  " o LEFT JOIN bridge_delivery_revisions r ON "
                  "r.delivery_id=o.delivery_id AND r.revision_id=o.revision_id "
                  "WHERE r.revision_id IS NULL LIMIT 1;")
             .empty())
      throw std::runtime_error("orphaned revision output evidence");
  }
  if (!db.query("SELECT r.delivery_id FROM bridge_delivery_revisions r "
                "LEFT JOIN bridge_delivery_members m ON "
                "m.delivery_id=r.delivery_id "
                "AND m.member_id=r.member_id LEFT JOIN bridge_deliveries d ON "
                "d.delivery_id=r.delivery_id WHERE m.member_id IS NULL OR "
                "d.delivery_id IS NULL OR d.legacy_unknown<>0 OR "
                "d.admission IS NULL LIMIT 1;")
           .empty())
    throw std::runtime_error("orphaned revision source evidence");
}
void validate_revision_admission(const ledger::RevisionAdmission &value) {
  require_blob(value.source_ref);
  require_blob(value.intent);
  require_blob(value.source_scope);
  require_blob(value.target_scope);
  if (value.pair_id.empty() || value.delivery_id.empty() ||
      value.revision_id.empty() || value.member_id.empty() ||
      value.native_edited_at_ms < 0 || value.content_fingerprint.empty() ||
      (value.previous_revision_id &&
       (value.previous_revision_id->empty() ||
        *value.previous_revision_id == value.revision_id)))
    throw std::invalid_argument("invalid revision identity or source evidence");
  std::set<std::string> refs;
  for (const auto &ref : value.expected_outputs) {
    require_blob(ref);
    if (!refs.insert(encode(ref)).second)
      throw std::invalid_argument("duplicate revision predecessor output");
  }
  std::set<std::string> inputs;
  for (const auto &input : value.input_blocks)
    if (input.empty() || !inputs.insert(input).second)
      throw std::invalid_argument("invalid revision source contribution");
  if (inputs.empty())
    throw std::invalid_argument("revision lacks source contributions");
}
void validate_revision_checkpoint(const ledger::RevisionAdmission &admission,
                                  const ledger::RevisionCheckpoint &value) {
  require_blob(value.evidence);
  if (value.sequence <= 0 || value.phase < ledger::RevisionPhase::Admitted ||
      value.phase > ledger::RevisionPhase::Finished ||
      ((value.complete || value.settled) &&
       value.phase != ledger::RevisionPhase::Finished) ||
      (value.complete && (!value.settled || value.outputs.empty())))
    throw std::invalid_argument("invalid revision checkpoint state");
  std::set<std::string> attempts;
  for (const auto &id : value.send_attempt_ids)
    if (id.empty() || !attempts.insert(id).second)
      throw std::invalid_argument("invalid revision send admission identity");
  if ((value.phase < ledger::RevisionPhase::Sending && !attempts.empty()) ||
      ((value.phase == ledger::RevisionPhase::Sending ||
        !value.outputs.empty()) &&
       attempts.empty()))
    throw std::invalid_argument("revision phase lacks matching send admission");
  if (value.send_results.size() > attempts.size() ||
      attempts.size() - value.send_results.size() > 1 ||
      (value.phase == ledger::RevisionPhase::Sending &&
       attempts.size() != value.send_results.size() + 1))
    throw std::invalid_argument("edit send result sequence mismatch");
  if ((value.settled && value.send_results.size() != attempts.size()) ||
      (value.complete && value.send_results.empty()))
    throw std::invalid_argument(
        "settled edit lacks every admitted send result");
  if (!value.send_results.empty() &&
      value.send_results.size() == attempts.size()) {
    const auto &last = value.send_results.back();
    if (last.complete != value.complete || last.outputs != value.outputs ||
        (value.settled && last.effect == ledger::Effect::PossiblySubmitted))
      throw std::invalid_argument(
          "edit checkpoint contradicts last send result");
  }
  for (std::size_t i = 0; i < value.send_results.size(); ++i) {
    const auto &result = value.send_results[i];
    require_blob(result.outcome);
    if (result.finished_at_ms < 0)
      throw std::invalid_argument("invalid edit send completion clock");
    if (i + 1 < attempts.size() &&
        (result.complete ||
         result.effect != ledger::Effect::DefinitelyNotSubmitted ||
         !result.outputs.empty() || !result.retryable))
      throw std::invalid_argument("edit retry follows unsafe result");
  }
  if (value.retry_due_at_ms &&
      (*value.retry_due_at_ms < 0 || !value.settled || value.complete ||
       !value.outputs.empty() || attempts.empty() ||
       value.send_results.size() != attempts.size() ||
       value.send_results.back().effect !=
           ledger::Effect::DefinitelyNotSubmitted ||
       !value.send_results.back().retryable ||
       !value.send_results.back().outputs.empty()))
    throw std::invalid_argument("unsafe edit retry scheduling");
  std::set<std::string> deleted;
  for (const auto &ref : value.deleted_outputs)
    if (std::ranges::find(admission.expected_outputs, ref) ==
            admission.expected_outputs.end() ||
        !deleted.insert(encode(ref)).second)
      throw std::invalid_argument("revision deletion was not planned");
  std::set<std::string> retained;
  for (const auto &ref : value.retained_outputs)
    if (std::ranges::find(admission.expected_outputs, ref) ==
            admission.expected_outputs.end() ||
        deleted.contains(encode(ref)) || !retained.insert(encode(ref)).second)
      throw std::invalid_argument(
          "revision retained output was not planned or contradicts deletion");
  if ((value.phase == ledger::RevisionPhase::Sending ||
       !value.outputs.empty()) &&
      deleted.size() + retained.size() != admission.expected_outputs.size())
    throw std::invalid_argument(
        "replacement requires settled predecessor deletion outcomes");
  std::set<std::string> outputs;
  for (const auto &output : value.outputs) {
    require_blob(output.message_ref);
    if (!outputs.insert(encode(output.message_ref)).second ||
        (output.reverse_primary_member &&
         output.reverse_primary_member != std::optional{admission.member_id}) ||
        (output.provenance && output.provenance->empty()))
      throw std::invalid_argument("revision output source proof conflicts");
    if (output.provenance)
      for (const auto &origin : *output.provenance)
        if (origin.member_id != admission.member_id ||
            std::ranges::find(admission.input_blocks, origin.input_block) ==
                admission.input_blocks.end())
          throw std::invalid_argument("revision output contribution mismatch");
    // Unknown provenance remains unknown; still retain the physical receipt.
    for (const auto &detail : output.detail_evidence)
      require_blob(detail);
    for (const auto &media : output.reusable_media)
      require_blob(media);
  }
}
auto revision_evidence_contains(const ledger::PhysicalOutput &observed,
                                const ledger::PhysicalOutput &reported)
    -> bool {
  if (observed.message_ref != reported.message_ref ||
      (reported.ordinal && reported.ordinal != observed.ordinal) ||
      (reported.provenance && reported.provenance != observed.provenance) ||
      (reported.reverse_primary_member &&
       reported.reverse_primary_member != observed.reverse_primary_member))
    return false;
  const auto contains = [](const auto &all, const auto &part) {
    return std::ranges::all_of(part, [&](const auto &item) {
      return std::ranges::find(all, item) != all.end();
    });
  };
  return contains(observed.reusable_media, reported.reusable_media) &&
         contains(observed.detail_evidence, reported.detail_evidence);
}
void merge_revision_output(
    obcx::core::IDbConnection &db, const ledger::RevisionAdmission &admission,
    const ledger::PhysicalOutput &output,
    const std::optional<std::string> &confirmation_attempt) {
  require_blob(output.message_ref);
  if (output.ordinal && *output.ordinal < 0)
    throw std::invalid_argument("negative edit output ordinal");
  if (output.reverse_primary_member &&
      output.reverse_primary_member != std::optional{admission.member_id})
    throw std::invalid_argument("edit output owner mismatch");
  if (output.provenance) {
    if (output.provenance->empty())
      throw std::invalid_argument("empty known edit provenance");
    for (const auto &origin : *output.provenance)
      if (origin.member_id != admission.member_id ||
          std::ranges::find(admission.input_blocks, origin.input_block) ==
              admission.input_blocks.end())
        throw std::invalid_argument("edit output input mismatch");
  }
  for (const auto &value : output.detail_evidence)
    require_blob(value);
  for (const auto &value : output.reusable_media)
    require_blob(value);
  const auto ref = encode(output.message_ref);
  if (!db.query("SELECT message_ref FROM bridge_delivery_outputs WHERE "
                "delivery_id=? "
                "AND message_ref=?;",
                {admission.delivery_id, ref})
           .empty())
    throw std::runtime_error("edit reused an original physical identity");
  const auto rows = db.query("SELECT revision_id,data,confirmation_attempts "
                             "FROM bridge_revision_outputs WHERE "
                             "delivery_id=? AND message_ref=?;",
                             {admission.delivery_id, ref});
  if (rows.empty()) {
    const auto attempts = confirmation_attempt
                              ? Json::array({*confirmation_attempt})
                              : Json::array();
    db.execute("INSERT INTO "
               "bridge_revision_outputs(delivery_id,revision_id,message_ref,"
               "data,confirmation_attempts) "
               "VALUES(?,?,?,?,?);",
               {admission.delivery_id, admission.revision_id, ref,
                Json(output).dump(), attempts.dump()});
    return;
  }
  auto merged = Json::parse(db_string(rows.front(), "data"))
                    .get<ledger::PhysicalOutput>();
  if (db_string(rows.front(), "revision_id") != admission.revision_id ||
      merged.message_ref != output.message_ref ||
      (merged.ordinal && output.ordinal && merged.ordinal != output.ordinal) ||
      (merged.provenance && output.provenance &&
       merged.provenance != output.provenance) ||
      (merged.reverse_primary_member && output.reverse_primary_member &&
       merged.reverse_primary_member != output.reverse_primary_member))
    throw std::runtime_error("conflicting edit physical output evidence");
  if (output.ordinal)
    merged.ordinal = output.ordinal;
  if (output.provenance)
    merged.provenance = output.provenance;
  if (output.reverse_primary_member)
    merged.reverse_primary_member = output.reverse_primary_member;
  const auto append = [](auto &all, const auto &incoming) {
    for (const auto &value : incoming)
      if (std::ranges::find(all, value) == all.end())
        all.push_back(value);
  };
  append(merged.reusable_media, output.reusable_media);
  append(merged.detail_evidence, output.detail_evidence);
  auto attempts = Json::parse(db_string(rows.front(), "confirmation_attempts"))
                      .get<std::vector<std::string>>();
  if (confirmation_attempt &&
      std::ranges::find(attempts, *confirmation_attempt) == attempts.end())
    attempts.push_back(*confirmation_attempt);
  db.execute(
      "UPDATE bridge_revision_outputs SET data=?,confirmation_attempts=? WHERE "
      "delivery_id=? AND revision_id=? "
      "AND message_ref=?;",
      {Json(merged).dump(), Json(attempts).dump(), admission.delivery_id,
       admission.revision_id, ref});
}
auto revision_snapshots(obcx::core::IDbConnection &db, const std::string &id)
    -> std::vector<ledger::RevisionSnapshot> {
  std::vector<ledger::RevisionSnapshot> result;
  const auto parent = admission_from_db(db, id);
  std::map<std::string, std::string> previous_per_member;
  std::map<std::string, bool> lineage_settled;
  const auto original_outputs = db.query(
      "SELECT message_ref FROM bridge_delivery_outputs WHERE delivery_id=?;",
      {id});
  const bool original_settled =
      original_delivery_state(db, id, original_outputs.size()) ==
      ledger::DeliveryState::Complete;
  for (const auto &row :
       db.query("SELECT * FROM bridge_delivery_revisions WHERE "
                "delivery_id=? ORDER BY native_edited_at_ms,revision_id;",
                {id})) {
    const auto serialized = db_string(row, "admission");
    auto admission = Json::parse(serialized).get<ledger::RevisionAdmission>();
    validate_revision_admission(admission);
    if (admission.delivery_id != id ||
        admission.revision_id != db_string(row, "revision_id") ||
        admission.member_id != db_string(row, "member_id") ||
        admission.native_edited_at_ms != db_int64(row, "native_edited_at_ms") ||
        Json(admission).dump() != serialized)
      throw std::runtime_error("revision row identity mismatch");
    if (admission.pair_id != parent.pair_id ||
        admission.source_scope != parent.source_scope ||
        admission.target_scope != parent.target_scope)
      throw std::runtime_error("revision snapshot scope mismatch");
    const auto member =
        db.query("SELECT message_ref FROM bridge_delivery_members WHERE "
                 "delivery_id=? AND member_id=?;",
                 {id, admission.member_id});
    if (member.size() != 1 || db_string(member.front(), "message_ref") !=
                                  encode(admission.source_ref))
      throw std::runtime_error("revision snapshot source binding mismatch");
    const auto previous = previous_per_member.find(admission.member_id);
    const auto expected_previous = previous == previous_per_member.end()
                                       ? std::optional<std::string>{}
                                       : std::optional{previous->second};
    if (admission.previous_revision_id != expected_previous)
      throw std::runtime_error("revision snapshot predecessor chain mismatch");
    previous_per_member[admission.member_id] = admission.revision_id;
    std::optional<ledger::RevisionCheckpoint> checkpoint;
    if (const auto value = db_optional_string(row, "checkpoint")) {
      checkpoint = Json::parse(*value).get<ledger::RevisionCheckpoint>();
      validate_revision_checkpoint(admission, *checkpoint);
      if (Json(*checkpoint).dump() != *value)
        throw std::runtime_error("noncanonical revision checkpoint");
    }
    const auto rows = db.query(
        "SELECT data,confirmation_attempts FROM bridge_revision_outputs WHERE "
        "delivery_id=? AND revision_id=?;",
        {id, admission.revision_id});
    std::vector<ledger::PhysicalOutput> observed;
    std::set<std::string> confirmed_attempts;
    for (const auto &output_row : rows) {
      observed.push_back(Json::parse(db_string(output_row, "data"))
                             .get<ledger::PhysicalOutput>());
      for (const auto &attempt :
           Json::parse(db_string(output_row, "confirmation_attempts"))
               .get<std::vector<std::string>>()) {
        if (!checkpoint ||
            std::ranges::find(checkpoint->send_attempt_ids, attempt) ==
                checkpoint->send_attempt_ids.end())
          throw std::runtime_error(
              "edit observation has no admitted send identity");
        confirmed_attempts.insert(attempt);
      }
    }
    if (!observed.empty() &&
        (!checkpoint || checkpoint->send_attempt_ids.empty()))
      throw std::runtime_error("edit observation precedes send admission");
    bool matches = original_settled && checkpoint &&
                   checkpoint->phase == ledger::RevisionPhase::Finished &&
                   checkpoint->settled &&
                   observed.size() == checkpoint->outputs.size();
    if (checkpoint)
      for (const auto &reported : checkpoint->outputs) {
        const auto found =
            std::ranges::find_if(observed, [&](const auto &output) {
              return output.message_ref == reported.message_ref;
            });
        if (found == observed.end() ||
            !revision_evidence_contains(*found, reported))
          throw std::runtime_error(
              "edit returned output lost persisted evidence");
      }
    if (checkpoint)
      for (std::size_t i = 0; i < checkpoint->send_results.size(); ++i)
        if (confirmed_attempts.contains(checkpoint->send_attempt_ids[i]) &&
            checkpoint->send_results[i].effect ==
                ledger::Effect::DefinitelyNotSubmitted)
          matches = false; // A late confirmation contradicts this attempt, even
                           // if IDs coincide.
    const auto previous_settled = lineage_settled.find(admission.member_id);
    if (previous_settled != lineage_settled.end())
      matches = matches && previous_settled->second;
    lineage_settled[admission.member_id] = matches;
    result.push_back({std::move(admission),
                      std::move(checkpoint),
                      std::move(observed),
                      matches,
                      {confirmed_attempts.begin(), confirmed_attempts.end()}});
  }
  return result;
}
void suppress_unresolved_revision_retries(obcx::core::IDbConnection &db,
                                          const std::string &id) {
  for (const auto &revision : revision_snapshots(db, id)) {
    if (revision.observations_match_checkpoint || !revision.checkpoint ||
        !revision.checkpoint->retry_due_at_ms)
      continue;
    auto next = *revision.checkpoint;
    // No SDK outcome or effect evidence is rewritten. Exact snapshot CAS also
    // observes this scheduling invalidation if the sequence is exhausted.
    if (next.sequence < std::numeric_limits<std::int64_t>::max())
      ++next.sequence;
    next.retry_due_at_ms.reset();
    db.execute("UPDATE bridge_delivery_revisions SET checkpoint=? WHERE "
               "delivery_id=? AND revision_id=?;",
               {Json(next).dump(), id, revision.admission.revision_id});
  }
}
auto active_revision_refs(const ledger::RevisionSnapshot &revision)
    -> std::vector<ledger::OpaqueValue> {
  if (!revision.checkpoint ||
      revision.checkpoint->phase != ledger::RevisionPhase::Finished ||
      !revision.checkpoint->settled || !revision.observations_match_checkpoint)
    throw std::runtime_error(
        "previous edit has incomplete or uncertain effects");
  std::vector<ledger::OpaqueValue> refs;
  for (const auto &ref : revision.admission.expected_outputs)
    if (std::ranges::find(revision.checkpoint->deleted_outputs, ref) ==
        revision.checkpoint->deleted_outputs.end())
      refs.push_back(ref);
  for (const auto &output : revision.checkpoint->outputs)
    refs.push_back(output.message_ref);
  return refs;
}
} // namespace

auto BridgeStateRepository::admit_revision(
    const ledger::RevisionAdmission &pending) -> ledger::AdmissionResult {
  validate_revision_admission(pending);
  return db_manager_.run_transaction<
      ledger::AdmissionResult>(db_instance_, [&](obcx::core::IDbConnection
                                                     &db) {
    require_v4(db);
    const auto original = admission_from_db(db, pending.delivery_id);
    if (original.pair_id != pending.pair_id ||
        original.source_scope != pending.source_scope ||
        original.target_scope != pending.target_scope)
      throw std::runtime_error("revision installation pair or scope changed");
    const auto member_rows =
        db.query("SELECT data FROM bridge_delivery_members WHERE "
                 "delivery_id=? AND member_id=?;",
                 {pending.delivery_id, pending.member_id});
    if (member_rows.size() != 1)
      throw std::runtime_error("revision source member missing");
    const auto member = Json::parse(db_string(member_rows.front(), "data"))
                            .get<ledger::SourceMember>();
    if (member.message_ref != pending.source_ref)
      throw std::runtime_error("revision source identity changed");
    const auto revisions = revision_snapshots(db, pending.delivery_id);
    const ledger::RevisionSnapshot *head = nullptr;
    for (const auto &revision : revisions) {
      if (revision.admission.revision_id == pending.revision_id) {
        if (revision.admission != pending)
          throw std::runtime_error("conflicting edit admission replay");
        return ledger::AdmissionResult::AlreadyExists;
      }
      if (revision.admission.member_id == pending.member_id)
        head = &revision;
    }
    if (head) {
      if (pending.previous_revision_id !=
              std::optional{head->admission.revision_id} ||
          pending.native_edited_at_ms <= head->admission.native_edited_at_ms)
        throw std::runtime_error("stale or unordered edit observation");
      auto expected = active_revision_refs(*head);
      auto proposed = pending.expected_outputs;
      const auto order = [](const auto &a, const auto &b) {
        return encode(a) < encode(b);
      };
      std::ranges::sort(expected, order);
      std::ranges::sort(proposed, order);
      if (expected != proposed)
        throw std::runtime_error("edit predecessor output set changed");
    } else {
      if (pending.previous_revision_id)
        throw std::runtime_error("edit predecessor disappeared");
      const auto attempts = attempt_rows(db, pending.delivery_id);
      if (attempts.empty())
        throw std::runtime_error("edit has no original delivery outcome");
      for (const auto &attempt : attempts) {
        const auto terminal = db_optional_string(attempt, "result");
        if (!terminal ||
            Json::parse(*terminal).get<ledger::AttemptResult>().effect ==
                ledger::Effect::PossiblySubmitted)
          throw std::runtime_error("edit original delivery is uncertain");
      }
      if (!Json::parse(*db_optional_string(attempts.back(), "result"))
               .get<ledger::AttemptResult>()
               .complete)
        throw std::runtime_error("edit requires a complete original delivery");
      std::set<std::string> actual;
      for (const auto &row : db.query(
               "SELECT data FROM bridge_delivery_outputs WHERE delivery_id=?;",
               {pending.delivery_id})) {
        const auto output =
            Json::parse(db_string(row, "data")).get<ledger::PhysicalOutput>();
        if (!output.provenance)
          throw std::runtime_error("edit original output provenance unknown");
        const bool selected =
            std::ranges::any_of(*output.provenance, [&](const auto &origin) {
              return origin.member_id == pending.member_id;
            });
        if (!selected)
          continue;
        if (output.provenance->empty() ||
            std::ranges::any_of(*output.provenance, [&](const auto &origin) {
              return origin.member_id != pending.member_id;
            }))
          throw std::runtime_error(
              "shared edit output requires reconstruction");
        actual.insert(encode(output.message_ref));
      }
      std::set<std::string> expected;
      for (const auto &ref : pending.expected_outputs)
        expected.insert(encode(ref));
      if (actual.empty() || actual != expected)
        throw std::runtime_error("edit original output set changed");
    }
    std::vector<ledger::OutputOperation> operations;
    for (const auto &row : db.query(
             "SELECT data FROM bridge_output_operations WHERE delivery_id=? "
             "ORDER BY operation_id;",
             {pending.delivery_id})) {
      const auto operation =
          Json::parse(db_string(row, "data")).get<ledger::OutputOperation>();
      if (std::ranges::find(pending.expected_outputs, operation.message_ref) !=
          pending.expected_outputs.end())
        operations.push_back(operation);
    }
    if (operations != pending.expected_output_operations)
      throw std::runtime_error(
          "output mutation changed during edit preparation");
    db.execute(
        "INSERT INTO "
        "bridge_delivery_revisions(delivery_id,revision_id,member_id,"
        "native_edited_at_ms,admission,checkpoint) VALUES(?,?,?,?,?,NULL);",
        {pending.delivery_id, pending.revision_id, pending.member_id,
         pending.native_edited_at_ms, Json(pending).dump()});
    for (const auto &ref : pending.expected_outputs) {
      const auto claim = db.query(
          "SELECT revision_id FROM bridge_revision_output_claims WHERE "
          "delivery_id=? AND message_ref=?;",
          {pending.delivery_id, encode(ref)});
      if (!claim.empty()) {
        if (!head || db_string(claim.front(), "revision_id") !=
                         head->admission.revision_id)
          throw std::runtime_error("edit predecessor output already claimed");
        db.execute("UPDATE bridge_revision_output_claims SET revision_id=? "
                   "WHERE delivery_id=? "
                   "AND message_ref=? AND revision_id=?;",
                   {pending.revision_id, pending.delivery_id, encode(ref),
                    head->admission.revision_id});
      } else {
        db.execute("INSERT INTO "
                   "bridge_revision_output_claims(delivery_id,message_ref,"
                   "revision_id) "
                   "VALUES(?,?,?);",
                   {pending.delivery_id, encode(ref), pending.revision_id});
      }
    }
    return ledger::AdmissionResult::Created;
  });
}

auto BridgeStateRepository::list_ready_revision_retries(
    std::int64_t ready_at_ms, std::int64_t limit) const
    -> std::vector<std::pair<std::string, std::string>> {
  if (ready_at_ms < 0 || limit <= 0 || limit > 65536)
    throw std::invalid_argument("invalid revision retry scan bound");
  return db_manager_.run_transaction<std::vector<std::pair<
      std::string, std::string>>>(db_instance_, [&](obcx::core::IDbConnection
                                                        &db) {
    require_v4(db);
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto &row : db.query(
             "SELECT r.delivery_id,r.revision_id FROM "
             "bridge_delivery_revisions r "
             "WHERE json_type(r.checkpoint,'$.retry_due_at_ms')='integer' "
             "AND json_extract(r.checkpoint,'$.retry_due_at_ms')<=? "
             "AND NOT EXISTS (SELECT 1 FROM bridge_revision_outputs o WHERE "
             "o.delivery_id=r.delivery_id AND o.revision_id=r.revision_id) "
             "AND NOT EXISTS (SELECT 1 FROM bridge_delivery_revisions n WHERE "
             "n.delivery_id=r.delivery_id AND n.member_id=r.member_id AND "
             "n.native_edited_at_ms>r.native_edited_at_ms) "
             "ORDER BY "
             "json_extract(r.checkpoint,'$.retry_due_at_ms'),r.delivery_id,r."
             "revision_id LIMIT ?;",
             {ready_at_ms, limit}))
      result.emplace_back(db_string(row, "delivery_id"),
                          db_string(row, "revision_id"));
    return result;
  });
}
auto BridgeStateRepository::admit_revision_retry(
    const ledger::RevisionSnapshot &expected, const std::string &attempt_id,
    std::int64_t now_ms) -> std::optional<ledger::RevisionSnapshot> {
  validate_revision_admission(expected.admission);
  if (!expected.checkpoint || !expected.observations_match_checkpoint ||
      !expected.observed_outputs.empty() ||
      !expected.confirmed_send_attempt_ids.empty() || attempt_id.empty() ||
      now_ms < 0)
    throw std::invalid_argument("revision is not safe for replacement retry");
  validate_revision_checkpoint(expected.admission, *expected.checkpoint);
  const auto &before = *expected.checkpoint;
  const auto intent = Json::from_cbor(expected.admission.intent.bytes.begin(),
                                      expected.admission.intent.bytes.end());
  const auto maximum = intent.at("max_attempts").get<std::int64_t>();
  if (!before.retry_due_at_ms || *before.retry_due_at_ms > now_ms ||
      maximum <= 1 ||
      before.send_attempt_ids.size() >= static_cast<std::uint64_t>(maximum) ||
      before.sequence == std::numeric_limits<std::int64_t>::max() ||
      std::ranges::find(before.send_attempt_ids, attempt_id) !=
          before.send_attempt_ids.end())
    throw std::runtime_error(
        "edit retry not due, exhausted or already admitted");
  auto next = before;
  ++next.sequence;
  next.phase = ledger::RevisionPhase::Sending;
  next.settled = false;
  next.send_attempt_ids.push_back(attempt_id);
  next.retry_due_at_ms.reset();
  validate_revision_checkpoint(expected.admission, next);
  return db_manager_.run_transaction<std::optional<ledger::RevisionSnapshot>>(
      db_instance_,
      [&](obcx::core::IDbConnection &db)
          -> std::optional<ledger::RevisionSnapshot> {
        require_v4(db);
        const auto all = revision_snapshots(db, expected.admission.delivery_id);
        const auto current = std::ranges::find_if(all, [&](const auto &entry) {
          return entry.admission.revision_id == expected.admission.revision_id;
        });
        if (current == all.end() || *current != expected ||
            std::ranges::any_of(all, [&](const auto &entry) {
              return entry.admission.member_id ==
                         expected.admission.member_id &&
                     entry.admission.native_edited_at_ms >
                         expected.admission.native_edited_at_ms;
            }))
          return std::nullopt;
        db.execute("UPDATE bridge_delivery_revisions SET checkpoint=? WHERE "
                   "delivery_id=? AND revision_id=?;",
                   {Json(next).dump(), expected.admission.delivery_id,
                    expected.admission.revision_id});
        const auto saved =
            revision_snapshots(db, expected.admission.delivery_id);
        const auto found = std::ranges::find_if(saved, [&](const auto &entry) {
          return entry.admission.revision_id == expected.admission.revision_id;
        });
        if (found == saved.end() || found->checkpoint != std::optional{next})
          throw std::runtime_error("edit retry admission not persisted");
        return *found;
      });
}

auto BridgeStateRepository::checkpoint_revision(
    const ledger::RevisionSnapshot &expected,
    const ledger::RevisionCheckpoint &next) -> ledger::RevisionSnapshot {
  validate_revision_admission(expected.admission);
  validate_revision_checkpoint(expected.admission, next);
  const auto prior = expected.checkpoint ? expected.checkpoint->sequence : 0;
  if (prior == std::numeric_limits<std::int64_t>::max() ||
      next.sequence != prior + 1 ||
      (expected.checkpoint &&
       expected.checkpoint->phase == ledger::RevisionPhase::Finished) ||
      (expected.checkpoint && next.phase < expected.checkpoint->phase) ||
      (!expected.checkpoint && next.phase == ledger::RevisionPhase::Finished &&
       !next.outputs.empty()) ||
      (next.phase == ledger::RevisionPhase::Finished && !next.outputs.empty() &&
       (!expected.checkpoint ||
        expected.checkpoint->phase != ledger::RevisionPhase::Sending)))
    throw std::invalid_argument("invalid edit checkpoint transition");
  const auto prior_attempts = expected.checkpoint
                                  ? expected.checkpoint->send_attempt_ids
                                  : std::vector<std::string>{};
  if (next.phase == ledger::RevisionPhase::Sending) {
    if (!prior_attempts.empty() || next.send_attempt_ids.size() != 1)
      throw std::invalid_argument(
          "edit send already admitted; no replay permitted");
  } else if (next.send_attempt_ids != prior_attempts) {
    throw std::invalid_argument("edit send identity changed without admission");
  }
  const auto prior_results = expected.checkpoint
                                 ? expected.checkpoint->send_results
                                 : std::vector<ledger::AttemptResult>{};
  if (next.send_results.size() < prior_results.size() ||
      next.send_results.size() > prior_results.size() + 1 ||
      !std::equal(prior_results.begin(), prior_results.end(),
                  next.send_results.begin()) ||
      (next.send_results.size() != prior_results.size() &&
       next.phase != ledger::RevisionPhase::Finished))
    throw std::invalid_argument(
        "edit discarded or rewrote send outcome history");
  if (expected.checkpoint) {
    for (const auto &output : expected.checkpoint->outputs)
      if (std::ranges::find(next.outputs, output) == next.outputs.end())
        throw std::invalid_argument(
            "edit checkpoint discarded confirmed output");
    for (const auto &ref : expected.checkpoint->deleted_outputs)
      if (std::ranges::find(next.deleted_outputs, ref) ==
          next.deleted_outputs.end())
        throw std::invalid_argument(
            "edit checkpoint discarded confirmed deletion");
    for (const auto &ref : expected.checkpoint->retained_outputs)
      if (std::ranges::find(next.retained_outputs, ref) ==
          next.retained_outputs.end())
        throw std::invalid_argument(
            "edit checkpoint discarded definite deletion rejection");
  }
  return db_manager_.run_transaction<
      ledger::RevisionSnapshot>(db_instance_, [&](obcx::core::IDbConnection
                                                      &db) {
    require_v4(db);
    const auto all = revision_snapshots(db, expected.admission.delivery_id);
    const auto current = std::ranges::find_if(all, [&](const auto &entry) {
      return entry.admission.revision_id == expected.admission.revision_id;
    });
    if (current == all.end() || current->admission != expected.admission)
      throw std::runtime_error("edit admission changed or missing");
    if (current->checkpoint == std::optional{next})
      return *current; // Persistence replay only; never a provider admission.
    if (current->checkpoint != expected.checkpoint)
      throw std::runtime_error("edit checkpoint changed");
    if (next.phase == ledger::RevisionPhase::Sending &&
        !current->observed_outputs.empty())
      throw std::runtime_error(
          "edit already has a physical observation; no submission permitted");
    for (const auto &output : next.outputs)
      merge_revision_output(db, expected.admission, output, std::nullopt);
    auto completed = next;
    if (completed.retry_due_at_ms) {
      const auto original_count =
          db.query("SELECT message_ref FROM bridge_delivery_outputs WHERE "
                   "delivery_id=?;",
                   {expected.admission.delivery_id})
              .size();
      if (!current->observed_outputs.empty() ||
          original_delivery_state(db, expected.admission.delivery_id,
                                  original_count) !=
              ledger::DeliveryState::Complete ||
          std::ranges::any_of(all, [&](const auto &entry) {
            return entry.admission.member_id == expected.admission.member_id &&
                   entry.admission.native_edited_at_ms <
                       expected.admission.native_edited_at_ms &&
                   !entry.observations_match_checkpoint;
          }))
        completed.retry_due_at_ms.reset();
    }
    const auto value = Json(completed).dump();
    db.execute("UPDATE bridge_delivery_revisions SET checkpoint=? WHERE "
               "delivery_id=? AND revision_id=?;",
               {value, expected.admission.delivery_id,
                expected.admission.revision_id});
    const auto persisted =
        revision_snapshots(db, expected.admission.delivery_id);
    const auto saved = std::ranges::find_if(persisted, [&](const auto &entry) {
      return entry.admission.revision_id == expected.admission.revision_id;
    });
    if (saved == persisted.end() || saved->admission != expected.admission ||
        saved->checkpoint != std::optional{completed})
      throw std::runtime_error("edit checkpoint finalization not persisted");
    return *saved;
  });
}
void BridgeStateRepository::record_revision_confirmation(
    const std::string &id, const std::string &revision_id,
    const std::string &attempt_id, const ledger::OpaqueValue &target_scope,
    const ledger::PhysicalOutput &output) {
  if (output.ordinal || output.provenance || output.reverse_primary_member)
    throw std::invalid_argument(
        "edit confirmation cannot invent order or ownership");
  db_manager_.run_transaction<void>(db_instance_, [&](obcx::core::IDbConnection
                                                          &db) {
    require_v4(db);
    const auto revisions = revision_snapshots(db, id);
    const auto found = std::ranges::find_if(revisions, [&](const auto &entry) {
      return entry.admission.revision_id == revision_id;
    });
    if (found == revisions.end() ||
        found->admission.target_scope != target_scope || !found->checkpoint ||
        std::ranges::find(found->checkpoint->send_attempt_ids, attempt_id) ==
            found->checkpoint->send_attempt_ids.end())
      throw std::runtime_error("edit confirmation lacks scoped send admission");
    merge_revision_output(db, found->admission, output, attempt_id);
    suppress_unresolved_revision_retries(db, id);
    // Preserve the returned outcome unchanged. A late extra observation makes
    // observations_match_checkpoint false and blocks
    // mutation/retry/publication.
  });
}
} // namespace bridge
