## 3. Bridge integration

- [x] 3.1 Extract shared pure command source-resolution/scope projection from Bridge's existing pair and group/topic mapping rules; preserve QQ direction checks and existing Telegram command-specific direction behavior.
- [x] 3.2 Mark `bridge_status`, `recall`, and `poke` as actor-scoped and publish their scopes from the parsed generation config, including the validation-only early-return path, without initializing forwarding services to answer availability.
- [x] 3.3 Reuse the shared resolution/matching helpers in Bridge execution guards rather than maintaining a separate applicability rule; retain target resolution, reply-specific validation, and valid-command completion behavior.
- [x] 3.4 Add Bridge parity tests for unmapped and mapped groups, multiple installation pairs with colliding native ids, group-to-group topics, exact topic mappings, disabled directions, private calls, and unsupported platform/command combinations.

## Shared integration acceptance (original task 5.3)

- [x] 5.3 Add a cross-actor integration regression using a synthetic equivalent of group `1004979108`: global ACL permits both commands, only ExHentai config includes the group, help lists `/exhentai` and `/help`, and manual `/bridge_status` dispatches no Bridge command handler.
