# Domain SDK migration — Bridge owner

## Current SDK 2.7.0 / Bridge schema 5

This section supersedes all older build, schema-4, conversion and migration
instructions below; those sections are historical evidence only.

Bridge now uses current ConversationResolver configuration, scoped references,
typed Store schema-5 messages and explicit current QQ/TG views. Generic reference
conversion and native-scalar ID round trips are removed. QQ profile/member native
selectors are query intent; a matching provider response supplies user facts.

Bridge schema 5 requires policy/EditIntent codec 2. Source members and album
observations no longer store legacy_source_key; publication and retry do not
restore old IDs/addresses. Actual group/topic facts preserve topic 1 versus
absent topic. MessageForwarded and durable persistence failures use schema 2
with typed source/target/output/anchor values. Full receipts, unknown effects,
exclusive provenance, revision deletion evidence and command fences remain.

Runtime startup rejects schema 4 without mutation. The core standalone Python
utility can explicitly convert current typed ledger copies, preserving attempts,
receipts, retry payloads, command fences and retired tables. It accepts already
current Store/Chat namespaces without reconversion. It has only been written and
syntax checked, not executed by the assistant. No automatic migration, replay,
provider query, database clearing, deployment or restart is included.

Use a frozen matching SDK/core >=2.7.0,<3.0.0 and scripts/actor_build.py, not actor
targets in the live core workspace. See core packaging/actors/domain-sdk-cutover.md
for offline CLI, backup/rollback boundaries and operator-owned runtime acceptance.

## QQ voice preparation

Ordinary and merged-forward QQ Voice attachments now share queued SILK decoding,
Ogg/Opus conversion and full output validation before delivery. Failures stop
before upload; music/files and TG → QQ remain unchanged. See
[QQ voice preparation](qq-voice-preparation.md) for runtime requirements,
resource/cancellation behavior, packaging and decoder license details.

## Current-only SDK transition

The operator has withdrawn legacy support; core's `legacy-removal` change
supersedes earlier compatibility promises below. The 15 uncompiled historical
formatter/media/handler/retry implementations and nine exclusive headers have
been deleted, not retained as migration evidence. Current command scopes use
explicit configured targets. Received-message reads now require Store schema
5 typed snapshots. QQ special content and Telegram profile/update evidence are
captured at ingress; forwarding, recalls and revision publication no longer
restore native archives. Legacy media associations, TG archive file imports and
QQ historical image-cache recipes are removed; current receipt ownership
remains enforced. Historical mapping/retry/media-group APIs, offline import/
upgrade entry points, quarantine binding/listing and command fallbacks are now
removed. Existing retired tables are not queried, created, cleared or deleted.
Unsupported non-typed admissions are rejected rather than restored. Current
retry safety still requires actual typed attempt evidence. Reply userinfo/poke
uses exact typed Store messages and senders. ID/configuration and publication/
retry conversions remain tracked; previous builds do not validate this work.

## Latest implementation handoff (2026-11-01)

The active typed forwarding, ledger, media and command migration is implemented
and built. The operator requested no new verification programs/tests and will
test after deployment. Earlier storage-only/partial-build notes below are
historical, not the current implementation state; unexecuted runtime checks are
still not passed.

This round also stops cancelled `/recall`, `/react`/`/unreact` and `/poke` from
sending feedback. Full results and existing journal settlement precede
cancellation propagation; no changes to keys, schema or retry permission.

Matching new frozen SDK2.5.19 and six-actor build products are under core
`build-dev/domain-implementation-final/`; core change `implementation-handoff.md`
lists the executable, limitations and operator-owned acceptance. This SDK
includes the core receipt/empty-photo fixes absent from the older snapshot.
Twenty-worker actor_build compiled 90 production actor/library objects and linked
all six DSOs. No tests, provider/database operation, deployment or restart.
QQ common ordinary Audio/File and TGS limitations remain explicit; no new
nonstandard fallback was introduced.

## Earlier scope and state

Bridge keeps ownership of forwarding/display policy, pairs, rich-media
conversion, album buffering, persistence and retry/resource lifetimes. The
approved core `unify-bot-sdk-domain-api` change supplies the shared SDK contract;
actor business specifications remain in this repository.

The schema-4 storage component is implemented, compiled against the unchanged
predecessor SDK and independently reviewed after fixes. That is **not** a runnable
v4 forwarding migration. See [the ledger handoff](bridge-ledger-v4.md) for exact
APIs, explicit migration, legacy binding gaps and rollback requirements. Do not
start the old forwarding path against v4 merely because storage compiles.

Target: SDK/core `>=2.5.18,<3.0.0`, input contract schema **3**, scheduler ABI and
package manifest schema **2**; DB schema **4** is a separate boundary. No live
DB/config changes, deployment, restart, commits, new unit tests or configuration
defaults are included. TGS playback and complete Telegram delete ingress remain
explicitly unverified/incomplete.

## Configuration target validation (2026-11-01)

Generation preparation now resolves every Telegram command route's QQ target
through `CommandScopeResolver`, before the validation-only return and any DB or
forwarder construction. Disabling QQ-to-Telegram forwarding removes the QQ
status-command source scope, but must not hide the destination of Telegram
commands or Telegram-to-QQ forwarding from native scope validation. Pair IDs,
original group strings, None/Exact/Any availability and forwarding switches are
unchanged. No new options, defaults, database access or provider I/O were added.

The actual Bridge actor and its Message Store dependency compiled/linked against
a new frozen snapshot of the existing SDK2.5.19 core using `scripts/actor_build.py`
and 20 workers. Chat LLM, ExHentai and the QQ probe also built successfully in
that isolated actor workspace. Evidence is in the core repository's
`build-dev/domain-scope-2.4/{configure.log,build.log}` and
`openspec/changes/unify-bot-sdk-domain-api/config-scope-review.md`.
This does not claim execution of validation-only cases, runtime ACL/reload,
provider behavior or schema migration; the schema-v4 guard remains unchanged.

## Legacy/configuration address separation (2026-11-01)

`BridgeCommandOperations` now passes the explicit configured Telegram group
kind to the reference resolver in both construction and command target lookup.
The old `chat:<id>` spelling remains the persistent mapping identity; it is not
used as an ambiguous configuration address. Topic selection, pair ownership and
allowed-target checks are unchanged. The actual production TU and Bridge DSO
were rebuilt against frozen SDK2.5.19 with 20 workers; core build evidence is
`build-dev/domain-scope-2.4/legacy-fix-build-complete.log`.

Separately, the user approved an offline backup/copy migration and replacement
of the named `bridge_bot.db`. The existing owner migration API validated v3,
imported 283301 mappings as legacy-unknown evidence, quarantined 592 retries,
and validated the complete v4 candidate. Old table contents, imported evidence,
write guards and SQLite integrity were checked before replacement. The original
backup and operation receipts remain under the restricted directory
`/home/vollate/Backups/OBCX/bridge-v3-v4-20261101T102131Z/`.
No process was started, no retry rebound and no message sent. This particular
approval is not blanket deployment/data authorization or crash-recovery
acceptance; historical replay and failure-injection gates remain open.

## Attachment encoding handoff (2026-11-01, partial)

The shared `read_bridge_attachment` no longer discards an available typed MIME
hint when the reader cannot supply one. If neither supplies MIME, it recognizes
limited MP4/Ogg-Opus/MPEG-audio byte signatures using the original media kind.
This fixes supported QQ audio/video bytes being rejected solely for missing
MIME during Telegram upload preflight. Unknown encoding stays unknown, Voice
is not converted to ordinary Audio, and the destination still validates bytes.
No transcoding, raw-URL fallback, new configuration or send retry is introduced.

The production TU and Bridge DSO were rebuilt using the frozen SDK2.5.19 and
20 workers; evidence: `build-dev/domain-scope-2.4/media-metadata-build.log`.
See the core change's `media-read-review.md` for the source-kind matrix and
remaining provider, playback, cancellation and historical acceptance gates.

## Typed entry retirement (2026-11-01)

`BridgeActor` no longer writes a v3 scalar mapping from `BridgeForwardResult`
or from target fields in a MessageStored payload when no forwarder is present.
Only complete durable v4 delivery/revision evidence can publish logical success;
missing runtime services and scalar predecessor results fail without permitting
whole-message replay. Unmatched routes, deferred album ownership and already
completed durable results retain their no-notification behavior. The legacy
mapping tables and offline migration APIs are unchanged.

RawMessage/BotMessageSent/heartbeat remain activity observations; physical
confirmations validate their installation/attempt and update the ledger only.
The active ordinary entry points preserve exact pair/topic/command scopes and
never fall back to the retired raw formatter/bot/retry files excluded by CMake.
Explicit proprietary and legacy restoration boundaries remain supported.

Seven actual production TUs and Bridge DSO rebuilt against frozen SDK2.5.19 with
20 workers (`build-dev/domain-scope-2.4/typed-entry-build.log` in core). The core
change's `bridge-entry-review.md` records source/build closure of tasks 9.1/14.3,
including the active six-consumer source scan. This does not establish runtime
notification counts, crash recovery, provider or complete media acceptance.

## Text/photo follow-up (2026-11-01)

Typed sender/caption/mention rendering was checked against predecessor display
choices. QQ keeps its italic prefix, original show-sender control and scoped
member cache; TG explicitly renders plain text and preserves observed-profile
fallback. Source mentions are display Text, not fabricated destination pings.
Both active forwarders were rebuilt; core task 9.2 closes only this source/build
scope (`bridge-text-review.md`), not provider rendering or history acceptance.

Photo preparation follow-up fixes:

- Configured placeholder HTTP acquisition now uses the smaller of the configured
  ceiling and the current slot's remaining item/total budget, before downloading;
  full-resource reads require HTTP 200. Embedded fallback and cancellation rules
  are unchanged.
- Queried-forward recovery captions count adjacent static-photo runs in actual
  attachment order. Audio/video/file boundaries and standalone GIFs no longer
  get merged into a flattened photo batch. The 10-item/no-singleton-tail rule
  and source order remain unchanged.

Both actual preparer TUs and Bridge DSO rebuilt with frozen SDK2.5.19/20 workers;
core evidence is `build-dev/domain-scope-2.4/photo-preparation-build.log` and
`bridge-photo-review.md`. Full image/FFmpeg/provider/cancellation, concatenated
forward/ordinary-body grouping and long-caption/provenance acceptance remain
open; tasks 9.3/9.4 are not closed by this build.

## Delivery reconciliation and retry review (2026-11-01)

The complete sender/ledger/confirmation/publication chain and typed retry worker
have now been reviewed against the active production build. Core tasks 11.2/11.3
close for implementation/source/build only; `bridge-delivery-review.md` records
all refs/details, unknown provenance, pre-submit attempt admission, exact replay
suppression, zero-effect-only retry and generation ownership. Active preparers
supply owned bytes; the durable retry serializer does not invent external file
leases or reacquire HttpSource/LocalFile inputs. Legacy quarantined retries were
not rebound or executed.

A concrete reconciliation bug is fixed: extra confirmations arriving before a
nominal complete result used to roll back that result and its newly returned
refs. Both independent observations and the full outcome are now retained.
Per-attempt output disagreement becomes Uncertain for complete and partial
results alike; it cannot authorize logical success or whole-delivery retry.
Schema, codec, keys and configuration remain unchanged.

`build-dev/domain-scope-2.4/delivery-reconciliation-build.log` records the actual
repository TU rebuild and Bridge relink through actor_build, frozen SDK2.5.19,
20 workers, with five selected targets successful and SDK unchanged. The matching
verification JSON records reviewed sources and production objects. No database,
provider or actor was executed. Tasks 11.4/11.5 retain DB fault/crash, cancellation,
concurrency and file/cache lifetime acceptance; this is not exactly-once event
publication or a runtime success claim. Earlier "not built" sections below are
historical, not the current build status.

Task 9.4's ordinary 1/2/10/11/21-photo, first-output reply, mixed-body and long
caption source matrix is recorded in `bridge-photo-review.md`; provider/media
execution remains open. The active path is `qq/text_forwarder.cpp`, not the
excluded predecessor `qq/photo_delivery.cpp`.

## Animation/sticker source/build closure (2026-11-01)

Core task 10.2 now closes for implementation/source and production-build scope.
The explicit per-format matrix is in core change
`bridge-animation-sticker-review.md`: QQ face/mface/shared GIF preparation,
paired legacy cache lookup, TG GIF/MP4-as-Video/WebM conversion, static WebP→PNG,
and actor-owned cache/CAS/worker cleanup. TGS remains explicitly unsupported;
other formats' playback, FFmpeg, provider and cache-concurrency behavior remain
unverified, not claimed successful merely because they compile.

Per the latest operator request, successful static and converted-video sticker
media now keep only the genuine source caption. Bridge no longer adds
`[贴纸 🤔]` or sticker/emoji/video labels. Existing failure/TGS notices and sender
prefixes are unchanged; user-authored text is not stripped. This supersedes
historical successful-label preservation descriptions below.

actor_build with frozen SDK2.5.19/20 workers rebuilt the actual sticker TU and
linked Bridge; all five selected targets passed. Fourteen source/header and
thirteen production-object hashes are in core evidence
`build-dev/domain-scope-2.4/sticker-animation-verification.json`. SDK and operator
core remained unchanged. No new tests/defaults/dependencies, DB/provider/cache
operations, deployment or restart. Full media and recovery acceptance remains
open independently of this source/build closure.

## Telegram receipt normalization and incomplete diagnostics (2026-11-01)

The operator's repeated `bridge_delivery_incomplete` report was investigated with
explicitly authorized read-only attempt-state/error queries. All 20 attempts in
the sampled interval had one confirmed output and the SDK's malformed-batch
error. No message body, database write or resend was involved.

The common Telegram send adapter compared returned text/captions literally with
Bridge's original tab-containing sender prefix. The core correction accepts
Telegram's tab-to-space and trailing space/newline normalization while retaining
all other content/scope/count/provenance checks. Bridge's submitted body and
rendering policy are unchanged. Its incomplete error now exposes the redacted
SDK reason and confirmed/possible-effect counts instead of hiding that reason.
The same error code/non-retryable ingress result and durable retry owner remain.

Core/app built successfully in a new isolated directory with 20 workers. The
Bridge entry TU/DSO also rebuilt via actor_build against frozen SDK2.5.19; all
five targets passed. That old SDK does not contain the core fix. Full core
activation/restart is separate from actor reload and was not performed by the
assistant. The operator's build/ core changed independently during final
verification; this is recorded, not overwritten or treated as runtime proof.
Historical uncertain attempts are not reclassified Complete or replayed: later
logical outputs may never have run. No new tests or runtime send were executed.
See core change `telegram-receipt-whitespace-fix.md` and evidence directory
`build-dev/bridge-incomplete-diagnosis/`.

## Mutation preparation cancellation (2026-11-01, partial 10.5)

QQ event profile lookup now rethrows recognized cancellation rather than
selecting a display-name fallback and proceeding to recall/poke I/O. The
/userinfo lookup likewise propagates cancellation; a cancelled avatar outcome
cannot authorize text fallback even when definitely unsubmitted with no outputs.
The complete SendFailure and existing non-retryable cancellation classification
are preserved. Non-cancelled ordinary failures retain their previous behavior.

The two actual TUs compiled against frozen SDK2.5.19 with actor_build/20 workers;
Bridge relinked and all five selected targets passed. Other existing worktree
changes also rebuilt Chat and two ordinary forwarders; those changes are not
attributed to this repair. SDK and the operator-rebuilt core remained unchanged.
See core `openspec/changes/unify-bot-sdk-domain-api/bridge-mutation-review.md` and
`build-dev/domain-scope-2.4/mutation-cancellation-build.log`.

Task 10.5 remains open. Source checks of member provenance, original edit
identity, deletion checkpoints, replay suppression and typed strikethrough do
not replace live cancellation, complete command/edit/recall or fault-recovery
acceptance. No database/provider operation, restart, new test, configuration or
fallback policy was introduced.

## Media resource ownership (2026-11-01, source/build only)

Core task 9.5 closes for the actual typed ordinary/album owned-byte path, not an
invented shared-file send path. Cache paths/CreatedFile guards stay worker-local;
the send and retry requests own their data through all output settlement. The
existing PathManager descriptor/no-follow/exclusive-publication and cache CAS/
uncertain-reference retention paths were reviewed without changing their policy.
See core `openspec/changes/unify-bot-sdk-domain-api/bridge-resource-review.md`.

The shared photo-normalizer process runner now stops polling a diagnostic pipe
after EOF: POLLHUP previously caused a busy loop if the child had closed output
but not exited. The existing absolute deadline, wait cadence and child kill/reap
ownership remain; EOF alone never counts as successful process completion.
Frozen SDK2.5.19/20-worker actor_build rebuilt that real TU and relinked Bridge;
all five selected targets passed. Fourteen source/header and twelve production
object hashes are in core `build-dev/domain-scope-2.4/media-resource-verification.json`.
No SDK/core replacement, provider or database work, restart or new tests occurred.

Task 11.5 remains open: early-EOF subprocess behavior, cancellation, concurrent
cache publication, file/DB failures, shared-path leases and crash/orphan recovery
were not exercised. No new retention policy or configuration was introduced.

## Card/forward and album closure (2026-11-01, source/build only)

Core tasks 10.3/10.4 now close for implementation/source and existing production
build evidence. `bridge-special-content-review.md` records the active actor-owned
card parser, exact source/block association, typed forward transcript/attachments,
shared photo preparation and one outcome-aware logical delivery. SDK card metadata
is not reinterpreted in core, and preparation failures do not send a prefix.

`bridge-album-review.md` records the complete scoped buffer key, arrival order,
unchanged 1500ms debounce, duplicate handling, arm-generation fence, lease-owned
flush-on-close, and transactionally rechecked overlap suppression. Suppressed
members remain observations, not sent contributions or fabricated output owners.
All original submitted-member publication traces and output details are retained.

No production change or new compilation was needed in this review. Fourteen
reviewed source/header/include hashes match earlier built-source audits; eleven
real production-object hashes and Bridge DSO are recorded under core evidence
`build-dev/domain-scope-2.4/album-forward-review-verification.json`. actor_build
with frozen SDK2.5.19/20 workers reports all five selected targets up to date and
successful, SDK unchanged. The operator's separately rebuilt core was not touched.

No timer/close race, SQL/replay, provider query/send, media playback or fault
injection was executed. These closures do not complete 9.3/9.4, full media,
mutation or crash/resource-lifetime acceptance. No new/rewritten unit tests,
configuration defaults, routing policies, database operations or deployment.
Earlier source-only/not-built descriptions below retain historical context.

## Command ownership and card rendering (source-only)

All `BridgeCommandOperations` asynchronous entries now freeze their typed
request and shared gateway before returning an awaitable. Legacy string views
are converted synchronously through the existing reference resolver; suspended
helpers no longer access the facade. Existing pair/target validation, explicit
budgets, complete outcomes and typed failures remain; the legacy void overloads
await the same checked typed operation without introducing retries.

The active `CardFormatter` now consumes only the explicitly approved
`RawCardView` metadata and produces an owned string. Its internal
`common::MessageSegment` construction/JSON text wrapper and legacy segment
methods were removed. Parsing, URL order/deduplication, HTML unescaping,
configured raw failure display, titles and placeholders remain Bridge-owned.
The parser and formatting body are preserved; this is not a new SDK card
interpreter or a native send path. Rendering finishes before suspension, so
its deferred result does not borrow the formatter or metadata. Retired QQ
formatter/dispatch sources are historical only and are not restored to CMake.

Preimages: `command-entry-ownership-JqiRfE` and `card-typed-render-kbZkpU`
under the retained Bridge implementation preimage directory. These changes
have not been compiled or runtime-tested; tasks9–10/14.3/16.x remain open.

## Queried QQ forward attachments (source-only)

The actual `qq/forward_preparer.cpp` already reads non-image attachments through
`read_bridge_attachment`; the SDK path previously rejected forward video,
record and file references. The source SDK now retains URLs observed in the
completed scoped forward query as private, origin-bound `queried-forward-*`
receipts. Images with observed URLs use the same retention boundary; images
without URLs retain the existing native-image lookup recipe.

The query owns its request and dependencies before suspension and retains its
original account origin and deadline. The receipt store shares existing FIFO
and text bounds; reads use bounded credential-isolated HTTP and return owned
bytes, available metadata and actual size. Eviction/account retirement cannot
renew a URL or authorize a fallback. The wrapper conversation is not the
original node/file group: a forwarded file without a readable observed URL is
explicitly unavailable, not queried against the wrapper group. No node
MessageRef, archive-created live authority, stable identity, new cache key or
SDK cache service is introduced. Standard detached queries remain separate.

This source change is not a successful download, complete attachment migration
or consumer-build result. SDK2.5.18 still requires a new build/freeze and actual
Bridge build; cancellation, expiry, byte limits and provider acceptance remain
unverified. No task closure or frozen-SDK modification is implied.

## Active-path retirement and WebM ownership (source-only)

The active target now builds `telegram/command_facade.cpp` instead of the old
Telegram handler/formatter/file sender. `BridgeForwardingRuntime` constructs
`BridgeCommandOperations` directly; the scalar `BridgeBotOperations` and legacy
`RetryQueueManager` implementations are excluded from CMake. Their historical
sources remain as migration evidence, not independently buildable alternatives
against the current headers. Command behavior stays in the existing typed command
handler. No second legacy album buffer or scalar send fallback remains in this
runtime. Album buffering owns the actual `MessageStored` and publication identity;
it no longer reconstructs a native `MessageEvent` or keeps a divergent source copy.

`TemporaryMediaUploads` belongs to the already-excluded QQ formatter. It is not
an active temporary-resource owner. The active WebM converter now uses the existing
process runner, not `std::system`: explicit argv, forced Matroska/WebM demuxing,
file/pipe protocols, the existing four quality tiers and one shared 15-second
normalization deadline. The caller intersects the configured GIF byte limit with
the actual preparation budget; zero in the existing GIF setting cannot bypass
that budget. Input bytes are checked against the pinned descriptor; output is
bounded and checked for GIF header/trailer and safe dimensions inside private
scratch before exclusive cache publication. These checks are not playback proof
or an RSS/hard-real-time guarantee. The unused implicit TGS decoder wrapper and
unbounded conversion entry are removed; TGS still has its explicit text disposition.

The shared process runner now constructs argv in the parent and uses `posix_spawnp`
instead of allocating after `fork`. It closes inherited non-stdio descriptors,
requires a nonblocking diagnostics pipe, limits each drain iteration, and owns
child termination/reaping through exceptions and timeout. WNOWAIT keeps the child
identity until process-group termination and reaping; unknown wait status cannot
be reported as exit zero. Child cancellation/join is not a hard real-time promise.

Bridge's CMake SDK minimum, dependency range and runtime compatibility minimum
all require 2.5.18. The frozen 2.5.17 SDK remains untouched. These are uncompiled
source changes: no FFmpeg execution, SQL, provider call, loader or generation
acceptance was performed. The worker-owned cache implementation below follows these changes; historical/
process-crash orphan recovery and consolidated lifecycle acceptance remain open.
Do not infer task 11.5 from the scratch-owner changes.

## Telegram cache ownership (source-only, not executed)

`telegram/media_processor/cache.cpp` replaces the two path-returning download
coroutines. Cache lookup/read/conversion and new-file preparation/persistence
are synchronous worker extents. Only owning bytes, their accounting and a retained
preparation exception cross suspension; `CreatedFile` never does. Failed
preparation still counts the accepted input bytes before a presentation placeholder.
Cancellation and cache-scope errors are rethrown, not converted into resend authority.

`PathManager::CreatedFile` owns real file/directory descriptors. New originals
are fully written in private `.prepare-*` scratch and exclusively linked into
the old filename only afterward; readers cannot observe an in-progress write.
Conversions likewise publish only validated output. Unretained entries are
removed through the anchored parent only while they still identify the created
inode; a replaced entry is not removed. Cleanup decisions stay in the worker
before its completion; failed removal is not claimed as completed cleanup.
Persistent cache files are retained only after a successful cache
write, an existing matching reference, or an explicitly uncertain reference read.
The last case is logged and deferred to recovery, not called successful cleanup.
No retention TTL or preparation-memory setting was introduced.

The original `telegram`/`telegram_animation` namespaces, unique-ID keys,
`sticker_`/`animation_` filename prefixes and missing-ID timestamp recipe remain.
Missing unique IDs bypass DB cache persistence. Original-name absence, empty
file_type, original MIME/size, created_at and converted metadata remain distinct.
A successfully cached static WebP original survives later PNG conversion failure,
as before; failed cached WebM is not implicitly reconverted. TGS stays text-only.
Send/retry requests own bytes, not these cache paths, so releasing a preparation
file does not release data needed by later/possibly submitted physical outputs.

Cache writes use insert-if-absent or full-row CAS (`IS` comparisons retain NULL
versus empty distinctions). A stale lookup cannot overwrite a newer conversion
or last-used update. Missing/unsafe-file lookups retain the actual old row as the
replacement precondition; no new key is synthesized. A conflicting filename
claim from another cache namespace/key is rejected before publication, not
rebound or overwritten. Existing path aliases are compared through PathManager.
After a rejected/failed save, currently referenced created files are retained;
unreferenced ones are cleaned. If reference lookup itself fails, files are kept
for explicit offline recovery. Existing/historical files are never swept by this
implementation. Cache I/O failure never authorizes replay of a delivery.

Recovery audit found an exceptional-exit gap: a static-original cache save could
commit but report failure, followed by PNG conversion throwing before the normal
reference check. Worker-local `CacheReferenceGuard` now checks original and
converted files before their inode guards unwind. A second exception retains the
file without replacing the original error. The outer guard also covers failure
while reading the just-published original before entering conversion. The normal
successful path disarms these guards after explicit reference/retention decisions.
The offline backup, unresolved-attempt and orphan-review procedure is in
`docs/bridge-ledger-v4.md`; it authorizes no SQL or automatic sweep.

The code below is now covered by the frozen SDK2.5.19 production build and
2026-11-01 source/object audit above; this supersedes its earlier not-compiled
status. No SQL, file cleanup, FFmpeg or provider operation was executed during
this verification. Descriptor/allocation/
cancellation races, concurrent cache publication, DB failures and crash/orphan
recovery still require the consolidated real-consumer acceptance. Scope-collision
rejection is not a claim of successful cross-installation cache reuse.

## Telegram edit revisions (source-wired, not built or accepted)

`telegram/edit_preparer.cpp` keeps the actual MessageStored and its original media
observations. The SDK supplies recorded edit_date and an equality fingerprint,
not a fabricated update_id, native ordering guarantee or refreshed media access.
Canonical receive-time/segment overlays are excluded from that fingerprint.
Preparation rejects shared/unknown output attribution before acquisition and
prepares every byte before any deletion. The original source identity and original
ledger outputs are retained; edited input-block positions live in a separate
revision admission rather than being written over the original source member.

The operator explicitly selected the predecessor behavior: a definitely rejected
delete still permits the new version, retaining the old output and the failure.
Cancellation or an uncertain delete stops this invocation. A complete NEW send
is not an atomic replacement or a claim that every old message disappeared.

New revision schema/admission/checkpoint code is additive to unpublished v4;
`upgrade_v4_revisions_offline` is explicit and has no online caller. No SQL was
executed. Revision output claims fence concurrent ordinary output mutations.
The actual runtime now routes edits to `OrdinaryForwarder::forward_edit`; the
actor handles its durable revision result before any legacy mapping path. The
uncalled legacy TelegramEventHandler is excluded from CMake, with historical
sources retained. Notifications include the revision identity, all new outputs,
full deletion results and separately retained/deleted predecessors. Original
source/event/correlation identity is preserved; replay does not publish.

Output observation/reverse/source-operation queries include revision outputs.
Current member mutation and quote selection remain distinct from historical echo
recognition. Independent confirmations retain their admitted send-attempt IDs;
contradictions in an original delivery or earlier revision invalidate the member
lineage without discarding the observation. Settled retained predecessors remain
eligible for NEW explicit commands, not automatic replay of the old edit.

Replacement retries use the existing generation-owned pump and configured
backoff. Every previous send must have complete SDK zero-confirmed/zero-unknown,
retryable DefinitelyNotSubmitted evidence. The transaction compares the full
snapshot, latest revision, due time and budget before admitting one replacement;
no predecessor deletion is repeated. Full send-result history and scheduling
share the terminal transaction. Later edits supersede older queued candidates,
and late contradictory observations suppress pending schedules. Both ordinary
and edit retries recheck the generation token after DB admission. Crashed
Submitting work remains uncertain; there is no implicit suffix recovery.

Explicit configuration group addresses are now kept separate from historical
chat keys. Source routing facts come from the original SDK archive conversion,
not native parsing of opaque identifiers. Existing output mutations are now
classified before media acquisition: matching completed delete receipts are reused
with their original operation IDs, not invoked again. Completed reactions and
old definitely-not-submitted failures do not blanket-reject a new edit; pending,
uncertain, mismatched or unsupported mutations still stop preparation. Retry and
publication preserve receipt attribution, and admission compares the entire prior
operation snapshot. A retained output identity does not prove provider-side presence.
The typed album owner and suppressed-member association storage are now
source-wired (details below). Media lease cleanup, offline recovery exercises and
consolidated build/runtime acceptance remain open. No implementation in this
section has been compiled or executed. Do not run it against live data.

## Implementation-first retry wiring (not built or accepted)

The source now uses a generation-owned typed retry pump instead of the legacy
runtime thread/scalar callbacks. Retry-enabled first submission persists an owning
versioned send body, scoped references and explicit budgets; retry admission atomically
rechecks immutable intent, due time, count and zero effects. Complete SDK failure
outcomes are checked as well as ledger flags. LocalFile/HttpSource without a
persisted owner are not treated as durable bytes. Existing polling/backoff
configuration is passed explicitly; no defaults were added.

Outcome persistence is protected from cancellation, and safe rescheduling shares
its transaction. A late contradictory confirmation prevents scheduling without
rolling back the returned outcome. Original event/correlation identity is stored
for notifications; a successful retry publishes every confirmed output and source
member only after durable completion. Publication failure never repeats sending.
The added core ActorEventPublisher uses the existing generation token/admission/
drain machinery. Retry root frames, completions and cancellation posts retain the
lease; no executor-stop timeout abandons them. Aborted-reload restart waits for the
old pump to retire before launching with the new token.

These are source changes, **not compilation/runtime evidence**. The frozen2.5.17
SDK/products remain untouched;2.5.18 must be built/frozen for consolidated testing.
The active runtime now routes Telegram ordinary messages, edits and albums to
typed owners. Unsupported complete bodies reject explicitly, without falling
back to the scalar QQ sender. Historical handler source still requires final
retirement/include audit. Full runtime/DB failure/lease/recovery acceptance remains
open. No online schema operation was performed.

## Typed album entry and core-owned callback boundary (source only)

Each pair now has its own `AlbumDeliveryWorker`, subscribed during actor preparation
before lifecycle activation seals subscriptions. It preserves the1500ms debounce,
first-observation deduplication and arrival order. The original MessageStored and
publication identity reach the typed preparer; only persisted complete delivery
publishes all outputs. Stop closes admission and flushes already accepted batches
under their existing drain leases. It does not admit new retries. A retired route
may refuse publication; that never causes sending again.

Timers (including cancelled arms), coroutine completions and retry cancellation
posts now use out-of-line core lifecycle helpers. Core owns the outer Asio
handlers and serial executor, so actor closures/frames/exceptions are destroyed
before the final lease expires. Terminal completion is deferred beyond the last
actor awaitable handler. This is implementation, not an unload-safety test result.

The speculative SendUnseen/RejectUnseen switches were removed. The actual entry
preserves observed-batch behavior: overlap suppresses the whole batch, never sends
an unseen suffix; a disjoint observed batch retains its own membership and send
identity. A flush is not evidence of native album completeness. Persisting the
association of newly observed suppressed members now uses a separate scoped
observation table: original keys/traces are retained, and the reply anchor is a
real admitted member. It supplies no input contribution, reverse ownership,
mutation permission or media authority. Both the early suppression check and
post-preparation admission reconcile races transactionally. Replay resolves the
association rather than treating it as Missing. Replies follow the anchor's current
confirmed revision; mutation selectors reject association-only sources. Notification
and retry retain all original submitted-member traces, excluding suppressed
observations. The separate offline schema upgrade has not been executed. Do not
claim10.4 accepted before consolidated runtime/recovery verification.

## Telegram sticker cache → typed delivery (partial10.2)

OrdinaryForwarder now selects sticker observations in the same whole-message
path as text/other media. It prepares one Image/Animation/text contribution,
retaining emoji/description/caption, source provenance and the one durable
submission. It never invokes the raw segment sender or a replacement send after
submission failure/uncertainty. Typed/Boost/std cancellation still propagates.

Static WebP uses the already configured FFmpeg, not a new decoder dependency.
Before conversion, Bridge checks complete bounded RIFF framing, unique permitted
chunks, no animation flag/ANIM/ANMF, one coded frame, matching VP8X/frame dimensions
and the existing64-million-pixel guard. FFmpeg receives explicit webp_pipe input,
PNG/image2 output, unchanged geometry, explicit byte bound and the existing15s
process deadline; no new config/default exists. PathManager pins and compares
input bytes, rechecks source stat after decode, and validates bounded static PNG
framing/dimensions while output is still private, before exclusive publication.
This is structural/geometry validation, not proof of provider playability or
full image-decoder validation of arbitrary cache contents.

Cache namespaces/installation/file_unique_id keys, file_id filename recipe and
missing-ID behavior remain. PNG uses the same sticker filename prefix in
stickers/converted with .png; existing conversion fields record that output.
Original MIME/size/path/key fields stay original. Source dimensions/duration are
not reused as unverified converted-output metadata. Cache persistence errors are
handled only in owned blocking work; scope/row mismatches never overwrite another
row. A persisted converted PNG is reused on the next ordinary cache hit. Failed
cache publication/DB writes can still leave recovery work;11.5 is not closed.

Video-WebM keeps existing configured WebM→GIF cache conversion; actual GIF bytes
become Animation. Failed WebM is not relabeled as Image. TGS has no verified
registered decoder/QQ recipe: it receives an explicit unsupported animation
sticker label without acquisition. Other preparation failures retain the
existing sticker/emoji text fallback. Caption and source position are retained.

The real sticker/animation/dispatch/PathManager/OrdinaryForwarder translation
units compile with frozen SDK2.5.17. No FFmpeg, filesystem scenario, SQL, provider
or loader execution was performed. Whole Bridge still has9 missing private-header
units; albums/edits still have legacy handlers. No10.2 or runtime task is closed.
Evidence:build-dev/domain-migration/bridge-tg-sticker-v1/.

## Telegram animation cache → typed delivery (partial10.2)

OrdinaryForwarder now selects non-sticker animation as part of the whole typed
body. The real TelegramMediaProcessor retains the same installation/
telegram_animation/file_unique_id cache keys, file_id filename contribution,
missing-unique-ID behavior, cache read/save and configured WebM→GIF conversion.
Cached/newly converted files are read in owned blocking work through PathManager:
anchored no-follow directory/leaf opens, regular-file checks, bounded chunks,
non-retained EOF probe and size/mtime change rejection. Provider reads and cache
reads receive the caller's remaining item/total preparation budget. The prepared
bytes, not a cache path or native segment, stay owned by the single durable send.

Actual GIF bytes become Animation. Recognized MP4 container signatures become
Video explicitly in Bridge; this does not claim QQ MP4 Animation support or
prove provider playability. Converted GIF metadata does not retain unverified
source dimensions/duration or a .webm filename. Original observations remain in
the source member/archive. Unavailable cache data retains the existing animation
placeholder; failed-WebM/TGS/unknown bytes produce an explicit unsupported-format
placeholder, never relabeled Image/GIF/File. Both preserve caption and source
contribution. These are preparation choices before admission, never replacement
sends after provider failure/uncertainty. Typed/Boost/std cancellation propagates.

The actual PathManager, animation preparer, dispatch and OrdinaryForwarder TUs
compile against the retained frozen SDK2.5.17. No filesystem/provider/FFmpeg
scenario was executed. Sticker, album, edit, lease/retry recovery, full runtime
compilation and multimedia acceptance are still open; no task checkbox closes.

## QQ ordinary-entry retirement (partial9.1; SDK2.5.17)

QQ MessageStored now has exactly one ordinary forwarding owner: TextForwarder.
Runtime keeps the original installation validation and one receipt heartbeat,
then returns its complete typed delivery result. It no longer reconstructs a
native MessageEvent/QQForwardMessage or falls through to QQHandler after typed
refusal, failure or replay. Unsupported whole bodies reject before media work;
unmatched/disabled routes still return no output. Empty/reply-only bodies may
produce the configured sender prefix, otherwise return no output without an
empty provider send. This replaces the old unknown-segment dropping behavior
with an explicit failure, not a partial-success copy.

QQHandler now owns only the existing command/recall/poke delegates, with the
installed-only BridgeCommandOperations boundary. Its actual TU now compiles
without private Telegram operations, formatter, media processor or raw retry
sender dependencies. Old QQ formatter/media source files remain unchanged in
the production source list pending final retirement; no source/target was
removed to improve the build. Legacy persisted retry callbacks remain a separate
11.3 migration gate and must not be mistaken for migrated typed replay.

Actual build:39 Bridge dependency objects (37 Bridge-owned),9 old units missing,
no whole Bridge DSO. Runtime dispatch edits remain source-only because its other
legacy dependencies still fail compilation. Evidence:
build-dev/domain-migration/bridge-qq-entry-v1/.

The operator requested no retained binaries under build-dev/. The current usable
frozen SDK is build/domain-sdk-2.5.17-current/install, rebuilt from unchanged
2.5.17 source with identical88 public headers. Earlier build-dev installations
are retired header/metadata evidence, not usable SDKs. Temporary binaries are
removed after verification; no existing build/src core is replaced.

## Empty display-name preservation (partial9.2/10.3; SDK2.5.17)

The scoped forward producer now retains an observed empty nickname in both
standard node.data and provider sender profiles. Missing provider nickname stays
absent; present non-string, oversized, invalid-UTF-8 or control-character names
reject before copying. ReceivedForwardNode fields/codecs stay unchanged; the
result validator and borrowed decoder permit empty observed names. Bridge's
existing value_or("未知用户") renderer therefore distinguishes missing from
empty without inventing authors. This is not a change to self-cache empty-name
normalization or outgoing authored-node requirements.

TextForwarder now treats a cached empty name as a cache hit, like the predecessor,
so it neither queries nor overwrites that value. Member-query cancellation retains
the typed operation failure instead of becoming an unrelated runtime_error.
New music/inline-node record preflight checks borrowed fields, part tags and
form/part consistency before owned decoding; literal empty/newline text remains
valid. This adds no configuration, runtime cache or resident-memory guarantee.

Actual SDK and consumers compile; the old Bridge private-header-dependent units
remain uncompiled. The operator explicitly selected semantic parsing for queried-node CQ strings:
standard string content now becomes ordered typed text, mentions and media,
rather than the predecessor's raw CQ display. This is an approved display
change, not an opaque-ID/raw-CQ media-read escape hatch. Inline-node literal
observations remain separate. No claim of full runtime compatibility is made.

Mixed-image preparation now retains the predecessor cache eligibility: one
image, or several images mixed with mentions/cards/forward/node/other media,
uses the existing cache preparer. Multiple images plus only literal text/reply
still bypass cache as before. Native source/target keys, missing-field recipes,
original live matching, cancellation and bounds are unchanged. Cached reads
prepare bytes only; no per-image send or outcome-loss fallback is restored. Evidence:
build-dev/domain-sdk-2.5.17/.

## QQ music and inline-node display (partial9.2/10.3; SDK2.5.16)

TextForwarder now selects the existing music/node display paths from the whole
typed body. The pure QQ restore_presentation_observations boundary supplies
original title/name/content observations with source, recorded receive time and
native/normalized positions. Bridge checks source/time/category/order/duplicates
and requires every selected block to have its observation before media work.
SDK fallback text, fabricated node authors, recursive lookups and node media
locators are not introduced; existing SpecialContentView/forward layouts stay
unchanged.

Bridge retains `[音乐分享: title]`, `👤 nickname: ...\n`, original literal node
strings, ordered array text/face/image/kind markers, missing versus empty names
and titles, and missing/unsupported content distinctions. Missing or malformed
face IDs cannot become invented zero. Empty node text is an observation, not an
invalid outbound Text: the renderer omits its empty span while retaining the
node prefix/newline. Known non-media forward-node categories retain the old
explicit `[type]` presentation; SDK `onebot11.forward_reply` becomes `[reply]`
without any reply lookup or synthetic node MessageRef. Malformed/unknown media
are still rejected, not silently dropped or converted to successful text-only
copies. Rendering is bounded and stays before the one durable logical delivery.

Actual installed SDK2.5.16 and TextForwarder compile; old private-header-dependent
formatter/handler/runtime units still do not. This does not retire their active
remaining branches or close10.3. Forward-query CQ-string display compatibility,
empty queried-node display-name preservation, full media/provenance/runtime
acceptance remain separate work. No provider/SQL/loader/FFmpeg execution or new
unit tests. Evidence: build-dev/domain-sdk-2.5.16/.

## QQ image cache preparation (partial10.2; SDK2.5.15)

The operator resumed cache migration. The actual TextForwarder now routes its
single-image path through image_cache_preparer.cpp before the existing one
logical delivery. Unsupported legacy branches remain; this is not full cache
or whole-Bridge retirement.

The QQ SDK supplies only original bounded image fields and source/time/positions
through restore_image_observations. file/url are transient SensitiveText with no
generic serialization. Bridge borrows them only for its existing classification
and calculate_hash(file + "_" + url) / calculate_hash(url.empty() ? file : url)
recipes. Missing strings retain the old empty-string recipe; present invalid
fields skip cache use rather than inventing an identity. Numeric subType remains
numeric data, not SDK sticker policy. Neither field becomes a download source,
output, log value, persisted raw locator or new MediaIdentity.

Before using a recipe, the existing LiveMediaLegacyResolver must bind the actual
live image, original archive/projection digests, source/time/native ordinal and
MediaRef. Availability is rechecked before and after cached acquisition. Missing
or expired association disables cache use; archive restoration never activates
historical media. Runtime passes the existing generation-owned resolver, not a
new service or a guessed sibling-event join. Runtime wiring is still uncompiled.

Cache lookup/update keeps source and target installation namespaces. The pure
Telegram restore_legacy_file_reference boundary validates the recorded file_id
and explicit kind; it does not query, invent file_unique_id, promise availability
or accept URLs/paths. A hit is read to bounded owned bytes; cached GIF stays
Animation, unsupported cached material returns to the original QQ read before
any message submission. Cancellation and uncertain read results do not become
cache misses. Cache database errors are isolated inside the blocking worker;
they do not discard prepared media, and cancellation remains propagated. Corrupt
row scope is checked outside that fallback. No cache branch sends independently
or retries a failed send.

PhotoReadInput passes completed acquisition into the same photo preparer without
a second download; original-request scope, actual bytes, total/item bounds,
normalization, placeholders and ordered statistics remain checked. Format rows
retain their separate key and empty telegram_file_id; original-byte MIME is
observed before normalization. Existing is_gif records are not rewritten, but a
stale flag cannot override actual bytes or relabel unsupported media. The old
implementation only writes these format rows; this increment does not invent
new upload-cache publications or target outputs.

SDK2.5.15 is a new immutable generation;2.5.10–2.5.14 are unchanged. Actual core
103 objects/115 installed payloads/87 headers and five other actor DSOs compile.
Bridge38 total/36 self-owned objects compile;10 private-header-dependent units
remain missing, no Bridge DSO. No SQL/provider/runtime/loader execution or new
tests. Evidence: build-dev/domain-sdk-2.5.15/. All30 open tasks remain open.

## Unknown QQ sender routing (partial9.1/9.2/10.3)

The real TextForwarder now accepts UnknownSender for its already-supported
whole-body subset, including forwards, instead of selecting the predecessor
solely because an author is unavailable. ConversationSender, albums and other
unsupported content retain separate eligibility; no legacy branch was deleted.
Archive restoration must match source/time/sender kind and exact known user.
For an unknown sender with sender display enabled, the SDK compatibility reader
must match all original storage keys and must not claim a known sender. This
pure validation occurs before media preparation.

Original display_sender_id is only a presentation/cache key. An owned worker
reads the existing installation/qq/user/group cache; cached empty names and
absent/empty ID fallback retain their predecessor behavior. It has no gateway,
ReferenceResolver or UserRef input and never performs a sender member/self
query. Known users keep get_member; genuine mentions in message content retain
their separate display lookup policy. Display keys never become ping/author
references. All content stays in the existing single logical delivery, with
unchanged qq-text-v1 identity, scopes, provenance, outcomes and replay handling.

QQ receipt heartbeat source wiring moved from the legacy handler to runtime
before branch selection, restoring updates for typed messages and avoiding a
duplicate legacy update. The typed source must validate and match the configured
QQ installation before that write. This is receipt liveness, not delivery
success. Runtime/handler still do not compile through remaining private headers;
the heartbeat change is source-reviewed only, with no SQL execution claim.

The actual TextForwarder/unknown-sender cache worker compiles against sealed
SDK2.5.14. Bridge remains35 self-owned/37 total objects,10 missing, no DSO. See
build-dev/domain-migration/bridge-qq-sender-v1/. Full node/cache/mixed-content
retirement, runtime and historical acceptance remain open;54/84 unchanged.

## Forward face observations (partial7.3/10.3)

SDK2.5.14 adds a distinct typed forward observation query over the same native
get_forward_msg. The real `qq/forward_preparer.cpp` consumes it: original face
IDs are paired with their exact node and normalized block, rendered as the
existing `[表情:id]`, and each supplied observation must be consumed once.
Malformed/missing face evidence rejects before any send instead of fabricating
ID0 or dropping the face. Names, author absence, timestamps and media references
still come from the same lookup; no second query or synthetic node archive is
used. The SDK does not render display text or expose native download URLs.

The existing forward request/result and ReceivedForwardNode layouts remain
unchanged; the new result has its own name/codec/action. Bridge now requires
SDK>=2.5.14. Core builds/links/installs into a new frozen generation; all earlier
SDKs remain sealed. The actual installed Bridge forward preparer compiles with
this new header, but the legacy formatter/handler/runtime still do not compile.
Other unsupported node content and CQ-string presentation remain compatibility
gates; this does not retire the old forward fallback or close10.3. No runtime,
provider, SQL, loader or FFmpeg operation was performed.

## Owned multi-image delivery (partial9.3/9.4)

The actual QQ formatter's multi-image+text branch now selects typed delivery
before effects. It consumes original image_inputs, prepares bytes through the
shared ImagePreparer, and submits one body/caption/AllowSplit request through
`qq/photo_delivery.cpp`. Sender italic spans are RichText; the obsolete manual
UTF-16 entity helper is removed. Missing image slots, normalization/replacement
statistics, GIF-as-Animation and11=>9+2 captions are retained. Non-text siblings
remain with their existing owner rather than being silently dropped; received
per-image captions without this branch's renderer reject before media reads.

The helper checks configured pair/source/full target scope and enabled route,
uses the existing qq-text-v1 delivery identity, checks echo/legacy binding/replay
before reads, resolves the original typed reply against the exact target, and
records complete provenance/outcomes. It never sends each batch separately or
retries URL/multipart. The typed send_delivery boundary moved from the private
facade into BridgeCommandOperations without changing its owning non-coroutine
behavior. DirectForwardOutcome now carries the full BridgeDeliveryResult;
handler/runtime source wiring propagates it before the legacy mapping branch.
A compatibility quote anchor is retained separately, not used as the result.
Selected typed failures cannot fall through to per-segment sends or feedback.

The real photo_delivery and typed boundary objects compile against frozen
SDK2.5.13. The new production source is included in the actual target; fresh
actor_build configuration uses `build-dev/domain-bridge-sdk2.5.13-photo-delivery`.
There are37 production objects (35 Bridge-owned),10 missing and no Bridge DSO.
Formatter/handler/runtime themselves still fail on remaining private TG headers,
so their wiring remains source-only. The older forward-node URL/multipart and
single-image/cache branches remain unfinished and were not deleted to compile.
No runtime/provider/SQL/ELF/FFmpeg acceptance;54/84 remains.

## QQ live-image association (partial9.1/9.3)

BridgeActor now obtains the existing generation-owned LiveMediaLegacyResolver
service and passes it into the runtime. Before entering the predecessor QQ
handler, runtime captures image associations once on QQForwardMessage. The
installed service checks the original archive/projection digests and admission;
the carrier also checks source, recorded receive time, original image ordinal,
original ReceivedMedia membership and duplicate ordinals. No-image messages do
not call the matcher. CacheUnavailable retains empty slots; other errors are
not converted into permission to read. Capture cannot be repeated on the same
carrier or copies made after capture; consuming image_inputs rechecks availability without
renewing a receipt, decoding an ID, or acquiring a replacement account.

Actual adapter/carrier and BridgeActor factory objects compile against frozen
SDK2.5.13 (`bridge-compile-4`). Runtime/handler/formatter remain uncompiled due
to private Telegram dependencies. At this earlier association-only checkpoint,
the formatter had not yet consumed image_inputs; the subsequent multi-image
implementation above supersedes that source state, not the remaining runtime
gates. Association alone does not close9.3/9.4. No provider calls, reads,
SQL or runtime execution were performed. Evidence:
`build-dev/domain-migration/bridge-qq-live-association-v1/`.

## Original QQ source carrier (partial9.1/9.3)

The predecessor QQ handler and its multi-image/forward formatter calls now own
`QQForwardMessage {event, stored}` across suspension. The retained MessageStored
contains the original MessageReceived/media receipts, exact storage keys and
archive payload. Its compiled validation checks configured installation,
message identity, SDK-restored archive identity/recorded time, group kind/key
and original display-sender key. Restoration does not replace live source data
or grant new media authority. This is input migration, NOT outgoing media or
receipt migration: the old formatter's URL-first/multipart path and primary-only
results still need replacement. No new media reads or sends were introduced.

`build-dev/domain-migration/bridge-qq-source-carrier-v1/` preserves the increment.
`bridge-compile-2` retained an incorrect private message_archive.hpp include;
corrected to the public reference_resolver.hpp that defines LegacyMessageArchive.
`bridge-compile-3` compiles the real source-carrier implementation. Handler,
formatter and runtime remain blocked by their private Telegram dependencies;
no whole-Bridge link or end-to-end wiring proof is claimed. SDK2.5.13 is unchanged.

## Typed update projection (SDK2.5.13; partial7.3/7.4)

Runtime edit dispatch now selects the original typed `message_update_view`, not
legacy `sub_type`/`is_edited`. The actual Telegram event handler independently
requires that view before edit work; this handler compiles against installed
2.5.13. Runtime/facade/TelegramHandler still fail at remaining private Telegram
headers, so complete wiring is not proven. Removed the three uncalled private
facade methods `fetch_telegram_file`, `delete_telegram_message`, and
`edit_telegram_message`; active typed read/delete/formatted-edit paths remain.
No active outgoing-media branch was removed to make a build pass.

SDK2.5.13 validates sticker/video-note media kinds and native stable identities,
and validates Telegram edit source references. It introduces no new DTO layout,
cache policy or authority. Real builds:103 core objects,47 other consumer objects
and five DSOs;36 Bridge-build objects (34 Bridge-owned),10 missing objects and
no Bridge DSO. Evidence: `build-dev/domain-sdk-2.5.13/verification-2/`.
The first audit's three disassembly files contain section headings, not selected
function bodies; corrected symbol selection in verification-2 provides the real
call sequences. Both audits' build/hash checks remain separately scoped. No
runtime/provider/SQL/loader or FFmpeg execution; tasks remain54/84.

## Telegram sender observations (SDK2.5.12; partial9.2)

The actual OrdinaryForwarder now accepts conversation senders as well as known
users and unknown senders for its existing ordinary-content subset. Before any
lookup or preparation it compares the restored sender variant and complete
user/conversation reference with the stored typed source. The named SDK archive
boundary supplies bounded original `from` profile fields, not an author identity.
Bridge retains first/last-name, then original-case `@username`, then `Unknown`
presentation. A successful common get_member remains preferred for known users;
missing/failed lookup uses the observed name. Cancellation still propagates.
Conversation and unknown senders never cause a user/self lookup, including when
Telegram carries a compatibility `from` alongside sender_chat.

SDK2.5.12 also exposes original file_id/optional file_unique_id as platform data,
without cache/filename/path policy or read authority. The subsequent
`bridge-tg-media-inputs-v1` increment migrates the actual animation/sticker
processors to that view and owned ReceivedMedia (details below). This is still
not completion of10.2 or of the whole Bridge migration.

The new core/SDK was sealed separately;2.5.10/2.5.11 were not overwritten.
The actual installed-only OrdinaryForwarder object compiles after correcting an
initial invalid Sender-to-JSON comparison to direct variant/reference comparison.
The complete real Bridge build still fails on other private Telegram headers;
no Bridge DSO, runtime, provider, database or loader acceptance is claimed.
Evidence: `build-dev/domain-sdk-2.5.12/` (both Bridge attempts retained).

## Telegram media inputs and album carrier (frozen SDK2.5.12; partial9–10)

The actual dispatch/files/animation/sticker processors now consume owned
ReceivedMedia and BytesSource instead of private TelegramFileRef/FetchedTelegramFile.
Original file observations supply cache IDs, not read authority. The read wrapper
owns the original media and shared gateway before suspension and checks the
configured Telegram installation. Original file_unique_id, cache namespaces,
filename prefixes, missing-ID bypass, conversion settings, PathManager publication
and actor-owned cache records remain unchanged. Cancellation escapes cache/text
fallback; ordinary attachment errors no longer silently drop the attachment.

Runtime, edit callback and album callbacks now carry the original MessageReceived
beside the transitional predecessor event and unchanged archival keys. Raw file IDs
can only select a unique already-present media observation, never construct a read
reference. Album buffering uses SDK serialization of the whole MediaGroupRef,
retains arrival order and first-seen MessageRefs, ignores pending duplicates without
rearming, and fences already-queued stale timer callbacks with per-arm tokens.
The debounce remains1500ms; this does not establish complete album membership.

Real installed-only objects for the four media processors, album buffer, event
handler and command read wrapper compile. The complete Bridge target still fails
on private Telegram headers in its old facade/QQ formatter; TelegramHandler and
forwarding-runtime wiring therefore remain source-inspected, not compiled.
Legacy outgoing segments/senders, caption/display extraction, edit mutation
journals, delayed publication/generation draining, persistent duplicate recovery
and complete resource cleanup still require migration. In particular, old TGS
and failed-WebM fallback paths are not evidence of playable QQ output; animation's
old File recipe must not be silently relabelled to bypass a missing capability.
No runtime/provider/DB/loader/FFmpeg verification or task closure is claimed.
Evidence: `build-dev/domain-migration/bridge-tg-media-inputs-v1/` and
`build-dev/domain-sdk-2.5.12/bridge-compile-3/`. The installed SDK is unchanged.

## QQ recall notification consumer (SDK2.5.11; partial10.5)

The actual actor now owns the validated MessageDeleted before suspension and
passes it through the forwarding runtime/QQ handler to QQEventHandler. Raw
notice fields cannot supply the recalled message ID or override a typed recall
with a poke. QQEventHandler depends on the installed-only command boundary,
not the private Telegram operations header. Its poke text send also uses the
common typed send and keeps a full failure outcome; the native poke observation
parser itself remains legacy work.

Recall selection queries the exact source/pair without constructing a target
from current configuration. Ambiguous/corrupt memberships, unknown/shared
output contributions, invalid native IDs, and targets no longer configured for
this pair fail closed. All actual targets are persisted MessageRefs. The
source-only lookup reads at most two matches, sufficient to reject ambiguity.
Only a newly persisted OutputOperation with rechecked provenance permits a
provider invocation. Known media-bearing outputs reject text-edit propagation
before mutating a prefix; media/caption editing and outputs lacking kind evidence
remain part of full10.5 acceptance, not claimed supported by this increment.
The first operation's stable source/delivery fence blocks
repeated notices and suffix continuation even when receive times differ or
outputs arrive later. Pending/terminal records retain the original observed time,
planned output set and full edit request/outcome. First failure, cancellation,
wrong receipt or persistence failure stops subsequent mutations. Owned outcomes
survive to the actor boundary; incomplete/unavailable results become a
non-retryable actor failure rather than a success or automatic retry.

The old wording `Message has been recalled` is a Telegram strikethrough span;
the optional `[sender] ` prefix is ordinary text, with the existing control-char
sanitization and user-cache/card/title/nickname policy. No MarkdownV2/entities
are built in the actor. Unknown/anonymous senders are not promoted to users.
Image recall deletes every proven exclusive output, not merely the anchor.
No legacy v3 row is used to guess a missing historical topic.

The received-message reader keeps the independent raw archive unchanged and
adds a canonical payload archive with the actual persisted `created_at`; native
message `timestamp` is no longer substituted for local insertion time. Existing
SQL keys/schema are unchanged. **One native compatibility predicate remains**:
its predecessor image-row/raw-array image classification is retained in the
legacy reader, including malformed/raw-CQ-string behavior. Generalizing this to
all typed image parts would silently turn some historical edits into deletes.
A future platform-owned compatibility observation is still needed to retire
that native extraction; this increment does not close task14.3 or the full
recall/migration acceptance task.

Evidence: `build-dev/domain-migration/bridge-typed-recall-v1/`. Real actor_build
against the unchanged sealed SDK produces 31 objects (29 Bridge, one Store,
one path-mapping); QQEventHandler, delivery sender, command operations, storage
reader/repository and actor entry compile. Whole Bridge linkage and the runtime /
QQHandler objects remain blocked by the other private Telegram dependencies.
No SQL, provider action, generated ELF loading/execution or unit test ran.

## Telegram-specific formatted editing (SDK2.5.10; partial10.5)

BridgeCommandOperations now accepts the owned Telegram EditFormattedTextRequest
and returns its full typed result/submission safety without retry or conversion
to a boolean. Its synchronous wrapper validates the exact message conversation
against configured TG group/topic scopes, then transfers the gateway and request
to an owning continuation. Both QQ and TG configured targets are resolved once
through ReferenceResolver; no opaque-ID decoding or second self cache is added.
The configured scope check is not a substitute for durable mutation admission or
provenance/ownership proof.

The SDK's StrikethroughText belongs to Telegram FormattedText only; common
TextStyle is unchanged. Bridge owns the actual recall wording and sender prefix.
The old QQ notice handler is not yet switched: historical MessageMapping has no
original target-topic evidence, so taking today's configuration as proof of the
old output's full MessageRef would be unsafe. The next step is the existing v4
output/provenance/journal route and explicit treatment of legacy incomplete
records, not a guessed topic or an unjournaled edit. No full recall/runtime
acceptance is implied by the new callable operation.

## Typed deletion observation gate (SDK2.5.6; not completed recall migration)

The real actor notice entry reads the SDK's optional `MessageDeleted`, checks
its installation/surface against the original envelope and resolves the legacy
conversation through ReferenceResolver before the first await. A conflicting
payload or scope fails without falling back to the raw notice. This is only
observation consistency: no new mutation/reply/media authority is granted.
The existing forwarding call and recall formatting are retained. Full typed
notice dispatch, provenance mutation and historical recovery remain open10.5;
no new deletion side effect, runtime execution or whole-Bridge success is claimed.

The latest user decision supersedes the earlier deferrals: all file caching,
conversion, paths and cleanup belong to Bridge. Do not introduce a core cache
compatibility/management API, decode opaque identities, hash replacement keys,
or change the existing filename recipes. Bridge passes the chosen file to SDK
sending. QQ file acquisition uses an explicit QQ file-ID read operation.
Strikethrough may now migrate, but only through a Telegram-specific API, never
through common TextStyle. These implementations and runtime gates remain open.

## Raw QQ card metadata (SDK2.5.9; partial10.3)

The SDK identifies Json/App/Ark/MiniApp/Share and retains the original metadata;
it no longer parses embedded card JSON or selects titles/links. Bridge's
`qq/card_formatter.hpp` owns the original parser and configuration. Its existing
segment entry points and the new raw-card entry share the same parsing, fallback,
HTML decoding, multi-link and rendering code. The ordinary QQ forwarder validates
source/block/kind before rendering and contributes the text at its original
position to the existing delivery plan. No arbitrary raw sending is introduced.

The core and five other consumers compile against frozen2.5.9. The first whole
Bridge attempt produced17 other objects but failed on the remaining private TG
header consumers before compiling the changed card parser/text forwarder. Do not
claim those two TUs compiled, or whole-Bridge linkage/runtime acceptance. The
legacy summary CardView remains decodable; new SDK observations use RawCardView.

## Unknown Telegram sender (SDK2.5.2, partial implementation9.2)

The ordinary typed forwarder now accepts `UnknownSender` as well as `KnownUser`.
For a genuinely absent Telegram author, the existing configured sender prefix
remains `[Unknown]\t`; no synthetic user zero, self-cache substitution or sender
member lookup is introduced. When sender display is disabled, no sender prefix
is added. Genuine mentions in the body still follow their existing independent
member-display path.

Before the first await, the restored archive must also have `UnknownSender` if
the original typed notification does. A known or conversation sender cannot be
relabeled unknown to enter this path. Restoration supplies consistency evidence,
not new live media/reply authority. `ConversationSender` remains excluded because
its predecessor display metadata is not yet represented; albums, edits, stickers,
animation and unsupported blocks retain their existing exclusions.

Routes, original source/delivery keys, echo/replay guards, reply handling, media
budgets/preparation, provenance and send/confirmation behavior are unchanged.
The real `telegram/ordinary_forwarder.cpp` object compiled through the isolated
actor build with SDK2.5.2; 13 metadata/preservation checks passed. Evidence:
`build-dev/domain-migration/bridge-tg-unknown-sender-v1/verification-2/` and the
final document recheck. Whole Bridge linking still fails on remaining private
`telegram/bot/operations.hpp` consumers; no runtime/provider/SQL execution or
new SDK/schema/configuration occurred. Task9.2 remains open.

## Reaction source/output migration (SDK2.5.2, partial implementation10.5)

`/react` and `/unreact` now share the exact v4 selection used by recall: a TG
original selects every proven exclusive QQ output; a confirmed TG copy selects
its unique QQ original source. Shared/unknown contribution, ambiguity and changed
scopes fail before effects. Selection alone is not authority: each real source or
output mutation is transactionally admitted with the existing journals and exact
expected provenance. Recall's selector was extracted, not replaced; its provider
loop, codec/key recipes and error strings are unchanged.

A separate `qq-message-reaction` command fence precedes lookup, freezes the typed
request vector and original evidence, and blocks repeat/suffix continuation even
if emoji/set, configuration, delivery or the confirmed-output set later changes.
Each target gets at most one typed SDK call after Created admission. The full
`BotOperationResult<OneBotMessageEmojiLikeResult>` is owned before validation and
persistence; matching requires message, emoji and set to agree. Independent
definite rejection may continue to the next target, but unknown/cancellation or
storage failure stops. Child and aggregate result records retain all outcomes,
including unsent targets and child-write status. The handler owns returned results
through feedback; success requires all matched target records and aggregate
finalization. Existing valid command replays produce neither effects nor feedback.

Only genuinely missing v4 evidence reaches historical lookup. Unique reverse
ownership can target the known QQ original source, recorded explicitly as legacy
command evidence without inventing a delivery. A legacy physical-output mapping
without contribution proof is refused, not promoted into exclusive ownership or
silently reduced to its primary ID. Historical keys are unchanged; completeness,
pre-journal replay and binding/recovery acceptance remain open.

No new table/schema/config/SDK/default is introduced in this increment. Existing
Unicode-emoji parsing and no-automatic-retry policy remain. Core evidence directory
`build-dev/domain-migration/bridge-reaction-v1/` covers actual object compilation
and static preservation only. Whole Bridge still lacks other private-TG migrations;
no SQL/provider/actor/loader execution occurred. Progress stays50/84;10.5 remains open.

## Poke durable command admission (SDK2.5.2, partial implementation10.5)

The previously open poke fence is now wired to the separate actor-owned
`bridge_command_operations` journal, not a source-delete/output row. The existing
void compatibility operation is unchanged; a new owning typed overload returns
`BotOperationResult<OneBotGroupPokeResult>` without reducing it to success/failure.
Pure request preparation validates configured QQ scope and uses only the selected
QQ Store user. No native payload or private platform DTO is introduced.

The original command/pair/kind is the stable fence, checked before attribution;
existing pending or terminal entries suppress provider calls and fresh feedback.
Created-only admission retains typed request/scopes/evidence. V4 source binding is
transactionally rechecked and prevents late contributor growth. Legacy attribution
is labeled as such, preserving old keys without inventing a typed delivery. The
full outcome is owned before validation/encoding/persistence. Only a matching
persisted receipt counts as success; commit uncertainty/cancellation/encoding or
provider exceptions never authorize replay. A pending outcome has no expiry-based
retry. The original no-success-feedback policy is retained.

Fresh v4 creates the new journal. Prior candidate-v4 layouts need explicit offline
upgrade stages described in `bridge-ledger-v4.md`; normal initialization/reload
rejects a missing journal. No migration was executed, no SDK/config/default/lease
policy changed. This does not solve historical pre-journal command replay,
cross-database author snapshot atomicity, feedback outboxes, other management
commands, or runtime/SQL/provider acceptance. Task10.5 remains open; progress50/84.
Evidence: core `build-dev/domain-migration/bridge-command-journal-v1/`.

## Poke source attribution (historical predecessor to command admission)

`/poke` no longer reads a Telegram Store sender ID and reinterprets it as a QQ
user. It also no longer substitutes the QQ bot author of a TG-origin output for
that Telegram sender. It first resolves a uniquely owned QQ original through the
v4 source-attribution helper, using the original same-conversation reply, pair and
configured QQ scope. A known owner without its QQ Store row yields unavailable,
not historical fallback. Only absent v4 attribution permits the existing reverse
legacy mapping lookup; that lookup also reads only the QQ source row.

Repository, Store reader and blocking executor must all be present. Ambiguous,
corrupt or changed-scope v4 ownership fails before poke. User attribution is not
permission to edit/delete all outputs. The older historical binding path still
needs its own migration; these checks do not invent missing author evidence.

Failure feedback distinguishes possible submission and does not suggest blind
retry. The actor command boundary still disallows automatic retries for poke.
There is **no durable poke command fence yet**: lookup is not mutation admission,
and this increment does not claim replay/crash protection or full result journaling.
The existing common error classifier was moved unchanged from the private-TG-
dependent bridge_bot_operations.cpp into the real bridge_bot_failures.cpp target
source. Its redaction/unknown/retry logic now compiles without either platform SDK.

Five current production objects (two changed/new, three supporting) and13 metadata
checks pass in core `build-dev/domain-migration/bridge-poke-ownership-v1/verification-1/`
and final `verification-2/`. Whole actor still fails on remaining private TG headers;
no SQL, provider, actor or loader execution. Progress stays50/84;10.5 remains open.

## Stored notification entry (SDK2.5.2, implementation gate8.3)

The real BridgeActor now explicitly validates the typed message and exact source
platform/installation surface before its first await or runtime resolution.
Unknown platforms and QQ/TG mismatches cannot start forwarding facilities. The
existing processed-command early return, owning source/envelope capture and all
other actor behavior are token-preserved. MessageStored remains the forwarding
input; observing RawMessage does not forward it a second time.

Store0.2.1 emits one successful typed notification per successful handler return,
with the same legacy keys, independent raw/payload, timestamp, headers and
correlation/causation. Its exception branch returns the existing failure event.
Bridge/Chat both require Store>=0.2.1,<0.3.0. Chat verifies the exact original stored
archive before combining it with live notification evidence. Source inspection is
not measurement of runtime notification counts or crash-level exactly-once proof.

Core `build-dev/domain-migration/stored-notification-v1/verification-1/` and final
`verification-2/` verify24 actual production objects:23 freshly built Store/Chat/
Template objects with three linked DSOs, plus the actual Bridge entry object,
using immutable SDK2.5.2 and20 workers.13 metadata commands pass. Whole Bridge
still fails on private TG consumers; legacy specialized forwarding, historical
replay, SQL/runtime/loader acceptance remain open. No SDK/online data was changed.

## Original QQ source recall (SDK2.5.2, partial actor build)

`recall_bridge_delivery` now handles both directions without conflating targets:
TG source -> exclusive confirmed QQ outputs, or quoted TG output -> its uniquely
owned original QQ source. The latter uses a separate `SourceOperation` journal,
with actual source MessageRef/member and a distinct TG anchor proving ownership.
The anchor is not deleted, counted as a source receipt or inserted as an output.
Scope/member/anchor/attempt/observation/provenance checks are repeated atomically
at source-operation admission. Late anchor contribution growth cannot silently
change that authority while the provider is awaited.

Both paths share the stable command fence, pre-invocation owning unknown result,
full typed deletion result, exact terminal replacement and no replay/suffix
continuation. Source success feedback only claims the QQ original, not all TG
copies. Existing output codec/key recipes and old command operations are unchanged.
No old source-command result is invented when the new journal is empty.

This change's v4 candidate adds `bridge_source_operations`; maintained HEAD remains
schema3. Normal startup/reload validation rejects earlier v4 layouts missing this
table. The separate, migration-locked `upgrade_v4_source_operations_offline()` API
can add it after validating the old layout, without altering existing evidence.
It is owner maintenance code, not a CLI, deployment or permission to migrate online.
No SQL or migration was executed; actual backup/upgrade/recovery acceptance is open.

Repository/sender/TG-handler objects compile against immutable SDK2.5.2; the unchanged
command-boundary object is also verified.13 metadata checks and preservation checks
pass in core `build-dev/domain-migration/bridge-recall-source-v1/verification-1/`
and final `verification-2/`. The first repository build exposed a wrong transaction
API call; using the existing migration-lock API fixed it, and the failed log remains.
Whole Bridge still fails on private TG header consumers. Historical-only recall,
other management/edit/delete paths and SQL/provider/loader acceptance remain open.
Progress49/84 is unchanged; this is implementation/partial-build evidence only.

## TG-source output recall wiring (prior increment)

`/recall` now selects all known QQ outputs exclusively contributed by the quoted
TG source. The full original reply, pair and QQ scope are owned across worker
queries; unknown/shared/contradictory evidence refuses selection before effects.
Only absence of both v4 source and observed-output bindings permits the old
historical lookup. Known v4 QQ-original-source recall is explicitly unavailable
until its separate source-operation lifecycle is implemented, never fabricated as
an output operation.

A stable first-output checkpoint fences the entire original command. Replaying
it cannot delete again, resume an unattempted suffix or send fresh feedback.
The first intent includes the selected output plan; each output has its own
versioned terminal record containing the full typed DeleteReceipt/error result.
Owned unknown outcomes are installed before invocation and all results survive
later calls or persistence failure. Definite rejection allows the next independent
target; unknown/mismatched outcomes, cancellation and storage failure stop.
Success feedback requires all selected receipts to match and be persisted and
only reports *known* outputs. Feedback delivery itself has no outbox guarantee.

Mutation admission compares selected provenance inside its transaction. An
operation record prevents late attachment from adding new contributors to that
output while mutation is awaited; exact existing members/subset provenance replay
remain allowed. Additional contribution evidence requires explicit reconciliation.
No schema, configuration, retention rule, forwarding dedup key or SDK changed.

Four current production objects (repository, sender, command boundary, TG handler)
and13 metadata checks pass against frozen SDK2.5.2. Evidence is in core
`build-dev/domain-migration/bridge-recall-output-v1/verification-3/` and final
`verification-4/`. A temporary optional-vector allocation produced a GCC warning;
comparison now borrows the existing vector and the repository recompiles without
that warning. Two collector-only name errors were corrected and preserved.
Whole Bridge still fails on the private Telegram header consumers; no SQL,
provider, actor or loader execution occurred. Original-source/historical recall,
other management/edit/delete flows and runtime races remain task10.5/11.x/16.x.
Progress remains49/84; this is not complete recall acceptance.

## Output mutation admission and exclusive-member selection (prior increment)

The existing output-operation table now has transactional admission and exact
checkpoint-to-result finalization APIs. Only a newly Created checkpoint can
precede one provider call. Existing records never authorize another call or
imply success. Conflicting finalization fails; an identical terminal replay is
idempotent. The operation must bind an existing confirmed output with a bound
delivery and attempt. SQL columns, serialized identity, codec/version and stored
state are cross-checked; no new schema/default or online DB action is included.

The pure `bridge_delivery_member_outputs` selector collects all proven exclusive
outputs for one member, not only a quote anchor. It rejects unknown contributions
or an output shared with that member before returning any plan, while skipping
proven unrelated outputs. Original member records must match admission; legitimate
late members require actual output provenance rather than initial-list membership.
The same distinction fixes the prior reverse-owner check for late members.

Actual repository and sender production objects compile against frozen SDK2.5.2,
with13 metadata checks and preservation checks for existing schema/writes/sends.
Evidence: core `build-dev/domain-migration/bridge-output-operation-admission-v1/`.
Whole Bridge still fails on private Telegram header consumers. These APIs are
not yet connected to `/recall`; command-wide replay fencing, all owned mutation
results, original-source recall and runtime/SQL/crash verification remain open.
No task is closed by this preparatory increment; progress remains49/84.

## Command source ownership (SDK2.5.2 partial build)

`/userinfo` replies first resolve the original QQ member from the v4 physical
output's unique reverse owner. Observation and ownership are read in one database
transaction with exact pair/full output scope, including topic. A known output
with no owner, a changed original QQ route, contradictory evidence or multiple
owners never falls through to the historical scalar lookup. A known owner whose
MessageStore row has expired also cannot be replaced by a v3 mapping. Only no
observed v4 output permits the existing historical lookup.

The owning reply/QQ target/pair and services are captured before the worker await.
The selected QQ MessageRef is checked against the configured full QQ scope and
converted to its unchanged Store key through the named ReferenceResolver, not
opaque-ID parsing. The source user's stored ID is still required: neither a
confirmation, quote anchor, output sender nor cached self establishes authorship.
Explicit `/userinfo <QQ号>`, forced profile refresh, avatar policy and zero-effect
text fallback are unchanged.

The reverse-owner read now retains orphan evidence via LEFT JOIN, cross-checks
output ref, admission ID, SQL member scopes/ref/ID and the full unique initial
member when present. Late members absent from immutable admission require output
provenance. Available provenance must not contradict that owner or reference absent
input blocks. An explicitly audited initial legacy owner may lack detailed provenance;
this does NOT turn ownership into member-edit/delete contribution evidence.
Observation is an ephemeral read-result flag, not a persisted schema change.
No new table, write path, namespace, key, SDK API or provider operation is added.

Four current production objects (repository, sender, command boundary, Telegram
handler) compile against frozen installed SDK2.5.2;26 partial objects and13
metadata checks are recorded under core
`build-dev/domain-migration/bridge-command-ownership-v1/verification-2/` and final
`verification-3/`. Whole Bridge still fails on the legacy private Telegram header.
The first collector's missing GCC cxx11 symbol tag was corrected; its failure is
retained. No SQL/command/provider/loader execution occurred. `/recall` and reaction
bodies are unchanged; complete-output mutation selection and durable per-output
results remain task10.5, not completed by this query-only increment. Card SDK
expansion is awaiting explicit authorization and was not implemented here.

## Live market-face preparation with SDK 2.5 (partial build)

After explicit approval to build a new frozen SDK, Bridge now requires SDK/core
2.5. Its `market_face_view` identifies an original received image-family mface
receipt, with the existing summary caption and no inferred animation flag.
Original live refs enter the shared bounded photo preparer; GIF bytes remain
Animation, static JPEG/PNG use the existing Image preparation, and unsupported
formats/read failures retain the explicit preparation failure/replacement policy.
The presence of a market face selects the mixed-order layout, keeping its summary
on that item instead of sharing it across an unrelated photo album.

Missing/unusable source evidence remains an unsupported mface projection. Its
named archive view may supply only the predecessor text presentation
`[表情包:summary]` or `[QQ超级表情]`, never a reconstructed URL/media authority.
Source/index/category/uniqueness checks remain before submission. This does not
add support for market faces nested inside queried forward nodes.

Actual frozen SDK2.5.0 builds compile the current QQ forwarder and reference the
installed public market-face function; the core SDK has been separately built,
installed and sealed under `build-dev/domain-sdk-2.5.0/`. Existing SDK2.4.1 and
online core/processes are unchanged. Whole Bridge still fails in legacy Telegram
consumers; no runtime/provider/SQL gate or broad task is closed. Evidence lives
under core `build-dev/domain-sdk-2.5.0/mface-observation-v1/` and its parent.

## Typed ordinary face/shake display (partial build)

The ordinary QQ entry now accepts `onebot11.face` and `onebot11.shake`.
Face IDs come only from the named SDK `FaceView` restoration boundary, checked
against the exact source, normalized block index and category. Duplicate,
missing or conflicting views fail before media preparation and submission;
no absent ID is replaced with a fabricated zero. The existing display strings
`[QQ表情:id]` and `[戳一戳]` are preserved. These are text presentations, not
Telegram sticker sends or QQ poke operations.

Both mixed-order output and the all-photo caption path include these literals
and their original `body:index` provenance. No extra sender/member lookup is
introduced for a literal, and the same historical-output/replay gates precede
preparation. One durable send still owns all physical outcomes. Dice/rps have
no equivalent display rule in the predecessor processor and are not newly
accepted. Forward-node face rendering, mface and sticker migration remain open.

The current production `qq/text_forwarder.cpp` object compiled through the
frozen SDK2.4.1 actor build (Debug, 20 workers), incrementally using
`build-dev/domain-bridge-command-isolation-v1/`. Evidence is in core
`build-dev/domain-migration/bridge-qq-display-v1/verification-1/`.
Whole Bridge still fails on retained private Telegram headers. No provider,
SQL or actor runtime execution and no task closure are claimed.

## Command boundary isolated from legacy Telegram DTOs (partial build)

`BridgeCommandOperations` now owns the existing typed command implementation:
original reply reference, text/profile image outcomes, configured QQ scope,
profile refresh, recall, reaction and poke. Both command handlers depend only
on that installed-SDK boundary. The predecessor forwarding facade derives from
it and delegates construction, sharing the same gateway, resolver and pair
state; there is no duplicate cache, replacement DTO or second send path.

Lexical comparison against actual disk preimages preserves the constructor,
all twelve moved methods and both command-handler bodies (apart from the owner
type). Configuration defaults, message keys, mutation/fallback rules and command
behavior were not changed. Recall still uses the predecessor mapping lookup;
full-ledger command/edit/delete integration remains unfinished.

Two real `scripts/actor_build.py` builds against frozen SDK2.4.1, Debug and
20 workers compiled `bridge_command_operations.cpp` and both command handlers.
Dependency records exclude the legacy facade and private Telegram header.
Twenty-four production objects exist, but no whole Bridge DSO: legacy forwarding
and media consumers still require the removed private header. Thirteen metadata
checks pass. No SQL, provider, actor or loader execution is claimed. Evidence:
`build-dev/domain-migration/bridge-command-isolation-v1/verification-2/` in core.
No broad task is closed by this partial build.

## Typed forward-node preparation and GIF preservation (partial build)

The QQ typed entry now selects observed forward segments before the predecessor
handler. Named restore_special_content checks exact source/projection identity;
only its QqForwardRef enters an explicit typed GetOneBotForwardMessageRequest.
There is no archive-derived media read. Image references come from that lookup's
validated node observations, and ordinary source images retain their live refs.

New qq/forward_preparer retains ordered node display names/text and numbered
image references, followed by the image-batch layout. Ordinary non-image node
attachments retain their typed kind and position among collected attachments;
read or unsupported-codec failure stops preparation. SDK2.4.1's legacy forward
media reader is image-only, so these non-image cases are NOT claimed operational.
Unmodeled node content and unresolved mentions fail explicitly before submission
rather than being reported as a successful text-only copy. Face/card/inline-node
view coverage remains open.

Single, multi and forward-node images use prepare_photos. An explicit absent
image slot produces a replacement without inventing a MediaRef or reading an
archive URL. GIF headers and dimensions select Animation with unchanged owned
bytes; the static-photo normalizer never flattens GIF frames. This does not claim
full GIF decode validation. JPEG/static PNG retain normalization; unsupported,
invalid or unsafe formats retain replacement statistics. SDK plans the actual
compatible groups and first-output reply; captions retain the 11=>9+2 intent.

All preparation completes before the one durable send. Per-item bytes use the
existing configured limit; forward expansion shares the remaining SDK-bounded
total. The expansion ceiling is replaced with the actual prepared block count
before submission. Repeated ordinary-photo caption literals are checked against
the SDK wire ceiling before copying them per batch. This is not an exact memory
or cumulative-wire guarantee. Long text/captions are not truncated and may fail
SDK preflight. All physical outcomes remain in the same logical delivery.

Actual SDK2.4.1/Debug/20-worker actor builds compiled image_preparer,
forward_preparer and the current text_forwarder. Whole Bridge still fails on
retained private Telegram headers; no provider, decoder, SQL or application
execution took place. Evidence: core
build-dev/domain-migration/bridge-forward-images-v1/. No broad task is closed.

## Earlier persisted physical-output echo observation (partial build)

Both ordinary forwarding entries now consult resolve_observed_output before
historical-output checks, profile queries, media preparation and new admission.
The lookup compares pair, complete output scope and exact typed MessageRef. It
does not require reverse-primary ownership, known ordinal or Complete state:
confirmation-only and partial outputs are still proven physical outputs.

The old source route is deliberately not part of this query, so a route change
within the pair cannot turn an already recorded physical output into a fresh
source message. SQL joins and serialized output/admission identities are checked;
Missing/Unique/Ambiguous/Corrupt remain distinct. Ambiguous/corrupt state fails
closed, not as a resend reason. A unique match returns the ignored disposition
without a new delivery, send or success publication. Existing textual markers
remain separate predecessor heuristics.

This is observation of already persisted confirmations, not provider idempotency
or a claim to eliminate the arrival-before-confirmation race. It does not infer
self identity, query provider history, modify legacy rows or execute schema
migration. Actor/runtime linking, SQL execution and actual race/route-change
acceptance remain open. Real frozen-SDK compilation covers the changed repository,
sender helper and both ordinary entries; whole Bridge still fails on retained
private Telegram headers. Evidence: core
build-dev/domain-migration/bridge-output-echo-v1/. Task 11.2 is not closed.

## Earlier Telegram ordinary entry and original topic routing (partial build)

A real telegram/ordinary_forwarder is selected in the runtime before legacy
MessageEvent reconstruction. It owns MessageStored and its services across
awaits and never falls through after a preparation/admission failure. Edits
(the telegram.edited marker), albums, non-user senders, animations, stickers
and unsupported blocks remain on their still-unmigrated dedicated paths.

Routing uses ReferenceResolver::compatibility_from_legacy to validate the
canonical archive and obtain original group/topic facts. Group-mode accepts
any observed topic; topic-mode compares the exact original topic string, so
an absent topic cannot match configured topic 1 merely because their canonical
ConversationRefs coincide. The live complete source conversation still binds
the delivery, member lookup and reply. Target QQ references resolve once from
configuration. Old chat:/group: storage keys are retained; the group: Telegram
configuration alias is not persisted in their place. Archives confer no new
read or reply permission.

Existing delivery/historical output checks precede profile and media work.
Local target capability declarations reject missing owned-byte recipes before
source downloads, without claiming provider permission. Common get_member
provides display names; mentions remain display text without cross-platform
ping. QQ plain text is an explicit Bridge presentation choice. Sender/reply
prefixes are separate leading text, because QQ places attachment captions after
media. Topic-root reply hints keep the predecessor distinction; quotes require
same-conversation ledger evidence. The old literal [QQ] loop marker is retained.

Original MediaRefs use the shared bounded reader. Image/Video/Voice become
owning bytes and retain source order/captions. The typed video_note_view makes
Bridge's deliberate ordinary-video presentation explicit. Audio is not relabeled
Voice and File is not relabeled Image: SDK2.4.1 lacks these two target recipes,
so they fail clearly before downloading. PathManager-based conversion/cache
files and leases are not replaced or claimed migrated by this bytes path.
The configured per-item limit and overflow-safe owned-total bound remain
explicit. OneBot sends one ordered envelope under SingleMessageOnly/{1,...}.

All prepared content enters one versioned delivery with source block/prefix
provenance and full outcomes. The actual new ordinary_forwarder production
object compiles against frozen SDK2.4.1. Whole Bridge still fails on retained
private Telegram operations headers, and the old retry worker still cannot run
against v4. This is not a linked/runnable Bridge or provider acceptance. Evidence:
core build-dev/domain-migration/bridge-tg-ordinary-v1/. No tasks close here.

## Earlier QQ ordinary attachments and mixed order (partial build)

The typed QQ entry accepts all common ReceivedMedia kinds with resolved display
text. A shared bridge_media_reader calls read_media on the original reference,
validates the complete result, bounds bytes, checks kind/installation and any
available stable identity, and preserves the observed filename. Read size is
verified from bytes; unverified MIME hints do not override detected MIME. It
never uses native URLs, get-file IDs, get_record parameters or server paths.

For mixed image/voice/audio/video/animation/file messages, body order is retained.
Images use the same preparation/placeholder function; recovery annotations stay
with the affected image. Sender styling prefixes the first rendered content.
Audio and Voice remain distinct; there is no implicit codec conversion. Files
require an actual display filename. Typed file-read errors retain the existing
visible filename/size failure representation (worded as unable to read rather
than claiming a URL lookup); cancellation and other-kind failures stop before
submission. Unsupported encoding is left to SDK preflight, without retrying an
alternate kind after any possible send. Specialized QQ file/card/mface/forward
projection and Telegram conversion paths are not thereby implemented.

The whole prepared body goes through one delivery. Maximum physical outputs for
mixed content is the original body block count; per-item and owned-total bytes
use the same explicit existing configuration/SDK-bound derivation as photos.
Excessively long text/caption can fail preflight rather than truncate. Existing
all-photo caption layout is unchanged. The original textual [Telegram] prefix
loop guard now runs before archive/query/read/send work; it uses literal typed
text only and is neither author proof nor an authorization rule.

Real frozen-SDK production objects compile for bridge_media_reader.cpp and the
updated QQ entry. Whole Bridge still fails on predecessor private Telegram DTO
headers; no runtime/SQL/provider checks or task closure are claimed. Evidence:
core build-dev/domain-migration/bridge-qq-media-v1/. Tasks 9.x/10.1/10.2 and the
remaining source-extension, codec, retry and provider acceptance gates stay open.

## Earlier QQ static-photo preparation and delivery integration (partial build)

The same ordinary typed entry now accepts text plus ReceivedMedia of kind Image.
It passes the original MediaRefs to the shared qq/image_preparer; no restored
reference, native URL, get_image locator or provider path is used for reading.
Historical-output and existing-delivery checks run before preparation. Other
media kinds, special content, albums and non-user senders remain unmigrated.

Each read is bounded by the existing qq_media_download_max_bytes and the
remaining explicit total. The send total is derived as min(image count * that
per-item limit, SDK domain byte bound), with overflow-safe arithmetic. Physical
output allowance is the image count, not HTTP-call count. All photos are
prepared before the one AllowSplit delivery; text-only remains SingleMessageOnly.
No new memory/retention configuration or default was introduced. This bounds
owned output bytes and each read, not exact resident memory or cumulative wire
traffic across failed reads and placeholder acquisition.

The existing PhotoNormalizer and its dimension/pixel/process limits remain
unchanged and run on the blocking executor. JPEG and structurally static PNG
are accepted by this preparation path. PNG animation chunks are rejected before
single-frame normalization. GIF/APNG/WebP/BMP are not claimed supported static
Telegram photos; a typed Image whose bytes cannot establish a supported static
format receives the existing explicit placeholder policy plus a format warning.
A typed Animation does not enter this path. No new decoder, silent frame loss,
remote-file guess or cross-installation locator reuse was introduced.

Per-item read/format/dimension/normalization status is retained in the prepared
result; failed positions remain positions. The configured public placeholder
uses the existing bounded HTTP helper independently of bot media authority.
The final embedded placeholder is byte-identical to the predecessor and shared
through photo_placeholder.hpp rather than duplicated. Cancellation propagates
without sending. Prepared bytes remain owned through SDK submission and outcome
persistence; there are no temporary upload files in this path.

Bridge explicitly constructs its existing image-batch caption layout, including
11=>9+2 and sender/recovery text. The SDK still owns actual physical grouping
and first-output reply placement. Attachment captions are retained. Every photo,
caption contribution and recovery-summary contribution enters the same logical
delivery; there is no per-batch send/catch/continue or messages.front mapping.
Long captions are not truncated: SDK preflight can reject the whole prepared
request before message-producing effects.

Actual frozen-SDK builds compile image_preparer.cpp and the updated ordinary
forwarder. The whole Bridge remains blocked by predecessor private Telegram
headers; no Bridge DSO, runtime/media download, DB or provider result is claimed.
Evidence: core build-dev/domain-migration/bridge-qq-images-v1/. Forward-node
integration, other media formats, retry payloads/leases, full build and the
1/2/10/11/21 real-actor acceptance matrix remain open. Tasks 9.3/9.4 are not closed.

## Earlier QQ text forwarding integration (partial production build)

The real runtime now selects `qq::TextForwarder` from the admitted typed body
before reconstructing any predecessor MessageEvent. This first ordinary path
covers known-user QQ text, structured user/everyone mentions, sender display and
same-conversation replies. Media, specialized content, albums and non-user
senders remain on the not-yet-migrated predecessor path; this is not completion
of 9.1/9.2 or permission to run the old forwarder/retry worker against v4.

Configured QQ/TG conversations (including the original topic selection) are
resolved once through ReferenceResolver. Original group:/chat: source/target
keys are retained. Common get_member supplies display names through the existing
installation/group user cache; unknown profiles retain ID display, not guessed
identity. Sender brackets remain explicitly italic with a literal tab. Mentions
render as Bridge display text, never a cross-installation ping; the existing
show-sender switches suppress mention display when disabled. Text uses the
explicit single-message contract and limits {1,0,0}; retry attempt count derives
from the existing message_retry_max_attempts when enabled, otherwise one.

One owned MessageStored supplies the content and recorded receive time. Its
archive is used only for the pure source binding check. Delivery IDs hash the
pair, unchanged source identity and complete target; this does not rewrite any
old key. A matching existing delivery returns its durable state before member
queries or sending. New submission uses send_bridge_delivery, persists source
contributions/full outcomes, and never falls through to the predecessor sender
on failure. Ledger-native replies distinguish missing, unique, ambiguous and
corrupt results, use the explicit quote anchor or proven reverse primary owner,
and never restore unbound historical topic authority or query provider history.

The actor handles the complete durable result before its old scalar mapping
branch. Only newly completed persisted receipts emit MessageForwarded, carrying
all target_messages/output_details and delivery_id; the old scalar target ID is
only the explicitly selected quote anchor. Replays do not publish. Failed or
uncertain deliveries do not authorize replay of MessageStored. Unclassified
exceptions are nonretryable because they may follow admitted submission.
Common failure types were extracted into bridge_bot_failures.hpp so the actor
boundary no longer transitively imports private Telegram operation DTOs.

Real actor_build.py attempts against immutable SDK2.4.1 (Debug, 20 workers) compile
the new text-forwarder and updated delivery-sender production objects. The actor
now passes dependency scanning; its object and runtime/facade are not yet
compiled. Whole Bridge still fails on the predecessor private Telegram header;
there is no Bridge DSO or runtime/provider/DB acceptance. Evidence is in core
build-dev/domain-migration/bridge-qq-forward-v1/. Retry payloads/worker, media,
legacy binding and full actor acceptance remain open. No new tests or defaults.

## Earlier delivery submission/confirmation binding increment (partial build)

`bridge_delivery_sender.cpp` is now a real `bridge_core` production source.
`BridgeBotOperations::send_delivery` exposes its full result without a primary-ID
adapter; the real actor has a `BotSendConfirmedEvent` observation handler.
**Existing QQ/TG forwarding preparers and their old send calls are not yet wired
to this entry.** This is not a working whole Bridge or completion of 11.2.

A prepared request supplies original source identities/archive evidence, exact
configured target/topic, explicit body/policy/limits, per-block source provenance,
quote-anchor policy and total attempt budget. Pure SDK restoration verifies only
archive/reference consistency; restored views never supply live media authority.
The original legacy source keys are retained in a new ledger envelope, not written
over old columns. Both legacy normal mappings and album-member mappings gate new
admission; any historical output blocks submission pending explicit binding.
Native Missing/Unique/Ambiguous/Corrupt stay separate. Identical admission replay
and existing attempts never invoke a provider. Only durable Created admits the
single common BotClient send; full SendOutcome and every physical output follow
into the ledger. The result exposes the authoritative persisted snapshot plus
full outcome; do not make retry decisions from the raw outcome alone when an
independent confirmation contradicts it. Only a newly persisted complete receipt
with a Complete snapshot sets newly_completed. There is no outbox/exactly-once
publication claim. The existing actor boundary now suppresses a second
MessageForwarded for AlreadyPersisted and treats the new persistence-failure
exception as nonretryable; these actor changes remain source-only.

Notification correlation is a canonical, bounded delivery/attempt pair, not an
authorization token. The handler owns the original notification before awaits,
checks its envelope and persisted target/attempt, and only records observation.
It never starts the old forwarder/retry worker, emits MessageForwarded, or applies
a later route change as permission to discard an already observed effect.
Unknown order/provenance stay unknown until the terminal result provides proof.
All SDK output detail observations (including text ranges and reusable media
metadata) are preserved as versioned binary-safe CBOR evidence. A partial text
slice of a shared input block cannot invent contributor/primary ownership.
Quote-anchor choice is explicit and independent from reverse-primary evidence.

The unpublished v4 schema now permits NULL physical-output ordinal: SDK
confirmation does not carry sequence. Unknown-to-known enrichment updates the
SQL column and JSON atomically; known order is never cleared or changed by a
notification, and complete results require exact ordered outputs. Startup refuses
an older pre-release v4 NOT NULL ordinal table. No ALTER, online migration,
version lowering, guard removal, or DB execution was performed. Existing v3
migration and legacy binding/history acceptance still need an authorized copy.

`actor_build.py` configured the real Bridge/Store/path-mapping closure against
frozen SDK 2.4.1, Debug, 20 workers. The new sender and changed repository objects
compiled successfully (13 actual production objects overall). Whole build failed
at the retained non-installed telegram/bot/operations.hpp; no Bridge DSO or
actor-handler compilation claim. No private header, stub or test actor was added.
Evidence: core build-dev/domain-migration/bridge-delivery-binding-v1/. Retry
payloads/media leases, preparer routing, final logical event publication and
runtime/SQL/provider acceptance remain open. No config defaults were introduced.

## Prior typed management command increment (uncompiled)

All current Bridge command handlers now receive the original typed observation
(or its MessageRef for status), not a reconstructed MessageEvent. Recall, poke,
userinfo and react/unreact carry BridgeCommandSource by value through both
handler layers. Same-conversation reply IDs come from the SDK ReplyInfo/ReplyRef
and the generation-owned ReferenceResolver; unavailable/external/cross-scope
replies do not trigger guessed lookups. No native group/topic/reply JSON remains
in telegram/command_handler.cpp, and runtime command_message reconstruction is
removed. This does not remove the still-unmigrated ordinary forwarding adapter.

The mapping conversation key stays chat:<configured group>, derived from the
original matched GroupScope selector. Core matches that selector against the
original ACL subject, including None/Exact/Any. This retains the predecessor
mapping namespace even when core accepts a group: input alias; opaque IDs are
not decoded and the incoming alias is not written over stored keys. The original
typed reference independently retains the real topic for replies.

BridgeBotOperations now requires ReferenceResolver and its original installation
pair. It resolves every configured QQ conversation once, without I/O; unconfigured
QQ targets fail closed. QQ delete uses common BotClient with a full MessageRef.
Explicit QQ profile/forced member queries, poke and emoji-like extensions now
use typed UserRef/ConversationRef/MessageRef, preserving no_cache and the original
extension behavior. Existing mapping lookup/removal and user-cache code remain;
this does NOT establish v4 ledger provenance or permission to start the old
mapping path against schema4.

All management text feedback uses common reply_text. Userinfo's existing public
avatar URL is passed as HttpSource/Image with original scoped reply and caption,
SingleMessageOnly, and an explicit positive image budget from the existing
qq_media_download_max_bytes configuration. No option/default, MIME assertion,
download or host authorization rule was added. Provider-side remote bytes are
not claimed inspected or locally byte-verified. The existing text fallback is
now restricted to zero confirmed/unknown effects and DefinitelyNotSubmitted;
possible/confirmed photo submission and unclassified exceptions do not send
another message.

Feedback failures propagate to ActorFailure. Recall/poke/react/unreact cannot
be retried as a whole after feedback failure, even if that last feedback itself
was definitely unsubmitted. Read-only status/userinfo may report retryable only
with the full zero-effect checks; unclassified command exceptions never prove
zero effects. No mutation retry loop, new command names, DB keys, namespace or
schema was introduced.

Actual preimages, format and static checks are under core
build-dev/domain-migration/bridge-command-domain-v2/. This increment is not
compiled or run: predecessor ordinary media/send/edit APIs still prevent the
whole actor build. In particular the retained TG operations header is not an
installed SDK header. No private header was exported/copied, no stub/fake TU or
new unit/integration actor was used. Full 9.x/10.5/11.x completion remains open.

## Prior typed status reply increment (uncompiled)

The actual QQ/TG bridge_status command path now branches after the original
CommandScopeResolver match and pair lookup, before predecessor MessageEvent
conversion. It copies the original scoped MessageRef and passes it by value
through both handler layers, retaining the installation/conversation/topic and
reply ID across the heartbeat/database await. The old native reply/text segment
builder in QQCommandHandler is removed; Telegram's status path no longer calls
the legacy group-only reply helper. Other management commands remain separately
unmigrated.

BridgeBotOperations::reply_text calls common BotClient::send with typed Text,
the original reply conversation, SingleMessageOnly and explicit {1,0,0} limits
(one text message, no media). These are operation semantics, not configuration
defaults. The source must belong to the existing installation pair. Status
rendering, heartbeat keys/update/read order, pair/ACL selection and success-only
CommandCompleted propagation remain intact. Telegram topic replies no longer
lose their scope by reconstructing a group-only address.

The helper returns full SendOutcome. Handlers propagate SendFailure in an owning
BridgeBotSendFailure (confirmed references, output details and uncertainty),
instead of swallowing the send error and reporting command completion. At the
actor boundary the existing ActorFailure carries diagnostic/retryability, not a
new persisted ledger format. Retry eligibility requires zero confirmed/unknown
effects plus retryable + DefinitelyNotSubmitted. Unclassified status exceptions
are non-retryable; predecessor management-command handling is otherwise retained.
There is no retry/fallback loop or fabricated self identity in the new path.

This is source migration only. The whole actor is still blocked on predecessor
media/send/delete interfaces, including the non-installed
telegram/bot/operations.hpp; those interfaces were not copied into the SDK or
replaced by stubs to force a build. No synthetic compilation unit, test actor,
new test, application execution or provider call was used. Preimages/static
checks: core build-dev/domain-migration/bridge-status-reply-v1/. The partial
increment does not close 9.1/9.2/10.5 or claim full Bridge compilation.

## Typed MessageStored input increment (uncompiled)

BridgeActor now checks the typed MessageStored source against the original
envelope before repository resolution or awaits. Missing/conflicting archival
identity is a non-retryable input failure. The existing command-processed skip
stays first. The await_asio factory owns the original MessageStored value and
passes it through IBridgeForwarder into BridgeForwardingRuntime, rather than
discarding the projection. Runtime pair selection uses its explicit source
platform/configured bot. The explicit mapping-only branch uses the same archived
source keys, not an optional source_message_id override. Normal Store-produced
keys, target mapping fields, correlations and pair identities are unchanged.

These are consistency checks, not authentication or renewed reply/media
admission. The predecessor event conversion and content/send handlers still
need migration; this does not close the ordinary forwarding or ledger tasks.
The changed internal forwarder interface requires dependent implementations to
accept the typed stored input. No new unit tests or synthetic consumers were
written, and the whole Bridge actor is not compiled or run yet.

The package now requires Store >=0.2.1,<0.3.0 alongside SDK >=2.4/schema3.
Store/Chat/Template production builds passed separately, not Bridge. Preimages
and static source records are in core build-dev/domain-migration/bridge-stored-source-v1/.
No online database/configuration, retries, deployment or startup was performed.

## Typed command selection increment (uncompiled)

The forwarding runtime now receives the generation's CommandScopeResolver and
validates coordinator invocations with source_from_invocation. Command route
selection matches ResolvedCommandSource against each original configured opaque
scope, preserving None/Exact/Any and pair/direction rules. It no longer extracts
native sender/group/topic fields to authorize/select the route. The predecessor
MessageEvent conversion remains only as input to the separately unmigrated
command handlers; this is not a complete handler or forwarding migration.

The runtime constructor requires the resolver explicitly; package/CMake now
require SDK/core >=2.4. resolve_forwarder also resolves configuration/repository
outside its final publication lock: those accessors acquire the same nonrecursive
mutex themselves. A second locked check retains single-runtime publication.
This fixes a source-visible self-deadlock without running DB or actor code.

This increment is **uncompiled** while predecessor SDK operations remain. Source
preimages/checks: core `build/domain-migration-records/bridge-command-source-v1/`.
No new unit tests, native operations, configuration defaults or data migration.

## Prior command-scope source increment (uncompiled)

`BridgeActor::prepare_generation` now resolves each existing command selector
through the generation-owned `CommandScopeResolver` and publishes opaque
`ResolvedGroupScope` values. Missing services or invalid installation/group/topic
fail preparation. The original command route generator, pair keys, direction
switches and None/Exact/Any selectors are unchanged; empty publications remain
explicit. No provider operation or new database operation was introduced.

Package/CMake requirements now state SDK/core >=2.2 (the first release containing
this service) and actor input schema 3. This is a source compatibility gate, not
a build/release claim. The forwarding/runtime still uses predecessor SDK calls;
full configuration-target migration, typed command consumption and a complete
frozen-SDK actor build remain open. Core preimages and static checks are recorded
under `build/domain-migration-records/command-scopes-v1/`.

## Preserve and replace

- [ ] Migrate `bridge_actor`, event adapters, forwarding runtime and bot operations
  to the single MessageStored typed projection. Raw/physical confirmations remain
  observation paths and must not forward the same source twice.
- [ ] Preserve exact installation/pair/source/destination identities and existing
  group-versus-topic/command scopes through the centralized reference resolver.
- [ ] Preserve sender display, explicit italic/style/caption policy and member
  lookup. Cross-platform names are not verified pings; do not invent mentions.
- [ ] Ordinary content/query/send paths use BotClient; native JSON/entity/locator
  parsing stays in platform SDK adapters or explicit legacy restoration.

## Full media migration

- [ ] Share one bounded read_media -> owned-bytes preparation path for QQ single
  images, multiple images and forward-node images. Preserve per-item recovery,
  placeholders, safe dimensions, normalization and adjustment/replacement counts.
- [ ] Use explicit body/caption/AllowSplit and every output_details entry for
  multi-output delivery. Check 1/2/10/11/21 images, mixed content and long captions;
  never save only messages.front().
- [ ] Migrate Telegram photos and ordinary attachments through read_media and
  caller-owned final paths, without SDK shared-file registration. Preserve PathManager's path
  restrictions, blocking boundary, leases and cleanup after all effects settle.
- [ ] Migrate record/audio/voice/video/document/file with typed kind, filename and
  caption. Preserve source-file resolution and explicit failure; video-note
  conversion requires its typed platform view and explicit Bridge policy.
- [ ] Migrate face/mface, GIF/MP4/WebM animations and each supported sticker path.
  Keep cache/transcoding policy; do not silently make animations static or claim
  TGS playback/new decoder support.
- [ ] Use typed QQ card/miniapp/forward views. Forward text and prepared images
  belong to one logical delivery, stop on first failure, and never swallow a
  partial-send exception as success.
- [ ] Retain every scoped album member, ordering, dedup, timeout and late-flush
  policy. Group membership is not proof of output provenance or completeness.

## Delivery, retry and edit/delete integration

- [x] Define the delivery/source/output/provenance/retry schema and storage API,
  separating quote anchor from reverse-primary ownership. Independent source
  review closed binary-envelope/index/guard findings; runtime checks remain open.
- [ ] Bind full canonical SDK values and binary bodies to the versioned opaque
  storage envelope; keep media/lease and explicit total-attempt budgets.
- [ ] Persist attempt admission before provider invocation; correlate physical
  confirmations with the caller-owned attempt ID. A repeated admission is not
  permission to send again.
- [ ] Persist every complete/partial/unknown outcome and independent confirmation
  idempotently. Publish logical success only after complete persisted success.
- [ ] Allow whole-delivery retry only with zero confirmed/possible effects and
  explicit retryable + DefinitelyNotSubmitted evidence. No implicit continuation.
- [ ] Close the documented legacy-source binding gap before native admissions
  accept historical sources. Preserve original keys/primary/group evidence and
  quarantine unresolved legacy retries; do not fabricate typed references.
- [ ] Apply member-level edits/deletes only with proven provenance; shared or
  unknown output associations require reconstruction/refusal. Preserve explicit
  platform management extensions without claiming unavailable native events.
- [ ] Verify late-member attachment is atomic and evidence-backed; genuinely new
  late members may form a separate bounded delivery, never bypass old effects.

## Validation, compatibility and publication

- [ ] On an explicitly authorized disposable offline copy, exercise explicit v3
  import, crash/submitting recovery, DB failure, partial outputs, retry binding,
  reverse ownership and late albums with existing facilities. No online upgrade.
- [ ] Check the full approved rich-media/addressing/failure/recovery matrix and
  lease cleanup. Compilation is not image/playback or persistence acceptance.
- [ ] Upgrade package/CMake/docs and tighten Message Store's actor dependency to
  its typed-projection version, alongside SDK 2.x and input schema 3.
- [ ] Build through core `scripts/actor_build.py` using the new frozen installed
  SDK, at least six workers/all cores under low load, never live-core targets.
- [ ] Update the independent registry metadata/index and core conformance mirror;
  retain metadata-only status, without fabricated download assets or publishing.
- [ ] Prepare matching core/SDK/actor/config/full-DB backups. No version-only
  downgrade or old snapshot restoration that forgets new physical effects.

Remaining gates include platform typed extensions, source/reference projection,
complete forwarding/media/retry binding, new-SDK build and offline/runtime
acceptance. Storage review alone does not complete these tasks.
