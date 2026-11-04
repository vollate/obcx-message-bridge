# Terminal retry-payload reclamation

`bridge_delivery_retries` stores the complete prepared send, including owned
media bytes. It is recovery material, not the admission/deduplication fence.
Keeping it after whole-delivery retry has terminated can retain hundreds of MiB.
The operator selected **clear on termination**, including uncertain outcomes;
there is no retention TTL, new configuration default or schema migration.

## Policy

| Current condition | Retry body |
|---|---|
| Initial preparation or locally owned sending/settlement | Keep |
| Definitely-not-submitted, retryable, remaining attempt budget | Keep, even if not due yet |
| Reconciled Complete | Clear |
| Nonretryable definite failure | Clear immediately, even on the first attempt |
| Attempt budget exhausted | Clear, even if the last result says retryable |
| Partial, possible effects, or contradictory physical observations, after local invocation ends | Clear; never resend the whole delivery |
| Unresolved attempt left after an invocation exits or a prior generation/process drains | Clear when no local sender owns it; retain the unresolved result/fence |
| Abandoned initial preparation with no attempt and no local owner | Clear |
| Missing, legacy or corrupt admission/reconciliation evidence | Retain for explicit inspection |

Budget accounting uses the existing authoritative admission, persisted attempts
and `spent_attempts`. It does not trust a payload's retry count as the lifetime
budget, increment counters just to reach the maximum, or retry uncertain sends.
An uncertain outcome may terminate at the first attempt, below that maximum.

Only retry rows are deleted. Admissions, source members, attempts, results,
confirmed outputs, revisions, source/output operations and command fences stay
intact. Deletion grants no provider-call authority. Identical admissions and
terminal-result replays stay deduplicated. Late confirmations can still record
physical effects after the body is gone; they do not resurrect a retry.

## Protect active work

The generation's sender and worker share one `BridgeStateRepository`:

- `hold_preparing_delivery` is acquired before initial admission/body storage.
  It protects the interval before a first attempt is registered.
- Successful `admit_attempt` / `admit_retry_attempt` registers the specific
  attempt in the same serialized writer job, before its admission returns.
  Failed admission/commit rolls back local ownership. AlreadyExists does not
  acquire or release another sender's ownership.
- After Created, the initial preparation guard is released. Settlement and
  exception-path scope guards release only the specific admitted attempt, not
  another attempt belonging to the same delivery. The public initial/retry
  send owners also cover failure to allocate or enter the submit coroutine.
- Sweeping skips any currently preparing or owned attempt. Reconciliation and
  deletion run on the same serialized writer as attempt admission. Activity
  mutexes cover only brief bookkeeping, never SQL or large-payload deletion,
  so coroutine cleanup cannot wait on a mutex held across database I/O.

An absent local owner is not proof that a remote provider sent nothing. It is
only permission, under this explicit retention policy, to remove the body of
an abandoned/terminated workflow. The durable uncertainty remains unchanged.
Normal reload drains the previous generation before activating the new worker;
candidate preparation only validates schema. External maintenance must be
performed offline, not by constructing an unrelated repository beside live
senders.

## Settlement and exception paths

`finish_attempt` commits the result and physical evidence first, releases local
attempt ownership, then attempts reclamation in a separate transaction. The
same path handles success, nonretryable failure, budget exhaustion and returned
uncertainty. Cleanup failure cannot roll back settlement or change a committed
result into a persistence failure.

If the invocation throws without returning an outcome, no fabricated no-effect
result is saved. Once the invocation has exited, the submit owner releases its
local ownership and attempts joined, cancellation-shielded reclamation while
preserving the original exception. Persistence failures similarly retain their
original outcome/error contract. Unexpected exits release ownership without
performing database I/O in a scope-guard destructor; a later sweep can reclaim
the abandoned body.

Warnings never include payloads. Optional cleanup failure leaves the row for a
later sweep, without resending the delivery or hiding a real send result.

## Bounded historical sweep

The worker calls `prune_terminal_retry_payloads(maximum_per_poll)` before retry
discovery. It visits at most the explicitly supplied number of IDs, reading
metadata only. A generation-local keyset cursor advances past live, runnable or
corrupt rows and wraps at the end; such rows cannot permanently hide later
terminal entries. A zero removed count means only that this page removed
nothing, not that the entire queue is clean. Failure is isolated from ordinary
retry discovery.

`prune_completed_retry_payloads(limit)` remains available with its older,
Complete-only meaning. Neither method decodes large retry bodies to reclaim
them. Historical sweeping uses the existing configured worker/timer/budget;
disabling that worker does not create a separate maintenance task. Immediate
settlement/error cleanup does not depend on the historical worker being enabled.

Limits bound visited IDs, not bytes or I/O duration. SQLite frees pages for
reuse; file size need not fall immediately. Runtime cleanup does not run VACUUM,
change journal settings or delete ledger evidence.

## Isolated manual verification (2026-11-03)

The actor builds with 20 workers through `scripts/actor_build.py`, against the
frozen SDK at `build/bridge-retry-memory/sdk`. No core target, deployment or
runtime database maintenance is part of that build. No unit tests were added;
manual diagnostic CLI runs use disposable databases under the ignored build
folder.

Observed scenarios include immediate cleanup of Complete, partial,
possibly-submitted, conflicting and nonretryable outcomes; keeping a safe
first/second failure until the third attempt exhausts a three-attempt budget;
accounting for earlier spent attempts; protecting preparation and live pending
attempts; cleaning abandoned unresolved and unattempted work; and preserving
another active attempt when an older terminal result is replayed.

A trigger-injected DELETE failure leaves the terminal result and output
committed, and a later sweep succeeds after the trigger is removed. A one-ID
sweep returns zero on a corrupt admission, then continues to later eligible
rows. Every non-retry table remains content-identical across the sweep.

A fresh copy of the earlier diagnostic database retained 549 bodies after the
old Complete-only cleanup, totaling 807,608,400 bytes (770.20 MiB). The new sweep
reclaimed all 549 in batches of at most 32, without parsing their media bodies.
All 51 other table fingerprints, including the old migration tables present in
that historical copy, were unchanged; integrity and foreign-key checks passed.
That run's standalone peak RSS was about 25 MiB. Large deletions can still take
seconds of database I/O; this is not a hard latency budget.

This changes retention intentionally: unlike the earlier Complete-only policy,
uncertain/failed bodies are no longer kept indefinitely for manual recovery.
Physical-effect evidence and replay fences are still retained.

## Deployment boundary

Installing/reloading the actor is a separate explicit operation. The frozen SDK
used here has core SHA-256
`255de5baeabd2f3b41e744062acb86d9b7f44a20f210437ff410e69f815d1867`;
verify it against the intended runtime rather than assuming an equal version
string proves identity. Do not bypass dependency-identity checks or replace a
running core to deploy this actor. Actual provider/cancellation integration
remains a deployment acceptance check, not a claim made by the offline probes.
