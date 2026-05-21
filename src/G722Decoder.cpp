#include "G722Decoder.h"

#include <string.h>

extern "C" {
#include "external/libg722/g722.h"
#include "external/libg722/g722_decoder.h"
}

// PCMFlowG722 :: G722Decoder implementation.
//
// Thin bridge between PCMFlow's PCMSource interface and the vendored
// sippy/libg722 codec context. The libg722 functions take ownership of
// a heap-allocated opaque context (G722_DEC_CTX *) created by
// g722_decoder_new() and released by g722_decoder_destroy(). We store
// it here as void* so the public header does not have to drag in any
// upstream declarations.
//
// The PCMSource path (readFrames(), used when the decoder is wired
// into PCMFlow::setInputSource()) keeps a small FIFO of yet-to-be-
// decoded G.722 bytes. decode() invoked with a non-null pcm buffer
// writes to that buffer directly and does NOT touch the FIFO;
// decode() invoked with pcm == nullptr enqueues the bytes for later
// readFrames().
//
// Mode is hard-coded to ITU-T G.722 Mode 1 (64 kbps), the only mode
// exposed by v0.1.x — see SPEC.md "Deferred features" for Mode 2 / 3.

namespace
{
    constexpr int kG722Rate = 64000;
    constexpr int kG722Options = G722_DEFAULT;
} // namespace

G722Decoder::~G722Decoder()
{
    end();
}

bool G722Decoder::begin(const PCMFormat &outputFormat)
{
    end();

    if (outputFormat.sampleRate != 16000 || outputFormat.channels != 1 ||
        outputFormat.bitsPerSample != 16)
    {
        error_ = Error::InvalidFormat;
        ready_ = false;
        return false;
    }

    ctx_ = g722_decoder_new(kG722Rate, kG722Options);
    if (ctx_ == nullptr)
    {
        error_ = Error::OutOfMemory;
        ready_ = false;
        return false;
    }

    format_ = outputFormat;
    head_ = 0;
    tail_ = 0;
    ready_ = true;
    error_ = Error::None;
    return true;
}

void G722Decoder::end()
{
    if (ctx_ != nullptr)
    {
        g722_decoder_destroy(static_cast<G722_DEC_CTX *>(ctx_));
        ctx_ = nullptr;
    }
    ready_ = false;
    error_ = Error::NotReady;
    head_ = 0;
    tail_ = 0;
}

bool G722Decoder::reset()
{
    if (!ready_)
    {
        return false;
    }
    const PCMFormat saved = format_;
    end();
    return begin(saved);
}

size_t G722Decoder::decode(const uint8_t *packet,
                           size_t packetBytes,
                           int16_t *pcm,
                           size_t maxFrames)
{
    if (!ready_ || packet == nullptr)
    {
        error_ = ready_ ? Error::InvalidFormat : Error::NotReady;
        return 0;
    }

    // Direct-decode path: caller provided a PCM buffer.
    if (pcm != nullptr)
    {
        const size_t expectedFrames = packetBytes * 2u;
        if (maxFrames < expectedFrames)
        {
            error_ = Error::BufferTooSmall;
            return 0;
        }
        const int produced = g722_decode(
            static_cast<G722_DEC_CTX *>(ctx_),
            packet,
            static_cast<int>(packetBytes),
            pcm);
        if (produced < 0)
        {
            error_ = Error::InvalidFormat;
            return 0;
        }
        error_ = Error::None;
        return static_cast<size_t>(produced);
    }

    // Queued path: enqueue bytes for later readFrames(). Bytes that
    // would overflow the FIFO are dropped (oldest-preserving policy is
    // pointless for an adaptive codec — callers should size their feed
    // cadence to the queue capacity).
    size_t enqueued = 0;
    for (size_t i = 0; i < packetBytes; ++i)
    {
        const size_t next = (head_ + 1) % kQueueCapacity;
        if (next == tail_)
        {
            break; // full
        }
        queue_[head_] = packet[i];
        head_ = next;
        ++enqueued;
    }
    error_ = Error::None;
    return enqueued;
}

size_t G722Decoder::readFrames(void *out, size_t frameCount)
{
    if (!ready_ || out == nullptr)
    {
        return 0;
    }
    int16_t *pcm = static_cast<int16_t *>(out);

    // Pull encoded bytes out of the FIFO and decode in stack-sized
    // chunks. Each byte produces two PCM samples, so a chunk of up to
    // 64 bytes yields up to 128 samples (= 8 ms at 16 kHz).
    constexpr size_t kChunkBytes = 64;
    uint8_t chunk[kChunkBytes];
    size_t produced = 0;
    while (produced < frameCount && tail_ != head_)
    {
        // Limit chunk size by (a) FIFO occupancy, (b) caller capacity
        // measured in pairs of samples, (c) the static chunk buffer.
        size_t available;
        if (head_ >= tail_)
        {
            available = head_ - tail_;
        }
        else
        {
            available = kQueueCapacity - tail_;
        }
        const size_t want_pairs = (frameCount - produced) / 2u;
        if (want_pairs == 0)
        {
            break;
        }
        size_t take = available;
        if (take > want_pairs)
        {
            take = want_pairs;
        }
        if (take > kChunkBytes)
        {
            take = kChunkBytes;
        }

        // Linearize this chunk into the stack buffer. We could feed
        // libg722 directly from the ring buffer in two segments, but
        // staging into `chunk` keeps the libg722 call site simple.
        for (size_t i = 0; i < take; ++i)
        {
            chunk[i] = queue_[(tail_ + i) % kQueueCapacity];
        }
        const int got = g722_decode(
            static_cast<G722_DEC_CTX *>(ctx_),
            chunk,
            static_cast<int>(take),
            pcm + produced);
        if (got <= 0)
        {
            break;
        }
        tail_ = (tail_ + take) % kQueueCapacity;
        produced += static_cast<size_t>(got);
    }
    return produced;
}
