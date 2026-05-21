// End-to-end encode -> decode roundtrip tests for PCMFlowG722.
//
// G.722 is lossy AND stateful AND introduces a fixed group delay
// (QMF + ADPCM ~ 3 ms = ~48 samples at 16 kHz). Sample-by-sample
// equality against the input is therefore not a meaningful metric.
// What we CAN assert is:
//   1. The decoded output has the right shape (count = 2x input bytes,
//      i.e. == input sample count for a closed loop).
//   2. Energy is preserved (decoded RMS ≈ input RMS within ±15%).
//   3. Frequency content is preserved (zero-crossing count matches).
//   4. Peak amplitude is preserved.
//   5. Silence in -> near-silence out (no DC bias from the codec).

#include <PCMFlowG722.h>
#include <math.h>
#include <stdlib.h>

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

// Sine length: 1 second of audio = 16000 samples = 8000 bytes. Long
// enough for the predictor to fully converge and to average out
// transient artefacts; small enough to fit on a host without strain
// (32 KB of int16 + 8 KB of bytes + ~32 KB of decoded int16).
static constexpr size_t kSamples = 16000;
static constexpr size_t kBytes = kSamples / 2;
static constexpr size_t kWarmup = 480; // 30 ms

static int16_t pcm_in[kSamples];
static uint8_t coded[kBytes];
static int16_t pcm_out[kSamples];

static void fill_sine(int16_t *p, size_t n, double freq_hz, int16_t amp)
{
    for (size_t i = 0; i < n; ++i) {
        p[i] = (int16_t)(amp * sin(2.0 * M_PI * freq_hz * i / 16000.0));
    }
}

static int count_zero_crossings(const int16_t *p, size_t start, size_t end)
{
    int crosses = 0;
    int prev_sign = p[start] >= 0 ? 1 : -1;
    for (size_t i = start + 1; i < end; ++i) {
        const int sign = p[i] >= 0 ? 1 : -1;
        if (sign != prev_sign) { ++crosses; prev_sign = sign; }
    }
    return crosses;
}

static void test_sine_1khz()
{
    fill_sine(pcm_in, kSamples, 1000.0, 8000);

    G722Encoder enc;
    G722Decoder dec;
    EXPECT_TRUE("enc.begin", enc.begin({16000, 1, 16}));
    EXPECT_TRUE("dec.begin", dec.begin({16000, 1, 16}));

    const size_t bytes = enc.encode(pcm_in, kSamples, coded, kBytes);
    EXPECT_EQ("enc.count", (long)kBytes, (long)bytes);
    const size_t frames = dec.decode(coded, bytes, pcm_out, kSamples);
    EXPECT_EQ("dec.count", (long)kSamples, (long)frames);

    // Energy preservation: RMS of decoded output ≈ RMS of input.
    // (We can't compare sample-by-sample because of the codec's ~3 ms
    // group delay, but total energy should be preserved well within
    // 15 % — G.722 Mode 1 SNR on speech-band tones is > 35 dB.)
    double sse_in = 0, sse_out = 0;
    size_t cnt = 0;
    for (size_t i = kWarmup; i < kSamples; ++i) {
        sse_in  += (double)pcm_in[i]  * (double)pcm_in[i];
        sse_out += (double)pcm_out[i] * (double)pcm_out[i];
        ++cnt;
    }
    const long rms_in  = (long)sqrt(sse_in  / (double)cnt);
    const long rms_out = (long)sqrt(sse_out / (double)cnt);
    Serial.print("INFO sine-1k/rms-in=");  Serial.print(rms_in);
    Serial.print(" rms-out="); Serial.println(rms_out);
    const long rms_diff = rms_in > rms_out ? (rms_in - rms_out) : (rms_out - rms_in);
    EXPECT_TRUE("sine-1k/energy-preserved", rms_diff * 100 < rms_in * 15);

    // Frequency preservation via zero-crossing count.
    // 1 kHz @ 16 kHz over kSamples-kWarmup samples = 2*1000*(t) crossings.
    const int expected_crosses = (int)(2.0 * 1000.0 * (double)(kSamples - kWarmup) / 16000.0);
    const int got_crosses = count_zero_crossings(pcm_out, kWarmup, kSamples);
    Serial.print("INFO sine-1k/zero-crosses got="); Serial.print(got_crosses);
    Serial.print(" expected="); Serial.println(expected_crosses);
    const int crosses_diff = abs(got_crosses - expected_crosses);
    EXPECT_TRUE("sine-1k/frequency-preserved", crosses_diff <= 10);

    // Amplitude preservation (peak in the post-warmup region).
    int16_t peak_out = 0;
    for (size_t i = kWarmup; i < kSamples; ++i) {
        const int16_t a = pcm_out[i] >= 0 ? pcm_out[i] : (int16_t)-pcm_out[i];
        if (a > peak_out) peak_out = a;
    }
    Serial.print("INFO sine-1k/peak-out="); Serial.println(peak_out);
    EXPECT_TRUE("sine-1k/peak-near-input", peak_out > 6000 && peak_out < 10000);
}

// Verify encoder/decoder consistency for a complementary use case:
// silence in, near-silence out.
static void test_silence()
{
    for (size_t i = 0; i < kSamples; ++i) pcm_in[i] = 0;

    G722Encoder enc;
    G722Decoder dec;
    enc.begin({16000, 1, 16});
    dec.begin({16000, 1, 16});
    (void)enc.encode(pcm_in, kSamples, coded, kBytes);
    (void)dec.decode(coded, kBytes, pcm_out, kSamples);

    long peak = 0;
    for (size_t i = kWarmup; i < kSamples; ++i) {
        const long a = pcm_out[i] >= 0 ? pcm_out[i] : -pcm_out[i];
        if (a > peak) peak = a;
    }
    Serial.print("INFO silence/peak-after-warmup="); Serial.println(peak);
    // Silence in -> a tiny amount of quantization noise out, but well
    // under any reasonable threshold.
    EXPECT_TRUE("silence/peak-small", peak < 500);
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("TEST start roundtrip");

    test_sine_1khz();
    test_silence();

    Serial.print("TEST done ");
    Serial.print(g_pass);
    Serial.print("/");
    Serial.println(g_total);
}

void loop()
{
    delay(1);
}
