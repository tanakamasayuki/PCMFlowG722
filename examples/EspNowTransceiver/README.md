# EspNowTransceiver

> 日本語版: [README.ja.md](README.ja.md)

Exchange encoded voice between ESP-NOW peers.

## Prepare and run

1. Check the [example guide](../../docs/GUIDE_EXAMPLES.md) for setup and hardware. Use the root README for supported boards and dependencies.
2. Open [EspNowTransceiver.ino](EspNowTransceiver.ino) and select the actual board/port. Prepare the dependencies used by these includes:

   `<M5Unified.h>`, `<WiFi.h>`, `<esp_now.h>`, `<esp_wifi.h>`, `<PCMFlow.h>`, `<PCMFlowDeviceM5.h>`, `<PCMFlowG722.h>`

3. Upload and open the Serial monitor at 115200 baud. Check startup messages and input conditions in the source.

## Follow the sketch

PCMFlowG722 example: EspNowTransceiver

Half-duplex HD-voice transceiver over ESP-NOW for two (or more)
M5Stack Core2 boards. The same firmware acts as both sender and
receiver:

  while button A is held:
      M5.Mic (16 kHz mono) -> G722Encoder -> ESP-NOW broadcast
  always:
      ESP-NOW recv -> G722Decoder -> PCMFlow -> M5.Speaker

One 20 ms voice frame at 16 kHz produces 160 G.722 bytes (320 input
samples / 2). This fits well under ESP-NOW's 250-byte payload limit,
and is twice the audio bandwidth of the G.711 sibling example at the
same packet budget.

Flash this binary on two boards (no pairing needed — ESP-NOW broadcast
reaches every peer in range on the same Wi-Fi channel). Hold A on one
board to talk; the other plays it back through the speaker.

These APIs are listed in their first source order. Registering a callback does not execute its body at that point.

1. `g_dec.decode(...)` — Validate incoming encoded length and format before using the decoded output.
2. `g_enc.encode(...)` — Encode into sufficiently large storage and validate the returned result before consuming output.
3. `g_audio.pump()` — Advance decoding/conversion, then consume only the available returned PCM frames.
4. `g_audio.readFrames(...)` — Use the actual returned frame count; bytes also depend on channel count and sample width.
5. `g_player.writeFrames(...)` — Check the number accepted and retain any unconsumed tail; a frame is not a byte.
6. `g_enc.begin(...)` — Initialize this component after its prerequisites; inspect the returned status where provided.
7. `g_audio.setOutputFormat(...)` — Choose a format supported by the next stage, not just the source format.
8. `g_audio.setBufferFrames(...)` — Size buffering in frames and account for both memory and added latency.
9. `g_audio.setInputSource(...)` — Attach a source that remains alive while the pipeline reads from it.

Review these application constants before adapting the sketch: `kRate`, `kFrameSamples`, `kFrameBytes`, `kMaxPlayFrames`, `kWifiChannel`. Match pins, channels and capacities to your application.

## API usage and precautions

The examples use M5Unified microphone and speaker hardware, PCMFlow and PCMFlowDevice as separate stages. Preserve encoder/decoder state across consecutive packets; wideband PCM rate and RTP clock conventions are different concerns. Check codec begin/encode/decode results and distinguish PCM frame counts from encoded byte counts.

For a transceiver, use two boards on the same ESP-NOW channel and configure the peer/broadcast path used in the source. Hold the indicated button to transmit where the sketch uses push-to-talk. Capturing audio, encoding, radio delivery, decoding and buffered playback can fail separately; inspect each stage instead of treating send success as audible output.

Keep Wi-Fi receive callbacks short and copy/queue packet bytes for later processing. A loopback needs only one board and tests the codec path without testing radio delivery.

## Inputs and observable output

Button conditions used by this sketch: `BtnA.isPressed()`. Distinguish press edges, release edges and a continuously held button.

## Verify and troubleshoot

Exchange encoded voice between ESP-NOW peers and verify the result on its output. First use the supplied input, then change one configuration or data source at a time.

| Symptom | First check |
|---|---|
| No output | Board/dependencies, initialization results, output destination and Serial baud |
| Starts but stops making progress | Wait conditions, buffer space, peer/input readiness and service calls |
| Unexpected values | Lengths/units, format, data lifetime, shared state and partial results |

See the [guide](../../docs/GUIDE_EXAMPLES.md) for shared principles and the [example index](../README.md) for the next sketch.
