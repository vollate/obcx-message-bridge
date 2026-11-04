#pragma once

#include "bridge_delivery_ledger.hpp"
#include "bridge_message_identity.hpp"
#include "bridge_storage_models.hpp"

#include <core/infrastructure/db_manager.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bridge {

class BridgeStateRepository {
public:
  static constexpr std::int64_t current_schema_version = 5;

  BridgeStateRepository(obcx::core::DbManager &db_manager,
                        std::string db_instance, std::string db_namespace);

  void initialize_schema();
  void validate_schema() const;
  [[nodiscard]] auto schema_version() const -> std::int64_t;

  auto admit_revision(const ledger::RevisionAdmission &admission)
      -> ledger::AdmissionResult;
  // Observation only: cannot schedule a send or turn an uncertain edit into a
  // resumable workflow. Checks scope and the durable Sending admission.
  void record_revision_confirmation(const std::string &delivery_id,
                                    const std::string &revision_id,
                                    const std::string &attempt_id,
                                    const ledger::OpaqueValue &target_scope,
                                    const ledger::PhysicalOutput &output);
  // CAS against the complete prior snapshot. Sending is admitted before the
  // provider call. A persisted Sending checkpoint never permits another send.
  [[nodiscard]] auto list_ready_revision_retries(std::int64_t ready_at_ms,
                                                 std::int64_t limit) const
      -> std::vector<std::pair<std::string, std::string>>;
  // Only an engaged newly-created checkpoint authorizes one replacement send.
  // Never repeats predecessor deletion, and never resumes an unknown attempt.
  auto admit_revision_retry(const ledger::RevisionSnapshot &expected,
                            const std::string &attempt_id, std::int64_t now_ms)
      -> std::optional<ledger::RevisionSnapshot>;
  auto checkpoint_revision(const ledger::RevisionSnapshot &expected,
                           const ledger::RevisionCheckpoint &next)
      -> ledger::RevisionSnapshot;

  auto admit_delivery(const ledger::Admission &admission)
      -> ledger::AdmissionResult;
  // The full scoped album value is evidence, not a native ID or completeness
  // claim. The admission check and inserts share one transaction; a preceding
  // read alone cannot fence two overlapping flushes.
  [[nodiscard]] auto has_album_delivery(
      const std::string &pair_id, const ledger::OpaqueValue &source_scope,
      const ledger::OpaqueValue &target_scope,
      const ledger::OpaqueValue &media_group) const -> bool;
  auto admit_album_delivery(
      const ledger::Admission &admission,
      const std::vector<ledger::AlbumObservation> &observations)
      -> ledger::AdmissionResult;
  // Atomically suppress an overlapping observed batch and remember new members
  // as associations only. This grants no send, edit, delete or media authority.
  auto observe_album_batch(
      const std::string &pair_id, const ledger::OpaqueValue &source_scope,
      const ledger::OpaqueValue &target_scope,
      const std::vector<ledger::AlbumObservation> &observations) -> bool;
  void append_delivery_members(
      const std::string &delivery_id,
      const std::vector<ledger::SourceMember> &members);
  // Post-attempt membership requires proof in an already-confirmed output;
  // this cannot authorize another send or change the admitted request body.
  void attach_proven_member(const std::string &delivery_id,
                            const std::string &attempt_id,
                            const ledger::SourceMember &member,
                            const ledger::PhysicalOutput &proven_output);
  // Hold before admitting/storing an initial request, until an attempt is
  // admitted. The shared repository protects this preparation from sweeping.
  [[nodiscard]] auto hold_preparing_delivery(const std::string &delivery_id)
      -> std::shared_ptr<void>;
  // Only Created authorizes a provider call. AlreadyExists NEVER authorizes it.
  // Created also registers local in-flight ownership, released by settlement
  // or explicitly after an invocation exits without a durable result.
  auto admit_attempt(const std::string &delivery_id,
                     const std::string &attempt_id, std::int64_t started_at_ms)
      -> ledger::AdmissionResult;
  auto admit_retry_attempt(const ledger::Admission &expected_admission,
                           const ledger::RetryPayload &expected_payload,
                           const std::string &attempt_id,
                           std::int64_t started_at_ms)
      -> ledger::AdmissionResult;
  // Local ownership only: never fabricates an outcome or authorizes a retry.
  // Call only when this attempt's invocation has ended. No database I/O.
  void release_retry_attempt_ownership(const std::string &delivery_id,
                                       const std::string &attempt_id) noexcept;
  void record_confirmation(const std::string &delivery_id,
                           const std::string &attempt_id,
                           const ledger::PhysicalOutput &output);
  // Created identifies the sole newly persisted terminal result. Replays must
  // not publish a second logical completion notification.
  auto finish_attempt(const std::string &delivery_id,
                      const std::string &attempt_id,
                      const ledger::AttemptResult &result)
      -> ledger::AdmissionResult;
  // Persist safe retry scheduling in the same transaction as the outcome;
  // a crash cannot expose a completed result with an obsolete due time.
  auto finish_attempt(const std::string &delivery_id,
                      const std::string &attempt_id,
                      const ledger::AttemptResult &result,
                      const std::optional<ledger::RetryPayload> &retry)
      -> ledger::AdmissionResult;
  [[nodiscard]] auto load_delivery(const std::string &delivery_id) const
      -> std::optional<ledger::DeliverySnapshot>;
  [[nodiscard]] auto resolve_delivery(
      const std::string &pair_id, const ledger::OpaqueValue &source_scope,
      const ledger::OpaqueValue &target_scope,
      const ledger::OpaqueValue &source_message) const -> ledger::Resolution;
  // Resolve original-source membership without inventing a historical target
  // from current configuration. Multiple destinations remain ambiguous.
  [[nodiscard]] auto resolve_source_delivery(
      const std::string &pair_id, const ledger::OpaqueValue &source_scope,
      const ledger::OpaqueValue &source_message) const -> ledger::Resolution;
  // Observe every confirmed physical output, not only a reverse-primary owner.
  // Previous source-route changes do not invalidate an already observed echo.
  [[nodiscard]] auto resolve_observed_output(
      const std::string &pair_id, const ledger::OpaqueValue &output_scope,
      const ledger::OpaqueValue &output_ref) const -> ledger::Resolution;
  [[nodiscard]] auto resolve_reverse_primary(
      const std::string &pair_id, const ledger::OpaqueValue &source_scope,
      const ledger::OpaqueValue &target_scope,
      const ledger::OpaqueValue &output_ref) const
      -> ledger::OwnershipResolution;
  // Durable checkpoint for a single confirmed-output mutation. Only Created
  // permits a provider call; AlreadyExists proves neither success nor
  // permission to retry. Caller supplies a versioned owning
  // intent/unknown-result envelope.
  // Recheck the selected contribution set atomically with admission; late
  // attachment may not add contributors after a mutation has been admitted.
  auto admit_output_operation(
      const std::string &delivery_id, const ledger::OutputOperation &pending,
      const std::vector<ledger::Provenance> &expected_provenance)
      -> ledger::AdmissionResult;
  // Replace exactly that checkpoint, or accept an identical terminal replay.
  // Intent/result use the same codec/version. No timeout turns pending into a
  // safely retryable mutation; failures leave the checkpoint intact.
  auto finish_output_operation(const std::string &delivery_id,
                               const ledger::OutputOperation &pending,
                               const ledger::OutputOperation &completed)
      -> ledger::AdmissionResult;
  // Original-source mutation, separately stored from physical-output actions.
  // Rechecks the anchor's unique reverse owner atomically before Created.
  auto admit_source_operation(const std::string &delivery_id,
                              const ledger::SourceOperation &pending)
      -> ledger::AdmissionResult;
  auto finish_source_operation(const std::string &delivery_id,
                               const ledger::SourceOperation &pending,
                               const ledger::SourceOperation &completed)
      -> ledger::AdmissionResult;
  // Independent command fence: immutable request/context, owning terminal
  // result. Only Created permits a call; read/replay never implies success.
  [[nodiscard]] auto find_command_operation(
      const std::string &pair_id, const std::string &operation_id) const
      -> std::optional<ledger::CommandOperation>;
  auto admit_command_operation(const ledger::CommandOperation &pending)
      -> ledger::AdmissionResult;
  auto finish_command_operation(const ledger::CommandOperation &pending,
                                const ledger::CommandOperation &completed)
      -> ledger::AdmissionResult;
  void record_output_operation(const std::string &delivery_id,
                               const ledger::OutputOperation &operation);
  [[nodiscard]] auto list_ready_retry_deliveries(std::int64_t ready_at_ms,
                                                 std::int64_t limit) const
      -> std::vector<std::string>;
  void store_retry_payload(const std::string &delivery_id,
                           const ledger::RetryPayload &payload);
  // Remove only redundant bodies for reconciled Complete deliveries. Keep all
  // admission/attempt/output evidence; the explicit limit bounds each batch.
  auto prune_completed_retry_payloads(std::int64_t limit) -> std::int64_t;
  // Clear only inactive, non-resumable bodies; preserve a safe retry with
  // remaining attempts. An inactive unresolved attempt is not replayed.
  auto reclaim_terminal_retry_payload(const std::string &delivery_id) -> bool;
  // Visit at most limit IDs, continuing across polls to avoid starvation.
  auto prune_terminal_retry_payloads(std::int64_t limit) -> std::int64_t;

  auto save_or_update_user(const storage::UserInfo &user_info,
                           bool force_update = false) -> bool;
  auto get_user(const std::string &installation_id, const std::string &platform,
                const std::string &user_id, const std::string &group_id = "")
      -> std::optional<storage::UserInfo>;
  auto query_user_display_name(const std::string &installation_id,
                               const std::string &platform,
                               const std::string &user_id,
                               const std::string &group_id = "")
      -> std::optional<std::string>;

  auto save_sticker_cache(const storage::StickerCacheInfo &cache_info) -> bool;
  // Null expected means insert-if-absent. Otherwise compare every stored field
  // (including missing values) before replacing; never overwrite a newer row.
  auto save_sticker_cache_if_unchanged(
      const std::optional<storage::StickerCacheInfo> &expected,
      const storage::StickerCacheInfo &cache_info) -> bool;
  auto sticker_cache_file_references(const std::string &filename)
      -> std::vector<storage::StickerCacheFileReference>;
  auto get_sticker_cache(const std::string &installation_id,
                         const std::string &platform,
                         const std::string &sticker_hash)
      -> std::optional<storage::StickerCacheInfo>;
  auto update_sticker_last_used(const std::string &installation_id,
                                const std::string &platform,
                                const std::string &sticker_hash) -> bool;
  auto update_sticker_conversion(
      const std::string &installation_id, const std::string &platform,
      const std::string &sticker_hash, const std::string &conversion_status,
      const std::optional<std::string> &converted_file_path = std::nullopt)
      -> bool;

  auto save_qq_sticker_mapping(const storage::QQStickerMapping &mapping)
      -> bool;
  auto get_qq_sticker_mapping(const std::string &source_installation,
                              const std::string &target_installation,
                              const std::string &qq_sticker_hash)
      -> std::optional<storage::QQStickerMapping>;
  auto update_qq_sticker_last_used(const std::string &source_installation,
                                   const std::string &target_installation,
                                   const std::string &qq_sticker_hash) -> bool;

  auto update_platform_heartbeat(
      const std::string &installation_id, const std::string &platform,
      const std::chrono::system_clock::time_point &heartbeat_time) -> bool;
  auto get_platform_heartbeat(const std::string &installation_id)
      -> std::optional<storage::PlatformHeartbeatInfo>;

private:
  struct RetryActivity;
  void track_retry_attempt(const std::string &delivery_id,
                           const std::string &attempt_id);
  std::shared_ptr<RetryActivity> retry_activity_;
  obcx::core::DbManager &db_manager_;
  std::string db_instance_;
  std::string db_namespace_;
};

} // namespace bridge
