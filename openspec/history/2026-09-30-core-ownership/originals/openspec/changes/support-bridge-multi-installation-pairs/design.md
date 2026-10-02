## Context

The capability-based bot-operation change made `BotInstallationRef` and exact installation dispatch available to actors, but Bridge deliberately remained a single-pair actor. `BridgeConfig` still owns one `telegram_installation`, one `onebot11_installation`, and a mapping cache keyed only by native Telegram/QQ group ids. The forwarding runtime constructs one `BridgeBotOperations` object for that pair, validates every source against those two ids, and registers retry callbacks by platform.

Bridge-owned SQLite state is also platform-scoped. Message mappings, retries, media-group mappings, users, sticker caches, QQ-to-Telegram sticker ids, and heartbeats either omit installation ids or use platform as the bot identity. Equal native ids from two accounts can therefore overwrite, deduplicate, recall, edit, or retry each other. The Message Store already persists and emits `source_bot` and `conversation_id`; no Message Store migration is required to remove this Bridge-only ambiguity.

This change crosses the root actor contract, Bridge configuration/routing, every stateful Bridge path, and a live SQLite migration. It must retain exact-installation bot operations, conservative retry safety, one-owner direct mapping writes, generation-owned workers/buffers, and current QQ/Telegram behavior. No additional platform or bot action becomes supported.

## Goals / Non-Goals

**Goals:**

- Allow one Bridge actor/database to configure multiple named Telegram/OneBot installation pairs.
- Select exactly one pair and target installation from `source_bot` plus the configured conversation route, without platform fallback or fan-out.
- Validate every repeated installation reference against enabled root bot definitions before activation, validation-only success, or reload cutover.
- Scope all Bridge-owned bot identities and durable retry/mapping keys by exact installations.
- Migrate the prior single-pair schema transactionally and deterministically, with no guessed legacy ownership.
- Preserve direct mapping operation counts, typed failure/retry semantics, media behavior, command behavior, reload ownership, and shutdown safety.
- Keep the current scalar pair and pair-less mappings usable as the single-pair compatibility form.

**Non-Goals:**

- Pairing one installation with several peers, routing one source conversation to several targets, or adding broadcast/fan-out semantics.
- Adding `qq.official`, another provider, or another bot-operation action.
- Introducing typed ingress, a portable message/content model, or a Message Store schema/event migration.
- Adding an outbox, provider reconciliation, operation ids, exactly-once delivery, a blob gateway, or a rate governor.
- Repairing the pre-existing lack of conversation/group columns in every legacy message-mapping key; this slice adds the installation dimension while retaining current native-id behavior within one installation.
- Hot-changing process-owned bot credentials/transports or performing an incompatible schema migration while an old Bridge generation is active.

## Decisions

### 1. Resolve configuration into named, disjoint installation pairs

The new form is a non-empty `installation_pairs` collection:

```toml
[actors.bridge.config]
legacy_state_pair = "primary" # needed only to assign non-empty v1 state

[[actors.bridge.config.installation_pairs]]
id = "primary"
telegram_installation = "telegram_bot"
onebot11_installation = "qq_bot"

[[actors.bridge.config.installation_pairs]]
id = "secondary"
telegram_installation = "telegram_bot_2"
onebot11_installation = "qq_bot_2"

[[group_mappings.group_to_group]]
pair = "primary"
telegram_group_id = "-1001"
qq_group_id = "10001"
```

Pair ids, Telegram installations, and OneBot installations are each unique across the collection. Each installation therefore belongs to exactly one pair, making message, notice, and command source selection deterministic before native conversation matching. A mapping names one pair. A source route may map to one target only; duplicate enabled routes are rejected rather than interpreted as fan-out.

The existing scalar fields remain a compatibility form when no `installation_pairs` collection exists. They resolve to one internal pair, and existing mappings without `pair` resolve to that sole pair. The two forms cannot be mixed. In the named form, an omitted mapping pair is accepted only when exactly one pair exists; it is invalid when two or more pairs exist.

Every pair must reference two different enabled root bots of types `telegram` and `qq`. `onebot11.qq` remains distinct from `qq.official`. Pair aliases are configuration labels only and are never treated as bot identities in requests or storage.

**Rejected alternatives:** putting installation ids directly on every mapping repeats credentials-adjacent routing data and makes migration diagnostics difficult; allowing an installation in several pairs makes notice/command routing ambiguous before a group is known; selecting the first compatible pair recreates the fallback removed by the preceding change.

### 2. Build immutable exact-source route indexes per actor generation

`BridgeConfig` resolves named pairs and mappings into immutable route values containing:

- pair id;
- exact Telegram and OneBot `BotInstallationRef` values;
- native Telegram group and optional topic;
- native QQ group;
- direction and sender-display flags.

The generation builds separate indexes for Telegram group/topic sources and OneBot group sources, keyed by exact source installation plus the existing native route fields. Message, notice, and command handling first requires `source_bot`, resolves its one pair, then resolves the conversation route. No route is a successful no-op as today; a mismatched or unknown configured installation is a route/configuration failure. Direction-disabled and loop-suppressed traffic remains a successful no-op.

`BridgeBotOperations` no longer stores one global pair. Its methods receive the selected installation/target value, or a lightweight pair-bound view is created from the shared data-only client. Bot objects, registries, credentials, and transports remain process-owned and unavailable to Bridge.

`BridgeForwardResult`, `DirectForwardOutcome`, and emitted `MessageForwarded` data carry source and target installation ids in addition to current platforms and message ids. Event ids include source installation so two bots with equal native message ids cannot emit the same Bridge identity.

**Rejected alternatives:** one forwarding runtime per pair duplicates retry workers and state ownership; routing by platform plus group still collides when two bots expose equal native ids; persisting pair aliases would make a harmless alias rename look like a new bot identity.

### 3. Scope all Bridge-owned bot state by installation

The current tables remain Bridge-owned but move to schema version 2. Exact installation columns participate in all provider identity and uniqueness rules:

| State | Installation scope |
| --- | --- |
| direct message mappings | source installation + existing source identity + target installation |
| message retry queue | source installation + existing source identity + target installation |
| media-group mappings | source installation + media-group/message identity + target installation |
| user cache | installation + user/group identity |
| sticker cache | source installation + sticker hash |
| QQ sticker to Telegram file-id cache | source OneBot installation + target Telegram installation + hash |
| platform heartbeat | installation (with platform retained as metadata) |

Storage models and repository methods accept installation ids explicitly; a platform-only overload is not retained in production. Queries, updates, deletes, retry completion, deferred media-group writes, recall/edit lookup, checkalive, and cache refresh all use the same scope. Diagnostics may include non-secret pair/installation ids but never credentials, payloads, signed media URLs, or complete provider responses.

The in-memory Telegram album key becomes `(source_installation, chat_id, media_group_id)`. In-memory retry callback registration is keyed by exact target installation, not platform. `ReceivedMessageRepository` queries the already-scoped Message Store columns using source platform, source bot, conversation id, and message id; it does not use a platform/message-only fallback.

This change deliberately does not add missing conversation columns to every old Bridge mapping key. New and existing handlers continue supplying current group/topic values where already required. A later typed-ingress/domain migration can make every persisted message identity a complete conversation-scoped `BotMessageRef` without coupling that broader migration to multi-account isolation.

### 4. Introduce a transactionally rebuilt, versioned Bridge schema

A namespaced `bridge_schema_version` table records schema version 2. A database with current unversioned Bridge tables is treated as version 1. Initialization remains under `DbManager::with_migration_lock`, and migration performs all table rebuilds, copies, index creation, row-count checks, and schema-version publication in one SQLite transaction.

For every v1 row, source/target platform determines which member of one designated legacy pair supplies its installation id. The designation is deterministic:

1. the scalar compatibility pair, when that form is active;
2. the sole named pair, when exactly one exists; or
3. `legacy_state_pair`, when several named pairs are configured.

A non-empty v1 database with several pairs and no valid selector is rejected. Unknown platforms, a selector that does not exist, duplicate rows produced by migration, row-count mismatch, malformed table shape, or a schema version newer than supported aborts the transaction and leaves version 1 unchanged. Empty/new databases create version 2 directly without requiring a selector. Running initialization again at version 2 is idempotent.

The migration stores installation ids, not pair aliases. `legacy_state_pair` is only migration input and has no effect after version 2 is committed. A schema migration is restart-required: reload may use an existing version-2 database, but a candidate must not rebuild v1 state while the active generation can still access it.

**Rejected alternatives:** adding nullable columns leaves unscoped rows available to accidental fallback; keeping old uniqueness indexes prevents equal ids from different installations; assigning all rows to the first pair silently corrupts ownership; dual-writing v1 and v2 tables extends the compatibility window and risks divergent retry/mapping state.

### 5. Extend the actor contract with finite collection constraints

The V2 contract retains schema version 1 and adds optional deterministic `bot_installation_collections` and `collection_identity_references` configuration members. Each installation constraint names an actor-config collection, a minimum item count, an item identity field, and bot-installation fields with allowed root bot types. Identity-reference constraints can bind an actor scalar or fields in named root table arrays to that collection identity. `ActorManager` parses and validates the declarations; runtime-generation validation checks every collection item against enabled `[bots]` entries and every configured pair reference before actor activation.

The Bridge contract declares unique pair ids plus `telegram_installation: telegram` and `onebot11_installation: qq`. The scalar `bot_installations` constraints remain supported for actors and for Bridge's legacy form. The collection constraint includes an explicit alternative-group identifier so generic validation requires exactly one of the scalar pair form or collection form rather than both.

Finite declarative constraints cover required/non-empty collection items, unique item ids, required installation fields, enabled bot existence, expected type, mutually exclusive forms, optional scalar references such as `legacy_state_pair`, and pair fields in root `group_mappings` arrays. Bridge's parser additionally validates non-fan-out route uniqueness using the same resolved immutable config. Validation-only and reload tests must prove that invalid nested or pair-id references fail before bots start or candidate activation.

**Rejected alternatives:** hard-coding Bridge table names in core configuration validation couples core to one actor; validating only on the first message activates a broken generation; a callable actor validator would add an executable ABI surface when a finite data contract is sufficient.

### 6. Persist retries with their exact dispatch target

`MessageRetryEntry` and `MessageRetryInfo` contain source and target installation ids. The durable identity and in-memory duplicate check include both. Restore registers/looks up callbacks by target installation and constructs the exact `GroupTarget`/`TelegramTopicTarget` stored for that row. Pair aliases and current platform defaults are not consulted during resend.

A successful retry persists a mapping with the same source/target installations before deleting its row. A pre-send mapping check uses that exact scope. `DefinitelyNotSubmitted` remains the only automatically rescheduled class; `PossiblySubmitted` remains terminal/diagnostic. Removing or renaming a pair alias does not retarget a durable retry, while disabling/removing its actual bot installation makes the row terminal or unavailable according to the existing typed route policy rather than sending through another bot.

### 7. Keep generation ownership and operation counts unchanged

One Bridge generation still owns one retry worker and one Telegram media-group buffer, now multiplexed by installation. Reload stops the old generation's worker/buffer before the candidate processes ingress. Direct sends still perform one pre-send lookup and at most one actor-owned direct mapping write; retry and deferred media groups retain their specialized write ownership. Installation columns do not reintroduce post-send mapping-recovery reads.

Tests retain separate operation counters and add colliding-id cases proving that the same platform/message/media/user ids can coexist in two pairs without cross-read, overwrite, recall, edit, or retry. Fresh installed-SDK Bridge tests remain the acceptance boundary; production credentials and databases are not used.

## Risks / Trade-offs

- **[A v2 migration is not readable by the preceding binary]** → Take a database snapshot before deployment, make migration startup-only, and restore both the preceding binary and snapshot for rollback.
- **[An operator selects the wrong legacy pair]** → Require an explicit existing pair id when inference is impossible, log a secret-safe migration summary, and validate row/platform counts transactionally before commit.
- **[Pair removal strands durable retries or caches]** → Persist exact installations, report unavailable routes without fallback, and document an operator drain/inspection step before disabling installations.
- **[Collection-contract support increases the V2 contract parser surface]** → Keep the extension optional, finite, deterministic, schema-1 compatible, and covered by malformed-contract plus validation-only tests.
- **[Installation scoping does not solve native message-id collision between groups on the same bot]** → Preserve current behavior explicitly and defer complete conversation-scoped persistence to the typed-ingress/domain-model change.
- **[One worker multiplexing several pairs can increase queue latency]** → Retain bounded batch/check behavior and measure pair-isolation/queue progression; do not add actor-local worker pools.
- **[Legacy scalar support creates two config forms]** → Resolve both immediately into one canonical pair model, reject mixing, and document named pairs as the multi-account form.

## Migration Plan

1. Add contract parsing/validation for installation collections while retaining existing scalar constraints and validation-only behavior.
2. Add canonical pair/route configuration and tests, keeping the scalar single-pair form behavior-identical.
3. Add version-2 storage models/repository APIs and migration fixtures for empty, valid v1, invalid/ambiguous v1, repeated initialization, and rollback-on-error databases.
4. Migrate direct mappings, lookup/edit/recall, user/sticker/heartbeat state, media-group state, and Message Store reads to exact installation scope.
5. Migrate retry persistence/callbacks and in-memory media-group keys, then preserve lifecycle and operation-count gates.
6. Deploy the new binary first with the existing scalar single pair. Stop OBCX, snapshot the database, start once to complete and verify schema version 2, and validate normal forwarding/retry behavior.
7. Convert configuration to named pairs, add `pair` to mappings and all desired command-route bot ids, run `--validate-config`, then restart or reload only after schema version 2 is confirmed.
8. Roll back by stopping OBCX and restoring both the preceding binary and the pre-migration database snapshot. Do not run the preceding binary against schema version 2.

## Open Questions

None. This slice intentionally supports disjoint one-to-one pairs and installation-scoped Bridge state only; shared-installation graphs, fan-out, and complete conversation-scoped mapping identities require later proposals.
