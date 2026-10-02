## ADDED Requirements

### Requirement: HTTP resources belong to the explicit owning executor
Asynchronous direct/proxy HTTP clients SHALL bind curl drivers, sockets, and timers to the executor supplied at construction, not to awaiting callers' executors. Completion SHALL resume on the associated caller executor. Completed requests MUST NOT leave cached resources referencing the caller context. The owning context MUST remain alive and be driven until shutdown work drains. Synchronous compatibility calls SHALL use isolated local clients and drain their cleanup before destroying local contexts.

#### Scenario: Actor caller retires before installation
- **WHEN** an installation-owned HTTP client completes requests originating on a separate actor executor and that actor context is then destroyed
- **THEN** the client can complete further requests and close without accessing the retired actor context

#### Scenario: Owning executor is not running
- **WHEN** a caller starts an asynchronous HTTP operation while the supplied owning executor is idle
- **THEN** transport work waits for the owning executor rather than silently running on the caller

#### Scenario: Synchronous request has an idle constructor executor
- **WHEN** a synchronous direct/proxy compatibility method is called with the original client executor idle
- **THEN** it completes through an isolated local context, preserves request settings, and retires that local transport before returning

#### Scenario: Bridge media uses a request-local HTTP client
- **WHEN** bridge image probing, downloading, or GIF detection constructs a request-local asynchronous HTTP client
- **THEN** it supplies the running coroutine executor, rather than an undriven temporary context, so requests, deadlines, and cancellation can complete and executor shutdown can drain cleanup

### Requirement: HTTP close owns cleanup until cancellation retires
Closing an HTTP client SHALL synchronously reject new admission and initiate idempotent serialized cleanup that retains its own ownership until processed. Pending transfers SHALL complete exactly once, with conservative submission classification, and curl handles SHALL be removed and freed without teardown callbacks rearming timers or sockets. Installation shutdown MUST allow cancellation callbacks and HTTP polling to drain before destroying components or their context. Polling MUST NOT rearm after disconnect.

#### Scenario: Client closes during a stalled request
- **WHEN** close is requested while a server has received a request but has not responded
- **THEN** the request completes with cancellation without waiting for its network timeout and later requests on that client are rejected

#### Scenario: Last external owner is released after shutdown
- **WHEN** shutdown is queued on a running owner context and the final external driver owner is released before cleanup executes
- **THEN** the queued operation retains cleanup ownership and the transfer's caller still receives one terminal result

#### Scenario: Installation stops with cancellation work still executing
- **WHEN** a stop callback or completion is running while installation stop is requested
- **THEN** the context remains runnable until that work and its queued cleanup have retired, and components remain alive throughout the drain

#### Scenario: Poll cancellation races with retry
- **WHEN** a polling request fails or is cancelled during disconnect
- **THEN** no new retry timer, request, or update dispatch is admitted after the polling stop decision
