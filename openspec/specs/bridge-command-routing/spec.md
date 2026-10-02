# bridge-command-routing Specification

## Purpose
Define Bridge-owned command scope projection, ordinary forwarding of unmatched commands, and actor integration expectations. Core owns generic command eligibility and propagation.

## Requirements

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

### Requirement: ExHentai-only groups do not advertise Bridge
With both actors active for the same QQ installation, a group configured only as an ExHentai destination and allowed by applicable ACLs SHALL see `/exhentai` and `/help` but not `/bridge_status`. Core MUST derive this behavior without a separate Bridge command ACL override or a hard-coded group id.

#### Scenario: Reported configuration shape is reproduced
- **WHEN** a synthetic group equivalent to `1004979108` is in the global ACL and ExHentai destinations but absent from Bridge mappings
- **THEN** its help contains only eligible entries, and manually entering `/bridge_status` produces `command_unavailable` without invoking the Bridge command handler

### Requirement: Unmatched slash-prefixed messages remain ordinary traffic

The generation command coordinator SHALL be the sole authority that converts a
platform command candidate into an intercepted command transaction. When a
candidate has no active route for its platform and bot scope, downstream
pipeline actors MUST treat the original event as ordinary business traffic and
MUST NOT suppress or consume it solely because its raw text begins with `/`.
In particular, a bridge forwarding handler MUST NOT use a raw leading-slash
check as a substitute for the active command routing table.

#### Scenario: An unregistered QQ command-shaped message is bridged

- **WHEN** QQ receives `/tp 2072 ~ 1080`, no active QQ route matches `tp`, and the source group has an enabled bridge mapping
- **THEN** the original message traverses the ordinary message-store and bridge stages once, the target bot is called once, and one forwarding mapping is produced

#### Scenario: An unregistered Telegram command entity is bridged

- **WHEN** Telegram receives a valid leading `bot_command` entity whose normalized name has no active route for that bot and the source group has an enabled bridge mapping
- **THEN** the original message traverses the ordinary pipeline and is forwarded once instead of being rejected by its leading slash

#### Scenario: Slash syntax appears without a platform command match

- **WHEN** a platform adapter reports no command candidate for a slash-prefixed event
- **THEN** downstream bridge processing applies only its ordinary routing, loop, de-duplication, and forwarding rules

#### Scenario: An active command is consumed

- **WHEN** a slash-prefixed event matches an active command route whose valid completion selects `consume`
- **THEN** the command coordinator completes the source operation without submitting it to the ordinary bridge pipeline

#### Scenario: An independent bridge rule rejects the message

- **WHEN** an unmatched slash-prefixed event reaches a bridge whose group mapping is disabled or absent
- **THEN** the bridge may skip it for that explicit routing policy but not because of the slash prefix

### Requirement: Unmatched-command coverage reaches terminal pipeline effects

Automated command-routing coverage SHALL verify the terminal effects of an
unmatched slash-prefixed event across the configured ordinary pipeline, not
only that the coordinator invoked a generic orchestrator. The coverage MUST
use isolated persistence and mock external bot transports.

#### Scenario: The unmatched QQ regression completes

- **WHEN** the end-to-end actor pipeline processes the unregistered QQ message `/tp 2072 ~ 1080`
- **THEN** it verifies message persistence, exactly one target send, a queryable source-to-target mapping, successful forwarding completion, and no missing-mapping bridge failure

#### Scenario: A matched command regression completes

- **WHEN** the same pipeline processes a command that is actively routed and consumed
- **THEN** it verifies the command actor is invoked while the bridge target bot is not called
