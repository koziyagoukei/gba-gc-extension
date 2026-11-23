# 特性システムの監査

対象はGC `44c51538c7e5efbeb27bee91d5cc8a0a89cab14b`（Request/APP V19・release 13）とFrontier `2d1e107ad820a3b0a601af968b49856f74151ee1`。文書の親は持ち物監査 `6550adcaa763d3651a88a0fef549a299d41c746c`。全定義と参照箇所の索引、B1cに関わる特性の仕様、実装・検証順を記録する。

- [全特性カタログ](ability-catalog.md) · [特性CSV](abilities.csv)
- [発動タイミング](activation-timing.md)
- [B1cのダメージ、接触、耐久](damage-and-contact.md)
- [特性の抑制、無視、変更](suppression-and-change.md)
- [持ち物・技との相互作用](items-and-moves.md)
- [GC対応台帳](gc-compatibility.md)
- [Frontier集計と実装順](implementation-roadmap.md) · [セットCSV](frontier.csv) · [全分岐の頻度](frequency.csv) · [既定設定のスロット](default.csv) · [既定設定の頻度](default-frequency.csv)
- [追加試験と未確認事項](open-questions.md) · [集計スクリプト](catalog.py)
- [戦闘リファレンス](../battle-reference/README.md) · [持ち物監査](../item-mechanics/README.md)

## 採用判断

B1c-0はいのちのたまの最終補正と技単位の減少に絞る。マジックガード・ちからずく・ぶきよう等を含む対戦の厳密受理は、それぞれが関係する機構を正規化・実行できてから行う。メット段階で接触反応を増やす前に、致死接触の通知順と、複数Hookを含む定義の受理契約を固定する。[GC対応台帳](gc-compatibility.md)

## 根拠区分

`SOURCE_CONFIRMED` は固定ソースの確認。 `TEST_CONFIRMED` には実行範囲を添える。 `INFERRED` はソース上の候補・設計予測、 `UNKNOWN` は未確定。CSVの参照は実装・設定の入口を示す。AIやUIの読者を戦闘処理の証拠に置き換えない。個別の対応は [GC対応台帳](gc-compatibility.md) の効果単位で評価する。

## 実行検証

- 固定ソースからGCネイティブ試験を `-O2 -Wall -Wextra -Werror` でビルドして成功。旧版の特性・wire、V19の4形式240戦とACT-002を含む。一時GCテストで複合接触Hookの受理と致死接触のイベント順を確認した。[特性回帰](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/gamecube/battle/battle_test.c#L9215) · [反動待機](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/gamecube/battle/battle_test.c#L8867)
- Frontierの既存headless ELFは一時コピーを使用した。SHA-256は `1d371d5c8404bc96f3c654c49533819af2e8c38302529cb93f55338794329bec`。既存ELFから固定コミットへのビルド由来は証明できなかったので、この実行は参考観測。バイナリのbuild ID生成もGit SHAを含まない。固定ソースの監査と、ELFでPASSしたことを区別する。[build IDの材料](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/gc_companion_pokemon.c#L63) · [観測内訳](open-questions.md)
- 抽出CSVの再生成、参照行と文書間リンクを確認。新規Dolphin・実機試験は未実施。
