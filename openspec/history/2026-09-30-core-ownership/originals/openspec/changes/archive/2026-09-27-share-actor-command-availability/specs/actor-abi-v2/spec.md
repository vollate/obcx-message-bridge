## ADDED Requirements

### Requirement: V2 commands may declare generation-prepared availability
The command SDK SHALL support an explicit actor-scope availability declaration whose generated command registration contains the data-only marker `availability = "actor_scope"`. ActorManager MUST preserve this marker for generation validation and reject unknown values, invalid types, or executable availability metadata before actor construction. Registrations without this marker SHALL preserve existing ACL-only behavior. The marker MUST NOT declare a handler, validator callback, member pointer, runtime configuration value, or provider object, and MUST NOT require a new executable export or a change to the V2 dispatch interface.

#### Scenario: Actor declares scoped availability
- **WHEN** a reflected actor marks a command as actor-scoped and exports its contract
- **THEN** the deterministic registration contains the marker alongside its canonical name, description, request type, and any existing matcher metadata

#### Scenario: Availability metadata is malformed
- **WHEN** a command registration supplies an unknown availability marker, an invalid value type, or a callable in place of the marker
- **THEN** ActorManager rejects the contract before construction or registration

#### Scenario: Existing actor has no availability marker
- **WHEN** a supported previously built actor exports valid commands without the marker
- **THEN** it remains loadable and its commands preserve their existing ACL-only eligibility

### Requirement: Installed SDK provides owner-bound data-only scope publication
The installed actor SDK SHALL expose value types for exact command group/topic scopes, the common in-memory matcher, and an owner-bound preparation publisher usable through the existing generation-preparation lifecycle. Participating actors MUST publish scopes from parsed configuration during preparation, including validation-only preparation. The runtime MUST validate and freeze required publications before command activation, and missing publication for an active marked command MUST fail candidate construction. The frozen representation MUST contain value data only and MUST NOT retain actor callables or plugin-owned runtime objects.

#### Scenario: Standalone actor publishes a scope
- **WHEN** a standalone actor is built using only installed SDK headers and libraries, marks a command, and publishes its scope during preparation
- **THEN** it compiles, loads, and supplies the same scope to generation-owned help and dispatch without importing core-private or platform-provider headers

#### Scenario: Marked active command has no scope
- **WHEN** an actor completes preparation without publishing an active command it marked as actor-scoped
- **THEN** generation finalization fails rather than treating a missing publication as unrestricted or as an explicit empty scope

#### Scenario: Scope is explicitly empty
- **WHEN** a standalone actor publishes an empty scope for a marked command
- **THEN** validation succeeds and that command remains unavailable in every conversation

#### Scenario: Validation-only actor returns early
- **WHEN** an actor has a validation-only early-return path
- **THEN** it publishes its required configuration-derived scopes before reporting ready

#### Scenario: Scope outlives actor preparation
- **WHEN** actor preparation ends or candidate teardown begins
- **THEN** copied immutable scope values require no callback into the actor library to evaluate or destroy
