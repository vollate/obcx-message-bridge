# Telegram `/react` and `/unreact`

Reply to a bridged message with `/react 👍` to add the QQ bot's reaction or
`/unreact 👍` to remove that specific reaction. Bot-qualified forms such as
`/unreact@bot ㊗️` are supported. Both commands require an emoji and a reply.
Enable `react` and `unreact` explicitly in the Telegram `command_runtime.routes`
entry; no new actor options or defaults are required.

Both directions work: a QQ message forwarded into Telegram, or a Telegram
message forwarded into QQ. The selected installation pair and QQ group scope
the lookup. Reverse merged-message lookup uses the repository's primary source.
Missing, expired, corrupt, or ambiguous mappings produce an error; displayed
text and sender identities are never used to guess a message ID.

The configured QQ **bot** owns the reaction, not the Telegram user. Both commands
use LLOneBot's `set_msg_emoji_like` extension: `/react` explicitly supplies
`set=true`, and `/unreact` explicitly supplies `set=false`. Removal affects only
the specified emoji on that mapped message for the bot; it cannot clear all
emoji or remove another QQ user's reactions. Any authorized Telegram user can
request removal of the shared bot's reaction, not just the user who added it.
Repeating either command requests the same state, never a toggle. If a reaction
is already absent, the command reports whatever outcome the provider returns.
Standard OneBot11 does not guarantee this extension. Provider rejection and
uncertain outcomes are reported without automatic retry. Success and error
replies stay in the invoking Telegram topic.

## UTF-8 mapping reference

The mapping follows [CrossGram](https://github.com/std-microblock/crossgram)
revision `22f7608ebbe518d87c6bf7b02e0f5f61b0a86c32`:

- `packages/platform-crossgram/src/index.ts::loadReactionCatalog` consumes the
  QQ bridge's catalog; `client.ts::getReactionCatalog` calls `/reactions/catalog`.
- CrossGram's `deploy/README.md` links its QQ implementation,
  [crossgram-qqnt](https://github.com/xuuuuan/crossgram-qqnt), inspected at
  `7ba44e34f3173f932df246fb3167c0112aa510fe`.
- That project's `src/qq-kernel.ts::loadReactionCatalog` reads QQ's
  `face_config.json`, skips `QHide == "1"`, and maps UTF-8 `QSid` to `QCid`
  (with a legacy `AQLid` fallback). Native system faces are a separate catalog.

This bridge snapshots the 68 visible `emoji` entries having `QCid` in
[LLOneBot's QQ resource table](https://github.com/LLOneBot/LuckyLilliaBot/blob/dc2b2b90092ef0f571b5546878288b83fff1e436/src/ntqqapi/helper/face_config.json).
All have explicit IDs; no runtime resource download or new dependency is
introduced. Unlike CrossGram's native typed reaction interface, LLOneBot infers
the reaction type from ID length, so this bridge does **not** use short legacy
`AQLid` aliases for Unicode reactions. It also maps `❤`/`❤️` explicitly to QQ
system face 66 (`/爱心`). Other system faces without a UTF-8 mapping and Telegram
custom-emoji placeholders are not accepted.

Explicit mappings retained from the QQ catalogue (not an exhaustive input whitelist):

```text
😊 😌 😚 😓 😰 😝 😁 😜 ☺ 😍 😔 😄 😏 😒 😳 😘 😭 😱 😂
💪 👊 👍 👏 👎 🙏 👌 👆 👀
🍜 🍧 🍞 🍺 🍻 ☕ 🍎 🍓 🍉 🚬 🌹 🎉 💝 💣 ✨ 💨 💦 🔥 💤 💩 💉 📫
🐎 👧 👦 🐵 🐷 🐮 🐔 🐸 👻 🐛 🐶 🐳 👢 ☀ ❔ 🔫 💓 🏪 ❤
```

Examples: `👍` → `128077`, `😂` → `128514`, `🔥` → `128293`, `❤️` → `66`.
IDs describe the wire format only: `/react 66` and `/unreact 66` are **not** accepted.
Both commands use this same emoji mapping and Unicode fallback.

## Unicode fallback

QQ's client catalogue is not a server whitelist. An emoji absent from that
catalogue can still be submitted using its decimal Unicode scalar value:

```text
/react ㊗️  → emoji_id "12951"   (U+3297)
/react 🫠   → emoji_id "129760"  (U+1FAE0)
```

The complete trimmed argument must be one valid UTF-8 emoji scalar, optionally
followed by one presentation selector: U+FE0E (text) or U+FE0F (emoji). The
selector affects rendering, not the ID. Explicit mappings take precedence for
the base emoji, so heart remains QQ-native `66` with or without a selector.
Otherwise a single-scalar Unicode emoji is converted to its decimal code point
and QQ decides whether to accept it. Rejection does not trigger a different
encoding, emoji, or automatic retry.

Multiple emoji, extra text, malformed/overlong UTF-8, surrogates, plain letters,
numeric IDs, and standalone emoji components are rejected. Compound sequences
(skin tones, regional-indicator flags, keycaps, ZWJ, and tag sequences) are also
rejected rather than silently reduced to their first scalar. There is no
normalization beyond trimming surrounding command whitespace and recognizing
one optional presentation selector.

LLOneBot interprets IDs with at most three digits as native QQ faces. Therefore
unmapped Unicode `©`/`®` are rejected: sending `169`/`174` would select a different
native face. Explicit verified native mappings are unaffected.

### Unicode data provenance

Fallback classification uses Unicode 17.0 `Emoji` minus `Emoji_Component`, not
the QQ client list. The private `src/telegram/emoji_codepoints.hpp` snapshot is
generated from [Unicode emoji-data.txt](https://www.unicode.org/Public/17.0.0/ucd/emoji/emoji-data.txt)
(SHA-256 `2cb2bb9455cda83e8481541ecf5b6dfda66a3bb89efa3fa7c5297eccf607b72b`).
The data's [Unicode License V3](../licenses/Unicode-3.0.txt) is included and
installed with the package. No new runtime dependency, network lookup, or
configuration default is introduced.

To regenerate from a downloaded copy of that exact data version, from the
bridge repository root:

```sh
python3 scripts/generate_reaction_emoji_ranges.py /path/to/emoji-data.txt src/telegram/emoji_codepoints.hpp
```

Then format the generated header with the core repository's `.clang-format`.
Emoji introduced after Unicode 17.0 require updating this property snapshot.
Eligibility for the fallback does not guarantee QQ acceptance, rendering, or
inclusion in a client's recent reactions.

Source inspection verifies the mapping/encoding, not live acceptance by every
QQ version. QQ may reject a reaction or lose its local message cache even when
the bridge still has the mapping. A timeout can leave the outcome unknown;
check QQ before manually repeating the command.
