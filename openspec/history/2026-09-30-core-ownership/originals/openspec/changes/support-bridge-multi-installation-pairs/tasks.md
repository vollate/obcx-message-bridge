## 1. Freeze single-pair behavior and multi-pair collision cases

- [x] 1.1 Inventory every Bridge configuration lookup, operation-client call, mapping/retry/media/cache/heartbeat repository call, and in-memory key that still uses platform or one global pair
- [x] 1.2 Add baseline tests proving the scalar installation fields and pair-less mappings retain current forwarding, command, notice, media, retry, reload, and shutdown behavior
- [x] 1.3 Preserve direct-forward repository operation-count assertions for new delivery, already-persisted delivery, retry completion, and deferred media-group ownership
- [x] 1.4 Add two-pair fake-operation-client fixtures with intentionally colliding native group, message, media-group, user, sticker, and file identifiers
- [x] 1.5 Add isolated version-1 Bridge database fixtures containing representative rows from every table affected by installation scoping

## 2. Extend actor contracts for repeated bot installations

- [x] 2.1 Add installable `ActorInputContract` values for finite bot-installation collections, item identity, cardinality, expected field types, and scalar/collection alternative groups
- [x] 2.2 Extend reflected actor contract generation to emit deterministic optional `bot_installation_collections` JSON without callable or runtime-object metadata
- [x] 2.3 Extend `ActorManager` contract parsing to accept valid collection constraints and reject unknown members, duplicate fields, invalid cardinality, unsupported types, and contradictory alternatives
- [x] 2.4 Extend runtime-generation configuration validation to require non-empty unique collection identities, validate every nested installation against enabled root bots of the expected type, and validate actor/root pair-id references
- [x] 2.5 Enforce mutually exclusive scalar and collection forms while preserving existing scalar `bot_installations` contracts for other actors
- [x] 2.6 Add ActorManager unit tests for deterministic collection/reference round-trip, malformed declarations, duplicate names, and executable/credential-like metadata rejection
- [x] 2.7 Add startup, `--validate-config`, and reload tests for valid collections plus missing, disabled, wrong-type, duplicate, and mixed-form installations
- [x] 2.8 Extend the standalone installed-SDK actor fixture to export and validate a repeated bot-installation collection

## 3. Resolve named Bridge pairs and exact source routes

- [x] 3.1 Add immutable named pair and route value types containing exact Telegram/OneBot installation refs and current group/topic direction flags
- [x] 3.2 Parse the named `installation_pairs` form and resolve current scalar fields into the same canonical single-pair model
- [x] 3.3 Parse mapping pair ids, default pair-less mappings only for one resolved pair, and reject mixed forms or ambiguous pair-less mappings
- [x] 3.4 Validate unique pair ids, disjoint installation membership, enabled expected bot types, valid pair references, and non-fan-out source routes
- [x] 3.5 Build generation-owned Telegram installation/group/topic and OneBot installation/group route indexes with no platform-only or first-pair lookup
- [x] 3.6 Update Bridge's exported configuration contract to declare the named collection and its scalar compatibility alternative
- [x] 3.7 Add focused configuration tests for two valid pairs, scalar compatibility, duplicate ids/installations, unknown pairs, duplicate routes, wrong surfaces, and disabled bots
- [x] 3.8 Update the tracked actor configuration example with named-pair, mapping-pair, command-route, and `legacy_state_pair` examples

## 4. Add the installation-scoped Bridge schema

- [x] 4.1 Add source/target installation fields to mapping, retry, media-group, user, sticker, QQ-sticker, and heartbeat storage models
- [x] 4.2 Replace production repository APIs with exact-installation parameters for reads, writes, updates, deletes, restores, and cache refreshes
- [x] 4.3 Add the namespaced Bridge schema-version table and create complete version-2 tables and indexes for an empty database
- [x] 4.4 Add config-aware legacy-pair resolution from scalar fields, the sole named pair, or explicit `legacy_state_pair`
- [x] 4.5 Transactionally rebuild and copy message mappings, retries, media-group mappings, users, sticker caches, QQ-sticker mappings, and heartbeats from version 1 to version 2
- [x] 4.6 Verify supported platform directions, copied row counts, uniqueness, table shape, and index creation before publishing schema version 2
- [x] 4.7 Reject ambiguous selectors, unknown platforms, malformed/colliding rows, and newer schema versions with full rollback and secret-safe diagnostics
- [x] 4.8 Make repeated version-2 initialization idempotent and reject version-1 migration during reload as restart-required
- [x] 4.9 Add migration tests for empty, scalar-owned, sole-pair, explicitly selected multi-pair, ambiguous, malformed, rollback, newer-version, and repeated-open databases
- [x] 4.10 Add repository tests proving colliding native ids coexist and remain isolated across every installation-scoped table

## 5. Route forwarding and direct mappings by selected pair

- [x] 5.1 Refactor `BridgeBotOperations` so every operation receives or derives the selected exact installation instead of storing one global pair
- [x] 5.2 Resolve each message's source pair from `source_bot` and its exact conversation route before constructing handlers or target requests
- [x] 5.3 Carry source and target installations through QQ/Telegram handler inputs, `DirectForwardOutcome`, and `BridgeForwardResult`
- [x] 5.4 Scope pre-send deduplication, direct actor-owned mapping writes, reverse lookups, updates, and deletes by both installations
- [x] 5.5 Include source/target bot ids in `MessageForwarded` payloads and source installation in emitted event identity while preserving current event types
- [x] 5.6 Preserve successful no-op behavior for unmapped, disabled-direction, loop-suppressed, and deferred events within the selected pair
- [x] 5.7 Add direct-forward tests proving equal native ids in two pairs call only their configured targets and retain one-write/no-recovery-read counts
- [x] 5.8 Add wrong/missing source bot and unavailable exact-target tests proving no provider call or alternate-pair fallback occurs

## 6. Scope replies, edits, recalls, notices, and commands

- [x] 6.1 Change `ReceivedMessageRepository` to query existing Message Store rows by source platform, source bot, conversation id, and message id with no platform-only fallback
- [x] 6.2 Route reply resolution, reverse mapping, Telegram text edit/replacement, and QQ/Telegram recall through exact installation-scoped mappings
- [x] 6.3 Route QQ notices by their exact OneBot source installation and reject notices outside configured pairs before mutation I/O
- [x] 6.4 Route `recall`, `checkalive`, and `poke` commands through the invocation's pair and scope heartbeat/user lookups to selected installations
- [x] 6.5 Add two-pair reply/edit/recall/notice/command tests with colliding message and user ids plus mismatched-source negative cases

## 7. Scope media buffers and provider-bound caches

- [x] 7.1 Include source Telegram installation in media-group buffer keys, flush captures, cancellation, and generation-shutdown draining
- [x] 7.2 Persist deferred media-group rows and their resulting message mappings with exact source and target installations
- [x] 7.3 Scope user display-name state, sticker conversion state, QQ-to-Telegram file-id mappings, and heartbeats by the relevant installations
- [x] 7.4 Pass the selected exact installation through Telegram fetch/photo/media-group/upload and OneBot file/member/forward lookup paths without changing conversion or cleanup behavior
- [x] 7.5 Add colliding-album tests proving independent debounce/flush ordering and no cross-pair mapping or send
- [x] 7.6 Add cache and heartbeat tests proving equal provider identifiers cannot be read, updated, or reused by another pair

## 8. Persist and dispatch retries by exact installation

- [x] 8.1 Add source and target installations to `MessageRetryEntry`, serialization conversion, duplicate identity, logs, and queue APIs
- [x] 8.2 Register retry callbacks by exact target installation and construct group/topic requests from the persisted installation rather than platform
- [x] 8.3 Scope durable enqueue, update, remove, ready-row restore, pre-send mapping check, successful mapping write, and cleanup by both installations
- [x] 8.4 Preserve `DefinitelyNotSubmitted` rescheduling and `PossiblySubmitted` terminalization independently for every pair
- [x] 8.5 Treat a removed/unavailable persisted installation as an exact typed route failure and never resend through another bot on the same platform
- [x] 8.6 Add concurrent two-pair retry tests for duplicate enqueue, restore, success, exhaustion, unavailable route, uncertain outcome, and completion-persistence failure
- [x] 8.7 Re-run reload and shutdown tests with pending/in-flight retries from several pairs to prove one generation worker remains the sole owner

## 9. Documentation, migration, and architecture gates

- [x] 9.1 Document the scalar compatibility form, named-pair form, disjoint-pair restriction, mapping pair requirement, command bot lists, and exact source routing
- [x] 9.2 Document the version-1 backup/startup migration/version-2 verification flow and the binary-plus-database rollback procedure
- [x] 9.3 Update Bridge documentation that still describes platform-only lookup, one live account, platform-only retry identity, or old partition guidance
- [x] 9.4 Add source-scan gates rejecting new platform-only Bridge mapping/retry/cache APIs, first-pair selection, and handler-owned bot objects
- [x] 9.5 Confirm raw ingress, Message Store schema/events, bot-operation action matrix, provider transports, media conversion, and unsupported platforms remain unchanged

## 10. End-to-end validation

- [x] 10.1 Run root actor-contract, runtime-generation, validation-only, reload, installed-SDK, architecture, and full test tiers
- [x] 10.2 Build and test Message Store unchanged against a fresh installed SDK and verify its existing source-bot/conversation queries remain compatible
- [x] 10.3 Build and run the complete standalone Bridge suite against the fresh SDK, including schema migration, two-pair business simulation, installed pipeline, and reload smoke
- [x] 10.4 Build and run `chat_llm` against the same fresh SDK to catch actor-contract ABI/package regressions
- [x] 10.5 Validate a scalar deployment config, a valid named multi-pair config, and deliberately invalid pair/migration configs with the rebuilt `--validate-config` executable
- [x] 10.6 Run `nix fmt` from the repository root and `git diff --check` in root, Bridge, Message Store, and `chat_llm`
- [x] 10.7 Run strict OpenSpec validation and confirm implementation contains no fan-out, new provider/action, typed-ingress, Message Store migration, outbox, reconciliation, blob, or rate-governor work
