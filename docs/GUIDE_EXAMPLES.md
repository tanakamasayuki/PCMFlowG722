# PCMFlowG722 — Example workflow

> 日本語版: [GUIDE_EXAMPLES.ja.md](GUIDE_EXAMPLES.ja.md)

Encode and decode voice with G.722.

Check installation and platform requirements in the [library README](../README.md), then choose a sketch from the [example index](../examples/README.md). Version/support tables and internal capacity defaults stay in their original reference rather than being duplicated here.

## From setup to a verified result

1. Choose the smallest relevant sketch and check its board, hardware, dependencies and bundled files.
2. Match pins, peers and input files to your actual setup. Each README identifies important placeholders.
3. Open the .ino, select board/port and upload. Match Serial baud where used.
4. Locate the last completed stage: initialization, input, processing or output. Compilation does not test hardware I/O.
5. Reproduce the supplied example before changing one setting at a time.

## API and data handling

The examples use M5Unified microphone and speaker hardware, PCMFlow and PCMFlowDevice as separate stages. Preserve encoder/decoder state across consecutive packets; wideband PCM rate and RTP clock conventions are different concerns. Check codec begin/encode/decode results and distinguish PCM frame counts from encoded byte counts.

For a transceiver, use two boards on the same ESP-NOW channel and configure the peer/broadcast path used in the source. Hold the indicated button to transmit where the sketch uses push-to-talk. Capturing audio, encoding, radio delivery, decoding and buffered playback can fail separately; inspect each stage instead of treating send success as audible output.

Keep Wi-Fi receive callbacks short and copy/queue packet bytes for later processing. A loopback needs only one board and tests the codec path without testing radio delivery.

## When adapting it

- Distinguish bool status, processed counts and handles; do not use stale output after failure.
- Registered callbacks run later from their documented context. Decide synchronization and retained-data lifetime.
- Keep bytes, samples, frames and pixels distinct; capacity is different from valid length.
- Retain views/pointers/buffers for the required period. Copying a pointer does not copy its payload.
- Add application-specific failure handling, retry and cleanup that a minimal example may omit.

## Read the individual README

Use Prepare and run for hardware and steps, Follow the sketch for the actual APIs and precautions for feature-specific constraints. If output is missing, check initialization first, continued service calls next and input/peer readiness after that.
