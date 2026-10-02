## ADDED Requirements

### Requirement: Actors derive command scopes from authoritative configuration
A participating actor SHALL derive a finite data-only scope set for each marked canonical command from the same validated immutable configuration used by execution. The scope set MUST identify exact normalized platform, installation, native group, and an explicit topic selector. Topic selectors SHALL distinguish no topic, one positive Telegram topic, and any topic including no topic. QQ scopes MUST select no topic. Private contexts MUST NOT match group scopes. Core MUST NOT parse actor-specific configuration fields or require operators to duplicate actor group lists into new command configuration.

#### Scenario: Installation identity is exact
- **WHEN** two installations contain the same native group id but the actor publishes a scope for only one installation
- **THEN** only that installation can match the scope

#### Scenario: Topic identity is exact
- **WHEN** a scope selects one positive Telegram topic
- **THEN** another topic and the no-topic context in that same group do not match

#### Scenario: Whole-group command supports topics
- **WHEN** an actor explicitly publishes an any-topic scope
- **THEN** both no-topic and positive-topic contexts in that exact group match

#### Scenario: Empty scope is explicit
- **WHEN** an actor publishes an empty set for a marked command
- **THEN** the command is unavailable in every conversation without making the configuration invalid

### Requirement: Scope publication is generation-owned and validated
Actors SHALL publish scopes through an owner-bound SDK preparation service. Publication MUST reject another actor's command, an undeclared or unmarked command, duplicate publication, malformed identities/topic selectors, unknown or disabled installations, and platform/installation mismatches. Every active marked command MUST have a publication before the candidate becomes ready. The finalized table MUST own immutable value data without actor callables, raw configuration documents, credentials, or provider objects, and publication after finalization MUST be rejected.

#### Scenario: Required publication is missing
- **WHEN** a candidate contains an active actor-scoped command whose actor does not publish its scope during preparation
- **THEN** startup, validation-only, or reload candidate construction fails rather than treating the command as unrestricted or silently hiding it

#### Scenario: Publication is owned by another actor
- **WHEN** a preparation publisher attempts to publish a command outside its bound actor contract
- **THEN** publication is rejected and cannot replace another actor's scope

#### Scenario: Scope references an invalid installation
- **WHEN** a scope names a missing or disabled bot, or a bot whose ingress platform differs from the scope
- **THEN** candidate validation fails before command activation

#### Scenario: Scope cannot change after activation
- **WHEN** code attempts to publish another scope after finalization
- **THEN** the attempt is rejected and the active table remains unchanged

### Requirement: Eligibility is shared by help and execution
Core SHALL provide one route-aware eligibility decision combining the exact active route, effective canonical-command ACL, and actor scope. Help filtering and command dispatch MUST use this decision with the same trusted normalized identity model. Actor scope MUST NOT grant access denied by ACL. Exact and RE2 alias invocations MUST evaluate the selected canonical command's scope. Invalid or inconsistent normalized identity MUST fail closed without consulting command arguments or raw provider fields as a replacement identity.

#### Scenario: ACL allows but actor scope does not
- **WHEN** a caller passes a command's effective ACL but its installation/group/topic is absent from the actor scope
- **THEN** help omits the command and direct invocation is classified as actor-unavailable

#### Scenario: Actor scope allows but ACL does not
- **WHEN** the source matches an actor scope but the caller fails its group or user ACL
- **THEN** help omits the command and direct invocation retains the ACL-denied outcome

#### Scenario: Alias is used outside the actor scope
- **WHEN** an alias resolves to a marked canonical command unavailable in the source context
- **THEN** the alias cannot bypass the same eligibility decision used for that canonical command in help

#### Scenario: Normalized identity is inconsistent
- **WHEN** source installation, conversation kind, native group, sender, or topic identity is inconsistent with the trusted envelope
- **THEN** no eligible command handler is dispatched and no raw-field fallback grants access

### Requirement: Actor-unavailable recognized commands terminate before dispatch
An ACL-permitted recognized command outside its actor scope SHALL produce one bounded `command_unavailable` terminal result and be consumed before transaction creation or command-handler dispatch. It MUST NOT submit a command-triggered provider operation, send an automatic chat reply, try another route, or enter ordinary fallback pipelines. This result MUST NOT apply the configured transaction-error fallback. Existing pre-command message observers and genuinely unmatched message routing SHALL retain their behavior. Diagnostics MUST NOT expose scope lists, native group/user ids, raw messages, arguments, or secrets.

#### Scenario: Route fallback is continue
- **WHEN** a recognized unavailable command has a route configured with `fallback = "continue"`
- **THEN** it is still consumed with `command_unavailable`, invokes no command handler, and does not enter ordinary pipelines

#### Scenario: Route fallback is consume
- **WHEN** a recognized unavailable command has a route configured with `fallback = "consume"`
- **THEN** it produces the same single terminal unavailable result without command-handler or provider calls

#### Scenario: Message has no active matching command
- **WHEN** a slash-prefixed message has no active exact or pattern match
- **THEN** it remains ordinary traffic and is not reclassified as actor-unavailable

#### Scenario: A pre-command observer is configured
- **WHEN** an event containing an unavailable command enters the coordinator
- **THEN** the existing observer phase remains intact, but no subsequent command handler or ordinary fallback pipeline runs

### Requirement: Bridge command scopes preserve source-route semantics
Bridge SHALL publish scopes for `bridge_status`, `recall`, and `poke` using shared pure helpers over its existing parsed installation pairs and mappings. QQ SHALL expose only `bridge_status` where the current QQ-to-Telegram source resolver finds a target, including its direction conditions. Telegram SHALL expose its supported commands where the current group/topic command resolver finds a QQ target, without adding an ordinary-forwarding direction condition absent from the command resolver. Group-to-group mappings SHALL preserve any-topic behavior; topic-to-group mappings SHALL expose only configured positive topics. Private, unmapped, unsupported-platform, and unsupported-command contexts MUST remain unavailable.

#### Scenario: Group has no bridge mapping
- **WHEN** a QQ group is globally ACL-permitted but has no resolvable Bridge source mapping
- **THEN** `bridge_status` has no matching scope even if Bridge is active on the same bot

#### Scenario: QQ direction is disabled
- **WHEN** a QQ mapping cannot be resolved because its QQ-to-Telegram direction is disabled
- **THEN** Bridge does not publish an available `bridge_status` scope for that source

#### Scenario: Telegram forwarding is disabled but command resolution exists
- **WHEN** a Telegram group/topic retains a command-resolvable QQ target while ordinary Telegram-to-QQ forwarding is disabled
- **THEN** command scope matches the existing command resolver rather than adding a forwarding-only restriction

#### Scenario: Only one Telegram topic is mapped
- **WHEN** a topic-to-group mapping contains one configured topic
- **THEN** Bridge commands are unavailable in other topics and in the no-topic context

### Requirement: ExHentai command scopes preserve destination and subcommand authorization
ExHentai SHALL publish `exhentai` scopes from its exact configured Telegram and OneBot destinations, using shared destination identity/matching helpers with `authorized_destination`. A configured no-topic Telegram destination MUST NOT match a positive topic. Availability MUST NOT replace source-envelope consistency validation or mutation-subcommand manager authorization. Ordinary users in a configured destination SHALL retain visibility of the canonical command even when they are not managers.

#### Scenario: Destination has no managers
- **WHEN** a destination is configured with an empty `manager_ids` list and the caller passes the core ACL
- **THEN** help includes `exhentai` for read/search functionality while manager-only mutations remain denied inside the actor

#### Scenario: Destination exists only on another installation
- **WHEN** the same group id is configured for a different bot installation
- **THEN** the source caller does not gain ExHentai availability

#### Scenario: Destination topic does not match
- **WHEN** the group is configured without a topic but the command originates in a positive Telegram topic
- **THEN** both help and dispatch consider ExHentai unavailable in that topic

#### Scenario: Direct actor request has contradictory identity
- **WHEN** a direct request supplies inconsistent normalized and source-envelope identity
- **THEN** actor-side validation rejects it rather than treating scope publication as a substitute for identity validation

### Requirement: Availability derivation and matching introduce no runtime side effects
Scope derivation SHALL use validated configuration only; scope matching SHALL use immutable in-memory values and perform no actor dispatch, DB access, network I/O, worker startup, or configuration mutation. Validation-only construction MUST derive and validate the same scopes without adding such side effects. A failed candidate MUST leave active availability unchanged, and old admitted messages MUST retain the old scope snapshot through reload.

#### Scenario: Help queries several commands
- **WHEN** help filters a bot catalog containing several participating actors
- **THEN** eligibility checks perform only in-memory matching and the only new provider operations are the authorized help reply pages

#### Scenario: Validation-only builds availability
- **WHEN** `--validate-config` checks participating actors
- **THEN** it validates the same required publications and identities as startup without adding command dispatch or provider operations

#### Scenario: Reload removes a destination
- **WHEN** a valid candidate removes a destination while old-generation work remains admitted
- **THEN** old work evaluates the old snapshot and only post-cutover admissions use the removed-destination scope

#### Scenario: Candidate scope validation fails
- **WHEN** reload prepares malformed or missing required scope data
- **THEN** the candidate is rejected and the old help/dispatch decisions remain authoritative

### Requirement: ExHentai-only groups do not advertise Bridge
With both actors active for the same QQ installation, a group configured only as an ExHentai destination and allowed by applicable ACLs SHALL see `/exhentai` and `/help` but not `/bridge_status`. Core MUST derive this behavior without a separate Bridge command ACL override or a hard-coded group id.

#### Scenario: Reported configuration shape is reproduced
- **WHEN** a synthetic group equivalent to `1004979108` is in the global ACL and ExHentai destinations but absent from Bridge mappings
- **THEN** its help contains only eligible entries, and manually entering `/bridge_status` produces `command_unavailable` without invoking the Bridge command handler
