# ビルド・世代設定の逆引き

## RULE-001

`SOURCE_CONFIRMED` — 以下は対象Frontierソースのコンパイル既定値。`GEN_LATEST` は `GEN_9`。`GetConfig()` を使う項目や試験側の設定は経路ごとに別途解決される。GCのRequestには実際に出力されたルールを使う。[GEN_LATEST](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/include/config/general.h#L74) · [ResolveConditionRules](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/frontier_gc_battle.c#L2602) · [現行Requestのルール](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/frontier_gc_battle.c#L2700)

| 設定・実装 | 対象ビルドの既定 | 主な仕様ID |
|---|---|---|
| `B_CRIT_CHANCE` / `B_CRIT_MULTIPLIER` | GEN_9の確率設定と1.5倍。現行GC Requestは急所禁止 | DMG-001 |
| `B_PARALYSIS_SPEED` / `B_PARALYSIS_CHANCE` | 速度1/2、停止1/4 | TURN-001、ACT-001 |
| `B_FREEZE_TURNS` / `B_USE_FROSTBITE` | 自然解凍20%。一般のしもやけ置換はFALSE、技個別の付与とは別 | ACT-001 |
| `B_CONFUSION_SELF_DMG_CHANCE` / `B_BURN_DAMAGE` | 混乱自傷1/3の世代、やけど残留1/16。しもやけ残留にも後者を使用 | ACT-001、TURN-004 |
| `B_SKIP_RECHARGE` | 第1世代のKO後待機省略は適用しない | ACT-002 |
| `B_MEGA_EVO_TURN_ORDER` / `B_RECALC_TURN_AFTER_ACTIONS` | 技前ギミック後の再整列と行動間の動的速度。一括ギミック禁止のGCは前者を実行しない | TURN-001 |
| `B_ABILITY_WEATHER` / `B_TERRAIN_TYPE_BOOST` | 通常の特性起動天候5ターン、対応する場の威力補正30%。延長と永続天候は別条件 | FIELD-001、FIELD-002、TURN-004 |
| `I_SITRUS_BERRY_HEAL` / `B_HEAL_BLOCKING` | オボン25%、かいふくふうじの抑止。HP閾値は `ItemHealHp` の除数2から決まる | ITEM-001 |
| `B_KNOCK_OFF_REMOVAL` / `B_TRAINERS_KNOCK_OFF_ITEMS` / `B_RESTORE_HELD_BATTLE_ITEMS` | 取り外し方式、トレーナー戦の許可、終了時復元を分けて評価。対象設定は GEN_9／TRUE／GEN_9 | ITEM-003、LIFE-004 |
| `P_MEGA_EVOLUTIONS` / `P_PRIMAL_REVERSIONS` / `P_ULTRA_BURST_FORMS` | TRUE。個別資格に加え、GCの現行モードではギミック禁止 | LIFE-001、TURN-001 |

Zワザはフォームのコンパイル設定とは別に `CanUseZMove` と元技・所持品で検査する。ダイマックス・テラスタルの資格は各処理と戦闘モードに依存する。全施設・世代別の許可マトリクスは未調査。[戦闘設定](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/include/config/battle.h#L5) · [しもやけ設定](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/include/config/battle.h#L393) · [アイテム設定](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/include/config/item.h#L9) · [フォームの有効化](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/include/config/species_enabled.h#L23) · [CanUseZMove](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_z_move.c#L116) · [ダイマックス資格](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_dynamax.c#L106) · [テラスタル資格](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_terastal.c#L111)

## FIELD-001

`SOURCE_CONFIRMED` — GCのV15/V16は対応する天候・フィールドを正規化したレコードで扱う。ゲンシ天候と天候の発生源に結び付く寿命、対応レコード外の場効果は範囲外。[GC天候の制限](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/docs/weather.md#L111) · [V16のフィールド](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/docs/terrain.md#L1)

## FIELD-002

`TEST_CONFIRMED` — 対応する通常天候・フィールドのダメージ補正と残留順には既存の参照ケースがある。勝敗確定、天候終了、補充を挟む場合は [TURN-004](turn-order.md#turn-004) の範囲も確認する。[Expansionのターン末](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_end_turn.c#L1614)
