## Why

`/help` already filters command ACLs, but it cannot see actor-owned group/topic configuration. A group such as QQ `1004979108`, configured for ExHentai but not Bridge, therefore sees `/bridge_status` when the global ACL permits it, even though Bridge will not handle it. Help and dispatch need one configuration-derived availability decision rather than a second manually maintained allowlist.

## What Changes

- Add an opt-in, data-only actor command availability contract. During generation preparation, participating actors derive immutable exact-installation group/topic scopes from their existing parsed configuration.
- Add one core eligibility check combining the active scoped route, effective command ACL, and actor-provided availability. Both `/help` filtering and command dispatch use this check.
- A recognized but actor-unavailable command is consumed with a bounded `command_unavailable` result before command-handler dispatch; transaction fallback does not apply. Existing pre-command message observers are unchanged.
- Integrate Bridge (`bridge_status`, `recall`, `poke`) and ExHentai (`exhentai`), preserving their existing installation, group/topic, platform, and direction semantics. Private conversations are unavailable for these group-only commands.
- Keep argument validation, reply requirements, transient failures, and ExHentai manager-only subcommand authorization inside handlers. Configuration availability is not a guarantee that an individual invocation will succeed.
- Freeze scopes per generation, validate required scope publication before activation, and keep old admitted requests on their original snapshot across reload.
- Preserve ACL-only behavior for non-participating actors, the reserved core `/help`, and the unfiltered bot-wide platform command catalog. No new operator configuration options, inferred configuration defaults, or duplicate group lists are introduced.

## Capabilities

### New Capabilities

- `actor-command-availability`: Actor-derived, data-only command scopes, shared eligibility evaluation, Bridge/ExHentai scope semantics, and immutable generation publication.

### Modified Capabilities

- `actor-command-routing`: Filter help and gate command dispatch with the same actor-aware eligibility decision while retaining ACL enforcement, exact routing, deterministic help, and aggregate platform catalogs.
- `actor-abi-v2`: Extend command registration metadata with a data-only availability marker and expose an installed-SDK preparation-time scope publisher without a new executable export or handler callable.

## Impact

- Root SDK/runtime: command observation metadata and contract parser, a public availability scope/publisher API, generation preparation/finalization, command routing/help, and SDK install/surface checks.
- `local_actor/obcx-message-bridge`: command declarations, generation preparation, shared group/topic command scope resolution, and command conformance tests.
- `local_actor/obcx-exhentai-fetch`: command declaration, generation preparation, destination authorization helpers, and command/configuration tests.
- Tests/documentation: actor contract fixtures, core eligibility/help/dispatch tests, validation-only/reload coverage, installed-SDK actor tests, and command routing/operator documentation.
- Observable behavior changes for participating actors: unavailable commands disappear from help and no longer enter handlers or ordinary fallback pipelines. No database migration or live configuration edit is required; updated actors require a runtime that understands the new registration metadata.
