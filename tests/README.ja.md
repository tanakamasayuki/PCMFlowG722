# Tests

> English: [README.md](README.md)

PCMFlowG722 の自動テストスイート。親 [PCMFlow テストスイート](https://github.com/tanakamasayuki/PCMFlow/tree/main/tests) と同じ規約:

- [pytest-embedded](https://docs.espressif.com/projects/pytest-embedded/en/latest/) + Arduino CLI バックエンド
- 2 プロファイル:`lang-ship:host`(ロジック検証・高速 CI)と `esp32:esp32:esp32`(実機検証・フットプリント計測)
- 機能ごとのサブディレクトリに `<feature>.ino` / `sketch.yaml` / `test_<feature>.py`
- アサーションは `EXPECT_TRUE` / `EXPECT_EQ` マクロ + Serial 経由の `TEST done N/M` プロトコル

## ディレクトリ構成

- `smoke/` — ビルドサニティチェック。集約ヘッダのインクルード、エンコーダ + デコーダ生成、バージョン表示。これが通れば vendor 配下の libg722 ソースが Arduino ライブラリローダに正しく拾われている。
- `g722_encoder/` — `G722Encoder` の API 契約テスト(フレーム数の偶数性、バッファサイズ、フォーマット検証、`reset()` 挙動)
- `g722_decoder/` — `G722Decoder` の API 契約テスト(直接 path、`PCMSource` 用 queued path、`reset()` 挙動)
- `roundtrip/` — サインと無音を入力した end-to-end encode → decode。コーデックの ~3 ms 群遅延を考慮した統計的性質(エネルギー / 周波数 / ピーク振幅)を検証
- `external_source/` — `G722Decoder` を `PCMFlow::setInputSource()` 経由で繋ぐ結合テスト

## G.722 固有のテスト設計

G.722 は**ステートフル**で、QMF + ADPCM フィルタチェーンに起因する固定の群遅延(~3 ms / 16 kHz で ~48 サンプル)を持つ。したがってロッシー roundtrip のサンプル単位等価性は意味のある指標**ではない**。代わりに:

| テストディレクトリ | 検証内容 | 許容差 |
|---|---|---|
| `smoke/` | ライブラリ + libg722 vendor がコンパイル / リンクできるか | ビルドのみ |
| `g722_encoder/` | encode(N) = N/2 バイト、N が偶数強制、reset() で予測器状態が戻る | API 契約 exact |
| `g722_decoder/` | decode(N) = 2N サンプル、queued path、reset() で予測器状態が戻る | API 契約 exact |
| `roundtrip/` | エネルギー保存(±15%)、周波数保存(ゼロクロス)、ピーク振幅保存、無音 → ほぼ無音 | 統計的 |
| `external_source/` | `PCMFlow::setInputSource()` 経由のサンプル数が入力と一致、非自明ピーク、モノラル → ステレオ展開 | 数は exact、信号は非自明 |

Vendor している libg722 は構造的に ITU リファレンスに対してビット精確(上流 README が "passes the ITU tests" と明記)なので、v0.1 のテストは ITU ベクタ再検証ではなく**こちらのブリッジコードでリグレッションしうる箇所**に集中する。ITU ベクタ比較は将来対応 — [SPEC §「将来対応」](../SPEC.ja.md#将来対応) 参照。

## 実行

```sh
# host (デフォルト)
uv run --env-file .env pytest

# 実機 ESP32
uv run --env-file .env pytest --profile=esp32

# 単一テスト
uv run --env-file .env pytest roundtrip/
```

前提(uv、arduino-cli、各ボードコア)については [親 PCMFlow tests README](https://github.com/tanakamasayuki/PCMFlow/tree/main/tests#prerequisites) を参照。
