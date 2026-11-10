# QQ → Telegram voice preparation

Ordinary QQ `record` attachments and records in queried merged/forwarded nodes
both read their original scoped media reference, then await `qq::prepare_voice`
before adding an outgoing `AudioKind::Voice` block. No core adapter change,
provider-side conversion request, alternate media-kind send, or URL fallback is
involved. Music (`MediaKind::Audio`), files, and TG → QQ are unchanged.

## Runtime requirements and formats

Continue supplying the existing `ffmpeg_path` explicitly. That executable must
support the `libopus` encoder, Opus decoder, Ogg demuxer/muxer, and PCM conversion.
No ffprobe, Node, dev/onebot installation, downloaded helper, executable fallback,
or new configuration option/default is used. Standard ffmpeg is **not** assumed
to decode standalone Tencent SILK.

The [vendored SILK SDK decoder](../third_party/silk/README.md) is compiled into the
actor. Its pinned source, checksums, source list and license are included. Binary
packages install its license notice alongside the existing Unicode notice. The
SDK license permits redistribution but explicitly does not grant patent rights.

Bytes, not MIME or filenames, select the decoder. Tencent `02 + #!SILK_V3` and
standard `#!SILK_V3` framing use the native SDK at 24 kHz mono. Packet lengths,
frame counts, complete payloads and every decoder status are checked; packet-loss
concealment is not used as recovery. Standard framing requires its end marker;
Tencent framing permits EOF at a complete packet boundary. Other recognized
voice inputs (AMR, WAV, MP3, FLAC, MP4 audio, mono/stereo mapping-family-0 Ogg Opus)
use an explicitly selected ffmpeg demuxer. Playlists, remote protocols and arbitrary
format probing are not enabled. MP4 external data references and absolute-path
references are explicitly disabled; ffmpeg receives only private absolute input
paths, never the original filename or locator. Unsupported input is a preparation error, not
evidence that the source is corrupt.

All supported inputs become complete bounded mono s16le PCM, then Ogg Opus using
32 kbit/s VoIP encoding. Output must pass page framing, sequence/continuation,
CRC, single-stream Opus headers, and end-of-stream checks. A second ffmpeg pass
must decode the entire result to PCM with exactly the original decoded sample
count. This catches a successful-looking but clipped `-fs` output; an Ogg magic
string or renamed extension alone never authorizes upload. Metadata is rebuilt
from prepared bytes: `qq-voice.ogg`, `audio/ogg`, actual size and decoded duration.
Original source metadata is not rewritten in the message store.

## Queue, budgets and failure

The complete synchronous operation—including format inspection, native SILK
CPU work, private temp-file I/O, subprocess waits and output validation—runs in
one owned `BlockingExecutor::run` callable, following the photo preparer pattern.
It captures configuration and bytes by value, not actor pointers or coroutine
references. The event loop awaits it; the existing processing pool bounds active
work, with one single-threaded ffmpeg child per active voice job. Its existing
pending queue is not capacity-bounded; this change adds no separate queue. Attachments
are awaited in body order, before the existing single delivery invocation.
No detached thread, parallel attachment fan-out, send retry or replay is added.

Cancellation before admission prevents work. Once admitted, the existing executor
drains the callable before completing; cancellation suppresses publication of its
result. It does not forcibly interrupt an in-process SDK frame. The one shared
15-second processing deadline reuses the photo-normalization process bound;
It starts inside the worker (queue wait is excluded). SILK checks it between
frames, Ogg validation between pages, and each child gets only the remaining time. The
existing runner uses argument vectors, bounded diagnostic capture, process-group
termination/reaping and no shell interpolation. Child diagnostics are never logged.

Input and final output each obey the lesser of the configured per-item budget
and the delivery's remaining aggregate budget. The caller charges the **converted**
size, including expansion, to the aggregate before preparing the next attachment.
Intermediate and verification PCM each obey the SDK's existing 64 MiB owned-media
ceiling (about 23.3 minutes at 24 kHz mono s16); no GIF duration policy or new voice
duration default is imposed. `-fs` stops child output near its budget (ffmpeg may
overshoot by a packet/page), after which exact file-size checks fail closed.
Each individual ffmpeg allocation is additionally capped at 64 MiB. Temp files
live in an atomically created 0700 directory, removed on success, error or drained
cancellation. These are per-job bounds, not an exact process RSS guarantee.
The Telegram adapter also applies its existing 16 MiB item/aggregate ceiling
before uploading; larger configured bridge budgets do not override that ceiling.
Such oversized deliveries can still incur preparation work before adapter rejection.

`VoicePreparationFailure` propagates as non-retryable
`bridge_voice_preparation_failed`, with content/locator-free diagnostics. A voice
preparation conversion, budget or validation failure prevents the delivery
invocation; there is no fallback to incompatible original bytes. Other messages retain their
existing delivery policy.

## Limits of validation

Decodability does not prove the recording is audible or that a historical runtime
failure involved that recording. No playback, transcription or live send is part
of validation. Complete-packet truncation in Tencent SILK (which has no required
end marker), or complete-frame truncation in other non-self-delimiting formats,
cannot be distinguished from a shorter recording. The old native SILK SDK is not
process-sandboxed; input framing/resource checks are not a security audit of its
codec internals. Only Linux x86_64 was built and exercised for this change;
arm64 uses portable code but still needs platform validation.

## Offline verification evidence

The bridge-only workspace in `/tmp/obcx-queued-voice/` was rebuilt against the
current working tree with 20 workers. No repository unit tests were authored.
The existing ephemeral smoke executable calls **`prepare_voice`**, uses six
owned processing workers and an event-loop heartbeat, and never invokes a bot
operation. The parallel offline case script used 20 workers:

```sh
jobs=$(nproc); (( jobs >= 6 )) || jobs=6
cmake --build /tmp/obcx-queued-voice/build-cxx --target bridge_actor --parallel "$jobs"
python3 /tmp/obcx-queued-voice/link-smoke.py
bash /tmp/obcx-queued-voice/verify-offline.sh
cmake --install /tmp/obcx-queued-voice/package-state-cxx/build/vollate.bridge \
  --prefix /tmp/obcx-queued-voice/verification-install
```

These are verification commands for that preconfigured isolated workspace, not
instructions to use a shared/live build. Ephemeral helpers and private samples
are deliberately not shipped. Results:

- Tencent SILK: 11,055 bytes, `qq-voice.ogg`, `audio/ogg`, 2,920 ms decoded audio;
  ffprobe independently reported Ogg/Opus/mono. Full ffmpeg decode-to-null passed.
- Standard SILK, WAV, Ogg Opus and synthetic MP4 audio also passed full decoding.
- Empty, garbage, invalid-packet and truncated input, missing encoder, corrupt
  Ogg output, 15-second timeout, input/output bounds and 64 MiB PCM expansion
  all failed without publishing output.
- Sequential remaining-budget simulation: first output used 11,055 of 17,000
  bytes; a second conversion with 5,945 remaining failed closed on expansion.
  Actual ordinary/merged aggregate accounting was inspected, not gateway-tested.
- Cancellation during processing drained the worker (`completed=1`) but returned
  `operation_aborted`, no output. Event-loop heartbeats continued during codec
  work and timeout. All private job directories were removed; newly generated
  smoke media was discarded, and the existing sample remained read-only.
- Before the review fix below, all 82 vendor checksums passed and files matched
  the cached source archive whose SHA-256 matches the pin. Isolated installation included the exact SILK license;
  the actor had no dynamically exported/unresolved `SKP_Silk` symbols.

Commands/results are retained in the isolated workspace's `verification-*.log`
files and `verify-offline.sh`. Verification used explicitly supplied local ffmpeg
9.0.2, **not** a deployed executable. Final image runtime closure, arm64, forced
shutdown under concurrent load, and ordinary/merged gateway execution remain
unverified. No live send or end-to-end correlation of the reported message was
performed; none of these results establishes that runtime incident's full cause.

## Review fix: bounded native range lookahead

The review's P1 static bounds finding is fixed in
`third_party/silk/src/SKP_Silk_range_coder.c`. Both normalization reads now account
for the four-byte pointer offset. At payload length 1,024 they cannot dereference
past index 1,023. Virtual zero lookahead preserves the logical byte counter and
termination checks without reading adjacent state or stale bytes from a previous
packet. Initialization independently rejects inputs shorter than four bytes.
The vendor README explains the decoder semantics; its patch reproduces the sole
source difference from the pinned archive. `SHA256SUMS` covers the patched files.

Review-phase verification (Linux x86_64, 20 compile/case workers):

```sh
cmake --build /tmp/obcx-queued-voice/build-cxx --target bridge_actor --parallel 20
python3 /tmp/obcx-queued-voice/link-smoke.py
bash /tmp/obcx-queued-voice/verify-review-offline.sh
python3 /tmp/obcx-queued-voice/review-sanitized-build.py
python3 /tmp/obcx-queued-voice/review-boundary-smoke.py
cmake --install /tmp/obcx-queued-voice/package-state-cxx/build/vollate.bridge \
  --prefix /tmp/obcx-queued-voice/verification-review-install
(cd local_actor/obcx-message-bridge/third_party/silk && sha256sum -c SHA256SUMS)
```

- Isolated incremental compilation rebuilt the patched SDK object, `bridge_core`
  and `actors/bridge.so`; the final build after documentation/CMake comment updates
  also passed. No shared/live build output was used.
- The existing queued-preparer smoke was relinked and all 19 previous cases were
  rerun. Tencent input was explicitly checked for `02 + #!SILK_V3`; output was
  11,055 bytes, mono Ogg/Opus, with 2,920 ms decoded duration. ffprobe's 2.9265 s
  container duration includes Opus timing overhead. Full decode-to-null passed.
  Invalid input, corrupt output, missing executable, timeout, cancellation and
  byte/PCM budget failures still produced no output. The remaining-budget
  simulation and empty-temp-directory check passed again.
- A separate ephemeral build instrumented all 54 SDK translation units and the
  existing smoke entry point with `-fsanitize=address,bounds` (not the entire
  actor). The Tencent output decoded to byte-identical 24-kHz mono PCM compared
  with the earlier conversion. Ogg container bytes need not be identical because
  stream serials can differ; comparison is of decoded PCM, not container bytes.
- The same preparer executable processed 96 synthetic boundary inputs: eight
  fixed seeds each at packet lengths 0, 1, 2, 3, 4, and 1,019 through 1,025.
  48 were rejected without output; 48 were accepted and their output fully
  decoded. Arbitrary payload bytes are not necessarily invalid SILK, so rejection
  of all synthetic inputs is not asserted. There were no address/bounds sanitizer
  diagnostics. This smoke does **not** prove every range-reader branch was reached,
  reproduce an exploitable packet, or establish general native-code safety.
- All 82 shipped checksums passed. A fresh temporary extraction of the
  checksum-pinned archive plus the recorded patch matched every shipped C/header
  file exactly. The isolated installed license matched, and `nm -D` found no
  dynamic SILK symbols. Generated smoke media was discarded and the private
  sample remained unchanged.

Evidence: `/tmp/obcx-queued-voice/verification-review-{build,final-build,link,cases,sanitized-build,boundary,provenance,install,environment}.log`.
The offline preparer was explicitly supplied the local `ffmpeg` executable
(version 9.0.2); this is not verification of deployed `ffmpeg_path` or final image
closure. No new unit tests were authored. Only the existing preparer smoke and
safe ephemeral build/input orchestration were used.

Ordinary and merged call sites were re-inspected: both await `prepare_voice`
with the remaining item/aggregate budget and charge the converted size before
delivery. No fallback to original voice bytes or `sendAudio` was added. These are
source-inspection conclusions, not executed gateway coverage. Concurrent shutdown,
generation retirement, arm64, final image dependencies, live QQ/TG forwarding and
correlation to the reported runtime incident remain unverified. Nothing was
committed, deployed, restarted or sent to either service.
