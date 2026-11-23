# ダメージとHP変化

## DMG-001

`SOURCE_CONFIRMED` — 対象・命中・かばう効果などを判定してから、有効なヒットの計算へ進む。通常ダメージの主要経路は、威力の解決 → 攻防値 → レベルを含む基礎ダメージ → 対象数補正等 → 天候 → 急所等 → ダメージ乱数 → STAB → 相性 → やけど／しもやけ → 防御効果と最終補正。基準の通常技は85–100%の乱数を使い、無効判定と最小ダメージは別。範囲補正や固定ダメージの扱いは効果に依存する。[命中からHP適用までの段階](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L3290) · [DoMoveDamageCalcVars / ApplyModifiersAfterDmgRoll](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_util.c#L7895) · [固定ダメージの別経路](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_util.c#L7952)

`TEST_CONFIRMED` — GC V14以降の対応Modifierは4.12固定小数点。威力合成と攻防・最終合成で丸めが異なり、V17で切り捨て係数を明示できる。今回のGCネイティブ試験で既存固定値を再確認した。対象Expansionのダメージ全経路の同等性を示す範囲ではない。[4.12固定小数点処理](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_modifiers.c#L116) · [Expansionの既存ダメージ参照値](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/test/battle/gc_damage_reference.c#L1)

## DMG-002

`SOURCE_CONFIRMED` — 通常の致死ヒットは、みがわり等による肩代わりを区別し、`GetAdjustedDamage` でこらえる → みねうち効果 → 満タンのがんじょう → ハチマキ → 満タンのタスキ等を評価する。耐久成立時は適用ダメージを `HP - 1` にする。タスキ消費は成立したヒットに結び付く。GCのV19は対応範囲でがんじょう → タスキ。固定の参照試験は、満タン、がんじょう優先、連続技中に一度だけ消費、自傷、どく残留では発動しないことを比較している。[GetAdjustedDamage](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_util.c#L8285) · [strike](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_effects.c#L507) · [タスキの検査と発動](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_items.c#L211) · [V19耐久参照試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/test/battle/gc_consumable_item_reference.c#L560)

## DMG-003

`SOURCE_CONFIRMED` — 連続技の一撃ごとに、HP・ひんし・接触効果・持ち物の結果で次のヒットの状態が変わる。対象の回復きのみは必要な場合にヒット間に発動する。GC V19の対応する単発範囲技では、全対象の被弾を処理してから耐久メッセージ・きのみ処理を進める。ドレイン、行動者の反動、双方のきのみ、ターン中の強制交代には個別の順序がある。例えば既存参照ケースは、ドレイン後に対象のきのみ、接触ダメージ後に行動者のきのみ、反動KO時のきのみがじしんかじょうより先、ドラゴンテールでは対象のきのみより交代が先。[MoveEndNextTarget](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L4019) · [ヒット間のHPチェック](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L4141) · [単発範囲技の保留](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_effects.c#L798) · [連続技・反動・接触の参照ケース](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/test/battle/gc_consumable_item_reference.c#L733)
