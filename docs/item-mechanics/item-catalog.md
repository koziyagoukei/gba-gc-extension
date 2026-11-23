# 品目と効果のカタログ

## 集計

| 範囲 | 件数 |
|---|---:|
| 明示されたitem ID（`ITEM_NONE` を含む） | 874 |
| 実品目（`ITEM_NONE` を除く） | 873 |
| ビルドの定義上 `HOLD_EFFECT_NONE` 以外の品目 | 366 |
| `HoldEffect` の種類（`NONE` とCOUNTを除く） | 125 |
| 現行品目に割り当てられた非NONE効果の種類 | 125 |
| 明示的な `flingPower` がある品目 | 554 |
| フォルム表にシンボルが現れる品目 | 169（うちNONE 12） |
| Frontierに出現する品目の種類 | 190 |

`SOURCE_CONFIRMED`。125は成長・逃走・報酬等の保持効果も含む列挙値の数。発動回数、戦闘中に持てる個数、GC対応品目数とは分母が異なる。12品目はフォーム表の静的な参照で、対象種族、ビルド設定、時点で発動の可否が変わる。くちたけん・くちたたてはNONEでも戦闘開始時に種族と技を変える。通常効果のNONEと所持の有無を別に扱う。[ITEMS_COUNT](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/include/constants/items.h#L1055) · [HoldEffect](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/include/constants/hold_effects.h#L5) · [gItemsInfo](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/data/items.h#L156) · [ザシアン・ザマゼンタの戦闘フォーム](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/data/pokemon/form_change_tables.h#L1992)

## 台帳の使い方

- [items.csv](items.csv) は全ID、取得できる原文名、ポケット、hold effect、パラメータ式、secondary ID、Fling威力欄、バッグ使用欄、分類タグ、V19の品目ゲート、行番号を持つ。英語の正式名がソースにない品目は空欄。ID別名は元のIDへ解決。 `item_block_has_cpp` は条件付き定義が入ったブロック。主な既定分岐を解決し、値が式の項目は式を残す。Fling欄の空欄は `GetFlingPowerFromItemId` が実行時に決める場合がある。品目ゲートは所持者・対戦全体を検証する前の判定。
- [effects.csv](effects.csv) の `effect_id` は当該コミットの列挙値。安定した仕様番号はタグ（下表）と効果シンボルの組で参照する。タグは重複可。ハンドラ選択フラグ・消費の典型・処理参照・候補試験を記録する。複数サイトのうち各ファイルの最初の参照を収録。似た名の試験は入口であり完全性は [GC台帳](gc-compatibility.md) に従う。
- `ItemBattleEffects` を経由しない補正・制限・無効判定がある。 `src/data/hold_effects.h` のTRUEフラグはディスパッチの入口を示す。能力や技に組み込まれた持ち物の意味は [技・特性](moves-and-abilities.md) を参照。[gHoldEffectsInfo](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/data/hold_effects.h#L1) · [ItemBattleEffects](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_hold_effects.c#L1020) · [GetFlingPowerFromItemId](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_util.c#L9479)

| 仕様ID | 効果群・代表例 | 最初に確認する処理 |
|---|---|---|
| ITM-DAMAGE | タイプ強化、たつじんのおび、いのちのたま、ジュエル、半減きのみ | 威力か最終ダメージか、対象ごとの発動と消費。[最終ダメージ補正](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_util.c#L7811) |
| ITM-STATS / ITM-ORDER | こだわり、とつげきチョッキ、しんかのきせき、せんせいのツメ、こうこうのしっぽ | 計算用の能力値と選択制限、同一優先度内の順序。[選択制限](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_util.c#L1640) · [順序変更](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_main.c#L5167) |
| ITM-ACCURACY / ITM-CRITICAL | こうかく・フォーカスレンズ、ひかりのこな、ピントレンズ | 命中用の乗算と行動済み判定、急所段階とRNG。[命中補正](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_util.c#L10833) · [急所設定](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/include/config/battle.h#L5) |
| ITM-HP / ITM-CURE / ITM-ENDURE | オレン・オボン・ラム、メンタルハーブ、タスキ・ハチマキ、かいがらのすず | しきい値・実被害・履歴・生存条件。[ItemHealHp](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_hold_effects.c#L805) · [GetAdjustedDamage](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_util.c#L8285) |
| ITM-HIT / ITM-MOVE-END / ITM-STAGE | ゴツゴツメット、じゃくてんほけん、しろいハーブ、PPのきのみ | 被弾側、行動者側、技全体、能力変化等の別段階。[MoveEnd順序表](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_move_resolution.c#L5483) |
| ITM-SWITCH / ITM-FIELD / ITM-RESIDUAL | レッドカード、ボタン、ブーツ、各種延長岩、たべのこし、どくどくだま | 交代予約、範囲・持続・接地・残留。[カード・ボタン](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_move_resolution.c#L4839) · [ターン末順序表](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_end_turn.c#L1614) |
| ITM-SPECIAL / ITM-FORM / ITM-GIMMICK | おんみつマント、パンチグローブ、フォルム品、メガ・Z・ゲンシ | 接触、発動資格、個体別制約、表示・復元。[着脱可否](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_util.c#L9114) |
| ITM-PRESENCE / ITM-FLING / ITM-BAG / ITM-POLICY | 所持の有無、なげつける、バッグ、報酬・成長・逃走 | 所持効果と所持参照、バッグ、戦闘外の契約を別に判定。[きのみジュースのバッグ・所持の両定義](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/data/items.h#L1149) |

対象資料の既定はGEN_9。オボンは25%、フィラ系の既定しきい値は1/4・回復1/3、通常タイプ強化20%、ジュエル30%。設定式と戦闘ルーチンの両方を確認する。[持ち物設定](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/include/config/item.h#L9) · [混乱するきのみの世代設定](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/include/config/battle.h#L249) · [オボンの条件付き定義](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/data/items.h#L11204)

## 再生成

Python 3.10以降で `python -B catalog.py <Frontier作業ディレクトリ> items` をこのディレクトリから実行する。最後の引数は `items`、`effects`、`frontier`、`frequency`、`summary`。固定コミットのGitオブジェクトを読み、CSVまたは集計を標準出力へ出す。ビルド、チェックアウト、作業ツリー内のデータ変更は不要。単純なCの初期化子に限定した解析で、PP・品目の実行時の状態や一般のC式を評価しない。
