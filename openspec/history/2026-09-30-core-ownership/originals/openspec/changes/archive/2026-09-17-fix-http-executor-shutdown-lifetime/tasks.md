## 1. HTTP ownership and cleanup

- [x] 1.1 Bind direct/proxy HTTP clients to their constructor executor, retain request state, close admission, and preserve isolated synchronous wrappers.
- [x] 1.2 Retain queued curl shutdown ownership, clean multi resources on the strand, and make cancellation/close idempotent.

## 2. Installation lifecycle

- [x] 2.1 Close polling HTTP clients during disconnect and prevent post-stop retry/update work.
- [x] 2.2 Drain installation cancellation before component/context destruction instead of forcing the context to stop, including suppressing queued OneBot WebSocket connects/reconnects after stop.

## 3. Regression verification

- [x] 3.1 Add cross-executor retirement, queued shutdown ownership, pending-transfer close, polling, and installation drain regression coverage.
- [x] 3.2 Run focused and sanitizer tests with at least six workers, build the application, document lifecycle contracts, and validate the OpenSpec change.

## 4. Bridge caller migration follow-up

- [x] 4.1 Bind bridge image probes, downloads, and GIF detection to their running coroutine executor instead of an undriven temporary context; audit other actor HTTP constructors.
- [x] 4.2 Cover probe completion/timeouts and media cancellation/drain, rerun bridge and core transport regressions with sanitizers, and rebuild the runtime bridge module.

## Verification results

- Built application and focused test targets with 20 workers.
- 98 regular tests passed: HTTP/curl, installation lifecycle/assembly, dispatcher/generation, WebSocket, and action tracking.
- 61 ASan/UBSan tests passed with leak detection and halt-on-error enabled; nine shutdown/race regressions also passed ten repetitions each.
- Sanitizer CTest used an isolated inventory of rebuilt targets because unrelated stale binaries in `build-asan` fail test discovery against the newer core ABI.
- Public `HttpClient` single-pointer object layout is preserved; request ownership lives behind its pimpl.

### Bridge follow-up verification

- Before migration, all five existing image URL validator tests timed out on local HTTP fixtures, including download cancellation.
- After migration, 108 bridge tests and 61 core HTTP/curl/installation tests passed with 20 workers. Added probe success/timeout drain tests and a 30-second CTest limit for this suite.
- 68 ASan/UBSan tests passed with leak detection; all seven image validator tests also passed ten repetitions each. Sanitizer inventory includes only rebuilt targets to avoid unrelated stale binary discovery.
- Rebuilt `build/actors/bridge.so` and `build/src/app/obcx`; production bridge configuration validation passed. Formatted touched C++ files with the root style; both repository diff checks and strict OpenSpec validation passed.
- Other actor HTTP constructor sites were audited: ExHentai supplies its owned executor and chat LLM explicitly drives its supplied context. No additional undriven temporary HTTP contexts were found in that audit.

