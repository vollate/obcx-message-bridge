# Bridge delivery ledger — current schema 5 and historical v4 notes

## Schema 5 cutover

Current source requires SDK/core >=2.7.0,<3.0.0 and Bridge schema 5. All v4
storage/API descriptions below are historical unless explicitly retained here.
The table/journal model retains full scoped members, outputs, attempt outcomes,
retry bodies, revisions and command fences. SourceMember/AlbumObservation no
longer carry scalar legacy_source_key. Policy and EditIntent codecs require
version 2; actual group/topic facts remain explicit, including topic 1 versus
no topic. Publication uses typed references instead of native ID exports.

Runtime constructors reject older schemas and never convert, clear or replay
data. Core scripts/migrate_typed_databases.py is the separately requested offline
conversion utility: it backs up, transforms copies and preserves row counts,
receipts, retry payloads and command fences. Retired tables and unsupported
historical rows are not promoted or deleted. The script was syntax checked only;
its migration/runtime acceptance has not been executed by the assistant.


## Current-only override (2026-11-02)

The legacy-removal change supersedes the historical API/migration descriptions
below. Historical mapping/retry/media-group APIs, all v3/v4 offline upgrade
entry points, quarantine binding/listing and LegacyUnknown restoration have
been deleted. Normal current initialization creates only the current auxiliary
and typed ledger tables. Current validation does not require retired tables,
indexes or write guards. Existing retired data is left untouched, not imported.
The persisted non-typed flag is only a rejection check, never a recovery path.

Current typed attempt/output/source/command/revision/album evidence and safe
retry rules remain. Recall/reaction/userinfo/poke have no historical mapping
fallback. See core's current implementation-status file for matching isolated
build evidence; older products listed below are not acceptance of this change.

## Status and deployment boundary

This is an **unreleased repository implementation**, not deployment or database
migration authorization. Typed forwarding, retry, edit and album consumers are
now connected in the maintained source and compiled in the actual complete
Bridge DSO against frozen SDK2.5.18. Core evidence is
`build/domain-sdk-2.5.18-current/acceptance-scope.md`; subsequent metadata/schema3
loader checks are in `runtime-acceptance-scope.md`. Those checks do not execute
ledger SQL, forwarding, retry, conversion or recovery. Historical storage-only
build records remain historical, not the current complete build. No live
database was opened or migrated. No new tests were written.

`BridgeStateRepository::current_schema_version` is 4. The constructor still only
records the process-owned `DbManager`, instance and actor namespace. Existing
`BridgeActor::resolve_repository` calls `validate_schema()` for reload candidates
and `initialize_schema()` otherwise. The former remains read-only; neither path
upgrades v3. Empty databases can explicitly initialize v4; unknown/unversioned,
partial and non-v4 existing databases fail closed. Earlier v4 candidates missing
`bridge_source_operations` or `bridge_command_operations` also fail validation
rather than being repaired. The owner-only `upgrade_v4_source_operations_offline()`
API takes the migration lock, validates the old base-v4 layout and creates only the
missing source journal. It validates that intermediate layout, not the final
command-journal candidate. `upgrade_v4_command_operations_offline()` separately
requires that source-journal layout, adds only a missing command journal under the
same namespace migration lock and validates that command-journal stage. The
revision and album-observation stages below are also required for the current
candidate; no ordinary startup performs any upgrade stage. Existing malformed journals are rejected, not replaced. These APIs
have been compiled, not executed; neither is a migration CLI or online migration
authorization. The maintained HEAD is still schema3; v4 is the ongoing
change's worktree candidate, not proof that all layouts stamped4 are compatible.
**Do not start the old
forwarder on a new v4 database**: its sends are not ledger-admitted, and its old
mapping/retry mutation calls intentionally fail.

## Suppressed album observations (compiled; SQL not executed)

`bridge_album_observations` stores a scoped observed message, unchanged legacy
source key, original event/correlation/source-bot identity, its position in the
first observed flush (not a native/send ordinal), that flush's first actual event
ID (not an invented batch ID), and a real admitted member used as its group reply
anchor. It does **not** store sent input blocks,
physical outputs, media leases or reverse-primary ownership. Its primary key is
pair + full source scope + full target scope + message reference.

Batch observation and alias insertion share one transaction. An overlapping
batch is suppressed as a whole; newly observed members are retained as group
associations. Repeated ingress retains the first trace. Existing associations
are flattened to a real admitted anchor, never chained. A disjoint batch can
prepare normally. Final album admission repeats the check transactionally after
preparation; a concurrent overlap records associations without admitting a send.
Ordinary source admission cannot promote an association into a new contributor.

Resolution includes associations so a replay is not a false Missing. Reply
selection follows the actual anchor member's current confirmed revision. Edit,
recall and reaction selectors still require actual member/output provenance and
reject association-only sources. Successful delivery notifications list only
submitted members, with each original publication trace; observed-but-suppressed
members are excluded. Retry restores the original immutable batch, not a suffix.

Fresh v4 creates this table. Existing candidates require explicit
`upgrade_v4_revisions_offline()` followed by
`upgrade_v4_album_observations_offline()` under the migration lock. The revision
step validates its intermediate layout; the album step validates the full current
layout. Missing tables, malformed existing columns/indexes, orphan anchors,
conflicting actual membership, noncanonical references and oversized snapshots
fail closed. No implicit repair, history replay, URL reacquisition or write to
legacy mapping tables occurs. Include this table in the full matching database
backup. These changes are compiled in SDK2.5.18's actual Bridge consumer, but
SQL/recovery remains unexecuted and migration is not authorized.

## Command journal and poke consumer

A user-targeted poke is not a message deletion or a created physical output. Its
own `CommandOperation` retains the exact typed SDK request and full owning result.
A stable `bridge-command-v1:` identity hashes canonical `[pair, "qq-group-poke",
original_command_MessageRef]`; neither the selected user nor a changed config
can create a new attempt for that command. Lookup precedes Store attribution so
an expired source cannot cause replay feedback or a new provider call.

Only transactional `admit_command_operation` returning Created permits invocation.
Existing intent must match (excluding terminal outcome), and a second unique key
also prevents different operation IDs for the same command/kind. For v4 attribution,
member/anchor/unique-owner/provenance evidence is rechecked in the same admission
transaction with pair and reversed full scopes. Late output attachment cannot grow
contributors while such a command depends on that binding. Legacy lookup retains
its original keys and is explicitly recorded as legacy evidence, not silently
promoted into a typed delivery or a claim of complete history. Store reads and
Bridge writes do not claim a cross-database atomic author snapshot.

Terminal replacement requires exactly the original pending JSON and immutable
intent, followed by reread; identical terminal replay is accepted, conflicting
results rejected. Pending or uncertain commit never grants a repeat permission.
The poke consumer owns an unknown outcome before invocation and the actual full
outcome before validation/encoding/await. A matching persisted receipt is required
to treat it as successful. Failure after invocation retains that owned result
through feedback and leaves durable uncertainty when it cannot be saved. Replayed
commands perform neither provider calls nor new feedback. This is not a feedback
outbox or provider-side exactly-once guarantee, and pre-journal commands cannot be
retrospectively deduplicated.

The two upgrade stages and command flow have production-object compilation and
static evidence only (`build-dev/domain-migration/bridge-command-journal-v1/`). No
SQL race/crash/provider/loader acceptance has run. Task10.5/11.4 remain open.

## Reaction command consumer

Reaction commands reuse the command journal with kind `qq-message-reaction` and
canonical `[pair, kind, original_command_MessageRef]` identity. Emoji/set and target
changes cannot create another attempt for that command. A complete typed request
vector is prepared before Created admission. V4 targets also require individual
Created source/output-operation checkpoints; output provenance is rechecked at
that boundary. Both command and per-target rows are needed, not interchangeable
proofs. Reaction source mutations legitimately target the original QQ message;
the confirmed TG anchor is attribution evidence, never the mutation target.

`BridgeReactionSourceState`/`BridgeReactionOutputState` preserve the exact request,
original reply, match decision and full SDK outcome. `BridgeReactionOutcome`
retains every planned target, owning outcome or explicit unsent null, match
status, and whether the child result was journaled. Aggregate persistence can
retain a receipt after a failed child write, but the UI still refuses complete
success in that case. If aggregate storage also fails, pending remains uncertain;
no result age or positive partial count authorizes replay or suffix resume.

The shared recall/reaction selector rejects shared or unknown contributions for
member-level output mutations. Historical reverse ownership may still identify
the original QQ source; an old physical-output primary ID without contribution
proof is not sufficient. Historical operations use the independent command record,
not fabricated physical-output/source-delivery rows. Valid repeated commands are
suppressed before selection and do not emit new feedback. This is not a provider
idempotency key or feedback outbox; runtime SQL/crash/provider acceptance remains
unexecuted. No schema change is part of this reaction increment.

## Storage layout

All tables remain actor-owned `bridge_*` tables in the configured process DB
instance. No private SQLite connection, service, configuration option, retention
default, package change or CMake change is introduced. The new implementation is
included by the existing repository translation unit.

| Table | Evidence / identity |
|---|---|
| `bridge_deliveries` | Immutable serialized admission (pair, exact opaque scopes, explicit policy/limits, timestamp and bounded attempts), quote anchor, historical-completeness flag and consumed imported retry budget |
| `bridge_delivery_members` | Source ref, unchanged legacy source key, optional media-group ref, input block IDs; unique `(pair, source scope, target scope, source message ref)` across deliveries |
| `bridge_delivery_attempts` | Admission committed before provider call; stable caller attempt ID, sequence/time, nullable terminal result |
| `bridge_delivery_outputs` | Every confirmed ref, attempt, ordinal, optional many-to-many member/input provenance, reusable media, independent reverse-primary owner; one physical ref per delivery, unique ordinal within an attempt |
| `bridge_output_operations` | Confirmed-output mutation intent and exact terminal replacement; immutable operation ID/target identity, versioned owning result |
| `bridge_source_operations` | Original-source mutation: operation/member/actual source MessageRef plus the confirmed reply anchor proving reverse ownership; separate intent/terminal evidence, never a fabricated physical output |
| `bridge_command_operations` | Independent command identity `(pair, operation_id)` and `(pair, command_ref, kind)`; immutable typed request, scopes and attribution evidence; optional v4 source binding; null outcome means possibly invoked, not retryable |
| `bridge_delivery_retries` | Outer payload format version **1**, typed body, exact target, media/lease set, total budget, original/scheduled time and count |
| `bridge_legacy_delivery_evidence` | Lossless v3 mapping/media-group rows proving their one stored output and source relation, with domain refs unresolved |
| `bridge_retry_quarantine` | Lossless v3 retry row, quarantine reason, optional audited release binding; never deletes the original evidence |

The schema enforces identities using unique constraints. Repository transactions
validate member/provenance ownership and prevent conflicting reverse-primary
owners in one exact pair/scope. Startup checks required columns, ledger unique
keys (non-partial indexes only), missing parent/attempt relations, v3
reverse-primary validity, and each legacy write guard's table and exact canonical
SQL definition. Validation never writes probes or repairs malformed guards. It is not a comprehensive forensic validator of arbitrarily
modified JSON blobs; deserialization/consistency failures must fail closed.

`ledger::OpaqueValue {codec, version, bytes}` is **not** a replacement SDK ID or
platform grammar. Storage verifies only nonempty bytes/codec and positive version.
The forwarding owner must use the frozen SDK's canonical serialization for full
installation/conversation/topic/message/media refs, validate scope before writes,
and use one canonical codec version consistently. Different encodings of the same
semantic ID are not interchangeable unique keys. No generated `legacy-v3:*`
delivery ID is an SDK ref; that prefix is reserved for migration evidence rows.

Opaque bytes are binary-safe, including embedded NUL and invalid UTF-8. Their
storage JSON is `{\"storage_format\":1,\"codec\":\"...\",\"version\":1,\"bytes_hex\":\"...\"}`:
`bytes_hex` is nonempty, even-length lowercase hexadecimal; `codec` and `version`
remain the caller's SDK serialization identity. The storage format is separate
from that codec and the outer retry payload version. Decode rejects other fields,
unknown storage formats, uppercase/invalid hex and nonpositive codec versions;
there is no lossy UTF-8 replacement or implicit legacy text fallback. The caller
still applies its explicit SDK/body budgets; hex storage requires two characters
per byte. Pre-release text-only v4 envelopes are not accepted as this format.

## Current SDK binding increment (not a complete forwarder)

`bridge_delivery_sender.cpp` is part of the real bridge_core target. Its first-send
entry is exposed through BridgeBotOperations; actual ordinary QQ/TG preparers
still use predecessor calls and must be rewired. The actor confirmation handler
is an observation-only source path, not a producer or logical-success publisher.
The new sender and this repository compiled as actual production objects against
frozen SDK 2.4.1; the whole actor build fails on the old TG operations header.
See core build-dev/domain-migration/bridge-delivery-binding-v1/ for the failed
whole-build log and precise partial-object evidence. No SQL/actor execution.

Binding uses the frozen SDK DTO JSON shape encoded deterministically by its CBOR
encoder: codec `obcx.bot.<type>.cbor`, version 1. Conversations/messages/media
retain full scopes. Schema/body/reference changes require reviewing this codec
identity rather than reinterpreting old opaque bytes. The BridgeDeliveryPolicy codec label describes an actor-owned envelope, not a
new SDK operation/type. Attempt policy stores
explicit limits/reply/quote/provenance metadata, not original raw archives or
image bytes. There is no retry-body/lease serialization implementation yet.

Source archive conversion is a pure consistency check, never new live admission.
The new has_legacy_output gate checks both old normal mappings and album members
by their original source and target fields (not by an inferred album ID). Any
match refuses new submission, including historical unknown-topic evidence. It
is conservative blocking, NOT a completed old-output domain binding/import.
Missing/Unique/Ambiguous/Corrupt native resolution remains distinct. Only Created
admission plus Created attempt permits the SDK call; no admission replay sends.

All final output details and full SendOutcome are retained. Separate notifications
retain complete SDK detail observations plus available published-media metadata,
with unknown provenance until request-aware terminal reconciliation. The nullable
ordinal is essential: notification arrival order does not prove send order.
Unknown-to-known enrichment updates SQL and JSON together; two known unequal
ordinals conflict; null observations do not erase known ordinals. Complete
terminal results require dense confirmed sequence order. Reverse-primary is
established only by single-member known provenance, not by quote-anchor choice;
shared/unknown contributors never gain fabricated ownership. Partial text slices
of shared prepared blocks remain unknown. The persisted snapshot, not raw SDK
retryability alone, is authoritative after independent confirmations.

This changes the unpublished v4 table definition to allow NULL ordinal. Startup
rejects older pre-release v4 NOT NULL ordinal tables and does not auto-upgrade
them. Existing integer ordinals remain readable in the JSON envelope; absent old
SDK detail evidence stays absent. No existing DB was opened, migrated or repaired.
An authorized offline upgrade procedure is still required for any such old v4
copy; lowering a version or removing guards is not an alternative.

The caller gets newly_completed only for a newly persisted complete outcome with
a Complete snapshot. Observation/replay cannot publish. Persistence exceptions
retain the owned complete SDK outcome while the admitted attempt prevents replay.
The existing actor boundary also suppresses repeated AlreadyPersisted success
notifications and makes this persistence exception nonretryable (source-only).
Preparer integration, retries, leases and final event publication remain open;
this API is not permission to run the predecessor forwarder on v4.

## Native API and integration protocol

Definitions: `include/bridge_delivery_ledger.hpp`; repository entrypoints:
`include/bridge_state_repository.hpp`.

1. Resolve legacy evidence first when an incoming source has an old storage key
   (see migration below), then `resolve_delivery(pair, source_scope, target_scope,
   source_message)`. Preserve Missing / Unique / Ambiguous / Corrupt; only a
   genuinely new identity can proceed to admission. An absent mapping is not
   evidence that an already admitted delivery was never submitted.
2. `admit_delivery(Admission)` commits the logical delivery and all members in one
   transaction. Repeating the identical ID/admission returns `AlreadyExists`;
   different data under the ID or a source already claimed in the exact scope
   fails. A competing fresh delivery ID cannot bypass native member deduplication.
   Explicit `max_attempts` bounds total logical SDK send attempts, not a default
   or a count of the adapter's internal HTTP steps. SendLimits independently
   bounds physical output messages.
3. `store_retry_payload` can preserve preparation body/leases before first send,
   but such preparation alone does not appear in the automatic retry list.
   Payload contents/leases/total budget cannot be replaced or counts reduced.
4. `admit_attempt(delivery_id, attempt_id, started_at_ms)` is the sole submission
   admission. **Only `Created` permits a provider call**. `AlreadyExists` means
   do not send, including after a crash. Different retry attempt IDs are allowed
   only when every previous attempt proves definitely-not-submitted, the latest
   result is retryable, no output exists, and budget remains. Caller executes on
   its blocking executor and must stop before provider invocation if admission
   persistence fails.
5. `record_confirmation` retains each output by attempt/ref. Unknown provenance
   is `nullopt`, never an invented association or empty known contributor set.
   Known metadata may enrich unknown metadata; old subset notifications after a
   proven attachment do not remove richer associations. Conflicting ordinals,
   owners or non-subset provenance fail rather than being silently overwritten.
6. `finish_attempt` atomically merges all output refs and records terminal result,
   explicit quote anchor and complete/partial/possible-effect evidence. Repeating
   an identical terminal result returns `AlreadyExists`; a conflicting terminal
   result is rejected. Extra independent confirmations do not roll back the
   returned outcome or its other confirmed refs, even when it claims completeness.
   Both sets remain stored; snapshot reconciliation marks their disagreement
   Uncertain regardless of arrival order. `Created` identifies one new terminal
   result, not itself a successful send. Publish logical success only for a complete result whose
   persisted delivery snapshot is `Complete`; never for confirmations alone.
   The forwarding owner must handle its notification delivery protocol. No
   transactional event outbox/exactly-once event publication is claimed here.
7. `load_delivery` returns members, outputs, attempts/results, output-operation
   evidence, quote anchor and current safe-retry classification in one serialized
   read snapshot. Unfinished admitted attempts read as `Uncertain` even if still
   active: storage cannot distinguish a live submission from crash recovery.
   Contradictory late confirmations are retained and make the snapshot uncertain;
   no-effect results cannot erase confirmed effects. Confirmed-only failures are
   `Partial` only when each attempt's returned refs agree with its accumulated
   confirmations; additional contradictory refs make even a partial result
   `Uncertain`. Possible effects are `Uncertain`. No retry removes effect evidence.
8. `resolve_reverse_primary` is independent of quote-anchor selection and returns
   the four resolution statuses. Unresolved ownership is Corrupt, not Missing.
   Unknown provenance cannot authorize member-level edit/delete. Shared outputs
   require reconstruction or explicit refusal by forwarding. For new mutations,
   `admit_output_operation` atomically compares the caller's selected nonempty
   provenance against the current output and persists a caller-owned versioned
   intent/unknown checkpoint before the provider call. Only `Created` authorizes that call;
   `AlreadyExists` proves neither success nor permission to resume/retry.
   `finish_output_operation` compare-and-sets the exact checkpoint to an owning
   terminal envelope with the same operation ID/ref/codec/version. Exact terminal
   replay returns `AlreadyExists`; missing admission or conflicting state fails.
   Both writes are transactional and verified by reread before returning. A
   commit/persistence failure never authorizes repeating the provider call.
   The existing `record_output_operation` remains exact-replay-only, not an
   alternative to admission for new effects. These APIs use the existing table;
   no schema/column/default or persistent key recipe is changed.
9. `list_ready_retry_deliveries(ready_at_ms, explicit_limit)` lists only safe native
   retries and audited released legacy preparations. It preserves ordering by
   retry time; listing does not authorize sending. Reload and atomically admit a
   new attempt immediately before any submission. Never delete a retry row as a
   substitute for resolving possible effects. All media/lease release decisions
   remain with the resource owner; this repository only durably retains evidence.

### Member mutation selection and current wiring

`bridge_delivery_member_outputs` is a pure domain-codec selector for every known
confirmed output exclusively attributable to a requested source member. It
validates complete scopes, canonical member identities, original-member
consistency, input-block membership and all available provenance before returning
any plan. Unknown provenance rejects the whole selection. Outputs shared with
the requested member require reconstruction/refusal; known unrelated outputs are
not selected. It does not use the quote anchor or infer contributions from the
reverse-primary field. Returned known outputs do not prove that an uncertain
or historical delivery has no additional outputs.

Admission is immutable. Original members must remain identical, but
`append_delivery_members`/`attach_proven_member` can legitimately add member rows.
The selector permits these rows only as candidates whose exact output provenance
must establish contribution. Reverse-owner lookup similarly checks original
members against admission and requires provenance for members absent from the
initial list. The prior source-equality check that rejected all such late members
was too restrictive and is corrected in the operation-admission increment.

The repository APIs and selector are actual production code compiled against
frozen SDK2.5.2. `/recall` now uses them for TG source -> QQ physical outputs.
Selection of all known exclusive outputs precedes effects. The first output's
operation ID is a stable hash of pair/delivery/original command MessageRef, not of
the changing output set/order. Its intent records the complete selected plan and
quoted source. Every later output gets its own operation/result record. Existing
admission blocks the entire replay, including suffix continuation and fresh
feedback. No timeout unlocks a pending operation.

Full typed delete results remain owned if a later call or finalization fails.
Definite rejection can continue to another independent target; uncertainty,
wrong receipt, cancellation or storage failure stops. Completion feedback requires
matching receipts and persisted results for every selected output and only claims
those *known* outputs. There is no atomic command-wide deletion or feedback outbox.

To close the selection/admission race, expected provenance is checked in the
admission transaction. Once an output has an operation record,
`attach_proven_member` accepts only existing exact members and no new contributing
origins. Unchanged/subset contribution replay remains valid; adding a contributor
requires explicit reconciliation rather than changing a mutation's authority
while the provider is awaited. Schema and ordinary confirmation/send writes are
unchanged. This is an implementation invariant, not an executed SQL/race proof.

Original-source recall now has a separate `SourceOperation` and journal. The
command resolves the original QQ member from the quoted TG output's unique reverse
owner and checks current configured QQ/full TG scope. Source admission rechecks
member SQL/data/admission identity, confirmed anchor/attempt, unique observation,
reverse owner and available provenance in the INSERT transaction. A source absent
from initial admission needs provenance; an audited initial legacy owner may lack
fine contribution evidence, as in source attribution. This does not grant permission
to mutate shared *outputs*.

`message_ref` is the actual QQ source to delete; `anchor_ref` is only the observed
TG reply context. The full typed outcome is stored in `BridgeRecallSourceState`
CBOR/version1, not in an output-operation row. Snapshot readers expose source
operations separately. Anchor contribution growth after source-operation admission
also needs reconciliation; exact/subset existing-member replay remains subject to
the prior guard. Source replay, cancellation and persistence failures use the same
no-resume/owned-outcome loop, with separate source admission/finalization methods.
Source success only reports QQ original deletion, not deletion of all TG copies.

Existing output records, their codec/keys, send attempts/budget and the historical
v3 evidence are not rewritten. The new journal invents no evidence about old
source commands that predate it. Historical-only fallback still warns of incomplete
records and does not recommend retry after possible deletion/persistence failure;
its binding/recovery migration remains open. SQL/race/crash/provider acceptance is
unexecuted; task10.5/11.x remain open.

### Late source members

`append_delivery_members` adds new members only before the first admitted attempt;
afterward it permits only identical existing-member replays. Admission request
content remains immutable. `attach_proven_member` atomically attaches a member
and exact provenance to an **already confirmed** output, preserving prior known
contributors. A matching album ID is not proof. It does not authorize submission.

A wholly disjoint observed flush can form a separate bounded delivery with the
same exact-scoped media-group reference. An overlapping flush is suppressed as a
whole: it MUST NOT be reduced to an unseen suffix and sent separately. New
observations in that flush are retained only through the association table above,
not as proven content contributors. The 1500ms aggregation window, arrival order,
first-observation deduplication and explicit flush-on-close remain unchanged.
There is no implicit continuation/replay policy after partial or unknown effects.

## Explicit offline v3 import

1. Stop at planning/review unless separately authorized to operate on a named,
   disposable, offline copy with backups. Use the process `DbManager` configured
   for that copy and the actor's explicit DB namespace; never invent a connection
   or use the running actor's DB instance.
2. `inspect_v3_migration_candidate()` only reads schema/version/shape and counts.
   Exactly one version row equal to 3 is required. Existing v3 reverse-primary
   validation is retained. Corrupt mapping ownership or preexisting partial v4
   tables abort rather than being repaired. Retry scope/effect ambiguity is not
   assumed away.
3. `migrate_v3_to_v4_offline()` repeats validation inside the process migration
   lock/transaction, creates v4 ledger tables/guards, imports each mapping and
   media-group row losslessly, quarantines every old retry, updates the version
   and validates the shape atomically. It never calls the provider, reschedules
   a retry, invents a target ID, parses CQ payload into domain values, or searches
   for missing historic outputs. The API name expresses an operator precondition;
   it cannot itself discover whether an arbitrary configured DB is online.
4. Each imported mapping/media row is a `LegacyUnknown` delivery containing exact
   original evidence, **not** a fabricated typed physical output. Its one stored
   target ID is proven; hidden old outputs are not. All source/target fields,
   keys, primary flags, group relations and timestamps remain in original tables
   and evidence JSON. No quote anchor is inferred from `is_primary`.
5. v3 mapping/media/retry tables get INSERT/UPDATE/DELETE abort guards. Legacy
   repository read APIs remain for evidence/identity binding. Legacy mutation
   methods remain link-compatible but fail on v4. Old pending-retry enumeration
   also fails on v4. User/sticker/cache/heartbeat APIs remain writable.
6. `list_quarantined_retries()` exposes full evidence without making it runnable.
   `bind_quarantined_retry(binding)` requires an explicit audited versioned proof
   of exact installation/conversation/topic/content binding and zero confirmed or
   possible effects, a complete native admission and version-1 retry payload.
   The storage layer does **not** perform this semantic SDK/platform proof. It
   refuses known matching v3 mapping/media outputs, preserves original schedule,
   retry count and maximum, counts consumed legacy retry budget without fabricating
   historical attempts, and commits binding + admission + payload atomically.
   Missing/ambiguous topic or effect evidence stays quarantined. Rebinding with
   different proof/content fails. Quarantine-all is an approved staging choice,
   **not** the final retry migration policy.

### Legacy binding gate: source-wired, not runtime-accepted

Opaque reference equality alone cannot match unresolved v3 fields.
`send_bridge_delivery` now validates each member's original archive and legacy
key through ReferenceResolver before admission, and calls `has_legacy_output`
for every member and the unchanged legacy target. The repository checks both
legacy mapping tables; any matching old chat/group output blocks a new send,
including when topic evidence is incomplete. These v4 legacy tables are guarded
read-only, so the lookup does not authorize a race with ordinary legacy writes.
The low-level admission API by itself is still not a legacy semantic decoder.

Legacy outputs remain old mapping/LegacyUnknown evidence, not fabricated complete
native output sets. No current runtime automatically binds quarantined retries
or obtains missing topic/old media authority. An approved proof-driven import
must preserve original keys and budgets. This gate still requires offline-copy
replay, corruption and concurrent-admission acceptance; source presence is not
proof that historical edit/delete or replay safety has passed.

## Crash recovery and cache retention

Recovery must use complete persisted evidence, never elapsed time or a fresh
provider lookup to reinterpret an uncertain send:

| Persisted state | Recovery behavior |
| --- | --- |
| Delivery admitted, no admitted attempt | Ordinary replay returns the existing snapshot; it does not infer a new Created admission or send. |
| Attempt admitted, terminal outcome absent | Keep unresolved submission and all confirmations; do not send again or resume a suffix. |
| Complete zero-effect failure | Retry requires all recorded outcomes, retryable + DefinitelyNotSubmitted, matching current route, remaining budget and due time; admission is transactional. |
| Complete or partial confirmed output | Preserve every output and provenance; replay does not resend. Notification failure is not a retry permission. |
| Edit revision or mutation pending | Preserve revision/operation identity and old-output evidence. Only an independently eligible replacement retry may proceed; settled deletes are not repeated. |

Cache files are not a substitute for delivery/attempt evidence. Current typed
preparation returns owned bytes, and retries retain typed owned payloads rather
than temporary cache paths. Cache writes use full-row CAS and never overwrite an
existing filename. Worker-local reference guards run before inode cleanup even
when conversion, allocation, cancellation or an uncertain database commit throws.
A referenced file, or one whose reference state cannot be read, is retained.
Cleanup failure/deferral and process-crash orphans are not silently repaired at
startup. There is no automatic orphan sweep, TTL or filename change.

For a separately authorized offline recovery:

1. Identify the disposable database copy, namespace and every installation/cache
   root sharing it. Keep matching configuration, complete DB/WAL state and file
   backups; do not inspect or modify the running actor's connection.
2. Preserve delivery/revision attempts, confirmations, operation journals,
   association rows, legacy evidence and retry quarantine together. Never lower
   a schema version or remove guards to make an old binary start.
3. Inventory cache original/converted paths and private `.prepare-*`/`.convert-*`
   entries against all relevant namespaces and path mappings. A filename, age,
   missing row or old key alone is not ownership or deletion proof.
4. Retain referenced and uncertain files. Before any proposed cleanup, prove no
   live worker/provider/path owner remains and obtain authorization for the exact
   entries. Current code supplies no crash-orphan automatic recovery guarantee.
5. Validate the copy with the matching build and inspect unresolved effects before
   permitting service restart. Do not requery stale URLs, manufacture leases or
   turn an incomplete legacy/revision result into a runnable retry.

No step above has been executed for this change. This is an operator procedure,
not permission to run SQL, remove files, deploy or restart.

## Rollback

Before any v4 runtime writes, restore the approved complete offline backup to
return to v3. There is deliberately no automatic down-migration or version-only
rollback. After v4 sends, v3's single unique slot cannot represent all outputs,
unknown attempts, source provenance or retry evidence. Downgrade requires an
independently reviewed data procedure (or continued v4-capable reader); restoring
an old snapshot alone would forget real effects and can cause duplicate sends.
Do not drop the guards or lower the version merely to run old forwarding code.

## Owner-local acceptance tasks

- Implemented: repository schema/API, read-only candidate inspection, explicit
  transactional offline import, retry quarantine/binding, legacy write guards.
- Checked: predecessor frozen-SDK actor compilation through `actor_build.py`,
  20 workers; `git diff --check`; baseline index hash preserved.
- Independent review identified binary-string persistence and malformed candidate
  index/trigger acceptance. The parent implemented a versioned binary-safe
  envelope, non-partial index checks and exact trigger-definition checks;
  predecessor-SDK recompilation passed through `actor_build.py --jobs 20`
  (`bridge-ledger-build/build-review-fixes.log`, 21 build steps). Independent
  follow-up review closed all three findings (OK with notes); no binary
  round-trip execution, SQL validation or DB migration was claimed.
- Built: complete real Bridge DSO against frozen SDK2.5.18/input-schema3;
  metadata and production loader discovery passed. Complete preparation with
  business configuration, database/runtime/media/recovery acceptance is pending.
- Source-wired, not accepted: SDK serialization, legacy duplicate gate, typed
  forward/retry/edit/album integration and notification publication. Cache
  exceptional-exit reference protection is also uncompiled/unexecuted.
- Pending: authorized offline-copy migration/recovery/partial-output/late-album
  existing checks. No runtime/SQL migration test has been run in this lane.
- Pending: platform image/edit/delete acceptance; no TG deletion ingress claim.
