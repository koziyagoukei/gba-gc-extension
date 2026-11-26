# Frontierの出現頻度と実装順

## ITM-PLAN-001

`TEST_CONFIRMED`。既存のExporter集計を対象のheadless ELFで再実行した。2,040セットのうち Request生成1,996、持ち物を正規化せず開始可能992、指定の技・特性を含む全条件／厳密モード108。計測は各セットを先発とし、対戦相手は持ち物なし・たいあたりのカビゴン、Single。対戦の頻度、複数形式の開始成功、今後の追加分はこの計測に含まれない。[GC V19 export tally](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/gc_consumable_export.c#L243) · [M4-B1b集計結果](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/docs/items.md#L795)

`SOURCE_CONFIRMED`（生の出現数）／`INFERRED`（上限）。下表の「品目だけ」は、全出現を効果対応にできた場合の上限。「品目＋特性の上限」は、テンプレートの特性が `ItemAbility` に列挙されるセットを除外。「厳密の上限」は、さらに `ResolveEffects` のswitchに入らないことを静的に検出できる技を1つ以上持つセットを除外。後二列は上限の絞り込みで、実際の増分は0からこの範囲にある。技フラグ、対応する追加効果、個体化後の特性、他の正規化条件、Request生成時の選別は別途効く。新しい品と一緒に技・特性まで対応した場合にはこの上限も変わる。複合ロスターの成功率には外挿しない。[ResolveEffects](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/frontier_gc_battle.c#L639) · [ItemAbility](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/frontier_gc_battle.c#L1700) · [CheckItems](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/frontier_gc_battle.c#L1796)

| 品目 | 出現＝品目だけの上限 | 品目＋特性の上限 | 厳密の上限 | 主な残件・コストと優先順 |
|---|---:|---:|---:|---|
| いのちのたま | 142 | 141 | 56 | ふいうち・はたきおとす・はねやすめ、ねんちゃく。中、B1c初回 |
| こだわりスカーフ | 119 | 114 | 40 | トリック等、おみとおし・じんばいったい。大、B1d |
| とつげきチョッキ | 95 | 91 | 37 | はたきおとす・ふいうち、ねんちゃく等。中、B1dの選択制限 |
| あつぞこブーツ | 79 | 77 | 11 | はねやすめ・こうそくスピン、おみとおし等。大、設置技基盤待ち |
| ゴツゴツメット | 73 | 72 | 7 | ステルスロック・ボディプレス、おみとおし。中、B1c被弾 |
| こだわりハチマキ | 62 | 59 | 25 | はたきおとす・おんがえし、おみとおし等。大、B1d |
| こだわりメガネ | 52 | 49 | 21 | めざめるパワー・フリーズドライ、きんちょうかん等。大、B1d |
| しんかのきせき | 30 | 28 | 4 | ねむる・ナイトヘッド、ねんちゃく。中、受動補正を分離 |
| ブーストエナジー | 20 | 20 | 7 | はたきおとす・アンコール。大、該当特性と場の寿命待ち |
| ひかりのねんど | 17 | 16 | 0 | 壁技、おみとおし。中、場の壁対応待ち |
| しろいハーブ | 15 | 13 | 8 | アクロバット等、かるわざ。中〜大、能力変化段階の構築後 |
| ふうせん | 9 | 9 | 2 | 設置技と接地・破裂。大、B2前後で独立 |
| じゃくてんほけん | 7 | 7 | 3 | ボディプレス等。中、B1c被弾後の能力変化 |
| せんせいのツメ | 2 | 2 | 2 | 残る技・特性判定、順序と乱数の固定。中、B1d |
| レッドカード | 3 | 3 | 0 | 強制交代競合、ボディプレス等。大、保留 |
| かいがらのすず | 0 | 0 | 0 | 今回のセット増分は0。技全体のダメージ集約が必要。中、B1cの次段。B1c-1aで再確認：Frontier 2,040セットは0（Battle Tent 1、Trainer Hill 8は対象外）。 |
| たつじんのおび | 1 | 1 | 0 | 最終補正の効果条件。小、補正拡張の対照用 |

[出現頻度CSV](frequency.csv) に全190品目と検出した主な制約、[セットCSV](frontier.csv) に個体セットを収録。フォーム・ギミック品の一部は種族とロスター次第なので、確定未対応として足さない。静的な未対応表示の品目では870セットが検出された。残る対戦全体の正規化・未生成はこの集計値に含まれない。

## ITM-PLAN-002

| 段階 | 対象と範囲 | GCとExporter／プロトコル | 必須の参照比較 |
|---|---|---|---|
| B1c-0 | **B1c-0a済み（仕様・参照22件・V20設計、実装なし）**。いのちのたまのみ。技の最終補正と技1回分のHP減少を一組で提供 | `BMSRC_ITEM` はV19で威力段階のみ。次版で最終補正段階の品ソースを解禁し、`BMOD_FLOORED` で1.3の4.12丸めを再利用。技コンテキストに命中／被弾記録と実行可否、一度だけ行う技終了Hook。HP減少の分母と条件は正規化して出力。マジックガード・ちからずくが未実装／未出力なら厳密には拒否。固定ダメージ、未来攻撃、踊り子等の未対応技・能力は現行ゲートを維持 | 成功、失敗、溜め、反動待機、みがわり、範囲・連続、行動者ひんし。ちからずくによる後半スキップとマジックガードはExpansion参照を先に固定 |
| B1c-1 | 技終了のかいがらのすず。受動補正だけのたつじんのおびは別に試験 | すずは別の値源（技全体の実ダメージ量／分母）と回復可否。生存者、耐久・みがわり、最低回復、反動・ドレインの後、交代の前を独立に実証。たつじんは実効相性の述語 | 2〜5ヒット、複数対象、過剰ダメージ、0／1ダメージ、みがわり、満タン、回復禁止。盗んだ当該技で発動するケースはB2。**B1c-1a済み（仕様・参照34件・失敗先行17件、実装なし）**：値源は既存 `MoveContext.lost`（技全体・全対象・味方込み・過剰分除外・みがわりは残HPまで）でExpansionの `savedDmg` と一致。V21で `BIHP_HEAL_DEALT` を技終了に追加する案。[仕様](../shell-bell.md) |
| B1c-2 | ゴツゴツメットを被弾側Hookの初例にする | ヒット側の接触・HP事実、ひんし直前後、対象から行動者への間接ダメージ、パッド・えんかく等のゲート。対象がひんしでも発動する例を維持 | さめはだとの相対順、後続打ミス、身代わり、所持者・攻撃者のひんし、範囲＋接触 |
| B1c-3 | じゃくてんほけん、対応できる単純な被弾能力きのみ | 実際の被弾・相性・技カテゴリ、既存能力段階処理と消費を組む。双方の段階上限、所持者の生存を検証。耐性きのみはダメージ前後の二段階なので別にする | 弱点、無効、身代わり、ばけのかわ、両方上限／片方上限、連続被弾 |
| B1d | チョッキ、こだわり3種、次にツメ・こうこうのしっぽ・イバン | チョッキは特防と技制限を同時提供。こだわりは選択・実行・交代で固定スロットを検査。ツメ等は順序グループ／乱数を倍率と分ける。合法行動、UI、AIと、再ソート時の消費／表示を同期 | 交代、失敗・強制実行、技枠変更、速度同値、トリックルーム、優先度技、Shift／Rotation |
| B2 | 制約＋除去 → 奪取・交換 → 再取得と抑制を分割 | 現在品の系譜と理由、受渡し双方向の資格、履歴、動的効果の再解決。カードとボタンは別の強制交代競合段階。Resultの永続化は通常戦の契約を実装する時点 | 同じ技で交換・取得した品が発動する例、復元・消費・破壊を区別。B1dの固定解除、引継ぎ、再接続も確認 |

`INFERRED`（設計）。「小」は既存入口を使う補正中心、「中」は新しいチェックポイント＋参照ケース、「大」は複数の状態遷移と既存UI・AI・Result等の検証を伴う。B1cの初回に持ち物交換、カード／ボタン、ふうせん、こだわり、全天候・フィールド品を入れない。既存形式のPivotを止める変更の前に、後続の行動へ再開する既存試験を固定する。[最終補正の定義](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_util.c#L7811) · [すずといのちのたまの条件](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_hold_effects.c#L494) · [技終了の専用段階](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_move_resolution.c#L4877) · [品ソースの許可ステージ](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/protocol/battle_protocol.c#L105) · [既存4.12変換](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/gamecube/battle/battle_modifiers.c#L120)

通信は追加レコードを既存上限の4件に収めれば対戦パケット形式の上限を増やす必要がない。1個体に1件追加で最大12個体×8 B＝96 Bの増分、実際のJoyBusフレーム境界で再計測する。V19までのレコードと意味は維持し、新しい種類／ステージは次の版とcapabilityで拒否・交渉する。B2の系譜や結果の永続化はこの見積もりの外。[持ち物ブロックとレコード長](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/protocol/battle_protocol.h#L447) · [最大Requestの式](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/protocol/battle_protocol.h#L659)
