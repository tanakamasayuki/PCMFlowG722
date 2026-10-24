// en: Exchange encoded voice between ESP-NOW peers
// ja: ESP-NOWの相手と符号化した音声を交換する
#include <M5Unified.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <PCMFlow.h>
#include <PCMFlowDeviceM5.h>
#include <PCMFlowG722.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
static constexpr uint32_t kRate = 16000;                 // G.722 fixed wideband rate
static constexpr size_t kFrameSamples = 320;             // 20 ms @ 16 kHz
static constexpr size_t kFrameBytes = kFrameSamples / 2; // G.722: 1 byte / 2 samples
static constexpr size_t kMaxPlayFrames = (kRate * 80u) / 1000u;
static constexpr uint8_t kWifiChannel = 1;               // both boards must agree
using Player = M5SpeakerBufferedPlayer<kMaxPlayFrames>;

static const uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static G722Encoder g_enc;
static G722Decoder g_dec;
static PCMFlow g_audio;
static Player g_player;

// ---------------------------------------------------------------------------
// ESP-NOW: receive callback
// ---------------------------------------------------------------------------
static void onEspNowRecv(const esp_now_recv_info_t * /*info*/,
                         const uint8_t *data,
                         int len)
{
    if (len <= 0 || data == nullptr)
        return;
    // Enqueue bytes for PCMFlow's pump() to pull via G722Decoder::readFrames().
    g_dec.decode(data, static_cast<size_t>(len), nullptr, 0);
}

static bool espnow_setup()
{
    WiFi.mode(WIFI_STA);
    if (esp_wifi_set_channel(kWifiChannel, WIFI_SECOND_CHAN_NONE) != ESP_OK)
    {
        Serial.println("esp_wifi_set_channel failed");
        return false;
    }
    if (esp_now_init() != ESP_OK)
    {
        Serial.println("esp_now_init failed");
        return false;
    }

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, kBroadcastMac, 6);
    peer.channel = kWifiChannel;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK)
    {
        Serial.println("esp_now_add_peer(broadcast) failed");
        return false;
    }

    esp_now_register_recv_cb(onEspNowRecv);
    return true;
}

// ---------------------------------------------------------------------------
// Mic capture: read one 20 ms frame, encode, broadcast.
// ---------------------------------------------------------------------------
static void capture_and_send_one_frame()
{
    static int16_t pcm[kFrameSamples];
    if (!M5.Mic.record(pcm, kFrameSamples, kRate, /*stereo=*/false))
        return;
    while (M5.Mic.isRecording())
        delay(1);

    uint8_t packet[kFrameBytes];
    // en: Encode into sufficiently large storage and validate the returned result before consuming output.
    // ja: 十分な領域へ符号化し、結果を確認してから出力を使います。
    const size_t n = g_enc.encode(pcm, kFrameSamples, packet, sizeof(packet));
    if (n == 0)
        return;

    esp_now_send(kBroadcastMac, packet, n);
}

// ---------------------------------------------------------------------------
// Playback: drain PCMFlow output into the speaker queue.
// ---------------------------------------------------------------------------
static void play_pending_audio()
{
    // en: Advance decoding/conversion, then consume only the available returned PCM frames.
    // ja: 復号／変換を進め、得られたPCM フレームだけを取り出します。
    g_audio.pump();
    while (g_audio.availableFrames() >= kFrameSamples)
    {
        static int16_t buf[kFrameSamples];
        // en: Use the actual returned frame count; bytes also depend on channel count and sample width.
        // ja: 実際に返ったフレーム数を使います。Byte数にはチャンネル数とサンプル幅も関係します。
        const size_t got = g_audio.readFrames(buf, kFrameSamples);
        if (got == 0)
            break;
        // en: Check the number accepted and retain any unconsumed tail; a frame is not a byte.
        // ja: 受理量を確認し、残りを保持します。フレーム数とByte数は異なります。
        g_player.writeFrames(buf, got);
    }
}

// ---------------------------------------------------------------------------
// setup / loop
// ---------------------------------------------------------------------------
void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Mic.begin();
    M5.Speaker.begin();
    M5.Speaker.setVolume(160);
    Serial.begin(115200);

    M5.Display.setTextSize(2);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setCursor(4, 4);
    M5.Display.print("PCMFlowG722");
    M5.Display.setCursor(4, 28);
    M5.Display.print("hold A: talk");

    // en: Initialize this component after its prerequisites; inspect the returned status where provided.
    // ja: 前提となる機器を準備してから初期化し、結果を返すAPIでは成否を確認します。
    if (!g_enc.begin({kRate, 1, 16}) ||
        !g_dec.begin({kRate, 1, 16}))
    {
        Serial.println("codec begin failed");
        return;
    }

    g_audio.setOutputFormat({kRate, 1, 16});
    g_audio.setBufferFrames(2048);
    g_audio.setInputSource(g_dec);

    if (!g_player.begin({kRate, 1, 16}, Player::stableProfile()))
    {
        Serial.println("player begin failed");
        return;
    }

    if (!espnow_setup())
    {
        M5.Display.setCursor(4, 60);
        M5.Display.print("ESP-NOW failed");
        return;
    }

    M5.Display.setCursor(4, 60);
    M5.Display.print("ch=");
    M5.Display.print(kWifiChannel);
}

void loop()
{
    M5.update();

    // TX: while A is held, push a 20 ms voice packet roughly every 20 ms.
    if (M5.BtnA.isPressed())
    {
        capture_and_send_one_frame();
    }

    // RX: always pull whatever has arrived through PCMFlow into the speaker.
    play_pending_audio();
}
