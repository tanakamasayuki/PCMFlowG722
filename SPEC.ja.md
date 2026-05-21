# PCMFlowG722 仕様

> English: [SPEC.md](SPEC.md)

## 1. スコープ

**PCMFlowG722** は [PCMFlow](https://github.com/tanakamasayuki/PCMFlow) 向けのオプション G.722 コーデックアドオン。**リアルタイム双方向の広帯域音声**(HD voice、VoIP / ESP-NOW トランシーバ / WebSocket / UDP 音声リンク)に焦点を絞る。G.722 は G.711 と同じ 64 kbps のワイヤレートで、広帯域音声(音声帯域 7 kHz、サンプリング 16 kHz)を運ぶ。コーデックフットプリントは中程度。

> **同ファミリーの姉妹ライブラリ:** [PCMFlowG711](https://github.com/tanakamasayuki/PCMFlowG711) — 狭帯域 64 kbps 音声、Flash 4 KB 以下。[PCMFlowOpus](https://github.com/tanakamasayuki/PCMFlowOpus) — 低ビットレート / 広帯域・全帯域音声。いずれも PCMFlow に `PCMSource` / `PCMSink` 経由で接続し、同じスケッチに共存できる。比較表は [README「PCMFlow コーデックファミリー」](README.ja.md#pcmflow-コーデックファミリー) 参照。

責務:

- 16-bit signed PCM サンプル(16 kHz mono)を 8-bit G.722 コードに**エンコード**(入力 2 サンプル → 1 バイト)
- 8-bit G.722 コードを 16-bit signed PCM サンプル(16 kHz mono)に**デコード**

v0.1.x で公開するのは ITU-T G.722 のデフォルト動作モードである **Mode 1 / 64 kbps**(低域 6 bit + 高域 2 bit)のみ。これは RTP payload type 9 で実際に使われているレートで、現実のデプロイはほぼすべてこれ。Mode 2 (56 kbps) / Mode 3 (48 kbps) は公開しない — §「将来対応」参照。

このコーデックは**ステートフル**(ADPCM の予測器 / 量子化器状態を呼び出し間で保持する必要がある)。エンコーダとデコーダはそれぞれ独立した状態を持つ。パケット境界そのものでリセットは必要ないが、長時間のパケット欠落の直後は適応フィルタの再収束まで一時的に音質が低下する。

このライブラリは PCMFlow パイプラインへ**インタフェースレベルで接続するだけ**。PCMFlow 本体(リングバッファ、フォーマット変換、ゲイン、出力デバイス連携)は再実装しない。

## 2. 非目標

- **WAV コンテナ(G.722 ペイロード)**(`WAVE_FORMAT_G722`、フォーマットタグ `0x028F` などの Microsoft 系)— 初版対象外。§「将来対応」参照。
- **G.722 Mode 2 / Mode 3**(56 / 48 kbps)— §「将来対応」参照。
- **PCM 側の 16 kHz 以外のサンプルレート** — G.722 は 16 kHz 広帯域固定の規格。I2S DAC を 44.1 / 48 kHz 等で駆動する際のリサンプリングは PCMFlow の責務。
- **ステレオ** — G.722 はモノラル。2 チャネル運用はインスタンスを 2 つ並べて行う。
- **音声ファイル再生 / 出力デバイス制御** — PCMFlow の責務。
- **`Stream` / ファイルシステム / ネットワークアダプタ** — PCMFlow の `ByteStream` / `ByteSink` の責務。
- **Jitter buffer / RTP パーサ / ネットワーク転送** — 呼び出し側の責任。PCMFlowG722 はコーデックであってスタックではない。
- **G.722 Appendix III / IV PLC** — §「将来対応」参照。
- **G.722.1 / G.722.2 (AMR-WB)** — 別コーデック、特許・ロイヤリティ事情も異なる。対象外。

## 3. 主目的ユースケース:ESP-NOW HD-voice トランシーバ

設計の動機となるユースケース。ESP-NOW は 1 パケット最大 250 byte ペイロード。G.722 はワイヤ上 8000 byte/s なので、20 ms = 160 byte で G.711 と同じパケットサイズ。同じパケット予算で**音声帯域は 2 倍**:

| フレーム長 | パケットあたり | 入力サンプル数 |
|------------|---------------|--------------|
| 10 ms      | 80 B  | 160 samples |
| 20 ms(RTP 既定)| 160 B | 320 samples |
| 30 ms      | 240 B | 480 samples |

参考に、生 16 kHz mono 16-bit PCM は 20 ms あたり 640 B なので G.722 で **4×** 圧縮。G.711 の倍の音声帯域を同じパケット予算で運べるのが特徴。エンドツーエンドのトランシーバ用サンプルは [examples/EspNowTransceiver/](examples/EspNowTransceiver/) に同梱(マイク 16 kHz → G.722 エンコード → ESP-NOW broadcast / ESP-NOW 受信 → G.722 デコード → I2S DAC)。

## 4. 公開 API

2 クラス。両方とも **N サンプル単位**で動作(N は呼び出し側が選ぶが、**偶数**である必要がある:G.722 は QMF で 2 サンプルを 1 組として処理するため)。PCM 2 サンプル ↔ G.722 1 バイト。

### 4.1 `G722Encoder` — `PCMSink` を実装

16-bit signed PCM(16 kHz mono)を受け、G.722 バイトを出力する。

```cpp
G722Encoder enc;
enc.begin({16000, 1, 16});

int16_t pcm20ms[320];   // 320 samples = 20 ms @ 16 kHz
uint8_t pkt[160];
const size_t bytes = enc.encode(pcm20ms, 320, pkt, sizeof(pkt));
// bytes == 160; pkt[0..bytes) を ESP-NOW / UDP / WebSocket で送信
```

エンコーダは**内部に適応予測器 / 量子化器の状態を持つ**ので、外部同期なしに複数タスクから共有してはいけない。

入力 `frameCount` は**偶数**である必要がある(G.722 は QMF で 2 サンプル 1 組で処理)。奇数の場合は 0 を返し `Error::FrameCountNotEven` をセットする。

### 4.2 `G722Decoder` — `PCMSource` を実装

G.722 バイトを受け、16-bit signed PCM(16 kHz mono)を出力する。

```cpp
G722Decoder dec;
dec.begin({16000, 1, 16});

int16_t pcm[320];
const size_t frames = dec.decode(pkt, 160, pcm, 320);
// frames == 320
```

エンコーダ同様、デコーダも**ステートフル**。v0.1 では **PLC は組み込まない**。パケットロス時に無音バイトを流す(デコーダは普通に処理し、数十 ms で予測器状態が回復する) / 直前サンプルを引き伸ばす / 別途生成した comfort-noise バイトを差し込むかは呼び出し側で決める。

`G722Encoder::format()` / `G722Decoder::format()` が示すのは PCM 側のフォーマット。バイト側は不透明。

### 4.3 PCMFlow パイプラインへの接続

`G722Decoder` は `PCMSource` を実装するので、`PCMFlow::setInputSource(dec)` で PCMFlow のリングバッファ / フォーマット変換 / 出力デバイス連携につながる。呼び出し側が `decode(packet, bytes, nullptr, 0)` で内部 FIFO にバイトを積み、PCMFlow が `pump()` / `readFrames()` で PCM を取り出す。

`G722Encoder` は `PCMSink` を実装。録音タスクから 16 kHz の 16-bit PCM を `writeFrames(...)` で押し込むと、内部でバイト列に変換して、指定 `PacketSink` コールバックに出す。

詳細シグネチャとエラー列挙は実装と同時に [src/](src/) のヘッダで確定する。

## 5. PCM 入出力フォーマット

- サンプルレート: **16000 Hz**(ITU-T G.722 広帯域固定レート)
- ビット深度: PCM 側 **16-bit signed**、コーデック側 **8-bit unsigned**
- チャネル数: **1 (mono)**

別レートでの出力(例: ESP32-S3 で I2S DAC を 44.1 kHz 駆動)が必要なら、PCMFlow のリサンプラに任せる。PCMFlowG722 は PCM 側 16 kHz でしか動作しない。

## 6. メモリ・フットプリント目標

G.722 はサブバンド ADPCM コーデック。QMF フィルタバンク + サブバンドごとの適応予測器 / 量子化器を持つ。フットプリントは方向ごとの状態(配列ベース)が支配的で、サンプル当たりの演算量は小さい。

| 項目 | 目標 |
|------|------|
| Flash(エンコーダ + デコーダ) | ≤ 12 KB |
| Flash(デコーダのみ、エンコーダはリンク時に破棄) | ≤ 8 KB |
| RAM、エンコーダ永続状態(インスタンスあたり) | ≤ 512 B |
| RAM、デコーダ永続状態(インスタンスあたり、直接 decode のみ) | ≤ 512 B |
| RAM、デコーダ永続状態(`setInputSource` 用の PCMSource キュー込) | ≤ 1 KB |
| 呼び出しごとのスクラッチ | ≤ 64 B(スタック) |

数値は `src/external/libg722/` に vendor する [sippy/libg722](https://github.com/sippy/libg722) を基準にした見込み。ブリッジ実装が入った後に実測し、見込みと 25 % 以上ズレた場合はここを書き直す。

## 7. リポジトリ構成

```
PCMFlowG722/
├─ README.md / README.ja.md
├─ SPEC.md   / SPEC.ja.md
├─ CHANGELOG.md
├─ LICENSE                       # MIT(本ライブラリ)
├─ library.properties            # Arduino IDE
├─ library.json                  # PlatformIO
├─ keywords.txt                  # Arduino IDE シンタックスハイライト
├─ src/
│  ├─ PCMFlowG722.h              # 集約ヘッダ
│  ├─ G722Encoder.h/.cpp         # PCMSink、 PCM 16 kHz -> G.722 バイト
│  ├─ G722Decoder.h/.cpp         # PCMSource、G.722 バイト -> PCM 16 kHz
│  ├─ pcmflowg722_version.h      # tools/bump_version.py が生成
│  └─ external/
│     ├─ LICENSE_libg722.md      # 上流ライセンス + クレジット
│     ├─ UPSTREAM.lock           # 上流コミット pin(情報用)
│     └─ libg722/                # sippy/libg722 の subset を verbatim 取り込み
├─ examples/
│  └─ EspNowTransceiver/         # 主目的ユースケースのサンプル(16 kHz HD)
├─ tests/
│  ├─ README.md / README.ja.md
│  ├─ conftest.py
│  ├─ pyproject.toml
│  ├─ smoke/
│  ├─ g722_encoder/
│  ├─ g722_decoder/
│  ├─ roundtrip/                 # encode → decode で元波形と比較
│  ├─ external_source/           # PCMFlow::setInputSource 経路の結合
│  └─ tools/gen_test_audio.py
├─ doc/
│  └─ sibling_library_brief.md
├─ tools/
│  ├─ bump_version.py            # 親 PCMFlow のものをそのまま流用
│  └─ sync_libg722.py            # メンテナ用:src/external/libg722/ の更新ツール
└─ .github/
   └─ workflows/
      └─ release.yml             # 親 PCMFlow のものをそのまま流用
```

## 8. Vendor している上流

PCMFlowG722 は G.722 実装を**上流から vendor する**。G.711 と違って G.722 のクリーンルーム実装は規模が大きく(サブバンド ADPCM + 適応量子化器 + 適応予測器、ITU テストベクタにビット精確に合わせる必要があり ~1000 行)、独自実装は労力が割に合わない。

**上流:** [sippy/libg722](https://github.com/sippy/libg722)。元実装は Milton Anderson (Bellcore)、改修が Chengxiang Lu と Alex Hauptmann (CMU Computer Science / Speech Group, 1993)、大幅な書き直しが Steve Underwood (2005)、現メンテナが Sippy Software。

**Vendor 構成:**

```
src/external/
├─ LICENSE_libg722.md            # クレジット + 上流ライセンス全文
├─ UPSTREAM.lock                 # pin したコミット SHA + チェック日
└─ libg722/
   ├─ g722_encoder.c / .h
   ├─ g722_decoder.c / .h
   ├─ g722_codec.h
   ├─ g722_common.h
   └─ g722_private.h
```

上流の Python バインディング / CMake ビルド / テストハーネスは vendor **しない**(Arduino ライブラリには不要、コーデック本体だけで自己完結する)。

### Vendor 部分のライセンス

上流通知(`src/external/LICENSE_libg722.md` に verbatim 配置)より要約:

> G.722 モジュールは大部分がパブリックドメイン。
> Copyright (C) 2005 Steve Underwood — "I hereby place my contributions in the public domain, for the benefit of all mankind."
> Copyright (c) 1993 Computer Science, Speech Group, Carnegie Mellon University, Chengxiang Lu and Alex Hauptmann.
> ライブラリのテストコードは BSD 2-clause(本リポでは vendor しない)。

これは MIT 互換:コーデック本体はパブリックドメイン、CMU 寄与分も permissive。PCMFlowG722 自体のコードは **MIT** で配布し、vendor 部分は `src/external/LICENSE_libg722.md` で明示する。ユーザー側で標準 MIT 表記以外の追加 attribution は不要(上流通知ファイルは license hygiene の監査のため `src/external/` に同梱)。

参照規格(アルゴリズム仕様、コード流用ではない):

- ITU-T Recommendation G.722 (09/2012 改訂), "7 kHz audio-coding within 64 kbit/s"

## 9. リリースフロー

PCMFlowG722 は上流追随の自動化レベルとして **L0 — 自動追随なし** を採用する。sippy/libg722 の G.722 コアは長年安定しており(Steve Underwood の実質的な作業は 2005 年に完了)、週次の tag 監視を入れてもほぼ発火せず CI ノイズになる。代わりに、vendor したスナップショットの更新はメンテナが判断したときに手動で行う。

`tools/sync_libg722.py` は **メンテナ用ツール**として用意する(CI からは呼ばない):§8 の subset を冪等に `src/external/libg722/` へ取り込み、`UPSTREAM.lock` を更新する。`upstream-check.yml` は持たない。`UPSTREAM.lock` は現在 vendor しているスナップショットの provenance(情報用)として残すのみ。

### 最終リリースのタグ付け

バージョン bump と Arduino Library Manager 用 tag は `tools/bump_version.py`(親 PCMFlow から流用)で生成し、[`.github/workflows/release.yml`](.github/workflows/release.yml)(これも親から移植)が駆動する。`version=` と CHANGELOG の `Unreleased` 節は同時に動く、親と同じ流儀。メンテナが `workflow_dispatch` で起動する。

## 10. テスト

親 PCMFlow と同じ規約:

- pytest-embedded + Arduino CLI バックエンド
- 2 プロファイル:`lang-ship:host`(ロジック・大サイズ fixture)と `esp32:esp32:esp32`(実機検証)
- 機能ごとのディレクトリに `<feature>.ino` / `sketch.yaml` / `test_<feature>.py` / `input/` fixture
- アサーション形式は `EXPECT_TRUE` / `EXPECT_EQ` / `EXPECT_NEAR` マクロ、Serial 経由の `TEST done N/M` プロトコル

G.722 固有のテスト設計:

| テストディレクトリ | 検査対象 | 戦略 |
|---|---|---|
| `smoke/` | 選択プロファイルでライブラリがビルドでき、テストハーネスが動くか | host ビルド、バージョン表示、エンコーダ/デコーダの生成 |
| `g722_encoder/` | `G722Encoder` の API 契約 | encode(N) → N/2 バイト、奇数 N は FrameCountNotEven で拒否、出力短すぎは BufferTooSmall、フォーマット異常は InvalidFormat、reset() で予測器状態が戻る |
| `g722_decoder/` | `G722Decoder` の API 契約 | decode(N) → 2N サンプル、PCM 短すぎは BufferTooSmall、queued path(`decode(_, _, nullptr, 0)` + `readFrames`)、reset() で予測器状態が戻る |
| `roundtrip/` | encode → decode で信号特性が保存されるか | 1 kHz サインと無音、エネルギー保存(±15%)、周波数保存(ゼロクロス数)、ピーク振幅保存、無音入力 → ほぼ無音出力 |
| `external_source/` | `G722Decoder` が `PCMFlow::setInputSource()` 経由で機能するか | G.711 兄弟と同じハーネス、サンプル数 / 非自明ピーク / モノラル→ステレオ展開を検証 |

G.722 は QMF + ADPCM のフィルタチェーンで固定のグループ遅延(~3 ms / 16 kHz で ~48 サンプル)を持つので、roundtrip テストはサンプル単位の等価性は**主張しない**。代わりに正常動作するコーデックなら成り立つはずの統計的性質(エネルギー / 周波数特性 / ピーク振幅)を検証する。ITU リファレンスベクタに対するサンプル単位のビット精確比較は将来対応 — §「将来対応」参照。

## 11. バージョニング

SemVer (`major.minor.patch`) を `library.properties`、`library.json`、`src/pcmflowg722_version.h` で同期管理。PCMFlow のバージョンとは**独立**。上流 sippy/libg722 のバージョンとも**独立**(上流側は `src/external/UPSTREAM.lock` で管理)。

## 12. 将来対応 {#将来対応}

忘れないようここに集約。v0.1.x には含めない:

- **WAV コンテナ(G.722 ペイロード)**(`WAVE_FORMAT_G722`)。デスクトップ相互運用(ffmpeg / VoIP 録音再生)用。親 PCMFlow `WAVDecoder` が非 PCM ペイロードのフックを持つ形で実装するか、本リポに `G722WAVDecoder` として実装する。具体的な要望が出てから着手。
- **G.722 Mode 2 / Mode 3**(56 / 48 kbps)。libg722 自体は 3 モードすべて実装済みなので、解禁は実質 API の話だけ:`begin()` に `G722Mode` 引数を増やし `setMode()` を生やす。実利用要望が出てから着手 — RTP 上の Mode 2/3 シグナリングは現実にはほぼ使われていない。
- **ITU G.722 リファレンステストベクタ。** vendor している libg722 が ITU に対してビット精確であるため、v0.1 のテストは API 契約と信号保存性の検証に絞り、再度のビット精確比較は行わない。リグレッションや第三者移植の必要が生じたら ITU-T G.191 STL のベクタを `tests/g722_*/input/` に同梱する形で追加可能。
- **G.722 Appendix III / IV PLC** — ITU-T 規定の高複雑度 / 低複雑度パケットロス補償。jitter のある RTP 向けに有用。Appendix IV のほうが実装例が多い。いずれも ~2 KB のコードと数百バイトの状態を要する。v0.2 以降で着手予定。
- **上流追随自動化**(L1 週次 tag チェック以上)。現状は L0 が妥当(sippy/libg722 はほぼ休眠状態)。上流活動が再開したら `.github/workflows/upstream-check.yml` を追加して再検討する。
- **`G722_PACKED` / `G722_SAMPLE_RATE_8000` 上流オプション** — libg722 はパックドビットモードと狭帯域モードを提供するが、いずれも RTP payload type 9 や標準 G.722 ハードウェアのワイヤ規約と一致しない。v0.1 ではデフォルトの非パック・16 kHz モードのみ公開。具体的な要望が出ればパックドモードを後追いで足す。

## 13. ライセンス

PCMFlowG722 自体のコード: **MIT** ([LICENSE](LICENSE))。

Vendor している上流(`src/external/libg722/`): パブリックドメイン(Steve Underwood の寄与)+ CMU permissive 通知 — 上流ライセンス全文は `src/external/LICENSE_libg722.md` に verbatim 同梱。ユーザー側で標準 MIT 表記以外の追加 attribution は不要。上流通知ファイルは license hygiene 監査のために `src/external/` に置く。
