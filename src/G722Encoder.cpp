#include "G722Encoder.h"

extern "C" {
#include "external/libg722/g722.h"
#include "external/libg722/g722_encoder.h"
}

// PCMFlowG722 :: G722Encoder implementation.
//
// Thin bridge between PCMFlow's PCMSink interface and the vendored
// sippy/libg722 codec context. The libg722 functions take ownership of
// a heap-allocated opaque context (G722_ENC_CTX *) created by
// g722_encoder_new() and released by g722_encoder_destroy(). We store
// it here as void* so the public header does not have to drag in any
// upstream declarations.
//
// Mode is hard-coded to ITU-T G.722 Mode 1 (64 kbps), the only mode
// exposed by v0.1.x — see SPEC.md "Deferred features" for Mode 2 / 3.

namespace
{
    constexpr int kG722Rate = 64000;
    constexpr int kG722Options = G722_DEFAULT;
} // namespace

G722Encoder::~G722Encoder()
{
    end();
}

bool G722Encoder::begin(const PCMFormat &inputFormat)
{
    end();

    if (inputFormat.sampleRate != 16000 || inputFormat.channels != 1 ||
        inputFormat.bitsPerSample != 16)
    {
        error_ = Error::InvalidFormat;
        ready_ = false;
        return false;
    }

    ctx_ = g722_encoder_new(kG722Rate, kG722Options);
    if (ctx_ == nullptr)
    {
        error_ = Error::OutOfMemory;
        ready_ = false;
        return false;
    }

    format_ = inputFormat;
    ready_ = true;
    error_ = Error::None;
    return true;
}

void G722Encoder::end()
{
    if (ctx_ != nullptr)
    {
        g722_encoder_destroy(static_cast<G722_ENC_CTX *>(ctx_));
        ctx_ = nullptr;
    }
    ready_ = false;
    error_ = Error::NotReady;
    packetSinkCb_ = nullptr;
    packetSinkUser_ = nullptr;
}

bool G722Encoder::reset()
{
    if (!ready_)
    {
        return false;
    }
    const PCMFormat saved = format_;
    PacketSink savedCb = packetSinkCb_;
    void *savedUser = packetSinkUser_;
    end();
    if (!begin(saved))
    {
        return false;
    }
    packetSinkCb_ = savedCb;
    packetSinkUser_ = savedUser;
    return true;
}

size_t G722Encoder::encode(const int16_t *pcm,
                           size_t frameCount,
                           uint8_t *out,
                           size_t outCapacity)
{
    if (!ready_ || pcm == nullptr || out == nullptr)
    {
        error_ = ready_ ? Error::InvalidFormat : Error::NotReady;
        return 0;
    }
    if ((frameCount & 1u) != 0u)
    {
        error_ = Error::FrameCountNotEven;
        return 0;
    }
    const size_t expected = frameCount / 2u;
    if (outCapacity < expected)
    {
        error_ = Error::BufferTooSmall;
        return 0;
    }

    const int produced = g722_encode(
        static_cast<G722_ENC_CTX *>(ctx_),
        pcm,
        static_cast<int>(frameCount),
        out);
    if (produced < 0)
    {
        error_ = Error::InvalidFormat;
        return 0;
    }
    error_ = Error::None;
    return static_cast<size_t>(produced);
}

size_t G722Encoder::writeFrames(const void *in, size_t frameCount)
{
    if (!ready_ || in == nullptr || packetSinkCb_ == nullptr)
    {
        return 0;
    }
    if ((frameCount & 1u) != 0u)
    {
        // Drop the trailing odd sample silently — the caller's framing
        // is broken but we should not block the stream.
        --frameCount;
        if (frameCount == 0)
        {
            return 0;
        }
    }
    const int16_t *pcm = static_cast<const int16_t *>(in);

    // Process in stack-sized chunks so writeFrames() with large inputs
    // doesn't require an unbounded scratch buffer. Chunk size must be
    // an even number of samples; 256 samples = 128 G.722 bytes per
    // chunk = 8 ms at 16 kHz, comfortably under one RTP frame.
    constexpr size_t kChunkSamples = 256;
    uint8_t chunk[kChunkSamples / 2];
    size_t consumed = 0;
    while (consumed < frameCount)
    {
        size_t take = (frameCount - consumed) < kChunkSamples
                          ? (frameCount - consumed)
                          : kChunkSamples;
        // Force the chunk size to be even (already guaranteed by
        // kChunkSamples being even and the tail-trim above, but the
        // explicit mask keeps the invariant local).
        take &= ~static_cast<size_t>(1u);
        if (take == 0)
        {
            break;
        }
        const size_t got = encode(pcm + consumed, take, chunk, sizeof(chunk));
        if (got == 0)
        {
            break;
        }
        packetSinkCb_(packetSinkUser_, chunk, got);
        consumed += take;
    }
    return consumed;
}

void G722Encoder::setPacketSink(PacketSink cb, void *userData)
{
    packetSinkCb_ = cb;
    packetSinkUser_ = userData;
}
