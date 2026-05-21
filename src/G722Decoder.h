#ifndef PCMFLOWG722_G722DECODER_H
#define PCMFLOWG722_G722DECODER_H

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

#include "PCMFormat.h"
#include "PCMSource.h"

// PCMFlowG722 :: G722Decoder
//
// Decodes 8-bit G.722 bytes (ITU-T G.722 Mode 1, 64 kbps) into 16-bit
// signed PCM samples (16 kHz mono). One G.722 byte produces exactly
// two PCM samples.
//
// The decoder is stateful (G.722 maintains adaptive predictor and
// quantizer state). There is no PLC built in: when a packet is lost,
// the caller decides whether to feed silence bytes (the decoder will
// process them and the predictor will recover within a few tens of
// ms), hold the last samples, or insert separately generated comfort
// noise.

class G722Decoder : public PCMSource
{
public:
    enum class Error : uint8_t
    {
        None,
        NotReady,
        InvalidFormat,  // sample rate != 16000, channels != 1, or bits != 16
        BufferTooSmall, // PCM capacity < packetBytes * 2
        OutOfMemory,    // underlying libg722 context allocation failed
    };

    G722Decoder() = default;
    ~G722Decoder();

    G722Decoder(const G722Decoder &) = delete;
    G722Decoder &operator=(const G722Decoder &) = delete;

    // Initialize the decoder.
    //   outputFormat : sampleRate must be 16000; channels 1; bitsPerSample == 16.
    bool begin(const PCMFormat &outputFormat);
    void end();

    // Reset the adaptive predictor / quantizer state. See encoder.
    bool reset();

    // Decode `packetBytes` G.722 bytes into `pcm`. PCM capacity must be
    // >= packetBytes * 2 (two samples per byte). Returns the number of
    // PCM samples produced.
    //
    // Direct-decode path: pcm != nullptr. The bytes are decoded
    // immediately into the supplied PCM buffer.
    //
    // Queued path (for PCMFlow::setInputSource integration): pcm ==
    // nullptr. Bytes are enqueued into an internal FIFO; readFrames()
    // pulls them out and decodes on demand.
    size_t decode(const uint8_t *packet,
                  size_t packetBytes,
                  int16_t *pcm,
                  size_t maxFrames);

    // PCMSource interface ------------------------------------------------
    // Returns decoded frames previously queued by decode(_, _, nullptr,
    // 0). The packet stream has no defined EOF, so isEof() is always
    // false.
    const PCMFormat &format() const override { return format_; }
    bool isReady() const override { return ready_; }
    bool isEof() const override { return false; }
    size_t readFrames(void *out, size_t frameCount) override;

    Error lastError() const { return error_; }

private:
    // Small FIFO of yet-to-be-decoded G.722 bytes for the PCMSource
    // (setInputSource) path. 640 bytes = 80 ms at 16 kHz / 64 kbps,
    // four times the typical RTP 20 ms cadence.
    static constexpr size_t kQueueCapacity = 640;

    PCMFormat format_{};
    bool ready_ = false;
    Error error_ = Error::NotReady;

    // Opaque libg722 decoder context.
    void *ctx_ = nullptr;

    uint8_t queue_[kQueueCapacity]{};
    size_t head_ = 0;
    size_t tail_ = 0;
};

#endif // PCMFLOWG722_G722DECODER_H
