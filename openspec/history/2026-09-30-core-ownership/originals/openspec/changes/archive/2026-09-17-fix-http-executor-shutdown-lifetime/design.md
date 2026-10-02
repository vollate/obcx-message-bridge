## Context

The HTTP pimpl caches one curl driver per awaiting executor, ignoring the constructor executor. The process destroys generations before installations, so these caches can outlive actor pools. Curl shutdown currently captures only a weak pointer and does not clean the multi handle until destruction. Telegram disconnect only stops its retry timer. Installation stop polls once and forcibly stops its context, potentially abandoning cancellation callbacks.

## Goals / Non-Goals

**Goals:** Stable HTTP executor ownership, terminal admission closure, serialized owned cleanup, completion on caller executors, and draining polling/cancellation before context destruction.

**Non-Goals:** Changing shutdown timeout configuration, adding worker threads, changing transport security, automatic request replay, or redesigning actor routing.

## Decisions

- Keep one lazy driver bound to the constructor executor. Protect client settings and driver/admission state with a mutex; retain shared request state across suspension so close/destruction cannot invalidate in-flight HTTP completions. Store that shared state behind the existing unique pimpl so the public HttpClient object layout is unchanged.
- Driver shutdown atomically closes admission and dispatches a strongly owned cleanup operation on its strand. Cleanup detaches socket watches, cancels timers, completes transfers once, disables curl callbacks, and frees the multi handle. Repeated shutdown must not dispatch into an executor again. The owning context must outlive client/driver state and keep running until its work drains; associated caller contexts must drain admitted completions before retirement.
- Connection-manager disconnect closes the HTTP client without prematurely resetting it, then polling observes stop before dispatching updates or arming another retry. Timer access is serialized against stop.
- Installation stop requests component cancellation without forcibly stopping the context; existing application runner joins remain the completion barrier. Regression testing also exposed queued OneBot WebSocket connects/reconnects surviving stop: guard these with atomic running state and serialize shutdown on the send strand so immediate-start/stop installations can drain. A destructor with no runner drains remaining stop work before destroying components and the context. This avoids blocking waits from inside executor handlers.
- Synchronous compatibility methods use an isolated temporary client on their local context, preserve proxy/options, and close/drain it before returning; they cannot dispatch into an idle installation executor.

## Risks / Trade-offs

- Previously masked uncancelled work can delay natural shutdown → regression tests for pending requests/poll retries and existing application bounded runner join remain in force.
- Close is terminal rather than recreating a cached driver → explicitly document it and use a new client on reconnect.
- Raw manager callbacks require the manager to survive drain → installation teardown drains before clearing components; direct manager callers retain the same ownership obligation.
- Existing actor clients sometimes use a caller executor intentionally → constructor binding remains supported for actor-owned clients, but now callers must run that supplied executor. Bridge image probing, downloading, and GIF detection must construct their request-local clients with `co_await this_coro::executor`, not an undriven temporary `io_context`. Coroutine-local ownership retains the client until completion; its executor must drain destructor cleanup.
- The application's current deadline covers bot-thread joins only, after runtime shutdown returns. It does not bound an actor-pool drain stalled by an undriven HTTP owner. Fix the invalid owners here; a process-wide shutdown watchdog remains separate work.
