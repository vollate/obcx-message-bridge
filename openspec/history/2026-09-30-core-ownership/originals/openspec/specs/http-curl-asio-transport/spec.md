# http-curl-asio-transport Specification

## Purpose
Define the asynchronous libcurl multi/Asio transport, security, cancellation, proxy, and compatibility requirements for OBCX HTTP clients.

## Requirements
### Requirement: OBCX HTTP operations use an asynchronous libcurl backend
The shared OBCX HTTP/proxy clients SHALL use libcurl's multi/socket interface integrated with Asio while preserving the supported OBCX awaitable request/response boundary. Curl handles MUST remain private to the implementation. The backend MUST NOT use blocking easy transfers on actor workers or create a worker thread per request. Supported builds SHALL provide asynchronous DNS, TLS, and HTTP/HTTPS/SOCKS5 proxy support.

#### Scenario: Actor executor issues concurrent requests
- **WHEN** multiple HTTP operations are awaited on an actor thread-pool executor
- **THEN** socket and timer readiness drive transfers without blocking the actor worker, curl operations are serialized for their multi handle, and each completion resumes through its associated executor

#### Scenario: Required curl capability is absent
- **WHEN** the selected libcurl build lacks asynchronous DNS or a required transport capability
- **THEN** build verification or backend initialization rejects it with a clear diagnostic rather than silently blocking or downgrading security

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

### Requirement: Cancellation owns and retires each transfer exactly once
The adapter SHALL own request data and callback state until completion, serialize cancellation with transfer processing, and remove cancelled transfers from the multi handle before releasing their curl resources. Cancelled or completed transfers MUST complete exactly once; stale readiness callbacks MUST NOT access released or recycled handles. Generation work ownership SHALL remain held until cancellation callbacks retire.

#### Scenario: Cancellation races with a successful transfer
- **WHEN** cancellation and completion become ready concurrently
- **THEN** only one terminal result is delivered and all callbacks retire without double cleanup or use-after-free

#### Scenario: Socket descriptor is reused
- **WHEN** a cancelled transfer's socket descriptor is reused while an old readiness callback remains queued
- **THEN** that old callback cannot operate on the new transfer

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

### Requirement: TLS verification covers both origin and HTTPS proxy
OBCX HTTP clients SHALL require trusted certificates and hostname verification for TLS origins and HTTPS proxies. HTTPS-over-HTTPS-proxy SHALL retain the proxy TLS session for the full tunneled transfer and separately verify the origin TLS session. The clients MUST NOT downgrade to plaintext or disable verification after TLS failure.

#### Scenario: Trusted HTTPS proxy and origin
- **WHEN** a request uses an HTTPS proxy to reach an HTTPS origin and both peers present trusted matching identities
- **THEN** the complete request succeeds with both TLS layers intact

#### Scenario: Untrusted or mismatched peer
- **WHEN** either the origin or HTTPS proxy presents an untrusted certificate or a certificate for another hostname
- **THEN** the affected handshake fails before application credentials are sent through that unverified TLS connection

### Requirement: Proxy and protocol behavior is explicit
The backend SHALL honor explicit OBCX proxy configuration, restrict transfers to HTTP/HTTPS, and preserve no-automatic-redirect behavior. Disabled proxy configuration MUST override ambient proxy environment variables; enabled proxy configuration MUST override ambient proxy/no-proxy routing. SOCKS5 target names SHALL be resolved by the proxy. No shared implicit cookie jar SHALL be enabled.

#### Scenario: Environment requests an unintended proxy
- **WHEN** proxy environment variables are set but the OBCX client explicitly disables its proxy
- **THEN** the transfer uses a direct connection rather than the environment proxy

#### Scenario: Explicit proxy conflicts with no-proxy environment
- **WHEN** an explicit proxy is configured and ambient no-proxy settings match the destination
- **THEN** the explicit proxy configuration still governs the transfer

#### Scenario: Redirect from authenticated origin
- **WHEN** an authenticated request receives a redirect response
- **THEN** the response is returned without following the destination or forwarding cookies automatically

### Requirement: Existing OBCX outcome and API semantics remain controlled
The replacement SHALL preserve public HTTP constructor/method compatibility, response representation, per-request and per-client bound selection, and conservative request-submission classification. A timeout, cancellation, or malformed result after a potentially submitted POST MUST NOT be classified definitely not submitted merely because no response was received. The backend MUST NOT expose raw curl error buffers, credentials, or response payloads in diagnostics.

#### Scenario: POST acknowledgement is lost
- **WHEN** a POST may have reached the origin but its response is lost or the operation is cancelled
- **THEN** the operation retains possibly-submitted classification and cannot trigger an unsafe automatic resend

#### Scenario: Existing client call sites are rebuilt
- **WHEN** installed-SDK and existing direct/proxy/synchronous HTTP tests are rebuilt with the new backend
- **THEN** the supported OBCX entrypoints remain usable without introducing curl handle types into callers
