# Bounded-memory schema-5 retry scanning

## Runtime behavior

`BridgeStateRepository::list_ready_retry_deliveries(ready_at_ms, limit)` no
longer loads every serialized retry body into C++ before deciding which IDs
can run. No schema migration, configuration option, or configuration default
is introduced.

Within the existing serialized database transaction, the scan:

1. Keyset-pages delivery IDs in identity order, with at most the caller's
   explicit positive `limit` IDs per page. Existing identity indexes support
   the lookup. SQL excludes deliveries with confirmed original outputs or no
   admitted attempt; these are necessary preconditions, not substitutes for
   `safe_retry`.
2. Loads admission metadata and applies the complete existing `safe_retry`
   gate, including unknown/possible effects, terminal outcomes and attempt
   limits, before inspecting any retry body.
3. Uses the existing SQLite JSON support to project only
   `next_retry_at_ms` for potentially safe work. Missing or non-integer schedule
   values fail closed; there is no invented scheduling fallback. Future work
   does not undergo full C++ payload decoding during this poll.
4. Keeps only the earliest `limit` safe, due candidates, ordered globally by
   `(next_retry_at_ms, delivery_id)`. A later ID page can displace an earlier
   page's later-scheduled entry. Unsafe entries cannot consume the result quota
   or starve safe entries beyond the first page.
5. Loads one selected full payload at a time, parses the stored string by
   reference instead of copying it via `db_string`, and retains full
   `validate_retry`, schedule-consistency and `safe_retry` checks before
   returning its ID. The sender still performs its separate snapshot and atomic
   attempt-admission checks before any provider call.

Only selected payloads receive complete body/format/scope validation in this
poll; retained completed, unsafe and unselected bodies are not decoded merely
to enumerate retries. This does not grant them send authority or mutate their
ledger evidence.

## Limits of this compatible fix

The C++ queue scan retains bounded ID/candidate batches, rather than all queued
payloads. A legitimately selected large retry still requires memory for that
one payload and its decoded representation. SQLite may parse a single large
JSON value while extracting schedule metadata. This is not a hard per-payload
memory cap or a claim that all future workloads use a fixed number of MiB.

Persisted scheduling columns plus an index could avoid inspecting JSON for
safe-but-future work, but require a separately coordinated schema change and
explicit offline migration. The bounded scan intentionally preserves schema 5
and does not itself delete retry records, truncate payloads or change retry
timing. Separately, [terminal-payload cleanup](retry-payload-cleanup.md) removes
bodies when the workflow is no longer resumable, including ended uncertain or
exhausted attempts, while retaining durable delivery evidence and protecting
currently owned preparations and sends.

## Offline verification (2026-11-02)

Core's `scripts/actor_build.py` froze the already-built SDK into
`build/bridge-retry-memory/sdk`, configured the explicit `vollate.bridge`
closure, and built Bridge plus Message Store with 20 workers. Core was imported
from the frozen SDK throughout; no actor targets were built in the live core
workspace and no application, bot or actor generation was started by the probe.
The pre-existing core/application SHA-256 values were unchanged after builds
and measurements. Nothing was deployed or reloaded.

A disposable SQLite snapshot was created with SQLite's online backup API from
a read-only source connection. The snapshot contained 3,463 retry records,
1,530,174,774 serialized payload bytes, and a 127,066,850-byte maximum payload.
Measurements linked the same manual diagnostic probe to either the old staged
Bridge DSO or the newly built DSO, with the same frozen core, copied database,
explicit cutoff/limit and jemalloc sampling settings. No actor was constructed.

| Measurement | Before | After |
|---|---:|---:|
| Scan time, three runs (ms) | 12237.7 / 11887.1 / 11892.5 | 8.789 / 5.331 / 5.195 |
| Median scan time (ms) | 11892.5 | 5.331 |
| Process high-water RSS (KiB) | 2081052 | 25860 |
| Ready IDs returned, each run | 0 | 0 |

RSS is the standalone diagnostic process's high-water mark, not the complete
production bot's RSS. These figures establish the removal of this queue's
bulk-payload allocation, not a universal latency or memory bound.

Disposable manual ledger inputs additionally exercised pagination with limits
1/2/3, earliest-due selection across later ID pages, deterministic equal-time
ordering, exclusion of future work, confirmed outputs, unattempted/pending/
possible-effect/terminal/exhausted attempts, invalid payload formats, scope
mismatch, malformed scheduling values and non-positive limits. Both versions
returned the same eligible IDs on valid input; selected corrupt payloads and
non-positive limits were rejected. These were manual diagnostic runs, not a
new unit-test suite.

Local core-checkout evidence and diagnostic sources are under
`build/bridge-retry-memory/` (ignored build output). The captured production
snapshot contains sensitive data and must not be published or committed.

## Build and deployment

Use the frozen-SDK actor workflow documented in core's
`packaging/actors/README.md`; compile and test with at least six workers and all
available cores when load is low. Preserve the existing explicit configuration
and retry-safety gates. Installing/reloading the new Bridge artifact is a
separate operator action. Do not overwrite the running core or clean up durable
retry rows as a shortcut to deploying this fix.
