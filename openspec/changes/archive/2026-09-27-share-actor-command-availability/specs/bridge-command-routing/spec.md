## ADDED Requirements

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
