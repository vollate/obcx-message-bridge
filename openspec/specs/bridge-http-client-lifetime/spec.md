# bridge-http-client-lifetime Specification

## Purpose
Define Bridge media caller obligations when using the core asynchronous HTTP client.

## Requirements

### Requirement: Bridge request-local HTTP clients use the running executor
Bridge SHALL bind request-local image probing, downloading, and GIF detection clients to the running coroutine executor and retain ownership through completion and cleanup drain. The generic HTTP executor and close contract remains core-owned.

#### Scenario: Bridge media uses a request-local HTTP client
- **WHEN** bridge image probing, downloading, or GIF detection constructs a request-local asynchronous HTTP client
- **THEN** it supplies the running coroutine executor, rather than an undriven temporary context, so requests, deadlines, and cancellation can complete and executor shutdown can drain cleanup
