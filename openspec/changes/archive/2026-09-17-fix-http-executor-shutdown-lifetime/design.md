## Caller ownership

- Existing actor clients sometimes use a caller executor intentionally → constructor binding remains supported for actor-owned clients, but now callers must run that supplied executor. Bridge image probing, downloading, and GIF detection must construct their request-local clients with `co_await this_coro::executor`, not an undriven temporary `io_context`. Coroutine-local ownership retains the client until completion; its executor must drain destructor cleanup.
