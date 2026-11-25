# 持ち物・技との相互作用

## ABL-B1C-05 ぶきよう・かるわざ

`SOURCE_CONFIRMED`。ぶきようは現在の所持IDを維持したまま有効効果を `HOLD_EFFECT_NONE` にする。賞金倍化が例外。さしおさえ・マジックルームも初期評価に入る。 `GetBattlerHoldEffectIgnoreAbility` は特性を介さず取得する入口で、とくせいガードの有効性との再帰を避ける。所持・交換や飛び道具の資格は経路ごとに確認する。 `ItemAbility` の全品正規化と、実際のぶきようによる個体単位の抑制は区別する。[実効アイテムと特性を無視する取得](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_util.c#L5907) · [Klutzと賞金例外](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_util.c#L5915) · [ItemAbility](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/frontier_gc_battle.c#L1700)

かるわざは `CheckSetUnburden` を呼ぶ消費・外し・移動の経路でイベントを記録し、その時点の特性が有効でマジックルームとさしおさえの影響がなければ有効化。最初から空欄の個体には履歴がない。現在の特性とフラグを速度計算で評価し、能力変更や品の再取得、交代で解除する。発動後の特性抑制と、抑制中の喪失は別ケース。動的速度設定では残りの行動順にも影響する。[CheckSetUnburden](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_util.c#L11093) · [速度計算](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_main.c#L4625) · [取得時のフラグ消去](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_util.c#L10517) · [特性変更時のフラグ消去](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_util.c#L11041) · [動的行動順](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/include/config/battle.h#L66) · [発動と抑制を分けた参照例](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/ability/unburden.c#L45)

## ABL-B1C-06 きのみ特性

| 特性 | 条件・発動段階 | 参照状態／変更 |
|---|---|---|
| きんちょうかん・じんばいったい | 相手側から摂食を抑止、場にいる有効な特性を確認 | 現在の品IDは保持。味方・対向個体・効果解除後の再評価。[IsUnnerveBlocked](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_util.c#L455) |
| くいしんぼう | 指定きのみのHPしきい値を引き上げる | 対象きのみの種類・HP条件。すべてのオボン等に一律適用しない。[HasEnoughHpToEatBerry](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_util.c#L5698) |
| じゅくせい | 効果の種類ごとの増幅 | HP回復・能力上昇・半減きのみ等で別の処理。単一倍率で全効果を変えない。[ItemHealHp](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_hold_effects.c#L805) · [能力きのみ](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_hold_effects.c#L467) · [半減きのみ](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_util.c#L7828) |
| ほおぶくろ | 摂食後の追加回復。発動経路が消費コマンドに再入する | HP、回復可否、摂食品。[TryCheekPouch](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_script_commands.c#L4504) |
| はんすう | きのみの消費でタイマーを設定。後の残留で履歴を読んで再摂食 | 使用履歴を更新、現在品の再取得とは別。[消費記録](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_script_commands.c#L4545) · [再摂食](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_util.c#L3965) |
| しゅうかく | ターン末。空き枠・使用きのみを確認。晴れでは適用対象かを確認、通常50%の抽選 | 使用品を持ち直す。ものひろいの取得候補と区別。[PickupとHarvest](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_util.c#L3791) |

`SOURCE_CONFIRMED`。GC V19の `ItemAbility`、`ConsumableItemAbility`、`HpItemAbility` は別の理由で正規化する。Cud Chewの判定は対応される消費きのみレコードを調べ、Magic GuardのHP品判定はダメージレコードのある組だけに作用する。今後の品・特性に同じゲートを無条件に流用しない。[Cud ChewとHPアイテム全体のゲート](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/frontier_gc_battle.c#L1742)

## ABL-X-001 技の入力契約

- `ResolveEffects` は対応する効果単位、接触 `BMF_CONTACT`、対象等を出力。追加効果側の `sheerForceOverride`、技毎の抑制・接触例外、ダメージの由来はV19で汎用表現されない。静的な有効条件をExporterで正規化し、命中・実被害・接触・場・生存等の動的条件はGCで解決する。
- `UnmodelledMoveFlags` は `shadowMove`、`ignoresTargetDefenseEvasionStages`、`ignoreTypeIfFlyingAndUngrounded`、特性が対象の版の `ignoresTargetAbility` を拒否。粉技も別に拒否するため、ぼうじんの粉免疫をHookが実装済みと数えない。
- 相性・無効、みがわり、1打ごとのひんし、技途中での持ち物の変更を区別する。複数対象・連続技で「命中した」と「HPが減った」と「接触副作用が成立した」を再利用する場合は、状態の寿命を先に定める。

[UnmodelledMoveFlags](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/frontier_gc_battle.c#L628) · [ResolveEffects](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/frontier_gc_battle.c#L639) · [粉技の例](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/frontier_gc_battle.c#L760) · [持ち物側の発動順](../item-mechanics/activation-timing.md)

## いのちのたまとの組み合わせ（M4-B1c-0b、V20）

`TEST_CONFIRMED`（Expansion参照とExporter試験）。Expansionでは、マジックガードは最終補正を残してHP減少だけを止め、ちからずくは追加効果が対象になる技だけ減少を止め（補正は両方掛かる）、ぶきようは補正も減少も止める。いえきで抑制すると通常どおり。GC V20はこれらの特性を実行しないため、所持者がいる戦闘では全持ち物をオフにする（strictは拒否）：マジックガードは既存 `HpItemAbility`（ダメージのHPレコード）、ちからずくは技終了HPレコードの所持者に追加、ぶきようは既存 `ItemAbility`。ちからずくをすべての技で減少なしと扱う実装はしない。かがくへんかガスは未対応特性（strictで拒否）。[Exporter](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/8219d703a08acc659a919709ba94ce0832da52ca/src/frontier_gc_battle.c#L1800) · [Exporter試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/8219d703a08acc659a919709ba94ce0832da52ca/test/battle/gc_life_orb_export.c#L92) · [仕様](../items.md#14-m4-b1c-0b-life-orb-in-request-v20)
