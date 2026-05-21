#ifndef PCMFLOWG722_G722ENCODER_H
#define PCMFLOWG722_G722ENCODER_H

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

#include "PCMFormat.h"
#include "PCMSink.h"

// PCMFlowG722 :: G722Encoder
//
// Encodes 16-bit signed PCM samples (16 kHz mono) to 8-bit G.722 bytes
// at 64 kbps (ITU-T G.722 Mode 1). Two PCM samples produce exactly one
// G.722 byte.
//
// The encoder is stateful (G.722 maintains adaptive predictor and
// quantizer state per direction). begin() initializes the state; end()
// releases it. The same instance must not be shared across tasks
// without external synchronization.
//
// Input frameCount must be even; G.722 processes samples in pairs of
// two via QMF. Odd-count calls return 0 with Error::FrameCountNotEven.

class G722Encoder : public PCMSink
{
public:
    enum class Error : uint8_t
    {
        None,
        NotReady,
        InvalidFormat,     // sample rate != 16000, channels != 1, or bits != 16
        BufferTooSmall,    // output capacity < frameCount / 2
        FrameCountNotEven, // frameCount % 2 != 0
        OutOfMemory,       // underlying libg722 context allocation failed
    };

    G722Encoder() = default;
    ~G722Encoder();

    G722Encoder(const G722Encoder &) = delete;
    G722Encoder &operator=(const G722Encoder &) = delete;

    // Initialize the encoder.
    //   inputFormat : sampleRate must be 16000; channels 1; bitsPerSample == 16.
    // Returns false on error; query lastError() for the cause.
    bool begin(const PCMFormat &inputFormat);
    void end();

    // Reset the adaptive predictor / quantizer state. Equivalent to
    // end() followed by begin() with the same format. Use when a stream
    // boundary is known (e.g. switching peers or after a long silence
    // gap during which fresh convergence is preferred over carry-over).
    bool reset();

    // Encode `frameCount` PCM samples into `out`. `frameCount` must be
    // even; output capacity must be >= frameCount / 2 (one byte per
    // pair of samples). Returns the number of bytes written, or 0 on
    // error.
    size_t encode(const int16_t *pcm,
                  size_t frameCount,
                  uint8_t *out,
                  size_t outCapacity);

    // PCMSink interface --------------------------------------------------
    // Pushes PCM into the encoder. If a packet sink callback is registered
    // via setPacketSink(), each writeFrames() emits encoded chunks of
    // bytes (half the input sample count) to it. Without a sink the
    // PCMSink path is a no-op; use encode() for direct control.
    const PCMFormat &format() const override { return format_; }
    bool isReady() const override { return ready_; }
    size_t writeFrames(const void *in, size_t frameCount) override;

    Error lastError() const { return error_; }

    // Packet sink callback signature for the PCMSink path.
    using PacketSink = void (*)(void *userData,
                                const uint8_t *packet,
                                size_t packetBytes);
    void setPacketSink(PacketSink cb, void *userData);

private:
    PCMFormat format_{};
    bool ready_ = false;
    Error error_ = Error::NotReady;

    // Opaque libg722 encoder context, allocated by begin(), freed by
    // end(). Declared as void* here so users of the umbrella header do
    // not have to drag in the upstream private struct definitions.
    void *ctx_ = nullptr;

    PacketSink packetSinkCb_ = nullptr;
    void *packetSinkUser_ = nullptr;
};

#endif // PCMFLOWG722_G722ENCODER_H
