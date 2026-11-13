# GBA-GC Extension

**GBA-GC Extension / Frontier Hack Public Alpha**

ゲームボーイアドバンス（GBA）とゲームキューブ（GC）を、標準の JoyBus 通信でつなぐ非公式の実験プロジェクトです。
GBA はゲームのデータを持ち、コントローラーと「2つ目の画面」になります。GC は対戦の計算と表示を受け持ちます。

> **非公式のファンプロジェクトです。** 任天堂、株式会社ゲームフリーク、株式会社ポケモン、クリーチャーズ、
> Dolphin の開発チームとは一切関係がなく、承認も受けていません。
> 「ポケットモンスター」「ポケモン」「ゲームキューブ」「ゲームボーイアドバンス」は各権利者の商標です。

*English summary: an unofficial experiment that links a GBA and a GameCube over JoyBus. The GBA keeps the game data and acts as controller and second screen; the GameCube runs and draws Single, Double, Triple and Rotation test battles. Target: Dolphin with its integrated GBA. Not affiliated with Nintendo, Game Freak, The Pokémon Company or the Dolphin team.*

## できること（v0.1.0-alpha 時点）

- **Party Viewer**：GBA の手持ちポケモン（名前・画像・HP・わざ）をテレビに表示。
- **4つの対戦形式**：シングル／ダブル／トリプル／ローテーションを GC 上で計算・表示し、コマンドは GBA で選ぶ。
  - 対象選択、全体わざ、トリプルの射程と Shift、ローテーションの回転
  - 能力ランク、主な状態異常（どく・もうどく・やけど・まひ・ねむり・こおり）、ターン終了時のダメージ
  - 交代、ひんし後の補充、わるあがき、ほえる／ドラゴンテール、とんぼがえり系
- **GBA second-screen**：対戦中、GBA はコマンド・わざ・対象・控えを表示する2つ目の画面になります。
- **せつぞく かくにん**：両方のリリース番号・ビルドID・プロトコルを表示し、組み合わせ違いを検出します。
- **キューブテスター**：Frontier Hack の「フロンティア ハブ」右上の扉から入るテスト用の部屋。

### まだできないこと

- とくせい・もちもの・急所・天気・フィールド・ギミックは計算しません。追加効果や優先度のあるわざの多くは選べません。
- 経験値・努力値・賞金はありません。4形式の結果は**表示だけ**で、セーブには残りません。
- 技ごとのアニメーションは開発中です。
- 実機（GC 本体・GBA・ケーブル）での動作は確認していません。対象は **Dolphin + 内蔵GBA** です。
- **GBA 用パッチ（BPS）は現在配布していません**（[理由](docs/releases.md#v010-alpha-status)）。

## 役割分担

| 層 | 場所 | 役割 |
|---|---|---|
| GC ランタイム（このリポジトリ） | `gamecube/`, `protocol/` | JoyBus 通信、メッセージ層、対戦の計算（Battle Core）、表示（GX）、アセットのキャッシュ |
| 汎用コネクタ | [pokeemerald-expansion fork `gc-extension`](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/tree/gc-extension) | GBA 側の通信・モード起動・パーティ/画像/文字の書き出し・結果の受け取り（ゲーム内容は持たない） |
| ゲーム内容 | [Frontier Hack (`frontier-hack`)](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/tree/frontier-hack) | キューブテスター、テスト用ポケモン、ルールの書き出し、GBA second-screen |

## 対応環境

| 項目 | 内容 |
|---|---|
| エミュレーター | Dolphin 2609b（検証済み）、GBA は「GBA (内蔵)」をポート 2 に |
| GBA ROM | 正規に所有する日本版『ポケットモンスター エメラルド』（BPEJ, rev 0）を元にした Frontier Hack |
| OS | Windows 11 で検証 |
| 実機 | 未検証・対象外 |

使い方は **[テストガイド（日本語）](docs/public-test-guide.md)** を見てください。
組み合わせ（リリース番号）は [releases.md](docs/releases.md)、既知の問題は [known-issues.md](docs/known-issues.md) にあります。

## ビルド

Windows PowerShell 5.1 または PowerShell 7 と、devkitPro（devkitARM + libgba、devkitPPC + libogc、gbafix、elf2dol）が必要です。

```powershell
./build.ps1              # 開発用（release 0）
./build.ps1 -Release 1   # 公開テスト用 release 1 (v0.1.0-alpha)
```

- `build/companion.dol`：GC Extension のホスト（Party Viewer・対戦）
- `build/client.gba` + `build/host.dol`：Phase 1 の通信テスト

詳しくは [build.md](docs/build.md)、[testing.md](docs/testing.md) を見てください。

## 権利関係

| 対象 | 権利・ライセンス |
|---|---|
| このリポジトリの独自コード・独自の画像（GC 側 UI・背景・効果） | [MIT License](LICENSE) |
| 第三者ライブラリ（LinkCube、libogc、newlib、libgcc、libgba） | 各ライセンス。[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) を参照 |
| ゲーム固有のデータ（ポケモンの画像・名前・わざ名・対戦の文章など） | 元のゲームの権利者に帰属。**このリポジトリには含まれません。** 実行時にユーザーが用意した GBA ROM から読み出して表示するだけです |
| Frontier Hack / pokeemerald-expansion | それぞれのリポジトリの扱いに従います |

このリポジトリには、ROM・BIOS・セーブデータ・任天堂やポケモンの公式画像・音声・フォント・エミュレーター本体は含まれていません。
Battle Core は独自に書いたもので、他のゲームの逆アセンブル／デコンパイルのコードは使っていません
（対戦ルールの確認に公開資料を参照した箇所は [battle-engine.md](docs/battle-engine.md) に出典を記載しています）。

## 不具合報告

[GitHub Issues](https://github.com/koziyagoukei/gba-gc-extension/issues) の「不具合報告」テンプレートから報告してください。
**ROM・セーブデータ・BIOS・個人情報はアップロードしないでください。**

## 開発資料

[アーキテクチャ](docs/architecture.md) · [プロトコル](docs/protocol.md) · [Battle Engine](docs/battle-engine.md) ·
[複数形式](docs/multiformat.md) · [表示](docs/presentation.md) · [Phase 2](docs/phase2.md) · [Phase 3](docs/phase3.md) ·
[Phase 4（旧方式）](docs/phase4.md) · [ベンチマーク](docs/benchmarks.md)

| ディレクトリ | 内容 |
|---|---|
| `gba/` | Phase 1 の GBA テストクライアントと独自のテスト画像 |
| `gamecube/` | SI 通信、ハンドシェイク、セッション、Phase 1 テスト、GX 表示 |
| `gamecube/companion/` | ホスト本体：メッセージ層、アセットキャッシュ、Party Viewer、対戦の進行 |
| `gamecube/presentation/` | 独自 UI、レイアウト、アニメーション、対戦画面 |
| `gamecube/battle/` | SDK に依存しない Battle Core とテスト |
| `protocol/` | フレーム、メッセージ（CAPS/APP/ASSET/BYE/MODE/RESULT）、ポケモン用・対戦用プロトコル |
| `tools/sync-pokeemerald.ps1` | `protocol/` を pokeemerald-expansion 側へコピー（`-Check` で確認） |
| `third_party/` | LinkCube（MIT、無改変） |
