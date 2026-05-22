# PCMFlowG722

> English: [README.md](README.md)

[PCMFlow](https://github.com/tanakamasayuki/PCMFlow) 用のオプション **G.722** 広帯域(HD voice)コーデックアドオン。**リアルタイム双方向音声**(VoIP / ESP-NOW トランシーバ / WebSocket / UDP 音声リンク)向け。

G.722 は G.711 と同じ 64 kbps のワイヤ予算で 16 kHz サンプリングの 7 kHz 音声帯域を運ぶ。**同じパケットサイズで音声帯域は 2 倍**。コーデック本体はパブリックドメインの [sippy/libg722](https://github.com/sippy/libg722)(Steve Underwood / CMU)を vendor し、PCMFlow の `PCMSource` / `PCMSink` インタフェースで包んでいる。

PCMFlowG722 自体のコードは **MIT**。vendor している上流はパブリックドメイン(Steve Underwood 寄与分)+ CMU permissive 通知の組み合わせで、いずれも MIT 互換 — 詳細は [`src/external/LICENSE_libg722.md`](src/external/LICENSE_libg722.md)。

詳細は [SPEC.ja.md](SPEC.ja.md) を参照。

---

## 構成

| クラス | 方向 | 担い手 | インタフェース |
|--------|------|--------|----------------|
| `G722Encoder` | PCM(16 kHz)→ G.722 バイト | 生バイト(RTP / ESP-NOW / UDP / WebSocket) | `PCMSink` |
| `G722Decoder` | G.722 バイト → PCM(16 kHz) | 生バイト | `PCMSource` |

PCM 2 サンプル ↔ G.722 1 バイト。v0.1 では **Mode 1 / 64 kbps** のみ公開(RTP payload type 9 で使われる事実上の標準レート)。Mode 2 / 3 は将来対応 — [SPEC §「将来対応」](SPEC.ja.md#将来対応) 参照。

WAV コンテナ(`WAVE_FORMAT_G722`)と G.722 Appendix III/IV PLC は v0.1 では対象外。

---

## PCMFlow コーデックファミリー {#pcmflow-コーデックファミリー}

PCMFlowG722 は PCMFlow のオプションコーデックアドオン群の 1 つ。帯域 / フットプリント / 音質のバランスで使い分ける:

| | [PCMFlowG711](https://github.com/tanakamasayuki/PCMFlowG711) | **PCMFlowG722**(本ライブラリ) | [PCMFlowOpus](https://github.com/tanakamasayuki/PCMFlowOpus) |
|---|---|---|---|
| 帯域 | 狭帯域(8 kHz / ≤ 3.4 kHz) | **広帯域(16 kHz / ≤ 7 kHz)** | 狭帯域/広帯域/全帯域(8〜48 kHz) |
| ビットレート(音声) | 64 kbps 固定 | **64 kbps 固定(Mode 1)** | 16〜32 kbps 程度 |
| 生 16-bit PCM 比 | 2× | 4× | 10〜15× |
| コーデック Flash | < 4 KB | ~10 KB | ~150〜180 KB |
| コーデック CPU | ほぼゼロ | 低 | M0/M3 クラスでは無視できない |
| 特許 / ライセンス | なし(1972 規格、満了済) | なし(1988 規格、満了済)、コア部分はパブリックドメイン | royalty-free 特許許諾、BSD-3-Clause ソース |
| 音質 | 電話品質(toll-grade) | **HD voice(広帯域電話品質)** | 広帯域 / 全帯域、圧倒的に良い |

G.722 を選ぶケース:
- 電波 / ネットワーク帯域に 64 kbps の余裕がある
- 狭帯域電話ではなく **HD-voice 品質**が欲しい — 明瞭度と「臨場感」の両方が G.711 より大きく向上する
- 中程度のコーデックフットプリント(~10 KB Flash + 方向ごとに ~512 B RAM)を許容できる
- 規格化されたワイヤフォーマットと特許フリーなライセンスが必要(G.722 は ITU-T 1988 勧告)

狭帯域電話で十分、かつコーデックフットプリント最小(Flash 4 KB 未満)を最優先するなら [G.711(PCMFlowG711)](https://github.com/tanakamasayuki/PCMFlowG711) を選ぶ。

帯域節約 / 全帯域音質が必要なら [Opus(PCMFlowOpus)](https://github.com/tanakamasayuki/PCMFlowOpus) を選ぶ。

---

## 主目的ユースケース — ESP-NOW HD-voice トランシーバ

ESP-NOW は 1 パケット最大 250 byte ペイロード。20 ms G.722 音声(16 kHz)= ちょうど 160 byte、G.711 と同じパケットサイズで音声帯域は 2 倍。生 16 kHz mono 16-bit PCM は 20 ms あたり 640 byte なので G.722 で **4×** 圧縮。

```cpp
#include <PCMFlow.h>
#include <PCMFlowG722.h>
#include <esp_now.h>

G722Encoder enc;
G722Decoder dec;
PCMFlow audio;

void setup() {
    audio.setOutputFormat({16000, 1, 16});
    audio.setInputSource(dec);

    dec.begin({16000, 1, 16});
    enc.begin({16000, 1, 16});

    esp_now_init();
    esp_now_register_recv_cb(onEspNowRecv);
}

// ESP-NOW 受信コールバック: バイト列をデコーダへ
void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
    dec.decode(data, len, /*pcm_out=*/nullptr, /*max_frames=*/0);
    // PCMFlow の pump() が PCM を取り出して I2S/DAC へ流す
}
```

エンドツーエンドのトランシーバスケッチ(マイク↔ESP-NOW↔DAC)は [examples/EspNowTransceiver/](examples/EspNowTransceiver/) に同梱。

---

## 依存

- **[PCMFlow](https://github.com/tanakamasayuki/PCMFlow)** — `PCMSource` / `PCMSink` / `ByteStream` / `ByteSink` / 再生パイプラインを提供。`library.properties` の `depends=PCMFlow` で宣言。

G.722 コーデック本体は [src/external/libg722/](src/external/libg722/) に([sippy/libg722](https://github.com/sippy/libg722) の verbatim subset として)同梱しているので、追加の外部ライブラリ導入は不要。

---

## 対応プラットフォーム

親 PCMFlow と同じ:`library.properties` は `architectures=*`、ただし実用ターゲットは malloc を持つ 32-bit MCU、SRAM 中程度以上、Flash ~10 KB 以上空いていること。

実用例: ESP32 / ESP32-S3 / ESP32-C3 / ESP32-C6 / ESP32-P4、RP2040 / RP2350、Teensy 4.x、STM32 F4 以上、nRF52。AVR(Uno / Mega / Nano)は **実用対象外** — G.722 の ADPCM 状態とワークバッファだけで AVR の ~2 KB SRAM を食い潰すため。

暫定フットプリント(エンコーダ + デコーダ): **Flash ≤ 12 KB / RAM 方向あたり ~512 B**。詳細目標は [SPEC.ja.md §6](SPEC.ja.md)。

---

## ライセンス

PCMFlowG722 自体のコード: **MIT** ([LICENSE](LICENSE))。

[src/external/libg722/](src/external/libg722/) の G.722 コーデック本体は [sippy/libg722](https://github.com/sippy/libg722) の verbatim subset。ライセンス内訳:

- コーデック実装本体: **パブリックドメイン**(Steve Underwood, 2005)
- 1993 年の CMU 寄与分: 出典明示の依頼付き permissive 通知
- Sippy Software 保守分: 2-clause BSD

いずれも MIT 互換。上流通知の全文は [`src/external/LICENSE_libg722.md`](src/external/LICENSE_libg722.md) に verbatim 同梱。ユーザー側で標準 MIT 表記以外の追加 attribution は不要(上流通知ファイルは license hygiene 監査のために `src/external/` に置いている)。

---

## テスト

```sh
cd tests
uv run --env-file .env pytest                  # host (デフォルト)
uv run --env-file .env pytest --profile=esp32  # 実機 ESP32
```

詳細は [tests/README.ja.md](tests/README.ja.md)。親 PCMFlow と同じ規約(pytest-embedded + Arduino CLI、`lang-ship:host` + `esp32:esp32:esp32` プロファイル)。
