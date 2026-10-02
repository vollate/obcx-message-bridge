# Implementation and acceptance evidence

## Delivered

- Opt-in `actor_scoped(...)` declaration emitting `availability = "actor_scope"`, an installed data-only SDK scope/matcher/publisher, and strict contract validation.
- Owner-bound preparation publication with copied values, validated installation identities, explicit-empty versus missing publication, and immutable generation finalization.
- One eligibility decision for textual help and dispatch: active exact route, effective ACL, then actor scope. Unavailable recognized commands are consumed as `command_unavailable` before transaction creation, without a command reply, handler invocation, or ordinary fallback.
- Bridge scopes and execution share its installation-pair/group/topic resolver. ExHentai scopes and authorization share exact destination matching. Existing command arguments, reply checks, manager checks, legacy actors, pre-command observers, and unfiltered platform menus are preserved.
- Startup, validation-only, failed candidates, draining admissions, and successful cutover are covered. Routing and SDK author documentation describe the new opt-in contract.

## Verification

Top-level compilation and CTest invocations requested 20 parallel workers (the available CPU count). The installed bot-SDK harness was also updated to use the available cores, with a minimum of six workers, instead of its previous fixed two. No outstanding test failures remain.

| Check | Result |
| --- | --- |
| `cmake --build build --parallel 20` | Complete workspace build succeeded |
| `ctest --test-dir build --parallel 20 --output-on-failure -LE installed-sdk` | 890/890 passed |
| `ctest --test-dir build --parallel 20 --output-on-failure -R '^actor_sdk_v2_smoke$'` | 1/1 passed; rebuilt and installed the standalone scoped SDK actor |
| `ctest --test-dir build --parallel 20 --output-on-failure -R '^bot_sdk_(common\|onebot11\|telegram)_isolation$'` | 3/3 passed |
| Independent Bridge/ExHentai build against `build/sdk-v2-smoke-install` | Both actor DSOs and affected tests built without the core source include directory; installed under `/tmp/obcx-availability-installed/prefix` |
| Independent installed-SDK Bridge/ExHentai configuration, authorization, and command selection tests | 59/59 passed |
| Target-group integration with `OBCX_AVAILABILITY_ACTOR_DIR=/tmp/obcx-availability-installed/prefix/lib/obcx/actors` | 1/1 passed using installed real actors |
| Earlier runtime loading the newly installed scoped SDK fixture | Clean rejection: `actor command registration contains an unsupported member 'availability'` |
| `openspec validate share-actor-command-availability --strict` | Passed |
| Root and both affected actor repositories: `git diff --check` | Passed; changed C++ files formatted with the root `.clang-format` |

The earlier-runtime probe used an existing pre-change installed core, SHA-256
`68af1577b7d492f9629563fa7f58b00478e8f4eff610622216797df2772467eb`,
before the final SDK-isolation tests refreshed that installation. The rejection
was verified by a small loader using `ActorManager`, not inferred from a header
or a synthetic parser.

The independent actor suites were selected with
`^(BridgeActorTest|BridgeCommandOperationTest|ExHentaiConfigTest|ExHentaiAuthorizationTest|ExHentaiCommandTest)\.`.
The regular workspace suite includes the same target-group integration test,
plus actual reload-controller draining/cutover and observer/fallback regressions.

Detailed local logs are in `/tmp/obcx-availability-{full-build,final-build,full-tests,final-sdk-tests,final-bot-sdk-tests,installed,installed-tests,installed-integration,legacy-probe}.log`.

## Target-group regression

`ActorCommandAvailabilityIntegrationTest.RealActorsHideBridgeInExHentaiOnlyGroup`
uses a synthetic QQ installation and group `1004979108`, with the global ACL
permitting both actors, an ExHentai destination, and no Bridge mapping:

- `/help` contains `/exhentai` and `/help`, but not `/bridge_status`.
- The bot-wide catalog still contains `bridge_status`.
- Manual `/bridge_status` yields `command_unavailable`, no actor stage or emission,
  and no additional provider call.
- Actor preparation and command handling leave the temporary SQLite schema
  unchanged from the post-configuration baseline.

## Workspace and operational boundaries

Changes span the root repository and the separate repositories
`local_actor/obcx-message-bridge` and `local_actor/obcx-exhentai-fetch`.
The user-approved local SDK provenance refresh updated
`.package-state/providers/sdk.json`, its reference in `packages.toml`, and the
associated development lock/resolved graph; provenance checking remains enabled.
No new production configuration options/defaults, duplicate group lists,
deployment configuration, flake configuration, live credentials, or live
databases were changed. Tests used fake gateways and temporary local storage.
No commits or deployments were made. If committing later, run root `nix fmt`
before every commit and GPG-sign the commits (amend any unsigned commits).
