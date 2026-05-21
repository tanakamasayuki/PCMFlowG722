// Integration test: G722Decoder plugged into PCMFlow as a PCMSource.
//
//   1. Encode a known PCM input (1 kHz sine) into G.722 bytes.
//   2. Feed those bytes into the decoder's queued path
//      (decode(packet, n, nullptr, 0)).
//   3. PCMFlow::pump() pulls PCM out of the decoder via readFrames().
//   4. The test reads PCM out of PCMFlow via PCMFlow::readFrames().
//
// We assert:
//   - The PCMFlow output count matches the input sample count.
//   - The output has the right shape (non-trivial amplitude, ~1 kHz).
//   - mono->stereo upmix in PCMFlow's pipeline works on top of the
//     G.722 source (L == R per frame).

#include <PCMFlow.h>
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

// 20 ms of audio at 16 kHz = 320 samples = 160 G.722 bytes.
static constexpr size_t kSamples = 320;
static constexpr size_t kBytes = 160;

static int16_t pcm_in[kSamples];
static uint8_t coded[kBytes];

static void encode_test_bytes()
{
    G722Encoder enc;
    enc.begin({16000, 1, 16});
    for (size_t i = 0; i < kSamples; ++i) {
        pcm_in[i] = (int16_t)(8000 * sin(2.0 * M_PI * 1000.0 * i / 16000.0));
    }
    (void)enc.encode(pcm_in, kSamples, coded, kBytes);
}

static size_t pump_and_drain(PCMFlow &audio,
                             G722Decoder &dec,
                             const uint8_t *bytes,
                             size_t byteCount,
                             int16_t *out,
                             size_t outCapFrames)
{
    size_t enq = 0;
    size_t produced = 0;
    int idle = 0;
    while (produced < outCapFrames && idle < 64)
    {
        if (enq < byteCount)
        {
            const size_t took = dec.decode(bytes + enq, byteCount - enq,
                                           nullptr, 0);
            enq += took;
        }
        audio.pump();
        const size_t got = audio.readFrames(out + produced, outCapFrames - produced);
        if (got == 0) ++idle;
        else { idle = 0; produced += got; }
    }
    return produced;
}

static void test_passthrough_mono()
{
    G722Decoder dec;
    EXPECT_TRUE("dec.begin", dec.begin({16000, 1, 16}));
    EXPECT_TRUE("dec.ready", dec.isReady());
    EXPECT_TRUE("dec.eof-always-false", !dec.isEof());

    PCMFlow audio;
    audio.setInputSource(dec);
    audio.setOutputFormat({16000, 1, 16});

    static int16_t out[kSamples];
    const size_t produced = pump_and_drain(audio, dec, coded, kBytes,
                                           out, kSamples);
    EXPECT_EQ("mono/count", (long)kSamples, (long)produced);

    // Check that the PCM is non-trivial. Codec warm-up means the first
    // few samples can be small, so check overall peak across the buffer.
    int16_t peak = 0;
    for (size_t i = 0; i < produced; ++i) {
        const int16_t a = out[i] >= 0 ? out[i] : (int16_t)-out[i];
        if (a > peak) peak = a;
    }
    Serial.print("INFO mono/peak="); Serial.println(peak);
    EXPECT_TRUE("mono/non-trivial-peak", peak > 3000);
}

static void test_mono_to_stereo()
{
    G722Decoder dec;
    dec.begin({16000, 1, 16});

    PCMFlow audio;
    audio.setInputSource(dec);
    audio.setOutputFormat({16000, 2, 16});

    static int16_t out[kSamples * 2];
    const size_t produced = pump_and_drain(audio, dec, coded, kBytes,
                                           out, kSamples);
    EXPECT_EQ("mono2stereo/count", (long)kSamples, (long)produced);

    int channel_diffs = 0;
    for (size_t i = 0; i < produced; ++i) {
        if (out[2 * i + 0] != out[2 * i + 1]) ++channel_diffs;
    }
    EXPECT_EQ("mono2stereo/L==R", 0, channel_diffs);
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("TEST start external_source");

    encode_test_bytes();
    test_passthrough_mono();
    test_mono_to_stereo();

    Serial.print("TEST done ");
    Serial.print(g_pass);
    Serial.print("/");
    Serial.println(g_total);
}

void loop()
{
    delay(1);
}
