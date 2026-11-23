# 持ち物・特性の発動順

[持ち物処理の詳細監査](../item-mechanics/README.md) に、ACT-002補修後の品目・発動順・状態遷移・GC対応台帳と実装計画を記録する。

## ITEM-001

`SOURCE_CONFIRMED` — `ItemBattleEffects` は現在有効なholdEffect、発動タイミング、きんちょうかんによる抑止、生存条件を確認する。Expansionの有効性はマジックルーム、さしおさえ、ぶきよう等を含む。所持ID、現在の効果、発動できるかは別状態。HP回復のオレン／オボンは基本条件が生存かつ `HP <= floor(maxHP / 2)`、かいふくふうじ等で回復できない場合は発動しない。基準品目は固定10／最大HPの25%、最小回復と最大HP上限を適用する。世代分岐は [設定の逆引き](build-rules.md)。[HasEnoughHpToEatBerry](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_util.c#L5698) · [有効な持ち物判定](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_util.c#L5915) · [ItemHealHp](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_hold_effects.c#L805) · [ItemBattleEffects](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_hold_effects.c#L1020)

`TEST_CONFIRMED` — 回復境界、治療、交代後も消費済み、タスキの基本条件を既存参照試験で確認した。V19の範囲はオレン、オボン、クラボ、カゴ、モモン、チーゴ、ナナシ、キー、ラム、きあいのタスキ。ラムは主要状態異常と混乱を一度に治療し一度だけ消費。GCの `item_from` が空になると所持なしになり、開始時定義を消さず消費元と時点を記録する。適用結果の通知と `EV_ITEM_CONSUMED` は別イベント。持ち物交換・奪取・復活は未対応。[TryCureAnyStatus](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_hold_effects.c#L743) · [Cmd_removeitem](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_script_commands.c#L4521) · [消費と所持元](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_items.c#L114) · [既存参照試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/test/battle/gc_consumable_item_reference.c#L1)

## ITEM-002

`SOURCE_CONFIRMED` — `moveendall` 内の対象範囲と主なチェックを以下に示す。表は持ち物に関わる抜粋で、他の効果が間に入る。

| Expansionの段階 | 対象と動作 |
|---|---|
| `MoveEndItemEffectsTarget` | 現在対象。被弾後効果、状態変化で発動する持ち物 |
| `MoveEndItemEffectsAttacker1` | 行動者。攻撃後、状態回復、HPしきい値の順に試す |
| `MoveEndHpThresholdItemsTarget` | 連続技のヒット間。カウンタ条件下で現在対象のHPしきい値 |
| `MoveEndItemEffectsAttacker2` | 反動等の後に行動者の状態・HPを再評価。対応ケースではその後にじしんかじょう |
| `MoveEndKeeMarangaHpThresholdItemTarget` | 技の後半で行動者以外のHPを順に評価 |
| `MoveEndItemsEffectsAll` | バトラー位置順で状態、次いでHP。発動スクリプトから復帰して走査を続ける |

根拠。[対象／行動者1](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L3734) · [連続技中](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L4141) · [行動者2](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L4674) · [他個体のHP](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L4756) · [全員](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L5099)

通常の状態付与、登場、残留ダメージにも独立したチェックがある。GCの `bc_set_status` は対応するシンクロの後にきのみを調べる。ターン末のどく・やけど等や砂嵐によるHP閾値は個体ごとに処理する。Expansionの `BattleScript_DoTurnDmg` はHP適用・ひんし・勝敗を経てHP判定。サンパワー／かんそうはだの日照ダメージで閾値に入った場合、既存参照ケースでは次の技終了まで保留される。残留ダメージを一律に「HPが変わったら即発動」とする実装ではこの差が失われる。[bc_set_status](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_effects.c#L258) · [BattleScript_DoTurnDmg](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/data/battle_scripts_1.s#L4189) · [晴れダメージと遅延発動](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/test/battle/gc_consumable_item_reference.c#L649)

## ITEM-003

`SOURCE_CONFIRMED` — GC V19は `bc_item_hp_check` / `bc_item_status_check` / `bc_item_endure` を対応する段階から呼ぶ。未対応品目または相互作用は厳密モードで拒否、通常モードでは一括正規化の対象になる。対応済み品目と「所持IDあり・効果なし」の混在は別途検査する。消費前後での有効性を読むため、B1cで被弾・技終了効果を拡張する際には `item_from` の旧参照を保持し続けない。[GCの消費判定](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_items.c#L157) · [開始時の判定](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_items.c#L48) · [ResolveItem](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/frontier_gc_battle.c#L1590)
