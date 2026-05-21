// API contract tests for G722Decoder.
//
// Mirrors g722_encoder/ in spirit:
//   - begin() validates format strictly
//   - decode() of N bytes produces N*2 samples (direct path)
//   - decode() rejects insufficient PCM capacity with BufferTooSmall
//   - decode() with pcm == nullptr enqueues for the readFrames() path
//   - reset() restores the predictor state
//
// G.722 decoding never reports EOF (the byte stream has no defined end
// of stream), so isEof() must always be false once begin() succeeds.

#include <PCMFlowG722.h>
#include <math.h>

static int g_pass = 0;
static int g_total = 0;

#define EXPECT_TRUE(name, cond) do { \
    ++g_total; \
    if (cond) { ++g_pass; Serial.print("PASS "); Serial.println(name); } \
    else { Serial.print("FAIL "); Serial.print(name); Serial.println(" cond"); } \
} while (0)

#define EXPECT_EQ(name, expected, actual) do { \
    ++g_total; \
    const long _e = (long)(expected); \
    const long _a = (long)(actual); \
    if (_e == _a) { ++g_pass; Serial.print("PASS "); Serial.println(name); } \
    else { \
        Serial.print("FAIL "); Serial.print(name); \
        Serial.print(" expected="); Serial.print(_e); \
        Serial.print(" actual=");   Serial.println(_a); \
    } \
} while (0)

// Helper: encode a 1 kHz sine into bytes we can then feed back to the
// decoder under test. The decoder API tests do not assume any specific
// PCM output — they only assert counts, ranges, and stateful behavior.
static size_t make_test_bytes(uint8_t *out, size_t outCap)
{
    G722Encoder enc;
    enc.begin({16000, 1, 16});
    static int16_t pcm[320];
    for (size_t i = 0; i < 320; ++i) {
        pcm[i] = (int16_t)(8000 * sin(2.0 * M_PI * 1000.0 * i / 16000.0));
    }
    return enc.encode(pcm, 320, out, outCap);
}

static void test_basic_decode()
{
    G722Decoder dec;
    EXPECT_TRUE("begin", dec.begin({16000, 1, 16}));
    EXPECT_TRUE("ready", dec.isReady());
    EXPECT_TRUE("eof-always-false", !dec.isEof());
    EXPECT_EQ("rate", 16000, dec.format().sampleRate);
    EXPECT_EQ("ch",   1,     dec.format().channels);
    EXPECT_EQ("bits", 16,    dec.format().bitsPerSample);

    static uint8_t bytes[160];
    const size_t b = make_test_bytes(bytes, sizeof(bytes));
    EXPECT_EQ("test-bytes-count", 160, (long)b);

    static int16_t pcm[320];
    const size_t frames = dec.decode(bytes, b, pcm, sizeof(pcm) / sizeof(pcm[0]));
    EXPECT_EQ("count/double", 320, (long)frames);

    // Output should contain meaningful (non-constant) PCM.
    int distinct = 0;
    int prev = pcm[0];
    for (size_t i = 1; i < frames; ++i) {
        if (pcm[i] != prev) { ++distinct; prev = pcm[i]; }
    }
    EXPECT_TRUE("output-not-constant", distinct >= 50);
}

static void test_buffer_too_small()
{
    G722Decoder dec;
    dec.begin({16000, 1, 16});
    uint8_t bytes[8] = {0};
    int16_t pcm[8]; // need 16 samples for 8 bytes
    EXPECT_EQ("too-small/zero", 0, (long)dec.decode(bytes, 8, pcm, 8));
    EXPECT_EQ("too-small/error", (int)G722Decoder::Error::BufferTooSmall, (int)dec.lastError());
}

static void test_invalid_format()
{
    G722Decoder dec;
    EXPECT_TRUE("bad-rate/rejected",     !dec.begin({8000,  1, 16}));
    EXPECT_TRUE("bad-channels/rejected", !dec.begin({16000, 2, 16}));
    EXPECT_TRUE("bad-bits/rejected",     !dec.begin({16000, 1, 8}));
}

static void test_queued_path()
{
    // decode(_, _, nullptr, 0) enqueues bytes; readFrames() pulls PCM.
    static uint8_t bytes[160];
    make_test_bytes(bytes, sizeof(bytes));

    G722Decoder dec;
    dec.begin({16000, 1, 16});
    const size_t enq = dec.decode(bytes, 160, nullptr, 0);
    EXPECT_EQ("queue/enqueued", 160, (long)enq);

    static int16_t pcm[320];
    const size_t pulled = dec.readFrames(pcm, 320);
    EXPECT_EQ("queue/readFrames", 320, (long)pulled);

    // After draining, a second readFrames returns 0.
    EXPECT_EQ("queue/empty-on-second-read", 0, (long)dec.readFrames(pcm, 320));
}

static void test_reset_clears_state()
{
    static uint8_t bytes[160];
    make_test_bytes(bytes, sizeof(bytes));

    G722Decoder dec;
    dec.begin({16000, 1, 16});
    static int16_t out_fresh[320];
    static int16_t out_after_reset[320];
    static int16_t out_continued[320];

    (void)dec.decode(bytes, 160, out_fresh, 320);
    (void)dec.decode(bytes, 160, out_continued, 320);
    EXPECT_TRUE("reset", dec.reset());
    (void)dec.decode(bytes, 160, out_after_reset, 320);

    EXPECT_TRUE("post-reset-matches-fresh",
                memcmp(out_fresh, out_after_reset, sizeof(out_fresh)) == 0);
    EXPECT_TRUE("consecutive-decode-differs",
                memcmp(out_fresh, out_continued, sizeof(out_fresh)) != 0);
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("TEST start g722_decoder");

    test_basic_decode();
    test_buffer_too_small();
    test_invalid_format();
    test_queued_path();
    test_reset_clears_state();

    Serial.print("TEST done ");
    Serial.print(g_pass);
    Serial.print("/");
    Serial.println(g_total);
}

void loop()
{
    delay(1);
}
