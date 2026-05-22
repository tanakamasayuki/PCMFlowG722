# PCMFlowG722

> 日本語版: [README.ja.md](README.ja.md)

Optional **G.722** wideband (HD voice) codec add-on for [PCMFlow](https://github.com/tanakamasayuki/PCMFlow), aimed at **real-time two-way voice over packet radio / network** — VoIP, ESP-NOW transceivers, WebSocket / UDP voice links.

G.722 carries 7 kHz audio at 16 kHz sampling using the same 64 kbps wire budget as G.711 — same packet size, **twice the audio bandwidth**. The codec core is the public-domain [sippy/libg722](https://github.com/sippy/libg722) (Steve Underwood / CMU); this library wraps it behind PCMFlow's `PCMSource` / `PCMSink` interfaces.

PCMFlowG722's own code is **MIT**. The vendored upstream is Public Domain (Steve Underwood's contributions) + a permissive CMU notice, both MIT-compatible — see [`src/external/LICENSE_libg722.md`](src/external/LICENSE_libg722.md).

See [SPEC.md](SPEC.md) for the full specification.

---

## What's inside

| Class | Direction | Carrier | Interface |
|-------|-----------|---------|-----------|
| `G722Encoder` | PCM (16 kHz) → G.722 byte | raw bytes (RTP / ESP-NOW / UDP / WebSocket) | `PCMSink` |
| `G722Decoder` | G.722 byte → PCM (16 kHz) | raw bytes | `PCMSource` |

Two PCM samples map to exactly one G.722 byte. v0.1 exposes only **Mode 1 / 64 kbps** (the rate used by RTP payload type 9 and essentially all real deployments). Mode 2 / 3 are deferred — see [SPEC §"Deferred features"](SPEC.md#deferred-features).

WAV-container support (`WAVE_FORMAT_G722`) and G.722 Appendix III/IV PLC are intentionally **out of scope for v0.1**.

---

## PCMFlow codec family {#pcmflow-codec-family}

PCMFlowG722 is one member of a family of optional codec add-ons for PCMFlow. Pick whichever matches your bandwidth / footprint / quality budget:

| | [PCMFlowG711](https://github.com/tanakamasayuki/PCMFlowG711) | **PCMFlowG722** (this lib) | [PCMFlowOpus](https://github.com/tanakamasayuki/PCMFlowOpus) |
|---|---|---|---|
| Audio band | narrowband (8 kHz / ≤ 3.4 kHz) | **wideband (16 kHz / ≤ 7 kHz)** | narrow / wide / fullband (8–48 kHz) |
| Bitrate (voice) | 64 kbps fixed | **64 kbps fixed (Mode 1)** | 16–32 kbps typical |
| Compression vs raw 16-bit PCM | 2× | 4× | 10–15× |
| Codec flash footprint | < 4 KB | ~10 KB | ~150–180 KB |
| Codec CPU | negligible | low | non-trivial on M0/M3-class MCUs |
| Patent / license complexity | none (1972 standard, expired) | none (1988 standard, expired); core is Public Domain | royalty-free patent grant, BSD-3-Clause source |
| Quality | toll-grade telephony | **HD voice (wideband telephony)** | wideband / fullband, far better |

Pick G.722 when:
- You have 64 kbps of radio / network budget to spend.
- You want **HD-voice quality** rather than narrowband telephony — voice intelligibility and "presence" both improve dramatically over G.711.
- A moderate codec footprint (~10 KB Flash + ~512 B RAM per direction) is fine.
- Standardised wire format and patent-free licensing matter (G.722 is an ITU-T 1988 recommendation).

Pick [G.711 (PCMFlowG711)](https://github.com/tanakamasayuki/PCMFlowG711) when narrowband telephony is enough and you want the smallest possible codec footprint (< 4 KB Flash).

Pick [Opus (PCMFlowOpus)](https://github.com/tanakamasayuki/PCMFlowOpus) when you need bandwidth savings or fullband audio quality.

---

## Headline use case — ESP-NOW HD-voice transceiver

ESP-NOW carries up to 250 byte payloads. A 20 ms G.722 voice frame at 16 kHz produces exactly 160 bytes — same as G.711, but with twice the audio bandwidth. Raw 16 kHz mono 16-bit PCM at the same 20 ms is 640 byte; G.722 compresses **4×**.

```cpp
#include <PCMFlow.h>
#include <PCMFlowG722.h>
#include <esp_now.h>

G722Encoder enc;
G722Decoder dec;
PCMFlow audio;

void setup() {
    audio.setOutputFormat({16000, 1, 16});
    audio.setInputSource(dec);

    dec.begin({16000, 1, 16});
    enc.begin({16000, 1, 16});

    esp_now_init();
    esp_now_register_recv_cb(onEspNowRecv);
}

// Called from the ESP-NOW recv callback: feed bytes into decoder
void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
    dec.decode(data, len, /*pcm_out=*/nullptr, /*max_frames=*/0);
    // PCMFlow will pump() and play the decoded PCM on the I2S/DAC side
}
```

End-to-end transceiver sketch (mic ↔ ESP-NOW ↔ DAC) lives in [examples/EspNowTransceiver/](examples/EspNowTransceiver/).

---

## Dependencies

- **[PCMFlow](https://github.com/tanakamasayuki/PCMFlow)** — provides `PCMSource`, `PCMSink`, `ByteStream`, `ByteSink`, and the playback pipeline. Declared via `depends=PCMFlow` in `library.properties`.

The G.722 codec core itself is vendored under [src/external/libg722/](src/external/libg722/) (verbatim subset of [sippy/libg722](https://github.com/sippy/libg722)); no external library needs to be installed.

---

## Target platforms

Same posture as PCMFlow: `architectures=*` in `library.properties`, with the same realistic constraints — 32-bit MCU with malloc, modest SRAM, ~10 KB of free flash for the codec.

Practical targets: ESP32 / ESP32-S3 / ESP32-C3 / ESP32-C6 / ESP32-P4, RP2040 / RP2350, Teensy 4.x, STM32 F4+, nRF52. AVR (Uno / Mega / Nano) is **not** a realistic target for G.722 — the ADPCM state plus working buffers eat too much SRAM relative to those parts' ~2 KB SRAM.

Provisional footprint (encoder + decoder): **Flash ≤ 12 KB, RAM ~512 B per direction**. Detailed targets live in [SPEC.md §6](SPEC.md).

---

## License

PCMFlowG722's own code: **MIT** ([LICENSE](LICENSE)).

The G.722 codec core under [src/external/libg722/](src/external/libg722/) is a verbatim subset of [sippy/libg722](https://github.com/sippy/libg722). Its license terms are:

- Codec implementation: **Public Domain** (Steve Underwood, 2005).
- 1993 CMU contribution: permissive notice with acknowledgement request.
- Sippy Software maintenance: 2-clause BSD.

All three are MIT-compatible. See [`src/external/LICENSE_libg722.md`](src/external/LICENSE_libg722.md) for the verbatim upstream notices. No additional user-side attribution is required beyond the standard MIT notice; the upstream notice file is shipped in `src/external/` for reference and license-hygiene auditing.

---

## Tests

```sh
cd tests
uv run --env-file .env pytest                  # host (default)
uv run --env-file .env pytest --profile=esp32  # real ESP32 hardware
```

See [tests/README.md](tests/README.md). Same conventions as the parent PCMFlow test suite (pytest-embedded + Arduino CLI, `lang-ship:host` + `esp32:esp32:esp32` profiles).
