## 4. Bridge caller migration follow-up

- [x] 4.1 Bind bridge image probes, downloads, and GIF detection to their running coroutine executor instead of an undriven temporary context; audit other actor HTTP constructors.
- [x] 4.2 Cover probe completion/timeouts and media cancellation/drain, rerun bridge and core transport regressions with sanitizers, and rebuild the runtime bridge module.

## Historical verification

### Bridge follow-up verification

- Before migration, all five existing image URL validator tests timed out on local HTTP fixtures, including download cancellation.
- After migration, 108 bridge tests and 61 core HTTP/curl/installation tests passed with 20 workers. Added probe success/timeout drain tests and a 30-second CTest limit for this suite.
- 68 ASan/UBSan tests passed with leak detection; all seven image validator tests also passed ten repetitions each. Sanitizer inventory includes only rebuilt targets to avoid unrelated stale binary discovery.
- Rebuilt `build/actors/bridge.so` and `build/src/app/obcx`; production bridge configuration validation passed. Formatted touched C++ files with the root style; both repository diff checks and strict OpenSpec validation passed.
- Other actor HTTP constructor sites were audited: ExHentai supplies its owned executor and chat LLM explicitly drives its supplied context. No additional undriven temporary HTTP contexts were found in that audit.
