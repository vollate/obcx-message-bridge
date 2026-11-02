#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bridge::ledger {

// The forwarding owner binds these bytes with the frozen SDK codec. Storage
// never parses platform identifiers or supplies installation/topic defaults.
// bytes is binary-safe, including NUL/non-UTF-8. The repository persists it in
// its own versioned canonical lowercase-hex JSON envelope, not as UTF-8 text.
struct OpaqueValue {
  std::string codec;
  std::int64_t version;
  std::string bytes;
  bool operator==(const OpaqueValue &) const = default;
};

struct SourceMember {
  std::string member_id;
  OpaqueValue message_ref;
  std::optional<OpaqueValue> media_group_ref;
  std::vector<std::string> input_blocks;
  bool operator==(const SourceMember &) const = default;
};

// Observed native album membership is NOT proof of output contribution.
struct AlbumObservation {
  OpaqueValue message_ref;
  OpaqueValue media_group_ref;
  std::string event_id;
  std::string correlation_id;
  std::string source_bot;
  bool operator==(const AlbumObservation &) const = default;
};
struct SuppressedAlbumMember {
  AlbumObservation observation;
  // Always a real admitted member, never another alias or an output ref.
  OpaqueValue anchor_member_ref;
  // Position in the first observed flush, not a native album/send ordinal.
  std::string first_observed_event_id;
  std::int64_t observed_position;
  bool operator==(const SuppressedAlbumMember &) const = default;
};

struct Admission {
  std::string delivery_id;
  std::string pair_id;
  OpaqueValue source_scope;
  OpaqueValue target_scope;
  OpaqueValue policy_and_limits;
  std::vector<SourceMember> members;
  std::int64_t admitted_at_ms;
  std::int64_t max_attempts;
  bool operator==(const Admission &) const = default;
};

enum class AdmissionResult { Created, AlreadyExists, AlbumBatchSuppressed };
enum class DeliveryState {
  Prepared,
  Submitting,
  Complete,
  Partial,
  Uncertain,
  Failed
};
enum class Effect { DefinitelyNotSubmitted, ConfirmedOnly, PossiblySubmitted };

struct Provenance {
  std::string member_id;
  std::string input_block;
  bool operator==(const Provenance &) const = default;
};

struct PhysicalOutput {
  OpaqueValue message_ref;
  // Notifications do not carry send order. Only a full outcome can fill it.
  std::optional<std::int64_t> ordinal;
  // nullopt means unknown association, not an empty set of contributors.
  std::optional<std::vector<Provenance>> provenance;
  std::vector<OpaqueValue> reusable_media;
  std::optional<std::string> reverse_primary_member;
  // Lossless SDK output-details observations, including range/media metadata.
  std::vector<OpaqueValue> detail_evidence;
  bool operator==(const PhysicalOutput &) const = default;
};

struct AttemptResult {
  bool complete;
  Effect effect;
  bool retryable;
  std::vector<PhysicalOutput> outputs;
  std::optional<OpaqueValue> quote_anchor;
  OpaqueValue outcome;
  std::int64_t finished_at_ms;
  bool operator==(const AttemptResult &) const = default;
};

struct RetryPayload {
  static constexpr std::int64_t current_format_version = 1;
  std::int64_t format_version;
  // Typed body, full target, required media + leases and total budget are
  // mandatory versioned blobs; ownership/retention remain with the caller.
  OpaqueValue body;
  OpaqueValue target_scope;
  OpaqueValue media_and_leases;
  OpaqueValue total_budget;
  std::int64_t next_retry_at_ms;
  std::int64_t retry_count;
  bool operator==(const RetryPayload &) const = default;
};

struct AttemptSnapshot {
  std::string attempt_id;
  std::int64_t sequence;
  std::int64_t started_at_ms;
  std::optional<AttemptResult> result;
};

struct OutputOperation {
  std::string operation_id;
  OpaqueValue message_ref;
  // Actor-owned versioned edit/delete result, including possible effects.
  OpaqueValue result;
  bool operator==(const OutputOperation &) const = default;
};

// Mutates the original source, not a Bridge-produced physical output.
// anchor_ref identifies the confirmed reply context proving reverse ownership;
// it is never reported as the mutation target or a new physical receipt.
struct SourceOperation {
  std::string operation_id;
  std::string member_id;
  OpaqueValue message_ref;
  OpaqueValue anchor_ref;
  OpaqueValue result;
  bool operator==(const SourceOperation &) const = default;
};

// Optional v4 attribution, not the target of a command's side effect.
struct SourceBinding {
  std::string delivery_id;
  std::string member_id;
  OpaqueValue message_ref;
  OpaqueValue anchor_ref;
  bool operator==(const SourceBinding &) const = default;
};

// A command can mutate a user/group rather than a message. Its actual typed
// request and full result stay separate from output/source message operations.
struct CommandOperation {
  std::string pair_id;
  std::string operation_id;
  std::string kind;
  OpaqueValue command_ref;
  OpaqueValue source_scope;
  OpaqueValue target_scope;
  OpaqueValue request;
  OpaqueValue evidence;
  std::optional<SourceBinding> source_binding;
  // Missing means admitted and possibly invoked, NEVER safe to replay.
  std::optional<OpaqueValue> outcome;
  bool operator==(const CommandOperation &) const = default;
};

// An edit is a version of an existing source member, not a new source/native
// message identity and not a retry of the original delivery. Its old outputs
// remain historical facts. Only its own confirmed replacements become current.
struct RevisionAdmission {
  std::string delivery_id;
  std::string pair_id;
  OpaqueValue source_scope;
  OpaqueValue target_scope;
  std::string revision_id;
  std::string member_id;
  OpaqueValue source_ref;
  std::int64_t native_edited_at_ms;
  std::string content_fingerprint;
  std::optional<std::string> previous_revision_id;
  std::vector<OpaqueValue> expected_outputs;
  // Positions belong to this edited observation, not the original body.
  std::vector<std::string> input_blocks;
  std::vector<OutputOperation> expected_output_operations;
  // Complete owning send plan, source contributions and original notification.
  OpaqueValue intent;
  bool operator==(const RevisionAdmission &) const = default;
};
enum class RevisionPhase { Admitted, Deleting, Sending, Finished };
struct RevisionCheckpoint {
  std::int64_t sequence;
  RevisionPhase phase;
  // Full outcomes/checkpoints, including possible effects. Storage does not
  // interpret SDK values or infer safety from an empty output vector.
  OpaqueValue evidence;
  std::vector<PhysicalOutput> outputs;
  std::vector<OpaqueValue> deleted_outputs;
  // An explicit definite rejection does not prove the old message deleted.
  // Retain its identity/evidence; do not assert current provider-side presence.
  // The approved predecessor policy still sends the new version in this case.
  std::vector<OpaqueValue> retained_outputs;
  bool settled;
  // Complete NEW content delivery, not an atomic replacement/rollback claim.
  bool complete;
  // Durable provider-submission identities, distinct from workflow admission.
  std::vector<std::string> send_attempt_ids;
  // Results correspond to the admitted prefix; an absent trailing result is
  // uncertain, never evidence permitting another submission.
  std::vector<AttemptResult> send_results;
  std::optional<std::int64_t> retry_due_at_ms;
  bool operator==(const RevisionCheckpoint &) const = default;
};
struct RevisionSnapshot {
  RevisionAdmission admission;
  std::optional<RevisionCheckpoint> checkpoint;
  // Independent physical observations may precede or contradict the returned
  // outcome. Keep both; a contradiction never erases either piece of evidence.
  std::vector<PhysicalOutput> observed_outputs;
  bool observations_match_checkpoint{};
  std::vector<std::string> confirmed_send_attempt_ids;
  bool operator==(const RevisionSnapshot &) const = default;
};

struct DeliverySnapshot {
  std::string delivery_id;
  DeliveryState state;
  bool completeness_known;
  std::optional<Admission> admission;
  std::vector<SourceMember> members;
  std::vector<PhysicalOutput> outputs;
  std::vector<AttemptSnapshot> attempts;
  std::vector<OutputOperation> output_operations;
  std::vector<SourceOperation> source_operations;
  std::vector<RevisionSnapshot> revisions;
  std::optional<OpaqueValue> quote_anchor;
  bool safe_to_retry;
  std::optional<RetryPayload> retry;
  std::vector<SuppressedAlbumMember> suppressed_album_members;
};

enum class ResolutionStatus { Missing, Unique, Ambiguous, Corrupt };
struct Resolution {
  ResolutionStatus status;
  std::vector<std::string> delivery_ids;
  std::string diagnostic;
};

struct OwnershipResolution {
  ResolutionStatus status;
  std::optional<SourceMember> primary_member;
  std::string diagnostic;
  // Observed in this pair/output scope, including an older source route.
  // Computed in the same read transaction as ownership, never persisted.
  bool observed_output;
};

} // namespace bridge::ledger
