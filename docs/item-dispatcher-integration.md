# B1c-0bへの統合計画

設計基準は [M4-B1c-0a §13](items.md#13-m4-b1c-0a-life-orb-specification-and-reference-no-implementation)。以下は **INFERRED** の組み込み計画。今回の試作は [独立したNativeモデル](https://github.com/koziyagoukei/gba-gc-extension/blob/b4391860363b98bf603f7a35a4bc323e72f00e6f/prototype/item-dispatcher/README.md) を対象とする。

## 1. V20のcodecと交渉を先に通す

| 変更候補 | 内容 |
| --- | --- |
| `protocol/battle_protocol.h`、`protocol/battle_protocol.c` | Request/APP V20を追加する。既存のレコード長8バイト、最大4件、Requestヘッダー、ActionとResultのレイアウトを維持する。 `item_record_valid`、`battle_item_valid` のV20分岐を共用し、encoder、decoder、直接渡す `battle_init` のすべてで検査する。 |
| `gamecube/companion/party_view.c`、`battle_view.c` | rosterのAPP上限とActionの版分岐にV20を加える。V6以来のActionレイアウトと既存EV表示を使用する。CAPS、mode照会、roster・Request・APPの版を組み合わせて確認し、V19までのpeerへ新規レコードを送らない。release照合は既存方式を使う。 |
| Frontierの `src/frontier_gc_battle.c` と共有プロトコル相当ファイル | `ResolveItem` が品目の意味を解決し、`FillEntry` と `BuildRequest` が対応するroster／APP版だけでV20のレコードを送る。旧版への参加可否はV19までの拒否・正規化方針に従う。旧クライアントへのフォールバックで効果の黙示的な喪失を新たに許さない。 |

- 最終補正には `BITEM_MODIFIER`、`BMOD_FINAL`、`BMOD_FLOORED`、百分率130を使用。既存の百分率・述語・フラグの検証を流用する。変換結果は5324。
- 技終了には `BITEM_HP`、`BITIME_AFTER_MOVE = 5`、`BIHP_DAMAGE`、除数2〜255、述語なし、消費フラグと予約バイトは0。いのちのたまは除数10。補正とHP減少の両レコードを別々に並べられる。終了時HP減少は1品につき1レコードに絞る。複数件を許す時点で評価順と個別の発動単位を決める。
- 新種のレコードやstageをV19以下で検出した場合は、現行通り拒否する。V20でデコードに成功しても、戦闘初期化を直接呼ぶ場合に同じ条件で拒否できるようにする。サイズ上限は既存の `BATTLE_REQUEST_MAX_BYTES` を維持する。いのちのたま1品当たり既存アイテムブロックに16バイト、12体なら最大192バイトのレコード量。持ち物なしから比較する場合、該当版の空ブロックはもともと存在する。224バイトのフレーム数はセッションのチャンクヘッダー込みで実測する。新しい持続的メモリ、Action、Result、EventLogのwire形式は不要。

[版・レコード定義](https://github.com/koziyagoukei/gba-gc-extension/blob/3269b7c8cbdd0e87ef539d5ff1baaeefe1566af0/protocol/battle_protocol.h#L447) · [検証関数](https://github.com/koziyagoukei/gba-gc-extension/blob/3269b7c8cbdd0e87ef539d5ff1baaeefe1566af0/protocol/battle_protocol.c#L482) · [APP roster上限](https://github.com/koziyagoukei/gba-gc-extension/blob/3269b7c8cbdd0e87ef539d5ff1baaeefe1566af0/gamecube/companion/party_view.c#L186) · [Frontierの品目解決](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/9601bc98afc3ae96d9df3b4b76b04d316f1062a5/src/frontier_gc_battle.c#L1590)

## 2. 既存Modifierと技の終了点を接続する

1. `battle_modifiers.c` のV20最終パスに既存の `run_items` を配線する。速い側は MOVE → ATTACKER → DEFENDER → ITEM、遅い側は MOVE → DEFENDER → ATTACKER → ITEM。ほかの段階とV14〜V19のパスはそのまま。V17以降の `passes` がITEMを含める段階を確認し、V20のFINALだけ追加する。倍率を途中でHPへ適用せず既存の4.12チェーンに合成する。
2. `battle_items.c` と `battle_internal.h` に `bc_item_after_move` を追加する。既存 `bc_item_next` で有効な現在品を検索し、現在も元の使用者が生存・在場か、ダメージ成立済みかを確認する。発動したら `bc_apply_damage` に `EV_ITEM_DAMAGE` と最小1の減少量を渡す。消費・履歴は更新しない。`RULE_IGNORE_ITEMS` は既存の品取得段階で処理する。HP減少で使用者が倒れた場合は既存のひんし・補充・勝敗判定経路を使用する。
3. `battle_effects.c` の `execute_move` は、反動→行動者のV19チェック→`bc_ability_after_move` の既存順のあとに新規フックを呼ぶ。終了点が一度しか到達しない同期処理なら試作の一度フラグは不要。範囲技や連続技は `c.landed` と実損失を使い、みがわりも含めて1回とする。反動待機予約と交代候補確定の前にHPを反映する。`battle_switch_mask` は生存を要求するため、使用者が倒れればピボットで交代を要求せず、通常の補充へ進むことを実戦試験で確認する。関数 `bc_execute_move` の既存双方チェック、`continue_turn` の ACT-002 呼び出しは保持する。
4. `battle_core.c` の `execute_struggle` は別経路。同じフックを追加する案を維持し、参照試験で成立する条件・補正・自身の反動との順序を先に固定する。PP切れは実戦中に発生するため、正確性を標榜する場合はこれをV20出荷条件に含める。

[Modifierの現行パス](https://github.com/koziyagoukei/gba-gc-extension/blob/3269b7c8cbdd0e87ef539d5ff1baaeefe1566af0/gamecube/battle/battle_modifiers.c#L200) · [技終了と行動終了](https://github.com/koziyagoukei/gba-gc-extension/blob/3269b7c8cbdd0e87ef539d5ff1baaeefe1566af0/gamecube/battle/battle_effects.c#L827) · [交代の生存チェック](https://github.com/koziyagoukei/gba-gc-extension/blob/3269b7c8cbdd0e87ef539d5ff1baaeefe1566af0/gamecube/battle/battle_core.c#L487) · [わるあがきと反動](https://github.com/koziyagoukei/gba-gc-extension/blob/3269b7c8cbdd0e87ef539d5ff1baaeefe1566af0/gamecube/battle/battle_core.c#L778)

## 3. GBAで組み合わせを検証する

`CheckItems` と `HpItemAbility` に、新規HP減少レコードとマジックガードの組み合わせの試験を追加する。ぶきようは既存 `ItemAbility`、ちからずくは技終了HP減少レコードの所持者に対する拒否条件を追加する。マジックガードおよびちからずくの実際の戦闘効果が入るまでは、厳密ルールで拒否し、非厳密ルールの持ち物全体の正規化は既存の通知付き経路を使う。ちからずくの影響は適用技ごとに違うため、全技をスキップする実装は導入しない。かがくへんかガスは非対応特性として厳密ルールで拒否し、非厳密では宣言された正規化範囲に従う。いえきは現行Exporterの対応外。試作の抑制入力はこれらの特性効果が既に解決された場合の判定例。

GC側は未知レコードや不正な組み合わせを拒否する。正規化されたデータから未転送のROM固有特性を推定できないため、FrontierはRequest生成前に両陣営を検査する。既存の正規化が発生した場合と、同じ条件を完全再現できる場合を、集計と表示上で区別する。

[アイテムと特性の判定](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/9601bc98afc3ae96d9df3b4b76b04d316f1062a5/src/frontier_gc_battle.c#L1704) · [ExpansionのTryLifeOrb](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/9601bc98afc3ae96d9df3b4b76b04d316f1062a5/src/battle_hold_effects.c#L515) · [特性の監査](ability-mechanics/items-and-moves.md)

## 4. 試験の移行と出荷条件

| 段階 | 次に必要な検証 |
| --- | --- |
| V20受付 | `sh tools/v20-tests.sh` は `GC_TEST_V20` を指定して失敗先行のグループを有効化する。試験内の仮定数を製品ヘッダーに置き換える。`v20_start` のencode→decode→initを段階別に確認し、各ケースが生成・初期化を通過して本来の効果アサーションまで到達することを検証する。現在の「14 of 14」はRequestで止まった件数。 |
| codecと能力 | V19以下の拒否・V20境界（除数1/2/255、予約バイト、述語・消費フラグ、終了時レコード重複、最大Request）、GC直初期化でも同一検査、厳密拒否と非厳密の正規化を両陣営で実施する。古いAPPで開始・拒否・結果反映を確認する。 |
| 算術と順序 | 無補正55/51/46→71/66/60、675→877、みがわり、範囲各対象、連続各ヒット、1回減少、外れ・無効・Protect・状態技・反動待機、反動と撃破特性、両者のひんし、ピボット直前のひんしと補充。素早さ両順で攻撃側・防御側の補正も混ぜる。マジックガード・ちからずくの適用技と非適用技・ぶきよう、先に持ち物が消費されたケースも含める。 |
| 参照・旧版 | [Expansionの22件](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/9601bc98afc3ae96d9df3b4b76b04d316f1062a5/test/battle/gc_life_orb_reference.c#L47) の値とイベント順を比較する。対象全滅後、0ダメージ扱い、わるあがき、途中退場、合成補正、ダイマックスは保留。V1〜V19の既存Native、V12〜V19の固定seed、Lab L01〜L72を再実行する。 |
| Dolphin・JoyBus | 4形式でHP減少表示、対象ひんし→使用者の減少、再充電、Shift／Rotation、ピボット、切断→復帰→再戦を確認する。V20導入前後のRequest転送量・再送・最大poll間隔を比較し、イベント増加中もpollが継続すること、結果nonceの二重反映防止、持ち物の永続保存に新たな変更が入らないことを確認する。 |

### 既存V20案との関係

wire、5324、技終了窓、既知差分の扱いは§13と同じ。本計画では、同品の技終了HPレコードを1件以下へ制約し、同期の呼び出し1回で十分な間は試作の一度フラグを本番の永続状態に加えない。わるあがきは§13の共通フック案を維持し、参照確認を出荷条件にした。ABL-G04の致死接触通知順とITM-TIME-003のきのみ順は別作業とする。ABL-G05の複合Hook拒否はV12〜V19で既存の受理条件を変更した例外であり、それ以降の基準として維持する。ACT-002の反動待機時のきのみ判定は今回のHP減少フックと独立して維持する。

[失敗先行スクリプト](../tools/v20-tests.sh) · [互換性台帳](battle-reference/compatibility-matrix.md) · [実行済みの回帰](https://github.com/koziyagoukei/gba-gc-extension/blob/b4391860363b98bf603f7a35a4bc323e72f00e6f/prototype/item-dispatcher/README.md#今回の検証)

## B1c-0bでの採用結果（TEST_CONFIRMED）

本ファイルと [設計比較](item-dispatcher-design.md) は、`prototype/item-dispatcher`（b439186）からファイル単位で取り込んだ（`git checkout b4391860363b98bf603f7a35a4bc323e72f00e6f -- docs/item-dispatcher-design.md docs/item-dispatcher-integration.md`）。mergeや試作コードの移植はしていない。試作の `prototype/` は同ブランチに残し、上のリンクはそのコミットを指す。

| 計画 | 採用 | 実装 |
| --- | --- | --- |
| §1 V20 codec・交渉 | 採用 | `BITIME_AFTER_MOVE`、品の最終補正をV20に限定。encoder・decoder・`battle_init` は共通の `battle_item_valid`。GCはAPP V20を受け、V19以下は新規レコードを拒否。[検査](https://github.com/koziyagoukei/gba-gc-extension/blob/0c6a37e4639cd9aa823b36c0f890d87f72e132ff/protocol/battle_protocol.c#L500) |
| §2-1 最終補正 | 採用 | V20の最終段階だけに `run_items` を追加（速い側 MOVE→ATTACKER→DEFENDER→ITEM、遅い側 MOVE→DEFENDER→ATTACKER→ITEM）。[パス](https://github.com/koziyagoukei/gba-gc-extension/blob/0c6a37e4639cd9aa823b36c0f890d87f72e132ff/gamecube/battle/battle_modifiers.c#L229) |
| §2-2 `bc_item_after_move` | 採用 | 既存 `bc_item_hp` に新タイミングを渡す。消費・履歴は更新しない。 [関数](https://github.com/koziyagoukei/gba-gc-extension/blob/0c6a37e4639cd9aa823b36c0f890d87f72e132ff/gamecube/battle/battle_items.c#L156) |
| §2-3 `execute_move` の呼び出し | 採用 | じしんかじょうの後、反動待機の予約・交代の前。同期の1呼び出しで技1回1度。試作の一度フラグは不採用（不要と試験で確認）。[呼び出し](https://github.com/koziyagoukei/gba-gc-extension/blob/0c6a37e4639cd9aa823b36c0f890d87f72e132ff/gamecube/battle/battle_effects.c#L838) |
| §2-4 わるあがき | 採用（参照後） | Expansionで補正あり・反動の後に減少・反動ひんしなら発動なしを確認してから `execute_struggle` に追加 |
| §3 GBAの組み合わせ検証 | 採用 | マジックガードは既存 `HpItemAbility`、ちからずくは技終了HPレコードの所持者に追加、ぶきようは既存 `ItemAbility`。いずれも全持ち物オフ（strictは拒否）。[Exporter](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/8219d703a08acc659a919709ba94ce0832da52ca/src/frontier_gc_battle.c#L1800) |
| 独立Dispatcher（A案） | 不採用 | B案どおり既存 `bc_item_*` の拡張のみ |
| 試作の抑制入力・一度フラグ・汎用Context | 不採用 | 現行経路で一度だけ到達し、特性効果はExporterの正規化で扱うため |
| §4 Dolphin・JoyBus | 未実施 | V20に届くLabがなく（一覧は満杯）、今回の範囲外。Native・Exporter・Expansion参照で確認 |

結果は [items.md §14](items.md#14-m4-b1c-0b-life-orb-in-request-v20)。
