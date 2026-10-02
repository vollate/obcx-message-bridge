## 1. SDK scope contract and publication

- [x] 1.1 Add the explicit actor-scope marker to command observations, generated contracts, and `ActorInputContract` parsing; test valid markers, malformed metadata, existing RE2 declarations, and legacy registrations without the marker.
- [x] 1.2 Add public data-only group/topic scope types and one exact-identity matcher; test installation isolation, no-topic/exact-topic/any-topic semantics, private rejection, explicit empty sets, and malformed QQ/topic combinations.
- [x] 1.3 Implement an owner-bound preparation publisher and candidate builder; test unknown/unmarked commands, wrong ownership, duplicate publication, invalid installation references, copied value ownership, and mutation rejection after freeze.
- [x] 1.4 Install the new SDK surface and update SDK header/surface auditing plus a standalone actor fixture that publishes scopes without core-private or platform-provider dependencies.

## 2. Generation construction and shared eligibility

- [x] 2.1 Separate command structural validation from availability finalization, supply owner-bound publishers during actor preparation, and reject missing required publications before a generation becomes ready.
- [x] 2.2 Bind finalized immutable scopes to canonical active routes and add the common route-aware eligibility result combining route, trusted identity, effective ACL, and actor scope; preserve ACL-only legacy commands and core-owned `help`.
- [x] 2.3 Route `/help` filtering through the common decision; verify deterministic order, descriptions, pagination, exact reply targets, and help-only output when all actor commands are unavailable.
- [x] 2.4 Gate dispatch with the same decision before transaction creation; implement bounded `command_unavailable` consumption without chat reply or transaction fallback, retaining ACL-denied precedence, alias behavior, unmatched traffic, and existing message observers.
- [x] 2.5 Add coordinator tests proving help/dispatch parity, zero unavailable command-handler/provider calls, no ordinary fallback for either route fallback mode, no cross-installation/topic leakage, and unchanged aggregate platform catalogs.

## 3. Bridge integration

- [x] 3.1 Extract shared pure command source-resolution/scope projection from Bridge's existing pair and group/topic mapping rules; preserve QQ direction checks and existing Telegram command-specific direction behavior.
- [x] 3.2 Mark `bridge_status`, `recall`, and `poke` as actor-scoped and publish their scopes from the parsed generation config, including the validation-only early-return path, without initializing forwarding services to answer availability.
- [x] 3.3 Reuse the shared resolution/matching helpers in Bridge execution guards rather than maintaining a separate applicability rule; retain target resolution, reply-specific validation, and valid-command completion behavior.
- [x] 3.4 Add Bridge parity tests for unmapped and mapped groups, multiple installation pairs with colliding native ids, group-to-group topics, exact topic mappings, disabled directions, private calls, and unsupported platform/command combinations.

## 4. ExHentai integration

- [x] 4.1 Mark `exhentai` as actor-scoped and publish exact scopes from parsed Telegram/OneBot destinations during startup, validation-only, and candidate preparation without new config options or destination lists.
- [x] 4.2 Share trusted destination normalization/matching with `authorized_destination`; retain direct-request identity consistency checks and reject contradictory raw/normalized installation/group/topic data.
- [x] 4.3 Add destination/authorization tests for absent groups, exact installations, no-topic versus positive-topic destinations, and private rejection; prove non-managers still see/search the canonical command while mutation subcommands retain manager enforcement.

## 5. Lifecycle and reported-case acceptance

- [x] 5.1 Add startup/validation-only fixtures for required missing versus explicit empty scopes, invalid publications, and scope derivation without additional provider/DB/worker side effects.
- [x] 5.2 Add successful and failed reload tests proving immutable old admissions, post-cutover scope changes, no active mutation from rejected candidates, and safe candidate/retired-generation teardown.
- [x] 5.3 Add a cross-actor integration regression using a synthetic equivalent of group `1004979108`: global ACL permits both commands, only ExHentai config includes the group, help lists `/exhentai` and `/help`, and manual `/bridge_status` dispatches no Bridge command handler.
- [x] 5.4 Exercise real installed-SDK scope preparation/finalization with affected actors and the standalone fixture; verify compatibility for actors without the marker and clean rejection by an unsupported runtime rather than silent scope loss.

## 6. Documentation and verification

- [x] 6.1 Update command routing and SDK documentation with scope publication, ACL intersection, unavailable-command consumption, generation lifecycle, legacy actor compatibility, and the textual-help versus platform-menu distinction; do not add configuration defaults.
- [x] 6.2 Build and run the affected core/Bridge/ExHentai command, contract, configuration, lifecycle, and installed-SDK suites with at least six parallel workers, using all available cores when load is low; record exact results and any remaining failures without touching live bot credentials, deployment config, or databases.
- [x] 6.3 Validate this OpenSpec change, review root and affected actor repository diffs, and record acceptance evidence. If committing is requested, run root `nix fmt` before every commit and remind the user to amend unsigned commits with GPG signing.
