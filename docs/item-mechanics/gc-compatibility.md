# GC V19対応台帳

判定語は [戦闘リファレンス](../battle-reference/compatibility-matrix.md) と共通。台帳の一行は一つの検証範囲。

| 仕様ID | 判定・根拠 | 現行の処理 | 追加に必要なもの |
|---|---|---|---|
| ITM-GC-01 | SUPPORTED / TEST_CONFIRMED | `BITEM_MODIFIER` でタイプ強化・ちからのハチマキ・ものしりメガネの対応範囲。威力ステージ、4.12、指定の丸め | 能力値、最終ダメージや命中、実行時条件ごとに既存Modifierの表現力を検査 |
| ITM-GC-02 | SUPPORTED / TEST_CONFIRMED | `BITEM_WEATHER_TURNS`、`BITEM_TERRAIN_TURNS` は対応の持続ターンのみ。初期場や永続天候に適用しない | 種類の追加、接地・抑制、ばんのうがさ・シード等は別 |
| ITM-GC-03 | SUPPORTED / TEST_CONFIRMED | `BITEM_HP` のターン末でたべのこし・くろいヘドロ。タイプ述語、初回一致、対応タイミング | 道具で状態付与、拘束、行動者から別個体へのダメージ、動的なHP特性との連携 |
| ITM-GC-04 | PARTIAL / TEST_CONFIRMED | `ResolveConsumable` と閾値HP・`BITEM_CURE`。オレン／オボン等と指定状態回復きのみ。反動待機の補修を含む | きのみジュースはHP効果があっても `POCKET_ITEMS` で未出力。他の回復量表現、混乱味、PP、能力、抑止・再摂食は別 |
| ITM-GC-05 | SUPPORTED / TEST_CONFIRMED | `BITEM_ENDURE`、満タンタスキ、がんじょう優先、対応する直接・自傷の例と消費 | ハチマキの確率・RNGと消費しない指定。別の耐久経路を個別に検証 |
| ITM-GC-06 | PARTIAL / SOURCE_CONFIRMED | `BIREC_CONSUMED`、`item_from`、`item_used`。V19の発動したレコードだけが所持を空にする | 品の移動、無効化、復活、由来・理由と永続ポリシー |
| ITM-GC-07 | MISMATCH / SOURCE_CONFIRMED | 日照後の保留きのみとピボットで全員判定と交代の順が異なるコード経路 | 条件を満たすSingle/Doubleの参照試験。発動前後の場と履歴を比較 |
| ITM-GC-08 | UNSUPPORTED / SOURCE_CONFIRMED（いのちのたま・かいがらのすずを除く） | メット、じゃくてんほけん、カード類、こだわり・命中品等の効果は未出力 | 技全体／被弾／選択等の入口。現在の品を毎段階解決する。**いのちのたまはV20で PARTIAL / TEST_CONFIRMED**：最終補正5324と技終了の最大HP÷10（最低1）をExpansion 27件とGC Nativeで一致確認。マジックガード・ちからずく・ぶきようの所持者では全持ち物オフ（strictは拒否）、Dolphin未確認（B1c-0b時点）。[仕様](../items.md#14-m4-b1c-0b-life-orb-in-request-v20)。B1c-0c：Dolphin（GC V20 Lab L73-L83、4形式）で補正・HP減少・表示・結果を確認、相手のHPきのみ（オボン）を減少より前に補修（Expansionと一致）。[実機検証](../items.md#15-m4-b1c-0c-v20-on-the-gba--gc-path-dolphin-joybus)。**かいがらのすずはV21で PARTIAL / TEST_CONFIRMED（B1c-1b、GC Native・Expansion参照・Exporter。Dolphin未確認）**：技全体の実HP損失（`MoveContext.lost`）÷8、最低1、最大HPまで、他個体HPきのみの後・交代の前に1回。ちからずく・ぶきよう・持ち物特性・特性コピー系の所持者では全持ち物オフ、マジックガードは維持。持ち物移動はB2。[実装](../shell-bell.md#11-m4-b1c-1b-implementation-and-verification) |
| ITM-GC-09 | NOT_APPLICABLE / SOURCE_CONFIRMED | 表示専用戦の報酬・経験値等。ギミック禁止下の監査済みZ／メガ品は条件付きで所持を保持 | 必要なルールを追加した時点で再監査。NONEは所持なしとは別 |
| ITM-GC-10 | UNVERIFIED / UNKNOWN | 既存対象外の複合失敗・連続技と強制交代の競合、抑止解除と同じ技内の再評価 | マトリクスを小さな順序試験に分割。Expansionにない形式はGC側の仕様も固定する |

1〜3の参照は補正・天候・場・ターン末の測定済みケースに限る。4〜6の既存試験はV19の所持・消費・順序に関する例で、比較済みの範囲は [発動順](activation-timing.md#itm-time-002)。7は既知のソース差で組み合わせの実行試験未実施。ACT-002自体は補修済み。[GC V19参照](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/gamecube/battle/battle_test.c#L8230) · [GC ACT-002](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/gamecube/battle/battle_test.c#L8867) · [Expansion 補正試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/gc_item_reference.c#L1) · [Expansion ターン末試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/gc_end_turn_item_reference.c#L1)

## ITM-GC-GATE

`SOURCE_CONFIRMED`。持ち物定義は `BattleItemRecord`、1個体最大4件、1件8バイト。codecは持ち物IDと定義の一致、バージョン別レコード、範囲を検証。 `ResolveItem` と `ResolveConsumable` が個別に分類し、 `AcceptGimmickItems` と `CheckItems` が対戦全体を検査する。未対応品のほか、品と技・特性の組も境界。`RULE_IGNORE_ITEMS` は全品の効果を正規化し、所持IDは保持する。 `RULE_STRICT_ITEMS` では効果が失われる対戦を拒否する。GCは入力を再検査する。[レコード形式](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/protocol/battle_protocol.h#L447) · [battle_item_valid](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/protocol/battle_protocol.c#L490) · [ResolveConsumable](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/frontier_gc_battle.c#L1546) · [CheckItems](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/frontier_gc_battle.c#L1796) · [ルール確定](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/frontier_gc_battle.c#L2771) · [bc_items_check](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/gamecube/battle/battle_items.c#L45)

`bc_item_*` は現在品の有効レコードを選び、しきい値・治療・耐久・ターン末の各局面で呼ばれる。 `bc_items_after_action` は通常と行動不能の入口を使い分ける。V19より下の戦闘でV19消費を通さない。次版では既存のフィールドを別の意味で再利用せず、必要な追加レコードを明示して旧クライアントへ送らない。[V19消費ゲート](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/gamecube/battle/battle_items.c#L157) · [技の出口](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/gamecube/battle/battle_effects.c#L846) · [行動不能の出口](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/gamecube/battle/battle_core.c#L973)
