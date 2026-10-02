## Why

HTTP clients currently discard their constructor executor and cache curl drivers tied to caller executors. Installation-owned Telegram clients can consequently retain resources referencing a retired generation's execution context, causing shutdown use-after-free; weak queued shutdown and forced context stopping also prevent reliable cancellation retirement.

## What Changes

- Bind asynchronous HTTP drivers to the explicitly supplied owning executor while returning completions to callers' associated executors.
- Close request admission and retain shutdown ownership until curl handles are cleaned up; make repeated shutdown harmless.
- Cancel HTTP polling transfers during connection-manager shutdown, prevent retry timers after shutdown, and drain installation work before context destruction.
- Keep synchronous compatibility operations isolated on a temporary client/context with complete cleanup.
- Migrate bridge request-local HTTP clients to their running coroutine executor and cover image probe/download completion and cancellation.
- Add executor-retirement, in-flight cancellation, stopped-context, and lifecycle regressions.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `http-curl-asio-transport`: Explicit owning-executor lifetime and reliable close/drain semantics.

## Impact

Shared direct/proxy HTTP clients, curl/Asio driver, Telegram and OneBot HTTP connection managers, installation teardown, compatibility wrappers, and tests. No configuration defaults or new dependencies. Close becomes terminal for a client; reconnect constructs a fresh client.
