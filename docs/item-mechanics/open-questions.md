# 未確定事項・次に追加する参照試験

## ITM-VERIFY-001

| 優先 | 確認対象 | 参照試験に固定する結果 |
|---|---|---|
| 済（B1c-0a） | いのちのたまの補正と技終了HP減少 | **実行済み**：Expansion参照22件がPASS（固定コミットから生成したheadless ELF、メッセージ照合なし）。最終補正は `UQ_4_12_FLOORED(1.3)`＝5324を最終補正の積に入れ一度だけ半切り捨てで適用（5325なら878のところ877）、Multiscaleとの積は速い・遅いとも2662。HP減少は技につき一度、最大HP÷10（最低1）。外れ・無効・吸収・まもる・変化技・反動待機・行動中のひんしでは発生せず、みがわり・範囲・連続・ドレイン・反動・交代技では発生（反動の後、交代の前）。マジックガードは補正維持・減少なし、ちからずく適用技は減少なし、ぶきようは両方なし、いえきで抑制すると通常どおり。0ダメージ扱い（みねうち）と相手全滅後は未実施。[参照試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/9601bc98afc3ae96d9df3b4b76b04d316f1062a5/test/battle/gc_life_orb_reference.c#L47) · [仕様と設計](../items.md#13-m4-b1c-0a-life-orb-specification-and-reference-no-implementation) |
| 高 | ピボットと保留きのみ | 前ターンの日照で次動作まで保留された交代技使用者のオボン。入場・HP・消費・交代・控えの状態を順番で比較。通常の被弾きのみ、強制交代、反動待機と合わせて回帰。既知のソース差の組み合わせは実行未検証 |
| 高 | メット・被弾反応 | 接触特性との順、対象ひんし、攻撃者ひんし、後続打ミス、身代わり、ばけのかわ。じゃくてんほけんは上限、弱点と対象生存、みがわりの有無 |
| 済（B1c-1a） | すずとダメージの集約 | **実行済み**：Expansion参照34件PASS（2回同値、誤期待値9件で失敗を確認）。基準は技全体の実際のHP損失（全ヒット・全対象・味方込み、ひんし対象はその残HP、みがわりは壊れる時の残HPまで）の合計÷8、合計後に切り捨て、最低1、最大HPまで、技終了に1回。ドレイン・接触特性・反動・じしんかじょう・対象のHPきのみの後、とんぼがえりの交代・わるいてぐせの前。奪取直後の発動は参照のみ（B2）。[仕様](../shell-bell.md) |
| 中 | 選択・強制交代・再入 | チョッキやこだわりの選択／実行時の差、再選択と動的速度。カード／ボタンは複数所持者、交代不可の個別条件、相手全滅、ピボット、交代予約を競合させる |
| 中 | B2の着脱と履歴 | ふうせん、やきつくす、腐食、はたきおとす、交換、しゅうかく、はんすう、きょうせい。抑制解除後の同一品再評価・終了時の復元。原品がきのみ／非きのみ／空きを比較 |
| 継続 | 形式・版・同期 | Single/DoubleでExpansion参照。GC側ではTriple/Rotationを含む4形式で位置・順・同一個体の引継ぎを固定。V1〜V19のcodec/固定seedを維持。Strict拒否、切断、再入、結果再送も別に確認 |

`SOURCE_CONFIRMED`（試験設計の根拠）。反動補修は対象ケースで `TEST_CONFIRMED`。B1c候補の複合条件は `UNKNOWN`。Expansionにない形式での発動順はGCの設計仕様として扱う。[MoveEndの順序表](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_move_resolution.c#L5483) · [ACT-002回帰](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/gamecube/battle/battle_test.c#L8867) · [既知のPivot制限](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/docs/items.md#L805)

## ITM-VERIFY-002

既存headless ELFに対し、B1c候補の名前で試験を選び再実行した。今回の実行結果は、同じ範囲のGC実装を裏付けるものではない。

| 対象 | 結果 | 未確認理由 |
|---|---:|---|
| Life Orb | 7/10 PASS | 成功、みがわり、0ダメージ扱いの3件が英語のHP減少メッセージで `Unmatched MESSAGE`。B1c-0aで言語に依存しない参照22件に置換し22/22 PASS（[参照試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/9601bc98afc3ae96d9df3b4b76b04d316f1062a5/test/battle/gc_life_orb_reference.c#L47)） |
| Shell Bell | 9/15 PASS | 失敗6件（未来攻撃4、どろぼう／ほしがる、マジシャン）はすべて英語メッセージ照合。B1c-1aで言語に依存しない参照34件に置換し34/34 PASS（[参照試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/frontier-gc-m4b1c1a/test/battle/gc_shell_bell_reference.c)） |
| Rocky Helmet | 1/2 PASS | 後続打ミス例が英語の発動メッセージ照合で失敗 |
| Weakness Policy | 1/1 PASS | ばけのかわによるダメージ遮断の既存例。通常発動と能力上限には追加が必要 |

メッセージ照合失敗は保留し、B1c着手時に固定seed、HP変化、能力ランク、所持・消費、および言語に依存しない通知順で参照を取り直す。Expansion側の現行ソースは変更していない。[Life Orb試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/hold_effect/life_orb.c#L9) · [未来攻撃の試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/hold_effect/shell_bell.c#L99) · [後続打ミスの試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/hold_effect/rocky_helmet.c#L24) · [Weakness Policy試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/hold_effect/weakness_policy.c#L9)

## ITM-VERIFY-003

品目・効果列挙値は静的に全件作成した。定義の全バリエーションを実際に発動させる網羅試験、全 `ITEM_*` の直接参照（技データ外・種族別を含む）と組み合わせの完全監査、全施設の永続ポリシーは継続対象。効果表のファイル先頭参照は入口なので、実装候補を増やすたびに呼び出し条件・世代分岐・試験を個別に追記する。[gItemsInfo](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/data/items.h#L156) · [ItemBattleEffects](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_hold_effects.c#L1020) · [AIが評価する持ち物関連技](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_ai_main.c#L2230)
