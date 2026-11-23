# 未確定事項・次に追加する参照試験

## ITM-VERIFY-001

| 優先 | 確認対象 | 参照試験に固定する結果 |
|---|---|---|
| 高 | いのちのたまの補正と技終了HP減少 | 4.12で130%を切り捨て変換した固定ダメージ例と、最終補正の競合順。HP減少は技につき一度、最大HP由来。命中失敗・吸収・みがわり・0ダメージ扱い・反動待機・使用者ひんし・相手全滅。ちからずくとマジックガードを別々に固定 |
| 高 | ピボットと保留きのみ | 前ターンの日照で次動作まで保留された交代技使用者のオボン。入場・HP・消費・交代・控えの状態を順番で比較。通常の被弾きのみ、強制交代、反動待機と合わせて回帰。既知のソース差の組み合わせは実行未検証 |
| 高 | メット・被弾反応 | 接触特性との順、対象ひんし、攻撃者ひんし、後続打ミス、身代わり、ばけのかわ。じゃくてんほけんは上限、弱点と対象生存、みがわりの有無 |
| 中 | すずとダメージの集約 | 丸め、最小値、みがわり、最終打、複数対象と過剰ダメージ、ドレイン、取り外される前後、未来攻撃の除外。B2で奪取直後の発動も追加 |
| 中 | 選択・強制交代・再入 | チョッキやこだわりの選択／実行時の差、再選択と動的速度。カード／ボタンは複数所持者、交代不可の個別条件、相手全滅、ピボット、交代予約を競合させる |
| 中 | B2の着脱と履歴 | ふうせん、やきつくす、腐食、はたきおとす、交換、しゅうかく、はんすう、きょうせい。抑制解除後の同一品再評価・終了時の復元。原品がきのみ／非きのみ／空きを比較 |
| 継続 | 形式・版・同期 | Single/DoubleでExpansion参照。GC側ではTriple/Rotationを含む4形式で位置・順・同一個体の引継ぎを固定。V1〜V19のcodec/固定seedを維持。Strict拒否、切断、再入、結果再送も別に確認 |

`SOURCE_CONFIRMED`（試験設計の根拠）。反動補修は対象ケースで `TEST_CONFIRMED`。B1c候補の複合条件は `UNKNOWN`。Expansionにない形式での発動順はGCの設計仕様として扱う。[MoveEndの順序表](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_move_resolution.c#L5483) · [ACT-002回帰](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/gamecube/battle/battle_test.c#L8867) · [既知のPivot制限](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/docs/items.md#L805)

## ITM-VERIFY-002

既存headless ELFに対し、B1c候補の名前で試験を選び再実行した。今回の実行結果は、同じ範囲のGC実装を裏付けるものではない。

| 対象 | 結果 | 未確認理由 |
|---|---:|---|
| Life Orb | 7/10 PASS | 成功、みがわり、0ダメージ扱いの3件が英語のHP減少メッセージで `Unmatched MESSAGE` |
| Shell Bell | 9/13 PASS | 未来攻撃4件が英語メッセージ照合で失敗。同ファイル末尾の奪取・マジシャンのケースは今回の名前フィルタの結果に含まれていない |
| Rocky Helmet | 1/2 PASS | 後続打ミス例が英語の発動メッセージ照合で失敗 |
| Weakness Policy | 1/1 PASS | ばけのかわによるダメージ遮断の既存例。通常発動と能力上限には追加が必要 |

メッセージ照合失敗は保留し、B1c着手時に固定seed、HP変化、能力ランク、所持・消費、および言語に依存しない通知順で参照を取り直す。Expansion側の現行ソースは変更していない。[Life Orb試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/hold_effect/life_orb.c#L9) · [未来攻撃の試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/hold_effect/shell_bell.c#L99) · [後続打ミスの試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/hold_effect/rocky_helmet.c#L24) · [Weakness Policy試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/hold_effect/weakness_policy.c#L9)

## ITM-VERIFY-003

品目・効果列挙値は静的に全件作成した。定義の全バリエーションを実際に発動させる網羅試験、全 `ITEM_*` の直接参照（技データ外・種族別を含む）と組み合わせの完全監査、全施設の永続ポリシーは継続対象。効果表のファイル先頭参照は入口なので、実装候補を増やすたびに呼び出し条件・世代分岐・試験を個別に追記する。[gItemsInfo](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/data/items.h#L156) · [ItemBattleEffects](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_hold_effects.c#L1020) · [AIが評価する持ち物関連技](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_ai_main.c#L2230)
