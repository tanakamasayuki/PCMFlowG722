// PCMFlowG722 example: EspNowTransceiver
//
// Half-duplex HD-voice transceiver over ESP-NOW for two (or more)
// M5Stack Core2 boards. The same firmware acts as both sender and
// receiver:
//
//   while button A is held:
//       M5.Mic (16 kHz mono) -> G722Encoder -> ESP-NOW broadcast
//   always:
//       ESP-NOW recv -> G722Decoder -> PCMFlow -> M5.Speaker
//
// One 20 ms voice frame at 16 kHz produces 160 G.722 bytes (320 input
// samples / 2). This fits well under ESP-NOW's 250-byte payload limit,
// and is twice the audio bandwidth of the G.711 sibling example at the
// same packet budget.
//
// Flash this binary on two boards (no pairing needed — ESP-NOW broadcast
// reaches every peer in range on the same Wi-Fi channel). Hold A on one
// board to talk; the other plays it back through the speaker.

#include <M5Unified.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <PCMFlow.h>
#include <PCMFlowG722.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
static constexpr uint32_t kRate = 16000;                 // G.722 fixed wideband rate
static constexpr size_t kFrameSamples = 320;             // 20 ms @ 16 kHz
static constexpr size_t kFrameBytes = kFrameSamples / 2; // G.722: 1 byte / 2 samples
static constexpr uint8_t kWifiChannel = 1;               // both boards must agree

static const uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static G722Encoder g_enc;
static G722Decoder g_dec;
static PCMFlow g_audio;

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
    g_audio.pump();
    while (g_audio.availableFrames() >= kFrameSamples)
    {
        static int16_t buf[kFrameSamples];
        const size_t got = g_audio.readFrames(buf, kFrameSamples);
        if (got == 0)
            break;
        while (!M5.Speaker.playRaw(buf, got, kRate, /*stereo=*/false))
            delay(1);
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

    if (!g_enc.begin({kRate, 1, 16}) ||
        !g_dec.begin({kRate, 1, 16}))
    {
        Serial.println("codec begin failed");
        return;
    }

    g_audio.setOutputFormat({kRate, 1, 16});
    g_audio.setBufferFrames(2048);
    g_audio.setInputSource(g_dec);

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
