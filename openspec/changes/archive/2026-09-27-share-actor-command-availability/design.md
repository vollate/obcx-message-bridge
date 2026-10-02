## Ownership

Actor-specific excerpt from the original archive; numbering and text are retained.

### 4. Bridge derives scopes from its existing source-route semantics

Bridge marks `bridge_status`, `recall`, and `poke` as actor-scoped. Extract a pure command source-resolution/projection helper around the existing pair and mapping logic; use that helper to generate scopes and retain matching defensive checks in execution. Do not create a second hand-written group list.

- Select pairs by exact source installation; equal native ids in another pair never match.
- QQ exposes only `bridge_status`, for sources resolvable by the existing `tg_group_and_topic_id` behavior, including its `enable_qq_to_tg` conditions.
- Telegram exposes the three commands where the current group/topic route resolver finds a QQ destination. A group-to-group mapping permits any topic as the current handler does; a topic-to-group mapping permits only its configured positive topics.
- Preserve current Telegram command direction behavior rather than adding a new forward-direction restriction. Command availability is not identical to whether ordinary forwarding is enabled.
- No mapping, unsupported platform/command, or private conversation produces no matching scope.

The same generic scope matcher is available to actor-side guards; target resolution remains Bridge-owned and uses the same parsed config and pure route helpers. Tests compare scope projection and resolution, including disabled directions, so the two paths cannot drift unnoticed.
