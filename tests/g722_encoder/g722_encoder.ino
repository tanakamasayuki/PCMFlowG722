// API contract tests for G722Encoder.
//
// Unlike G.711, G.722 is stateful and the math is not a simple table
// lookup, so this test focuses on the contract the bridge exposes:
//   - begin() validates format strictly (16 kHz mono 16-bit)
//   - encode() of N samples produces N/2 bytes
//   - encode() rejects odd N with FrameCountNotEven
//   - encode() rejects insufficient output capacity with BufferTooSmall
//   - encode() emits non-trivial output for non-trivial input
//   - reset() restores the predictor state
//
// Bit-exactness against ITU vectors is delegated to the vendored libg722
// itself (which is bit-exact to ITU by construction; see its README);
// see SPEC.md "Deferred features" for plans to add ITU vector assertions
// downstream.

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

static void fill_sine(int16_t *pcm, size_t n, double freq_hz, int16_t amp)
{
    for (size_t i = 0; i < n; ++i) {
        pcm[i] = (int16_t)(amp * sin(2.0 * M_PI * freq_hz * i / 16000.0));
    }
}

static void test_basic_encode()
{
    G722Encoder enc;
    EXPECT_TRUE("begin", enc.begin({16000, 1, 16}));
    EXPECT_TRUE("ready", enc.isReady());
    EXPECT_EQ("rate", 16000, enc.format().sampleRate);
    EXPECT_EQ("ch",   1,     enc.format().channels);
    EXPECT_EQ("bits", 16,    enc.format().bitsPerSample);

    static int16_t pcm[320];
    static uint8_t out[160];
    fill_sine(pcm, 320, 1000.0, 8000);
    const size_t bytes = enc.encode(pcm, 320, out, sizeof(out));
    EXPECT_EQ("count/half", 160, (long)bytes);

    // Output must not be entirely identical bytes — that would indicate
    // the codec produced silence/constant for a 1 kHz tone, i.e. broken.
    int distinct = 0;
    uint8_t seen[256] = {0};
    for (size_t i = 0; i < bytes; ++i) {
        if (!seen[out[i]]) { seen[out[i]] = 1; ++distinct; }
    }
    EXPECT_TRUE("output-not-constant", distinct >= 4);
}

static void test_frame_count_not_even()
{
    G722Encoder enc;
    enc.begin({16000, 1, 16});
    int16_t pcm[7] = {0};
    uint8_t out[8];
    EXPECT_EQ("odd/zero-bytes", 0, (long)enc.encode(pcm, 7, out, sizeof(out)));
    EXPECT_EQ("odd/error", (int)G722Encoder::Error::FrameCountNotEven, (int)enc.lastError());
}

static void test_buffer_too_small()
{
    G722Encoder enc;
    enc.begin({16000, 1, 16});
    int16_t pcm[8] = {0};
    uint8_t out[3]; // need 4 bytes for 8 samples
    EXPECT_EQ("too-small/zero", 0, (long)enc.encode(pcm, 8, out, sizeof(out)));
    EXPECT_EQ("too-small/error", (int)G722Encoder::Error::BufferTooSmall, (int)enc.lastError());
}

static void test_invalid_format()
{
    G722Encoder enc;
    EXPECT_TRUE("bad-rate/rejected",     !enc.begin({8000,  1, 16}));
    EXPECT_TRUE("bad-channels/rejected", !enc.begin({16000, 2, 16}));
    EXPECT_TRUE("bad-bits/rejected",     !enc.begin({16000, 1, 8}));
}

static void test_reset_clears_state()
{
    // Encode the same buffer twice with a fresh encoder each time -> the
    // two outputs must be identical (predictor starts from the same
    // state). After encoding once then calling reset(), encoding the
    // same buffer should reproduce the first-call output, not the
    // second-call output.
    static int16_t pcm[320];
    fill_sine(pcm, 320, 1500.0, 6000);

    static uint8_t out_fresh[160];
    static uint8_t out_after_reset[160];
    static uint8_t out_continued[160];

    G722Encoder enc;
    enc.begin({16000, 1, 16});
    (void)enc.encode(pcm, 320, out_fresh, sizeof(out_fresh));
    (void)enc.encode(pcm, 320, out_continued, sizeof(out_continued));
    EXPECT_TRUE("reset", enc.reset());
    (void)enc.encode(pcm, 320, out_after_reset, sizeof(out_after_reset));

    EXPECT_TRUE("post-reset-matches-fresh",
                memcmp(out_fresh, out_after_reset, sizeof(out_fresh)) == 0);
    // The second consecutive encode of the same input must differ from
    // the first (stateful predictor), or this test is not exercising
    // statefulness — guards against a regression where reset is a no-op.
    EXPECT_TRUE("consecutive-encode-differs",
                memcmp(out_fresh, out_continued, sizeof(out_fresh)) != 0);
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("TEST start g722_encoder");

    test_basic_encode();
    test_frame_count_not_even();
    test_buffer_too_small();
    test_invalid_format();
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
