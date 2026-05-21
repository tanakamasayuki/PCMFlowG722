# PCMFlowG722 Specification

> 日本語版: [SPEC.ja.md](SPEC.ja.md)

## 1. Scope

**PCMFlowG722** is an optional G.722 codec add-on for [PCMFlow](https://github.com/tanakamasayuki/PCMFlow), focused on **real-time two-way wideband voice** (HD voice over VoIP, ESP-NOW transceivers, WebSocket / UDP voice links). G.722 delivers wideband audio (7 kHz audio band, 16 kHz sampling) at the same 64 kbps wire rate as G.711, with substantially better speech quality at moderate codec footprint.

> **Sibling libraries in the same family:** [PCMFlowG711](https://github.com/tanakamasayuki/PCMFlowG711) for narrowband 64 kbps voice with sub-4 KB footprint; [PCMFlowOpus](https://github.com/tanakamasayuki/PCMFlowOpus) for low-bitrate / wide- and fullband audio. All three plug into PCMFlow through `PCMSource` / `PCMSink` and can coexist in the same sketch. See [README "PCMFlow codec family"](README.md#pcmflow-codec-family) for the trade-off table.

Responsibility:

- **Encode** 16-bit signed PCM samples (16 kHz mono) into 8-bit G.722 codewords (1 byte per pair of input samples).
- **Decode** 8-bit G.722 codewords back into 16-bit signed PCM samples (16 kHz mono).

Only the ITU-T G.722 default operating mode (**Mode 1 / 64 kbps**, low-band 6 bits + high-band 2 bits) is exposed in v0.1.x. This is the rate used by RTP payload type 9 and by essentially all real deployments. Modes 2 (56 kbps) and 3 (48 kbps) are not exposed — see §"Deferred features".

The codec is **stateful** (the ADPCM predictor / quantizer state must be preserved across calls within one direction). Encoder and decoder hold independent state; loss of either packet boundary requires no reset, but a long packet gap will momentarily degrade quality before the adaptive filters re-converge.

The library plugs into PCMFlow's pipeline at the interface level only. It does not reimplement PCMFlow internals (ring buffer, format conversion, gain, output handoff are owned by PCMFlow).

## 2. Non-goals

- **WAV container with G.722 payload** (`WAVE_FORMAT_G722`, format tag `0x028F` / Microsoft variants) — out of scope for the initial release. See §"Deferred features".
- **G.722 Mode 2 / Mode 3** (56 / 48 kbps) — see §"Deferred features".
- **Sample rates other than 16 kHz** on the PCM side — G.722 is defined only for 16 kHz wideband input. Resampling for an I2S DAC running at a different rate (e.g. 44.1 / 48 kHz) is owned by PCMFlow.
- **Stereo** — G.722 is mono. Two-channel use is achieved by running two independent codec instances.
- **Audio file playback / device output** — owned by PCMFlow.
- **`Stream` / file system / network adapters** — owned by PCMFlow's `ByteStream` / `ByteSink`.
- **Jitter buffering / RTP parsing / network transport** — caller's responsibility. PCMFlowG722 is a codec, not a stack.
- **G.722 Appendix III / IV PLC** — see §"Deferred features".
- **G.722.1 / G.722.2 (AMR-WB)** — different codecs with different patent / royalty situations; out of scope.

## 3. Primary use case: ESP-NOW HD-voice transceiver

The motivating use case. ESP-NOW carries up to 250 byte payloads. G.722 produces 8000 byte/s on the wire, so 20 ms = 160 byte (the same packet size as G.711, just carrying twice the audio bandwidth):

| Frame duration | Bytes per packet | Input samples encoded |
|----------------|------------------|-----------------------|
| 10 ms          | 80 B  | 160 samples |
| 20 ms (RTP default) | 160 B | 320 samples |
| 30 ms          | 240 B | 480 samples |

Compared to raw 16 kHz mono 16-bit PCM (640 B per 20 ms), G.722 compresses **4×** — twice the bandwidth of G.711 at the same packet budget. An end-to-end transceiver sketch lives in [examples/EspNowTransceiver/](examples/EspNowTransceiver/) (mic at 16 kHz → G.722 encode → ESP-NOW broadcast / ESP-NOW receive → G.722 decode → I2S DAC).

## 4. Public API

Two classes; both work on **N input samples at a time** (N chosen by the caller — there is no internal framing beyond G.722's mandatory **even** sample count per call). Two PCM input samples produce exactly one G.722 byte; one G.722 byte produces exactly two PCM samples.

### 4.1 `G722Encoder` — implements `PCMSink`

Accepts 16-bit signed PCM at 16 kHz mono and emits G.722 bytes.

```cpp
G722Encoder enc;
enc.begin({16000, 1, 16});

int16_t pcm20ms[320];   // 320 samples = 20 ms at 16 kHz
uint8_t pkt[160];
const size_t bytes = enc.encode(pcm20ms, 320, pkt, sizeof(pkt));
// bytes == 160; send pkt[0..bytes) over ESP-NOW / UDP / WebSocket
```

The encoder **holds adaptive predictor / quantizer state** internally and must not be shared across tasks without external synchronization.

The input `frameCount` must be **even** (G.722 processes samples in pairs of two via QMF); odd-count calls return 0 with `Error::FrameCountNotEven`.

### 4.2 `G722Decoder` — implements `PCMSource`

Accepts G.722 bytes and emits 16-bit signed PCM at 16 kHz mono.

```cpp
G722Decoder dec;
dec.begin({16000, 1, 16});

int16_t pcm[320];
const size_t frames = dec.decode(pkt, 160, pcm, 320);
// frames == 320
```

Like the encoder, the decoder is **stateful**. There is **no PLC** built into v0.1: when a packet is lost, the caller decides whether to feed silence bytes (which the decoder will process normally, recovering predictor state over a few tens of ms), hold the last sample, or insert a separately-generated comfort-noise byte stream.

Both `G722Encoder::format()` and `G722Decoder::format()` describe the PCM side; the byte side is opaque.

### 4.3 PCMFlow pipeline integration

`G722Decoder` implements `PCMSource`, so it plugs into PCMFlow's ring buffer / format converter / output device chain via `PCMFlow::setInputSource(dec)`. The caller feeds bytes into the decoder (`decode(packet, bytes, nullptr, 0)` enqueues into the internal FIFO); PCMFlow consumes the resulting PCM through `pump()` / `readFrames()`.

`G722Encoder` implements `PCMSink`. Source PCM (e.g. from a 16 kHz mic recording task) is pushed via `writeFrames(...)`; the encoder converts internally and emits the bytes to the caller-supplied `PacketSink` callback.

Detailed signatures and error enums are finalized in the headers under [src/](src/) once implemented.

## 5. PCM I/O format

- Sample rate: **16000 Hz** (ITU-T G.722 fixed wideband rate).
- Bit depth: **16-bit signed** on the PCM side, **8-bit unsigned** on the codec side.
- Channels: **1 (mono)**.

If the application needs a different output sample rate (e.g. ESP32-S3 I2S DAC running at 44.1 kHz), PCMFlow's resampler handles the conversion. PCMFlowG722 only operates at 16 kHz on the PCM side.

## 6. Memory & footprint targets

G.722 is a sub-band ADPCM codec with a QMF filter bank plus adaptive predictor / quantizer per sub-band. Footprint is dominated by the per-direction state (the per-sample arithmetic is small).

| Item | Target |
|------|--------|
| Flash (encoder + decoder) | ≤ 12 KB |
| Flash (decoder only, link-time discarded encoder) | ≤ 8 KB |
| RAM, persistent encoder state (per instance) | ≤ 512 B |
| RAM, persistent decoder state (per instance, direct decode only) | ≤ 512 B |
| RAM, persistent decoder state (with PCMSource queue for `setInputSource`) | ≤ 1 KB |
| Per-call scratch | ≤ 64 B (stack) |

These numbers track the [sippy/libg722](https://github.com/sippy/libg722) baseline vendored under `src/external/libg722/`. The numbers will be re-measured once the bridge code is in place and revised here if reality differs by more than ~25 %.

## 7. Repository layout

```
PCMFlowG722/
├─ README.md / README.ja.md
├─ SPEC.md   / SPEC.ja.md
├─ CHANGELOG.md
├─ LICENSE                       # MIT (this library)
├─ library.properties            # Arduino IDE
├─ library.json                  # PlatformIO
├─ keywords.txt                  # Arduino IDE syntax highlight
├─ src/
│  ├─ PCMFlowG722.h              # umbrella header
│  ├─ G722Encoder.h/.cpp         # PCMSink,   PCM 16 kHz -> G.722 byte
│  ├─ G722Decoder.h/.cpp         # PCMSource, G.722 byte -> PCM 16 kHz
│  ├─ pcmflowg722_version.h      # auto-generated by tools/bump_version.py
│  └─ external/
│     ├─ LICENSE_libg722.md      # upstream license + credits
│     ├─ UPSTREAM.lock           # pinned upstream commit (informational)
│     └─ libg722/                # verbatim subset of sippy/libg722
├─ examples/
│  └─ EspNowTransceiver/         # showcase sketch (mic↔ESP-NOW↔DAC, 16 kHz HD)
├─ tests/
│  ├─ README.md / README.ja.md
│  ├─ conftest.py
│  ├─ pyproject.toml
│  ├─ smoke/
│  ├─ g722_encoder/
│  ├─ g722_decoder/
│  ├─ roundtrip/                 # encode → decode → compare against original
│  ├─ external_source/           # PCMFlow::setInputSource integration
│  └─ tools/gen_test_audio.py
├─ doc/
│  └─ sibling_library_brief.md
├─ tools/
│  ├─ bump_version.py            # mirrors parent PCMFlow's tooling
│  └─ sync_libg722.py            # maintainer tool: refresh src/external/libg722/
└─ .github/
   └─ workflows/
      └─ release.yml             # mirrors parent PCMFlow's tooling
```

## 8. Vendored upstream

PCMFlowG722 **does** vendor an upstream G.722 implementation, because a clean-room write of G.722 is significantly more involved than G.711 (sub-band ADPCM with adaptive quantizers and predictors, ~1000 lines of bit-exact arithmetic to match ITU test vectors).

**Upstream:** [sippy/libg722](https://github.com/sippy/libg722). Originally written by Milton Anderson (Bellcore), modified by Chengxiang Lu and Alex Hauptmann (CMU Computer Science / Speech Group, 1993), substantially improved by Steve Underwood (2005), and maintained by Sippy Software.

**Vendored layout:**

```
src/external/
├─ LICENSE_libg722.md            # credit + full text of upstream notice
├─ UPSTREAM.lock                 # pinned commit SHA + checked date
└─ libg722/
   ├─ g722_encoder.c / .h
   ├─ g722_decoder.c / .h
   ├─ g722_codec.h
   ├─ g722_common.h
   └─ g722_private.h
```

The Python bindings, CMake build, and test harness from upstream are **not** vendored — they are not needed for the Arduino library, and the codec sources stand alone.

### License of the vendored code

From the upstream notice (placed in `src/external/LICENSE_libg722.md` verbatim):

> The G.722 module is mostly Public Domain.
> Copyright (C) 2005 Steve Underwood — "I hereby place my contributions in the public domain, for the benefit of all mankind."
> Copyright (c) 1993 Computer Science, Speech Group, Carnegie Mellon University, Chengxiang Lu and Alex Hauptmann.
> Library test code is under BSD 2-clause license (not vendored).

This is MIT-compatible: the codec code is Public Domain and the CMU contribution is on permissive terms. PCMFlowG722 ships under **MIT** (this library's own code) with the vendored portion explicitly identified in `src/external/LICENSE_libg722.md`. Users do not need to add attribution beyond the standard MIT notice in `LICENSE` and the unchanged upstream notice file in `src/external/`.

Reference standards (algorithm specification, not source code):

- ITU-T Recommendation G.722 (09/2012 revision), "7 kHz audio-coding within 64 kbit/s".

## 9. Release workflow

PCMFlowG722 adopts **upstream-tracking level L0 — no automatic tracking**. sippy/libg722's G.722 core has been stable for years (Steve Underwood's substantive work concluded in 2005), so a weekly tag-watcher would almost never fire and would mostly be CI noise. Instead, the vendored snapshot is refreshed manually if and when a maintainer chooses to.

`tools/sync_libg722.py` is provided as a **maintainer tool**, not as part of any CI workflow: it idempotently pulls the upstream subset listed in §8 into `src/external/libg722/` and updates `UPSTREAM.lock`. There is no `upstream-check.yml`; `UPSTREAM.lock` exists purely as informational provenance for the currently vendored snapshot.

### Final release tagging

Version bumps and the Arduino Library Manager tag are produced by `tools/bump_version.py` (copied verbatim from parent PCMFlow), driven by [`.github/workflows/release.yml`](.github/workflows/release.yml) (also mirrored from parent). PCMFlowG722's `version=` and `Unreleased` CHANGELOG section move together, identically to PCMFlow. The maintainer triggers it via `workflow_dispatch` after merging any change worth releasing.

## 10. Testing

Same conventions as parent PCMFlow:

- pytest-embedded + Arduino CLI backend.
- Two profiles: `lang-ship:host` (logic, large fixtures) and `esp32:esp32:esp32` (real hardware verification).
- Per-feature test directory with `<feature>.ino`, `sketch.yaml`, `test_<feature>.py`, and `input/` fixtures.
- Assertions use the `EXPECT_TRUE / EXPECT_EQ / EXPECT_NEAR` macro family and the `TEST done N/M` Serial protocol.

G.722-specific test design:

| Test dir | Subject | Strategy |
|----------|---------|----------|
| `smoke/` | Library compiles against the chosen profile and the harness wiring works | host build, prints version, instantiates encoder + decoder |
| `g722_encoder/` | `G722Encoder` API contract | encode(N) → N/2 bytes; reject odd N (FrameCountNotEven); reject short output buffer (BufferTooSmall); reject wrong format (InvalidFormat); reset() restores the predictor state |
| `g722_decoder/` | `G722Decoder` API contract | decode(N) → 2N samples; reject short PCM buffer; queued path (`decode(_, _, nullptr, 0)` + `readFrames`); reset() restores the predictor state |
| `roundtrip/` | encode → decode preserves signal characteristics | 1 kHz sine and silence; assert energy preserved within ±15 %, frequency preserved (zero-crossing count), peak amplitude preserved, silence-in → near-silence-out |
| `external_source/` | `G722Decoder` works as a `PCMSource` plugged into `PCMFlow::setInputSource()` | same harness as the G.711 sibling; verify count, non-trivial peak, mono → stereo upmix |

G.722 has a fixed group delay (~3 ms / ~48 samples at 16 kHz, from the QMF + ADPCM filter chain), so the roundtrip test does **not** assert sample-by-sample equality with the input. Instead it asserts the statistical properties (energy, frequency content, peak amplitude) that a working codec must preserve. Per-sample bit-exactness against ITU reference vectors is deferred — see §"Deferred features".

## 11. Versioning

SemVer (`major.minor.patch`) maintained in `library.properties`, `library.json`, and `src/pcmflowg722_version.h`. **Independent of** the PCMFlow version. **Independent of** the upstream sippy/libg722 version (which is tracked in `src/external/UPSTREAM.lock`).

## 12. Deferred features {#deferred-features}

Captured here so they aren't lost; not in v0.1.x:

- **WAV container with G.722 payload** (`WAVE_FORMAT_G722`). Useful for desktop interop (ffmpeg / VoIP recording playback). Implemented as a thin shim around the existing parent PCMFlow `WAVDecoder` once the parent grows a non-PCM payload hook, or as a standalone `G722WAVDecoder` here. Defer until a concrete user request appears.
- **G.722 Mode 2 / Mode 3** (56 / 48 kbps). libg722 already implements all three modes, so unlocking them is mostly an API question: `begin()` would grow a `G722Mode` argument and a `setMode()` method appears. Defer until a concrete deployment needs them — RTP signalling for Mode 2 / 3 is rare in practice.
- **ITU G.722 reference test vectors.** The vendored libg722 is itself bit-exact to ITU by construction, so the v0.1 test suite validates API contract and signal-preservation properties rather than re-asserting bit-exactness. ITU-T G.191 STL vectors can be embedded later under `tests/g722_*/input/` if a regression or third-party port motivates it.
- **G.722 Appendix III / IV PLC** — ITU-T-specified high-complexity / low-complexity packet-loss concealment. Worthwhile for jittery RTP; Appendix IV is the more commonly deployed one. Both add ~2 KB of code and a few hundred bytes of state. Defer to v0.2.
- **Upstream-tracking automation** (L1 weekly tag check or higher). The current L0 posture is appropriate because sippy/libg722 is essentially dormant. If upstream activity picks up, revisit and add `.github/workflows/upstream-check.yml`.
- **`G722_PACKED` / `G722_SAMPLE_RATE_8000` upstream options** — libg722 exposes a packed-bits wire mode and a narrowband mode. Neither matches the on-the-wire convention used by RTP payload type 9 / standard G.722 hardware. v0.1 exposes only the default unpacked, 16 kHz mode. May be added later if a concrete user requests it.

## 13. License

PCMFlowG722's own code: **MIT** ([LICENSE](LICENSE)).

Vendored upstream (`src/external/libg722/`): Public Domain (Steve Underwood's contributions) + CMU permissive notice — see `src/external/LICENSE_libg722.md` for the verbatim upstream license text. No additional user-side attribution is required beyond the standard MIT notice; the upstream notice file is shipped in `src/external/` for reference and license-hygiene auditing.
