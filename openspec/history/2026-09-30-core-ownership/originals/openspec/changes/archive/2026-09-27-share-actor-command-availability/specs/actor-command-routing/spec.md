## MODIFIED Requirements

### Requirement: Help lists every permitted routable canonical command
For an authorized `/help` call, core SHALL evaluate the shared route-aware eligibility decision independently for every command routed to the exact source platform/installation. This decision MUST require the caller's effective ACL and any declared actor availability scope to permit the trusted source context. It SHALL render `help` and every eligible canonical actor command exactly once in deterministic canonical-name order with each registered description. It MUST NOT display commands routed only to other installations, access-denied commands, actor-unavailable commands, RE2 expressions, or matcher-derived aliases. Reserved `help` SHALL remain core-owned and governed by its ACL without an actor scope. Existing help bounds and exact reply targeting MUST be preserved.

#### Scenario: Caller has a restricted command override
- **WHEN** three commands are routed for the source bot and actor-available but one command's effective policy denies the caller
- **THEN** help lists the two permitted actor commands plus permitted `help`, and omits the denied command

#### Scenario: Same bot command names are aggregated
- **WHEN** multiple actors contribute distinct eligible commands to one bot scope
- **THEN** help renders one sorted combined list using their registered descriptions

#### Scenario: Another installation has commands
- **WHEN** an active command exists only for a different bot installation
- **THEN** it is absent from the caller's help output

#### Scenario: Actor does not serve the source group
- **WHEN** a command passes ACL evaluation but its actor scope excludes the caller's exact installation/group/topic
- **THEN** help omits that command using the same eligibility decision as execution

#### Scenario: No actor command is available
- **WHEN** the caller is authorized for `help` but no actor command is eligible in the conversation
- **THEN** help succeeds and lists only `/help`

#### Scenario: Eligible commands span pages
- **WHEN** actor-scope filtering leaves eligible entries requiring more than one permitted page
- **THEN** help sends every eligible entry once in deterministic order without truncating an entry or changing its exact reply destination

### Requirement: Active command routing is validated per generation
The generation builder SHALL combine candidate configuration, loaded actor contracts, configured bot metadata, available platform adapters, and generation-prepared actor availability scopes into one immutable command routing table. It MUST reject an inactive or missing actor, an undeclared command, a request type absent from accepted inputs, an unknown bot, a platform/bot mismatch, an unavailable adapter, an invalid fallback, or more than one active target for the same platform, bot, and canonical command name. It MUST structurally validate routes before actor preparation and reject missing or invalid required availability publications before the candidate becomes ready. Structural validation MUST NOT activate an incomplete table, and availability finalization MUST NOT mutate the active generation.

#### Scenario: Two actors claim one scoped command
- **WHEN** candidate configuration activates two actor registrations for the same platform, bot, and canonical command name
- **THEN** the candidate is rejected with a command-route-conflict diagnostic

#### Scenario: Same name is separated by bot scope
- **WHEN** two command routes use the same canonical name on distinct bot identities and every other reference is valid
- **THEN** the generation builds one unambiguous routing entry per bot

#### Scenario: Validation-only startup inspects commands
- **WHEN** `--validate-config` loads actors and a command route or required availability publication is invalid
- **THEN** validation reports the same command-contract, route, or availability failure without starting bots, ingress, command transactions, or catalog publication

#### Scenario: Preparation publishes an empty scope
- **WHEN** a marked active command has a valid explicit empty actor scope
- **THEN** its route remains active and valid but no source context is eligible for that command

### Requirement: Command observation sends a typed actor message
For every root `RawMessageEvent`, the generation's command coordinator SHALL perform command observation before ordinary configured pipelines. An unmatched event SHALL enter ordinary routing exactly once. For an active match that passes the shared ACL-and-availability eligibility check, the coordinator SHALL create a generation-scoped transaction, retain the source event, construct the actor-declared request envelope using the SDK's common command invocation schema, and submit that envelope to the selected actor through the normal actor scheduler and reflected message dispatcher. An ACL-permitted active match outside its actor scope MUST instead be consumed with `command_unavailable` before transaction creation and without applying transaction fallback. The command runtime MUST NOT directly invoke a handler function. Existing pre-command message observers SHALL remain unchanged.

#### Scenario: Event does not contain an active command
- **WHEN** the selected platform adapter reports no command or the normalized name has no active scoped route
- **THEN** the original `RawMessageEvent` enters its ordinary pipelines once without a command transaction

#### Scenario: Active command reaches its actor
- **WHEN** a raw event normalizes to an active `chat` route and passes its effective ACL and any actor scope
- **THEN** the configured actor receives its declared `ChatCommand` request with transaction identity, normalized name and arguments, source context, and inherited routing metadata

#### Scenario: Request message is unsupported at execution
- **WHEN** a command request reaches an actor generation that does not accept its declared request type despite prior validation
- **THEN** the transaction fails with a stable command-dispatch diagnostic and applies its configured fallback

#### Scenario: Recognized command is actor-unavailable
- **WHEN** a recognized command passes ACL checks but its actor does not serve the source context
- **THEN** the coordinator emits one bounded `command_unavailable` result without command-handler dispatch, command-triggered provider operations, automatic reply, or ordinary fallback routing

### Requirement: Access and help state follow generation lifecycle
Compiled policies, overrides, help routes, render bounds, catalogs, and finalized actor availability scopes SHALL be immutable and generation-owned. Validation-only and reload candidates MUST validate required scope publications and perform no help send or catalog publication. Old admitted commands SHALL finish under the old policy and scope snapshot; successful cutover SHALL apply only the new policy and scope to subsequent messages. Missing or malformed required scopes MUST reject a candidate without altering active help or execution eligibility.

#### Scenario: Reload changes an allowlist
- **WHEN** a valid candidate changes command access entries before cutover
- **THEN** active requests continue using the old entries until successful generation publication

#### Scenario: Candidate access configuration is invalid
- **WHEN** a candidate has an invalid policy, override, or help bound
- **THEN** reload rejects it while active routing, access, and published catalogs remain unchanged

#### Scenario: Validation-only runs
- **WHEN** `--validate-config` checks help/access configuration
- **THEN** it validates identities, output bounds, routes, operation capabilities, and prepared availability data without invoking command handlers/providers or publishing a catalog

#### Scenario: Reload changes an actor destination
- **WHEN** a valid candidate changes actor group/topic configuration
- **THEN** old admitted requests retain the old availability scope and post-cutover help/dispatch share the new scope

#### Scenario: Candidate omits a required scope
- **WHEN** a marked active command has no prepared scope in a reload candidate
- **THEN** reload fails and the active command table and help output remain unchanged

### Requirement: Platform catalogs aggregate active registrations
For each bot whose adapter supports command-catalog publication, OBCX SHALL derive one deterministic aggregate catalog from all active scoped actor command routes plus the reserved `help` entry. It MUST publish the complete aggregate rather than allowing individual actors to replace platform state independently. It MUST NOT publish access-filtered or actor-availability-filtered per-caller variants, RE2 pattern text, or matcher-derived aliases. Reconciliation SHALL begin only after startup activation or successful generation cutover. Publication failure MUST leave local routing active, expose degraded status, and use bounded retry without rolling back to a partially active generation.

#### Scenario: Multiple actors contribute Telegram commands
- **WHEN** two actors have distinct active commands for one Telegram bot
- **THEN** the Telegram adapter receives one sorted aggregate catalog containing both registrations and one `help` entry

#### Scenario: Access policy denies one caller
- **WHEN** a command is active but denied to a particular group or user
- **THEN** the platform-wide catalog still contains that canonical command while caller-specific `/help` filters it at invocation time

#### Scenario: Candidate preparation succeeds before cutover
- **WHEN** a candidate command table is valid but has not become active
- **THEN** no platform command-menu mutation occurs

#### Scenario: Remote catalog update fails
- **WHEN** the platform rejects or times out an aggregate catalog publication after activation
- **THEN** local command detection, access enforcement, and help routing remain active while diagnostics expose the desired catalog, last outcome, and retry state without credentials

#### Scenario: Actor scope excludes one conversation
- **WHEN** an active command is unavailable in one caller's group/topic according to actor configuration
- **THEN** the aggregate catalog still includes it while that caller's textual `/help` omits it
