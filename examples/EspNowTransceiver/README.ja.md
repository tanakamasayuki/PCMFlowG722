# EspNowTransceiver

> English: [README.md](README.md)

ESP-NOWの相手と符号化した音声を交換するサンプルです。

## 準備と実行

1. [サンプル入門ガイド](../../docs/GUIDE_EXAMPLES.ja.md)で導入条件と必要な機器を確認します。対応するボード・外部ライブラリはルートREADMEを参照します。
2. [EspNowTransceiver.ino](EspNowTransceiver.ino)を開き、実際のボードとポートを選びます。次のincludeで使う依存機能も準備してください。

   `<M5Unified.h>`, `<WiFi.h>`, `<esp_now.h>`, `<esp_wifi.h>`, `<PCMFlow.h>`, `<PCMFlowDeviceM5.h>`, `<PCMFlowG722.h>`

3. 書き込んだらシリアルモニタを115200 baudへ合わせます。起動ログとソースの操作条件を確認します。

## 動作の流れ

ESP-NOWの相手と符号化した音声を交換する例です。詳細なAPIとデータの扱いは次の項目で確認します。

以下はソース内で最初に現れるAPIの順です。コールバック登録の位置は、実際の呼び出し順序とは異なります。

1. `g_dec.decode(...)` — 符号化された入力の長さと形式を確認してから復号結果を使います。
2. `g_enc.encode(...)` — 十分な領域へ符号化し、結果を確認してから出力を使います。
3. `g_audio.pump()` — 復号／変換を進め、得られたPCM フレームだけを取り出します。
4. `g_audio.readFrames(...)` — 実際に返ったフレーム数を使います。Byte数にはチャンネル数とサンプル幅も関係します。
5. `g_player.writeFrames(...)` — 受理量を確認し、残りを保持します。フレーム数とByte数は異なります。
6. `g_enc.begin(...)` — 前提となる機器を準備してから初期化し、結果を返すAPIでは成否を確認します。
7. `g_audio.setOutputFormat(...)` — 入力形式だけでなく、次の段階が受け取れる形式を選びます。
8. `g_audio.setBufferFrames(...)` — フレーム数で容量を決め、メモリと増える待ち時間の両方を考慮します。
9. `g_audio.setInputSource(...)` — パイプラインが読む間ずっと有効なSourceを接続します。

変更する前に確認する定数: `kRate`, `kFrameSamples`, `kFrameBytes`, `kMaxPlayFrames`, `kWifiChannel`。GPIOやチャンネル、容量などは用途に合わせます。

## APIの使い方と注意点

例はM5Unifiedのマイク／スピーカー、PCMFlow、PCMFlowDeviceを別々の段階として使います。連続Packet間でEncoder／Decoderの状態を保持します。広帯域PCMの周波数とRTP Clockの規則は別の問題です。

 Codecのbegin／encode／decodeを確認し、PCMのframe数と圧縮後のbyte数を区別します。送受信には同じESP-NOW チャンネルの2台を使い、ソースにある相手／Broadcastの設定を合わせます。Push-to-talkの例では指定ボタンを押して送信します。

録音・符号化・無線配送・復号・バッファ再生は別々に失敗し得るので、送信成功だけで音が出ると判断しません。Wi-Fiの受信コールバックを短くし、後の処理用にPacketをコピー／キューへ渡します。Loopbackは1台でCodecを確認できますが、無線の配送は検証しません。

## 操作と出力の見方

この例が確認するボタン状態: `BtnA.isPressed()`。押下・解除・保持中を区別します。

## 確認と問題の切り分け

ESP-NOWの相手と符号化した音声を交換することを出力から確認します。まず例のままの入力で試し、その後に設定やデータ源を1つずつ変えます。

| 症状 | 最初に確認すること |
|---|---|
| 出力がない | ボード／依存ライブラリ、初期化結果、表示先とシリアル速度 |
| 開始するが途中で止まる | 待機条件、バッファの空き、相手や入力の準備、継続呼び出し |
| 値が不自然・不安定 | 長さと単位、形式、寿命、共有状態、部分的な処理結果 |

共通の考え方は[入門ガイド](../../docs/GUIDE_EXAMPLES.ja.md)、別の例は[サンプル一覧](../README.ja.md)へ進んでください。
