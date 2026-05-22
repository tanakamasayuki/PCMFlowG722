# Tests

> 日本語版: [README.ja.md](README.ja.md)

Automated test suite for PCMFlowG722. Mirrors the conventions of the parent [PCMFlow test suite](https://github.com/tanakamasayuki/PCMFlow/tree/main/tests):

- [pytest-embedded](https://docs.espressif.com/projects/pytest-embedded/en/latest/) + Arduino CLI backend.
- Two profiles: `lang-ship:host` (logic verification, fast CI) and `esp32:esp32:esp32` (real hardware verification, footprint measurement).
- Per-feature subdirectory containing `<feature>.ino`, `sketch.yaml`, `test_<feature>.py`.
- Assertions use the `EXPECT_TRUE` / `EXPECT_EQ` macros and the `TEST done N/M` Serial protocol.

## Directory layout

- `smoke/` — Build sanity check. Compiles the umbrella header, instantiates encoder + decoder, and prints the library version. If this passes, the vendored libg722 codec sources have been picked up correctly by the Arduino library loader.
- `g722_encoder/` — API contract tests for `G722Encoder` (frame-count parity, buffer size, format validation, `reset()` semantics).
- `g722_decoder/` — API contract tests for `G722Decoder` (direct path, queued path for `PCMSource`, `reset()` semantics).
- `roundtrip/` — End-to-end encode → decode tests with sine and silence inputs. Asserts statistical properties (energy, frequency, peak amplitude) that account for the codec's ~3 ms group delay.
- `external_source/` — Integration test for `G722Decoder` plugged into PCMFlow's `setInputSource()` pipeline.

## G.722-specific test design

G.722 is **stateful** and has a fixed group delay (~3 ms / ~48 samples at 16 kHz, from the QMF + ADPCM filter chain). Sample-by-sample equality with the input is therefore **not** a meaningful metric for the lossy roundtrip. Instead:

| Test dir | What we check | Tolerance |
|----------|---------------|-----------|
| `smoke/` | Library + libg722 vendor compile and link | build only |
| `g722_encoder/` | encode(N) = N/2 bytes; even N enforced; reset() restores predictor state | exact API contract |
| `g722_decoder/` | decode(N) = 2N samples; queued path works; reset() restores predictor state | exact API contract |
| `roundtrip/` | Energy preserved within ±15 %, frequency preserved (zero-crossings), peak amplitude preserved, silence → near-silence | statistical |
| `external_source/` | Sample count through `PCMFlow::setInputSource()` matches input; non-trivial peak; mono → stereo upmix works | count exact, signal non-trivial |

The vendored libg722 is itself bit-exact to the ITU reference (by construction; the upstream README states it passes the ITU tests), so the v0.1 test suite focuses on what could regress in *our* bridge code rather than re-asserting bit-exactness against the ITU vectors. ITU vector tests are deferred — see [SPEC §"Deferred features"](../SPEC.md#deferred-features).

## Running

```sh
# host (default)
uv run --env-file .env pytest

# real ESP32
uv run --env-file .env pytest --profile=esp32

# single test
uv run --env-file .env pytest roundtrip/
```

See the [parent PCMFlow tests README](https://github.com/tanakamasayuki/PCMFlow/tree/main/tests#prerequisites) for prerequisites (uv, arduino-cli, board cores).
