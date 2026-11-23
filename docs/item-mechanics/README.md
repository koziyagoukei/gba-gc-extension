# 持ち物処理の監査

対象は ACT-002 補修後の GC `44c51538c7e5efbeb27bee91d5cc8a0a89cab14b` と Frontier `2d1e107ad820a3b0a601af968b49856f74151ee1`。Request/APP V19、release 13。対象ExpansionのCコード、スクリプト、実データ、設定、試験を使い、戦闘での発動順を記録する。対象コードと設定への参照はコミットを固定する。[ACT-002の補修記録](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/docs/items.md#L818) · [GEN_LATEST](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/include/config/general.h#L74)

## 索引

- [品目・効果カタログ](item-catalog.md)
- [発動タイミング](activation-timing.md)
- [所持・消費・交換・復元](consumption-and-state.md)
- [技・特性との相互作用](moves-and-abilities.md)
- [GC V19対応台帳](gc-compatibility.md)
- [Frontierの出現頻度と実装順](implementation-roadmap.md)
- [未確定事項と参照試験](open-questions.md)
- [品目表](items.csv) · [効果表](effects.csv) · [全セット表](frontier.csv) · [出現頻度](frequency.csv) · [集計スクリプト](catalog.py)

## 根拠区分

`SOURCE_CONFIRMED` は固定ソースの分岐・順序を確認した箇所。 `TEST_CONFIRMED` は記載の条件を試験した箇所。 `INFERRED` はコードから導いた組合せや導入時の見込み。 `UNKNOWN` は未照合。台帳の `SUPPORTED` は参照ケースの範囲に限定する。HTML表示で大きいCSVはRawを利用できる。CSVの行番号は当該固定コミットのソース用。

## 採用判断

B1cの初回は **いのちのたま** を推奨する。最終補正と技単位のHP減少をまとめて実装できる境界を作る。同じ技終了段階を使うかいがらのすず、被弾対象側を処理するゴツゴツメット、能力段階を変更するじゃくてんほけんは別単位。こだわり、強制交代、移動・復元には、それぞれ独立した状態管理と試験が必要。詳細は [実装順](implementation-roadmap.md)。

## この版の検証

- Frontierの既存headless ELFを一時コピーして実行。V19の参照47件（ACT-002の6件を含む）を再確認。ELFの再リンクは行っていない。対象外の `secret_power.c:7` にASSUME通知が出た。[反動の参照試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/gc_recharge_item_reference.c#L24) · [消費型持ち物の参照試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/gc_consumable_item_reference.c#L1)
- Frontierの既存Exporter試験は、同じELFで全2,040セットをSingleの先発＋たいあたりのカビゴンとして評価。再実測値は生成1,996／持ち物992／厳密108。出現頻度はGit上の全セットから別途集計。[既存Exporter集計](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/gc_consumable_export.c#L243)
- GCネイティブ試験を固定ソースからビルドして成功。V19の4形式240戦とACT-002の回帰を含む。Dolphin・実機での新規試験と新規の参照テストは未実施。[test_v19_recharge](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/gamecube/battle/battle_test.c#L8867)
