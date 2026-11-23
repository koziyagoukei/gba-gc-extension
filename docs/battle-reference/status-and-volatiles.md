# 状態異常・揮発状態と行動不能

## ACT-001

`SOURCE_CONFIRMED` — 行動不可の理由は、選択の拒否、行動枠のスキップ、実行可否の打ち切り、実行後の失敗に分ける。後二者でも終了処理を通る場合がある。PP確認とPP減算も別段階。[HandleAction_UseMove](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_util.c#L492) · [実行可否の順序](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L3242)

以下の「両判定」は、生存した対象の状態異常回復とHPしきい値の処理に到達することを指す。条件を満たし、抑制されていないV19対象きのみなら発動・消費する。具体的な段階は [ITEM-002](items.md#item-002) を参照。行動中の判定なので、戦闘が続けば当該ターンの天候・どく・やけど・しもやけ処理より前。残留ダメージそのものから行うHPチェックは別経路。

| ケース | Expansionの経路と判定 | GC V19の確認結果 |
|---|---|---|
| はかいこうせん等の反動待機 | `CancelerRecharge` → `BattleScript_MoveUsedMustRecharge` → `moveendall`。両判定。[Cの分岐](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L95) · [待機スクリプト](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/data/battle_scripts_1.s#L1912) | `EV_RECHARGE` の後に `bc_items_after_action`（V19）。**SUPPORTED**、Expansion参照試験・GCネイティブ試験・Dolphin L72で一致（[ACT-002](#act-002)）。 |
| まひで停止 | `CancelerParalyzed` → `BattleScript_MoveUsedIsParalyzed` → `moveendall`。両判定。[まひ判定](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L580) · [終了経路](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/data/battle_scripts_1.s#L4238) | 他個体のオボンが後続行動前に発動する固定ケースを両側で確認。 |
| ねむりで停止 | 睡眠カウンタ処理、例外技の判定 → `BattleScript_MoveUsedIsAsleep` → `moveendall`。両判定。[CancelerAsleepOrFrozen](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L124) · [睡眠で停止](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/data/battle_scripts_1.s#L4162) | `blocked_by_status` 後にチェック。保留きのみ（サンパワー）の発動順をExpansion参照試験とGCネイティブの同条件ケースで比較し一致。[参照試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/329ceb28ad48f52c60803e6beb4686260bca1f3f/test/battle/gc_recharge_item_reference.c#L143) |
| こおりで停止 | 解凍しなければ `BattleScript_MoveUsedIsFrozen` → `moveendall`。両判定。先に解凍した場合の治療きのみは、そのこおりでは発動しない。[凍結・解凍判定](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L176) · [凍結で停止](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/data/battle_scripts_1.s#L4220) | GCにも終了パスあり。こおりで停止したターンの保留きのみは独立試験待ち。 |
| しもやけ | 睡眠・凍結の行動停止分岐に入らない。技実行経路のチェックと、後段の残留ダメージのHP判定を区別する。[行動停止の対象](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L124) · [しもやけの残留](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/data/battle_scripts_1.s#L4215) | 特攻側ダメージ補正と残留処理を持つ。保留からの発動順は未比較。 |
| こんらん自傷 | 自傷のHP反映 → ひんし判定 → `moveendall`。生存者に両判定。[CancelerConfused](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L519) · [自傷スクリプト](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/data/battle_scripts_1.s#L4294) | 自傷 → 回復きのみ、またはタスキ → 次の行動を固定した参照ケースあり。 |
| ひるみ | `BattleScript_MoveUsedFlinched` → `moveendall`。ふくつのこころ経路では能力変化後に終了。両判定。[CancelerFlinch](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L428) · [ひるみの各経路](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/data/battle_scripts_1.s#L4260) | ひるみから共通チェックに進む。ふくつのこころを含む比較は未確認。 |
| ミス・通常の失敗 | `BattleScript_MoveMissed`、`BattleScript_ButItFailed` から `moveendall`。両判定。技固有の部分MoveEndは別。[ミス](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/data/battle_scripts_1.s#L1491) · [失敗](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/data/battle_scripts_1.s#L2415) | `bc_execute_move` のラッパーが内側の早期return後もチェックする。個別の技固有順までの一致は未確認。 |
| PP不足 | 選択時の拒否は `endselectionscript`。実行時に0なら `CancelerPowerPoints` → `BattleScript_NoPPForMove` → `moveendall`。後者は両判定。[実行時のPP判定](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L373) · [選択と実行時のスクリプト](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/data/battle_scripts_1.s#L3700) | 選択時に使用可能技を検証し、合法技が全てPP切れならStruggleを扱う。選択後のPP喪失からの失敗経路は未対応の組み合わせを含み、網羅比較外。 |
| 行動前のひんし・不在 | `HandleAction_UseMove` の入口でスキップ。この行動枠由来のMoveEndと持ち物判定はない。[行動枠スキップ](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_util.c#L492) | `TA_SKIPPED`。ひんし個体のきのみは発動しない。 |
| なまけ・ちょうはつ等 | 実行可否テーブルに個別の段階がある。選択拒否、実行キャンセル、部分MoveEndは個別に記録する必要がある。[CancelerTruant](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L388) · [ディスパッチ](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L3242) | 未対応の特性・技は拒否または正規化の対象。その他の経路は未監査。 |

GC側の主な根拠は `continue_turn`、`bc_execute_move`、`bc_items_after_action`。反動待機を通る分岐と、状態停止を通る分岐は別。まひの参照試験は行動者自身のきのみではなく、**他個体**の発動もMoveEndに含まれることを固定している。[GCの中断分岐](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_core.c#L952) · [実行ラッパー](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_effects.c#L844) · [行動後の検査](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_items.c#L261) · [完全まひの参照試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/test/battle/gc_consumable_item_reference.c#L713) · [GCの睡眠ケース](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_test.c#L8732)

## ACT-002

`TEST_CONFIRMED` — 反動待機のターンは、Expansionでは `CancelerRecharge` で行動が打ち切られる。これは睡眠・こおりの判定より前である。続いて `BattleScript_MoveUsedMustRecharge` が反動メッセージを表示してから `moveendall` を実行する。この終了処理の持ち物判定は次の順に進む。

1. `MOVEEND_ITEM_EFFECTS_TARGET`：選択対象の状態異常きのみ。
2. `MOVEEND_ITEM_EFFECTS_ATTACKER_1`：行動者自身の状態異常、次にHP。
3. `MOVEEND_KEE_MARANGA_HP_THRESHOLD_ITEM_TARGET`：行動者以外のHP。battler順（自分左、相手左、自分右、相手右）。
4. `MOVEEND_ITEMS_EFFECTS_ALL`：全員の状態異常、次にHP。

`MOVEEND_HP_THRESHOLD_ITEMS_TARGET` は `gMultiHitCounter` が0なので働かない。条件を満たすV19対象きのみは発動・消費され、次の行動はその後に始まる。元の技のPPは待機ターンに減らない。行動前にひんしした個体の行動枠は `HandleAction_UseMove` の入口でスキップされ、反動メッセージも、この行動枠のMoveEndもない。[CancelerRecharge](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L95) · [待機スクリプト](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/data/battle_scripts_1.s#L1912) · [対象の判定](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L3734) · [行動者の判定](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L3747) · [連続技専用の対象HP判定](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L4141)

GC V19の補修では、`continue_turn` の反動分岐で `EV_RECHARGE` の後に `bc_items_after_action(b, user, false, log)` を呼ぶ。これは他の行動不能分岐と同じ呼び出しである。判定順は次のとおり。

1. 行動者の状態異常、次にHP。
2. 行動者以外のHP（battler順）。
3. 全員の状態異常、次にHP。

使用済みのきのみは `item_from` がNULLになり、再度は発動しない。ひんしした個体は判定から外れる。V18以前は `bc_items_consumable` が偽なので何もせず、固定seedトレースV12〜V19は補修前と同一である。[補修](https://github.com/koziyagoukei/gba-gc-extension/blob/78b3125be6efc57d710f51c730e564111bcb4b3e/gamecube/battle/battle_core.c#L973) · [行動後の検査](https://github.com/koziyagoukei/gba-gc-extension/blob/78b3125be6efc57d710f51c730e564111bcb4b3e/gamecube/battle/battle_items.c#L264)

| 条件 | Expansion参照試験 | GC |
|---|---|---|
| Single。サンパワーの晴れダメージ（90→70）でオボンが保留されたまま反動待機 | 反動メッセージ → オボン（70→110） → 相手の行動 → ターン末のサンパワー（110→90）。オボンは消費、PPは1回分だけ減る | 補修前は `EV_RECHARGE` → 相手の行動 → オボンで**不一致**。補修後は `EV_RECHARGE` → オボン → 相手の行動。Single／Double／Triple／Rotationすべて同じ |
| 持ち物なし、きんちょうかん（GCでは `RULE_IGNORE_ITEMS` の正規化） | 何も発動しない | 何も発動しない（持ち物なし、`RULE_IGNORE_ITEMS`）。厳密モードでもオボンは同じ順で発動する |
| Double。待機する個体、味方、相手の3体にオボンが保留 | 待機する個体 → 味方（自分右） → 相手（相手右） → 次の行動 | 同順 |
| 反動待機の前にひんし | 行動枠はスキップされる。反動なし、きのみは残る | 同じ（`TA_SKIPPED`） |
| 状態異常きのみ | このビルドで状態異常を付与する処理は、すべてその場で持ち物を判定する（あくびの眠りも `tryactivateitem`）。そのため待機ターンまで保留された状態異常は作れず、実行比較の対象外 | ネイティブ試験で状態を直接設定。待機ターンにクラボが発動し、反動 → 治療 → 相手の行動の順 |
| 比較：睡眠での行動不能 | 同じ保留オボンが睡眠の後、次の行動の前に発動する | 同順（`test_v19_more` の lost turn） |

Dolphin L72（Single、開始時の晴れ）では、GC画面に次の順で表示された。

1. 「Must recharge! リザードン」（HP 56/118）
2. 「Restored HP with its held item. リザードン オボンのみ」（HP 85/118）
3. 「Move used カビゴン」「But it failed!」

PPは4/5で、ターン末のサンパワーにより71/118になった。[Expansion参照試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/329ceb28ad48f52c60803e6beb4686260bca1f3f/test/battle/gc_recharge_item_reference.c#L24) · [GCネイティブ試験](https://github.com/koziyagoukei/gba-gc-extension/blob/78b3125be6efc57d710f51c730e564111bcb4b3e/gamecube/battle/battle_test.c#L8867) · [Lab L72](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/frontier_gc_battle.c#L2239)

範囲の注記：Expansionは選択対象の未処理の状態異常を行動者より先に判定する（`MOVEEND_ITEM_EFFECTS_TARGET`）が、GCは行動者を先に判定する。この差は選択対象に未処理の状態異常がある場合にだけ現れる。上の理由で現行の対応範囲では到達しないが、すべての行動不能経路に共通する差として [未確認事項](open-questions.md) に残す。V18のターン末HP持ち物は別経路なので、今回は変更していない。
