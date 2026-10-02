## Why

Bridge can dispatch bot operations by exact installation, but the actor still permits only one Telegram/OneBot pair and persists most identities by platform alone. That restriction prevents safe multi-account forwarding because equal native message, media, user, or retry identifiers from different bot installations can collide or be replayed through the wrong bot.

## What Changes

- Add named Bridge installation pairs, each containing one exact `telegram.bot_api` installation and one exact `onebot11.qq` installation, and bind every group/topic mapping to one pair.
- Route messages, notices, commands, media groups, mutations, lookups, and replies from the exact `source_bot` plus mapped conversation to the corresponding target installation; never fan out or fall back by platform.
- Extend actor configuration contracts so startup, `--validate-config`, and reload can validate repeated/nested bot-installation references plus mapping/legacy pair-id references before actor activation.
- Add installation identity to Bridge mappings, retry rows, media-group state, user/sticker caches, and heartbeat state, with uniqueness and lookups scoped by the relevant source and target installations.
- Add a versioned, transactional Bridge schema migration. Existing single-pair rows are assigned only to an explicitly identified legacy pair (or the sole configured pair); ambiguous or invalid legacy state fails migration rather than being guessed.
- Preserve the current one-write direct-mapping owner, typed bot-operation boundary, finite retry policy, media conversion behavior, raw ingress, and Message Store schema.
- Keep the existing scalar installation fields and pair-less mappings as a single-pair compatibility form; reject mixing legacy and named-pair forms, and require explicit pair references once more than one pair is configured.
- **BREAKING**: deployments enabling multiple pairs must add pair identifiers to mappings, scope command routes to every intended source bot, and migrate Bridge-owned persistent state before the candidate generation can activate.
- Do not add new providers/actions, fan-out routing, an outbox, outcome reconciliation, a blob gateway, or a universal ingress/domain model.

## Capabilities

### New Capabilities

- `bridge-installation-scoped-state`: Versioned Bridge persistence, deterministic single-pair backfill, installation-scoped keys, and migration failure/rollback safety for all bot-owned state.

### Modified Capabilities

- `bridge-capability-forwarding`: Replace the one-pair restriction with named exact-installation pairs and deterministic per-message, command, notice, media, and mutation route selection.
- `bridge-forwarding-mapping-persistence`: Persist and query direct/deferred mappings using exact source and target installations while retaining single-owner writes and operation-count guarantees.
- `bridge-actor-message-retry`: Persist, restore, deduplicate, and submit retries through their exact target installation rather than a platform callback.
- `actor-abi-v2`: Allow actor configuration contracts to declare repeated/nested bot-installation references that generic pre-activation validation checks against enabled root bot definitions and expected types.

## Impact

- Root SDK/runtime: `ActorInputContract`, contract parsing, actor-aware configuration validation, validation-only/reload diagnostics, and fixture tests.
- `local_actor/obcx-message-bridge`: configuration model and examples, route indexes, bot-operation selection, forwarding outcomes, commands/notices/media, retry callbacks, storage models/repository, schema migration, diagnostics, and standalone/conformance tests.
- Bridge SQLite data: message mappings, retry queue, media-group mappings, users, sticker caches, QQ-to-Telegram sticker mappings, and heartbeats gain installation-scoped identities under a schema version.
- Message Store continues to use its existing `source_bot` and `conversation_id` columns; Bridge queries them explicitly but requires no Message Store schema or event-type migration.
- Operators keep current single-pair configuration unchanged, or opt into named pairs and provide the legacy-pair migration selector when an existing non-empty Bridge database cannot infer it from a sole pair.
