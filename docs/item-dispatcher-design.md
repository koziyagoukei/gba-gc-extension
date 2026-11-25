# 共通Item Hookの設計比較

基準は GC `3269b7c8cbdd0e87ef539d5ff1baaeefe1566af0` と Frontier `9601bc98afc3ae96d9df3b4b76b04d316f1062a5`。実装根拠は **SOURCE_CONFIRMED**、参照試験や今回実行した試験は **TEST_CONFIRMED**（対象を併記）、提案・見積もりは **INFERRED**、参照結果がないものは **UNKNOWN** とする。

## 結論

B案を選ぶ。B1c-0bでは既存の `bc_item_*` に技単位の終了フックを一つ追加する。最終ダメージは既存Modifier、現在品と消費は既存の持ち物状態、HP変更と通知は既存のダメージ処理を利用する。接触・弱点被弾で実装が重なった時点で、共有できる条件式と適用処理を切り出す。[統合手順](item-dispatcher-integration.md) · [独立試作](../prototype/item-dispatcher/README.md)

## 現行コードの責務（SOURCE_CONFIRMED）

| 責務 | 現行の動作と再利用点 |
| --- | --- |
| レコードと現在品 | `BattleItemRecord` は8バイト、1品最大4件。`bc_item_next` がバージョン、`RULE_IGNORE_ITEMS`、現在の `item_from` を参照する。消費は `use_up` が `item_used` と時点・ターンを記録して現在品を外す。効果を判定する時点で現在品を読む。 |
| ダメージ補正 | `bc_modify`、`run_items`、`fx_final_passes` / `fx_final_slower_passes` が発動源・素早さに応じた順序を持つ。`BITEM_MODIFIER` の `BMOD_FLOORED` と既存4.12演算で1.3を5324へ変換できる。最終値へ直接掛けると既存の合成・丸めと結果がずれる。 |
| HP・治療・耐久 | `bc_item_hp`、`bc_item_hp_check`、`bc_item_status_check`、`bc_item_endure`、`bc_item_endured` に処理時点が分かれている。タスキの量の判定と消費は別。負傷しきい値・治療・登場をそのまま利用する。 |
| 行動終了 | 行動者の判定は `bc_items_actor_check`、全体の保留判定は `bc_items_after_action`。後者は反動待機等の技を実行しない経路からも呼ばれる。技終了時の持ち物とは別の処理時点。 |
| ヒットと通知 | `MoveContext` に `landed`、実損失の集計 `lost`、`struck` と対象ごとの保留判定がある。`bc_apply_damage` がHP反映・`EV_ITEM_DAMAGE` 等の通知・ひんしを処理する。 |
| 入力検証 | `battle_item_valid` 系の検査をcodecと `battle_init` の両方に維持する。持ち物の内部宣言は `battle_internal.h` にあり、現行ツリーに `battle_items.h` はない。 |

[持ち物参照と各判定](https://github.com/koziyagoukei/gba-gc-extension/blob/3269b7c8cbdd0e87ef539d5ff1baaeefe1566af0/gamecube/battle/battle_items.c#L38) · [Modifierの順序](https://github.com/koziyagoukei/gba-gc-extension/blob/3269b7c8cbdd0e87ef539d5ff1baaeefe1566af0/gamecube/battle/battle_modifiers.c#L200) · [MoveContext](https://github.com/koziyagoukei/gba-gc-extension/blob/3269b7c8cbdd0e87ef539d5ff1baaeefe1566af0/gamecube/battle/battle_effects.c#L205) · [HP適用](https://github.com/koziyagoukei/gba-gc-extension/blob/3269b7c8cbdd0e87ef539d5ff1baaeefe1566af0/gamecube/battle/battle_core.c#L764) · [監査台帳](item-mechanics/gc-compatibility.md)

## 比較（INFERRED）

| 評価軸 | A 独立Dispatcher | B 既存フックの拡張 | 折衷案 |
| --- | --- | --- | --- |
| 追加コードと対象 | GC実行部の概算200〜350行以上、5ファイル以上。走査、優先度、適用結果、既存の呼び出し先を接続する。 | GC実行部の概算40〜100行、主に `battle_items.c`、`battle_internal.h`、`battle_effects.c`。追加の実行フックは1本。Modifierは既存経路を利用。 | Bを先に導入し、独立したヒット種別が追加される段階で共通述語・適用処理を抽出。 |
| 既存処理と互換性 | 全フックを移すと、V1〜V19の順序・間接効果・消費の差分を広く検証する必要がある。 | 変化はV20の新規時点に限定できる。既存チェックポイントと状態をそのまま使う。 | バージョン境界ごとに既存呼び出し順を固定できる。 |
| 実装・デバッグ | 単体試験で入力を与えやすい。手番途中の再開、通知、再入、旧版の順序を二重に追うコストが高い。 | 戦闘中の呼び出し順を既存のコード上で読める。Nativeの既存回帰を広く再利用できる。 | 同じ判定が実際に複数箇所で必要になった後に統一できる。 |
| CPU・メモリ | イベント登録・キュー・大きな共通Contextを採ると毎ヒットの走査と状態が増える。 | 1品最大4件の既存レコードを必要な時点だけ参照。同期の1呼び出しなら持続状態の追加は0。 | ヒット事実をローカルに渡す。必要に応じて処理済みフラグを技単位で確保できる。 |
| 拡張性と主なリスク | 製品全体の規則を実行する拡張には向くが、B1cでは既存経路との二重管理が大きい。 | 異なる時点・役割に小さな入口を足せる。共通化が遅れると同じ条件の重複が生じる。 | 扱うチェックポイントが増えた時点で抽出の有効性を再評価する。 |

行数は製品側の実行部の概算。両方式に共通するcodec、Exporter、UI/APPの版交渉、試験を含めない。登録順だけで優先順位が決まるイベントバス、全チェックポイントを一つの可変Contextへ詰める方式、ゲーム固有IDによる効果の分岐は採用候補から外す。現在のModifierにはすでに明示的な適用順があり、複数効果の同時許可は確認済みの順序契約ごとに増やせる。

## 順序と状態の契約（INFERRED）

1. **最終補正**。現在の使用者のレコードを既存 `BMOD_FINAL` パスで合成する。読み取り専用とし、発動枠・消費・RNGを変更しない。現行と同じパス内の同順レコードは既存順を保持する。
2. **各ヒット**。攻撃対象・使用者・実ダメージ・みがわりへの実ダメージ・接触・相性を、そのヒットに対応する値として確定する。被弾フックは実参照の順序が立証された窓にだけ追加する。対象ごとの保留チェックや、複数対象の列挙を技単位の判定に流用しない。
3. **技終了**。全対象・ヒットと技の追加効果、ドレイン・反動、行動者の現行判定、既存の撃破特性の後で、元の使用者の現在品を1回評価する。新規評価は反動待機の予約と実際の交代より前。使用者のひんし・退場、効果無効化、実ダメージなしを判定する。V20で技終了HPレコードを複数置く構成は拒否する。[現行の技終了経路](https://github.com/koziyagoukei/gba-gc-extension/blob/3269b7c8cbdd0e87ef539d5ff1baaeefe1566af0/gamecube/battle/battle_effects.c#L827)
4. **行動終了と交代**。V19由来の保留きのみの全体判定と交代の順序は既存通り。ACT-002の反動待機のチェックは別経路のまま。B1c-0bで致死接触の表示順（ABL-G04）やピボットと保留きのみの差（ITM-TIME-003）を修正しない。V12〜V19で既に禁止となった複合特性Hookの受理条件（ABL-G05）は維持する。[既知差分](battle-reference/compatibility-matrix.md) · [持ち物の順序](item-mechanics/activation-timing.md)

| 状態の単位 | 必要な事実・発動回数 |
| --- | --- |
| 個体／現在の所持 | 定義と有効性を分ける。`Battle`・`Battler` から現在品、履歴、HP、最大HP、生存、Positionを取得する。消費は現行の `use_up` に集約し、次の評価は空の現在品を見る。発動可否の将来の抑制は所持・履歴を維持する。 |
| 技1回 | 既存 `MoveContext` に元の使用者（陣営＋パーティ枠、必要な場合は登場世代）、現在も同じ個体であること、ダメージが成立したかを対応づける。同期の単一路なら `landed` と使用者の照合で足りる。将来同じ技へ再入できる場合のみ、技ごとの一度フラグを保持してHP適用・通知より先に確定する。同じターンに別の技を実行した場合は新しい状態を使う。 |
| ヒット／対象 | メット等は命中して接触した実ヒット単位。対象単位の判定は対象の個体識別とともに保持する。再入可能な処理ではヒット番号＋対象＋判定時点で一度を保証する。`per_use_done` は既存の技効果用なので持ち物で使わない。 |
| ターン／交代 | ターン制限の効果に限りターン番号を使う。交代・再登場で位置が同じでも進行中の技の発動権を渡さない。Shift・Rotationで位置だけが変わる場合も所持の同一性は変わらない。 |

既存の登場、ターン終了、HPしきい値、状態異常、致死ダメージは現行フックを利用する。B1dの技選択・優先度、B2の交代予約・交換・破壊・再取得・抑制は、合法性・所持権・履歴という別々の状態遷移として追加する。イベントは確定した状態変更から生成する。

## B1cの適用例と拡張条件

| 持ち物 | 使う時点と状態 | 残る条件 |
| --- | --- | --- |
| いのちのたま | 最終補正5324＋技終了HP減少 `max(1, 非ダイマックス最大HP / 10)`。みがわりを含む実損失がある技1回で1回。品は保持する。 | マジックガードはHP減少のみ、ちからずくは対象技の終了時HP減少のみ、ぶきようは品由来の効果を抑える。V20初版は未対応特性との組み合わせを明示的に制限する。 |
| かいがらのすず | 技終了時に実ダメージの合計から回復。回復禁止、残り回復可能HP、使用者継続・生存を確認する。 | `lost` はみがわり込み。Expansionでの対象別・ヒット別・みがわりの量、回復禁止・ドレイン後・交代前の順序を試験し、必要に応じて集計を分ける。 |
| ゴツゴツメット | 実ヒットごとに対象の現在品、接触、特性発動後の使用者状態を判定し、使用者へ間接ダメージ。 | さめはだ・てつのトゲ、同時ひんし、対象がHP0の場合、みがわり、えんかく・接触抑止・マジックガードを含む。ABL-G04を一緒に変える場合は独立した契約と版を設ける。 |
| じゃくてんほけん | 直接の弱点被弾を対象に、攻撃・特攻のランク変更を規定順で適用し、既存の消費と履歴へ接続。 | 無効、みがわり、複数ヒット、片方・両方上限、対象ひんしと消費順の参照が必要。能力変動後フックはランクの実変動を区別し、連鎖の再入を明示する。 |

[ITM-PLAN-002と段階計画](item-mechanics/implementation-roadmap.md) · [ITM-VERIFY-001と参照試験](item-mechanics/open-questions.md) · [ABL-Q01と接触の未検証条件](ability-mechanics/open-questions.md) · [いのちのたまV20仕様 §13](items.md#13-m4-b1c-0a-life-orb-specification-and-reference-no-implementation)
