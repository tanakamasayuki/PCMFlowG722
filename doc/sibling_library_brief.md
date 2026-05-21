# 兄弟ライブラリ作成 — 横展開作業メモ

> このメモは、既存の PCMFlow 系コーデックライブラリを雛形として、
> 別コーデック対応の兄弟ライブラリ(`PCMFlow<Codec>`)を新規に立ち上げる
> ときの引き継ぎ資料です。
>
> 新リポを起こすとそれ以前の暗黙文脈は持ち越されないので、
> 「次に同じことをやる人(または自分)が読めば再現できる」状態を
> ファイルとして残しておくのが目的。同じ内容のメモを全ての兄弟ライブラリ
> に同梱して構いません(同期させても、各リポで微修正してもよい)。

---

## 1. PCMFlow ファミリーの考え方

```
PCMFlow (親, 必須)
   ├─ PCMSource / PCMSink / ByteStream / ByteSink を提供
   ├─ リングバッファ・フォーマット変換・リサンプラ・ゲイン
   └─ 内蔵デコーダ: WAV / MP3 / FLAC

PCMFlow<CodecA> (子, optional)
PCMFlow<CodecB> (子, optional)
PCMFlow<CodecC> (子, optional)
...
```

**設計原則**:

- 子は必ず `depends=PCMFlow`(`library.properties`)で親に依存する。親の
  抽象(`PCMSource` 等)経由でしか結線しない。親の内部は再実装しない。
- 子は **「コーデック + その vendor 管理」だけ**を責務とする。再生
  デバイス・ファイル I/O・ネットワークは子に持ち込まない(親 or アプリ側)。
- Arduino ライブラリ 1 リポ = 1 ファミリーメンバー。mono-repo にはしない
  (Arduino Library Manager / PlatformIO Registry の流通単位がリポなので)。

---

## 2. 子リポで揃える規約

兄弟ライブラリのどれか 1 つを開けば実例として全部書いてあります。要点だけ抜粋:

### ファイル構成(必須)

```
PCMFlow<Codec>/
├─ README.md / README.ja.md          bilingual、互いに先頭でリンク
├─ SPEC.md   / SPEC.ja.md
├─ CHANGELOG.md                      (EN)/(JA) を 1 ファイルに混在
├─ LICENSE                           MIT(本体)
├─ library.properties                depends=PCMFlow 必須
├─ library.json
├─ keywords.txt
├─ .gitignore
├─ src/
│  ├─ PCMFlow<Codec>.h               umbrella header
│  ├─ <Codec>Encoder.h/.cpp          (録音/送信を扱うなら)
│  ├─ <Codec>Decoder.h/.cpp          (再生/受信を扱うなら)
│  ├─ pcmflow<codec>_version.h       bump_version.py が生成
│  └─ external/                      (vendor がある場合のみ)
│     ├─ LICENSE_<upstream>.md
│     ├─ UPSTREAM.lock
│     └─ <upstream>/
├─ examples/<HeadlineUseCase>/
├─ tests/                            親 PCMFlow と同じ規約
│  ├─ README.md / README.ja.md
│  ├─ conftest.py
│  ├─ pyproject.toml
│  ├─ .gitignore
│  ├─ smoke/
│  └─ <feature>/{<feature>.ino, sketch.yaml, test_<feature>.py, input/}
├─ doc/
│  └─ sibling_library_brief.md       このメモ(任意で同梱)
├─ tools/
│  ├─ bump_version.py                親 PCMFlow からそのままコピー
│  └─ sync_<upstream>.py             vendor がある場合のみ
└─ .github/workflows/
   ├─ release.yml                    親 PCMFlow からそのままコピー(汎用)
   └─ upstream-check.yml             vendor + L1 を採るときだけ
```

### 流用するもの(verbatim コピー OK)

| ファイル | コピー元 | 理由 |
|---|---|---|
| `tools/bump_version.py` | 親 PCMFlow | ライブラリ名は `library.properties` から動的取得するので無改変で動く |
| `.github/workflows/release.yml` | 親 PCMFlow | `${{ github.event.repository.name }}` で動くので無改変 |
| `tests/conftest.py` | 親 PCMFlow | `output/` ディレクトリの掃除だけ、汎用 |
| `tests/pyproject.toml` | 親 PCMFlow | パッケージ名だけ差し替え |
| `tests/<feature>/sketch.yaml` | 既存の任意の兄弟ライブラリ | host + esp32 デュアルプロファイル、`depends=PCMFlow` 入りの定型 |

### 命名規則

- リポ名: `PCMFlow<Codec>`(CamelCase)
- umbrella ヘッダ: `PCMFlow<Codec>.h`
- バージョンヘッダ: `pcmflow<codec>_version.h`(lowercase + `_version`)
- マクロ prefix: `PCMFLOW<CODEC>_VERSION_*`
- クラス名: `<Codec>Encoder` / `<Codec>Decoder`

`bump_version.py` がライブラリ名を sanitize してこれらを自動生成するので、
正しい `library.properties` の `name=` を書けば一致します。

### テスト規約(親と完全に同じ)

- pytest-embedded + Arduino CLI、`lang-ship:host` + `esp32:esp32:esp32` の 2 プロファイル
- `EXPECT_TRUE` / `EXPECT_EQ` / `EXPECT_NEAR` マクロ
- Serial 経由の `TEST start` … `TEST done N/M` プロトコル
- ロッシーコーデックは ±許容差、ロスレスは near-exact
- ground-truth fixture は ffmpeg または当該コーデックの標準実装で生成

---

## 3. コーデック追加時の判断ポイント

新コーデックを設計する前に、最低限これを決める。

### 3-1. 用途

- [ ] **デコード only** — ファイル再生が主。`<Codec>Decoder` のみ。
- [ ] **エンコード only** — まずあり得ない。
- [ ] **双方向(エンコード + デコード)** — VoIP / トランシーバ。両方実装。

### 3-2. キャリア(運ぶ単位)

- [ ] **ファイル / コンテナ** — `ByteStream` 経由、サンプルレートはコンテナから読む
- [ ] **生パケット**(RTP / ESP-NOW / UDP / WebSocket)— packet-at-a-time API、
      サンプルレートは `begin()` で固定
- [ ] **両方サポート** — クラスを分ける(コンテナ用とパケット用)

### 3-3. 上流コード戦略

| ケース | vendor 戦略 |
|---|---|
| 大規模ライブラリ(数千行〜) | `src/external/<name>/` に subset を verbatim 取り込み、`sync_*.py` で追随 |
| 単一ヘッダ(`*.h` 1 本) | `src/external/<name>.h` に置く、コミット時に手で更新 |
| 自前実装で十分(数百行以下、標準が単純) | `src/` に直接書く、`src/external/` 自体不要 |

### 3-4. 上流追随の自動化

| レベル | 何をする | いつ採る |
|---|---|---|
| **L0** | 何もしない | 自前実装、または上流が枯れている(変更なし)場合 |
| **L1** | 週次で tag 検知 → Issue 起票。sync 自体は人手 | 上流が年数回更新される library 系 |
| **L2** | 上記 + 自動 PR + host pytest + ESP32 compile | L1 を 1 年運用して頻度が体感で年 6 回超えてから検討 |
| **L3** | 完全自動リリース | 採らない([SPEC](../SPEC.md#release-workflow) §9 の根拠) |

### 3-5. ライセンスの整理

- 上流コードが BSD / MIT / Apache / Public Domain など permissive なら
  `src/external/LICENSE_<upstream>.md` にクレジット + 全文を載せれば終わり。
- GPL / LGPL は **採用しない**(Arduino スケッチへのリンクで影響が大きすぎる)。
  代替実装の有無を先に確認。
- 特許関連の声明(Opus のように royalty-free patent grant を別ファイルで
  出している例)がある場合は、上流から `PATENTS` ファイルを vendor 同梱
  して、`LICENSE_<upstream>.md` でも言及する。

### 3-6. フットプリント目標を SPEC に書く

`Flash <= XXX KB` / `RAM <= YY KB` を SPEC §「メモリ・フットプリント目標」
に書いておく。「軽い」と思っていても積み重なるので最初に上限を宣言する。

---

## 4. 新リポ立ち上げ手順

手で進めても AI 支援で進めても、順序自体は同じ。

1. **入力資料を揃える** — 親 PCMFlow と既存の兄弟ライブラリのリポを
   ローカルに置く。本メモを最初に通読する。
2. **§3 の判断ポイントを確定** — 立ち上げの最初に潰しておかないと、
   後から SPEC を書き直すコストが高い。
3. **判断結果を起点に SPEC を書く** —
   `SPEC.md` / `SPEC.ja.md` を既存兄弟からコピーして新コーデック用に調整。
   この時点で API 形状(クラス名・enum・公開メソッド)も固める。
4. **ライブラリメタデータと骨格を整備** — `library.properties` /
   `library.json` / `keywords.txt` / `.gitignore` / `src/<Codec>Encoder.h` /
   `src/<Codec>Decoder.h` / `src/PCMFlow<Codec>.h` / `pcmflow<codec>_version.h`。
   §2 の verbatim コピー対象をこのタイミングで持ち込む。
5. **tests/ 雛形を作る** — 既存兄弟の `tests/` を `cp -r` してスケッチ・
   テストスクリプトの中身をリネーム & 空にする。
6. **examples/ に headline ユースケースを 1 つ置く** — プレースホルダで
   十分。中身はあとで埋める。
7. **README / CHANGELOG を書く** — README の「Quick start」は実コードに
   できる粒度で。CHANGELOG は `## Unreleased` で立ち上げ作業を記録。
8. **`tools/bump_version.py` を動作確認** — `--preview` で意図した
   バージョン文字列とヘッダ生成パスになることを最初に確かめておく。
9. **(vendor がある場合)上流の初回 sync を手元で実行** — `sync_*.py
   --apply` を走らせて `src/external/<upstream>/` を populate し、
   `UPSTREAM.lock` が書き換わることを確認。CI で動かす前にローカルで一度
   通す。
10. **初期コミット & GitHub に push** — Library Manager 反映待ち。

AI 支援で進める場合は、§3 の判断結果と §2 の規約表を最初に読ませて
「既存の兄弟ライブラリと揃った構造で同じことをやってほしい」と指示すれば、
上記のステップを淡々と回せます。

---

## 5. 横展開のたびに考え直すべきこと(=ここに書きにくいこと)

- **コーデックごとに API の自然な形は違う**。packet-at-a-time が自然な
  ものもあれば、sample-at-a-time / frame-at-a-time の方が素直なものも
  ある。「兄弟だから API 表記も全部揃える」より「**コーデックの素直さ**を
  優先」して、共通部分は `PCMSource` / `PCMSink` の interface 側で吸収する。
- **テストの許容差はコーデックごとに違う**。ロスレスは near-exact、
  心理音響モデル系のロッシーは振幅 ±数 %、テーブル変換系のロッシーは
  量子化誤差を理論値と突き合わせ可能 — 三者で別の許容差ポリシーになる。
- **メモリ目標もコーデックごと**。テンプレートはそのまま流用できるが、
  数字は全部書き直す。
- **mono-repo 化の誘惑に注意**。Arduino Library Manager / PlatformIO
  Registry の流通単位はリポなので、まとめると配布できなくなる。今後 4 つ
  5 つ増えても別リポ運用を維持する前提。

---

## 6. このメモのメンテナンスポリシー

- 新しい兄弟ライブラリを 1 つ作るたびに、§2 / §3 のどこかが必ず増減する。
  立ち上げ完了時に「実際にやってみてズレた箇所」を本メモに反映する。
- 古い議論をそのまま残さず、決着がついた選択肢は確定版に置き換える
  (将来の読者が選択肢で迷わないように)。
- 同一内容を複数の兄弟リポに同梱する場合、どこか 1 つを正本にして他は
  同期するか、各リポで独立に育てるかを最初に決める。後者の場合は
  「リポ固有の話は §「コーデック固有メモ」を新設してそこに書く」など、
  共通部分を侵食しないルールを最初に置く。
