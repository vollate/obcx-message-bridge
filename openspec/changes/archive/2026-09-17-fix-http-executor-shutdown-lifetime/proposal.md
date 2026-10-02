## Why

Bridge request-local HTTP clients must use a driven executor after the core HTTP ownership correction. This is the actor-owned slice of the original 2026-09-17 archive.

## What Changes

- Bind image probes, downloads and GIF detection to their running coroutine executor.
- Preserve completion, timeout, cancellation and drain coverage.

## Capabilities

### New Capabilities

- `bridge-http-client-lifetime`: Bridge media caller executor ownership.

### Modified Capabilities

None.

## Impact

Bridge HTTP call sites and tests. Generic transport shutdown remains core-owned; this documentation migration neither changes code nor reruns the historical tests.
