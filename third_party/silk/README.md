# SILK SDK decoder (vendored)

Source: SILK SDK ARM v1.0.9 portable sources in
[idranme/silk-wasm v3.7.1](https://github.com/idranme/silk-wasm/tree/2793875764ad98245c177ac496eb5b7100a71038),
commit `2793875764ad98245c177ac496eb5b7100a71038`, directory
`binding/libSilkCodec/silk/src/SILK_SDK_SRC_ARM_v1.0.9/`.

Pinned source archive:
`https://codeload.github.com/idranme/silk-wasm/tar.gz/2793875764ad98245c177ac496eb5b7100a71038`

Archive SHA-256:
`36b65b5c9691f79049ad97417cc7cf1d0d32e9b72186a3d295a0461e25d6cc8b`.

Only SDK headers and the 54 portable translation units in the link dependency
closure of `SKP_Silk_SDK_Get_Decoder_Size`, `SKP_Silk_SDK_InitDecoder`, and
`SKP_Silk_SDK_Decode` are included. The explicit CMake source list is authoritative;
`SHA256SUMS` records the shipped C/header files, including the local patch below.
No encoder, assembly, upstream wrapper (which ignores decode errors), CLI, Node, WASM, or test audio is
included. Builds perform no downloads. To verify a source update, compare every
manifest entry against that directory of the pinned, checksum-verified archive
after applying the recorded patch.

These portable C sources compile as C++ in OBCX's C++-only package workspace;
the public SDK declarations retain C linkage. Integer wrapping and relaxed
aliasing are explicit compiler flags, symbols are hidden, and objects are
incorporated into `bridge_core`/the actor, not a separately installed helper.
Both supported host architectures use the portable implementation. The bridge
also inspects the pinned decoder state's range-coder error and final frame marker:
the legacy API can overwrite an error during resampler initialization and can
stop at five frames even if the packet advertises more. Neither behavior is
accepted as successful preparation. This is a deliberate internal-SDK dependency
that must be reviewed when updating the pinned source.

## Local bounds patch

[`patches/0001-bound-range-decoder-lookahead.patch`](patches/0001-bound-range-decoder-lookahead.patch)
is the only source difference from the pinned archive (apart from formatting
inside its touched blocks). Apply with `patch -p1` in the upstream SDK directory;
then verify the shipped files with `sha256sum -c SHA256SUMS`.

The upstream range reader uses `buffer + 4` after loading its initial 32-bit
state, but guards reads using `bufferIx < bufferLength`. At a 1,024-byte payload,
the last four allowed indices would therefore read outside its 1,024-byte array.
The patched reader only dereferences when `bufferIx < bufferLength - 4`, so the
physical index is at most `bufferLength - 1`. Past the real payload it shifts
in virtual zero bytes, still advancing the logical index under the original
`bufferIx < bufferLength` guard. This retains the range coder's four-byte
lookahead and byte-count/termination semantics: merely stopping the index at
`bufferLength - 4` would undercount consumption in `range_coder_get_length`.
No padding is treated as extra payload and no payload limit is increased.
This also prevents a shorter packet from reading stale bytes of a prior packet.
Initialization now rejects lengths below four before reading `buffer[0..3]`.
The bridge's decoder-status and final-marker checks remain in place.

The bounds issue was established statically; no crashing/exploitable QQ packet
was reproduced. The real Tencent sample still produces identical decoded PCM.
Offline address/bounds-instrumented SDK smoke coverage and remaining limitations
are recorded in [the voice preparation notes](../../docs/qq-voice-preparation.md).
This focused patch is not a general security audit of the legacy native decoder.

## License review

The **SDK files**, unlike silk-wasm's MIT wrapper, carry Skype's BSD-3-Clause-style
license, reproduced in `LICENSE` and retained in each source file. It permits
source/binary redistribution with notice and disclaimer retention, prohibits
endorsement using Skype/contributor names, and **expressly grants no patent
licenses**. The bridge installs this notice as
`share/obcx/packages/vollate.bridge/licenses/SILK-SDK.txt`; distributors must retain
it. No MIT-wrapper code is copied here. This is a code-license review, not a
patent clearance or a security audit of this legacy codec.
