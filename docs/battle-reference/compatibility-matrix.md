# GC対応台帳

分類は `SUPPORTED`（対象ケースの参照比較済み）、`PARTIAL`（一部の処理に対応）、`UNSUPPORTED`（GCで実行しない）、`MISMATCH`（不一致）、`UNVERIFIED`（確認不足）、`NOT_APPLICABLE`（対象GCルールでは使わない）。試験の根拠区分は [索引](README.md#規約)。件数はこの表の項目単位。規則ごとの参照リンクは各章を参照。

| 仕様ID | Expansion → GC・版 | 判定／根拠 | 適用範囲と次段階 |
|---|---|---|---|
| [LIFE-001](battle-lifecycle.md#life-001) | 初期化・開始時フォーム → battle_init / battle_begin | PARTIAL / SOURCE_CONFIRMED | 全形式。出力済み初期状態 |
| [LIFE-002](battle-lifecycle.md#life-002) | 登場時特性ときのみ → bc_ability_on_entry / bc_item_on_entry V19 | SUPPORTED / TEST_CONFIRMED | 参照Single／Doubleの先発・交代。設置技や特殊フォームは別 |
| [LIFE-003](battle-lifecycle.md#life-003) | 複数の補充点 → continue_turn / Pivot再開 | PARTIAL / SOURCE_CONFIRMED | 全形式。V19のPivot／ドラゴンテールに参照試験あり |
| [LIFE-004](battle-lifecycle.md#life-004) | 戦闘終了時の所持品の永続反映 → 表示専用のロスター戦 | NOT_APPLICABLE / SOURCE_CONFIRMED | 全形式。Resultの契約更新時 |
| [TURN-001](turn-order.md#turn-001) | 速度・優先度 → battle_run_turn / update_action_order | PARTIAL / SOURCE_CONFIRMED | 全形式。順序変更品目と技前ギミックは後段 |
| [TURN-002](turn-order.md#turn-002) | 実行可否・PP・命中 → continue_turn / bc_execute_move | PARTIAL / SOURCE_CONFIRMED | 対応技の範囲。選択後のPP喪失やなまけは別 |
| [TURN-003](turn-order.md#turn-003) | MoveEndの再入・部分実行 → V19の個別チェック | PARTIAL / SOURCE_CONFIRMED | 全形式。B1cで対象・行動者・全員の順を照合 |
| [TURN-004](turn-order.md#turn-004) | ターン末の段階と補充 → resolve_end_of_turn | PARTIAL / SOURCE_CONFIRMED | 全形式。既存の回復・残留順は参照済み。補充とひんしの割り込みは別 |
| [DMG-001](damage-resolution.md#dmg-001) | 固定値で比較した通常ダメージ → V14以降の対応Modifier | SUPPORTED / TEST_CONFIRMED | 既存のダメージ参照例。急所等は対象外 |
| [DMG-002](damage-resolution.md#dmg-002) | 満タン耐久 → V19のがんじょう・タスキ | SUPPORTED / TEST_CONFIRMED | 既存の満タン・連続技・自傷・どく残留の参照例 |
| [DMG-003](damage-resolution.md#dmg-003) | 単発範囲・連続技・反動の代表順 → V19 strike等 | SUPPORTED / TEST_CONFIRMED | 既存の比較例。多対象かつ連続ヒットは未監査 |
| [ITEM-001](items.md#item-001) | 回復・治療・消費 → V19 bc_item_*_check | SUPPORTED / TEST_CONFIRMED | 基本きのみ、治療、交代の既存参照例 |
| [ITEM-002](items.md#item-002) | 個体別残留と日照特性の保留 → V19の残留処理・MoveEnd | SUPPORTED / TEST_CONFIRMED | 既存のどく・砂嵐・日照ダメージの範囲。反動待機は ACT-002（補修済み） |
| [ITEM-003](items.md#item-003) | 持ち物抑制・交換・復活 → 開始時の拒否／正規化 | UNSUPPORTED / SOURCE_CONFIRMED | ぶきよう、きんちょうかん等と譲渡・復活は後段 |
| [ITEM-004](../items.md#14-m4-b1c-0b-life-orb-in-request-v20) | いのちのたま → V20 品の最終補正（5324）と `BITIME_AFTER_MOVE`（最大HP÷10、最低1） | PARTIAL / TEST_CONFIRMED | Expansion 27件とGC Native（4形式、わるあがき、交代技、じしんかじょう）。マジックガード・ちからずく・ぶきよう所持者は全持ち物オフ。Dolphin未確認（B1c-0b時点）。B1c-0c：Dolphin実機（L73-L83、4形式）とJoyBusで確認、相手のHPきのみとの順序を補修。[実機](../items.md#15-m4-b1c-0c-v20-on-the-gba--gc-path-dolphin-joybus) |
| [ACT-001a](status-and-volatiles.md#act-001) | 完全まひ・混乱自傷後のMoveEnd → V19行動後チェック | SUPPORTED / TEST_CONFIRMED | 既存のまひ・自傷の固定ケース |
| [ACT-001b](status-and-volatiles.md#act-001) | 睡眠・凍結・ひるみ・失敗・実行時PP不足 → V19個別経路 | UNVERIFIED / SOURCE_CONFIRMED | 睡眠は保留きのみを両側で比較し一致（ACT-002の比較ケース）。こおり・ひるみ・失敗・実行時PP不足は比較待ち |
| [ACT-001c](status-and-volatiles.md#act-001) | 行動前のひんしでスキップ → TA_SKIPPED | PARTIAL / SOURCE_CONFIRMED | 入口は両側のコードで確認。全員の持ち物順は未試験 |
| [ACT-002](status-and-volatiles.md#act-002) | 反動待機後にMoveEnd → EV_RECHARGE後に bc_items_after_action（V19） | SUPPORTED / TEST_CONFIRMED | V19消費有効戦。Single／DoubleをExpansionと比較し、Dolphin L72でも確認。Triple／Rotation、状態異常きのみ、RULE_IGNORE_ITEMS、厳密モードはネイティブ試験。選択対象の未処理の状態異常は到達しない（open-questions） |
| [MOVE-001](move-and-ability-audit.md#move-001) | 特性無視、Shadow等 → Exporterで拒否 | UNSUPPORTED / SOURCE_CONFIRMED | 受理判定と効果再現を別に扱う。技フィールド台帳へ |
| [RULE-001](build-rules.md#rule-001) | 急所・メガ／Z／ゲンシ等 → 現行GCルールでは発動しない | NOT_APPLICABLE / SOURCE_CONFIRMED | V19。解禁時に寿命・順序を照合 |
| [FIELD-001](build-rules.md#field-001) | ゲンシ天候・未表現の場効果 → V15/V16の対応レコードのみ | UNSUPPORTED / SOURCE_CONFIRMED | ゲンシ天候等。追加時に持ち物連携を照合 |
| [FIELD-002](build-rules.md#field-002) | 通常天候とフィールドの比較済み補正 → V15/V16 | SUPPORTED / TEST_CONFIRMED | 既存の固定ダメージ値とV19残留の参照範囲 |
| [ABL-G04](../ability-mechanics/gc-compatibility.md) | 致死接触のさめはだとひんし通知 → bc_apply_damage の `EV_FAINT`、bc_ability_contact | MISMATCH / TEST_CONFIRMED | Single／Doubleを両側実行。結果は一致、対象の `EV_FAINT` だけが接触反応より前。V1〜V19では保留、V20以降の契約で段階化 |
| [ABL-G05](../ability-mechanics/gc-compatibility.md) | 接触状態異常＋接触ダメージ等の複合Hook → battle_ability_valid で拒否（V12〜V19） | UNSUPPORTED / TEST_CONFIRMED | Exporterは生成しない（全特性×V11〜V19で確認）。複合の実行は根拠となる特性がなく新版で検討 |

件数は SUPPORTED 9、PARTIAL 8、UNSUPPORTED 4、MISMATCH 1、UNVERIFIED 1、NOT_APPLICABLE 2、計25件（ABL-G04／G05、ITEM-004を追加）。分岐の全件試験率とは区別する。

特性監査ABL-G04／G05。[拒否条件](https://github.com/koziyagoukei/gba-gc-extension/blob/1664fb45a7aee928c32c90430073c7bea0711ee5/protocol/battle_protocol.c#L445) · [GC試験](https://github.com/koziyagoukei/gba-gc-extension/blob/1664fb45a7aee928c32c90430073c7bea0711ee5/gamecube/battle/battle_test.c#L9010) · [致死接触のGC試験](https://github.com/koziyagoukei/gba-gc-extension/blob/1664fb45a7aee928c32c90430073c7bea0711ee5/gamecube/battle/battle_test.c#L9162) · [Expansion参照試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/b091548d05e39450e6c5619244af8ea3c7ee88eb/test/battle/gc_contact_faint_reference.c#L12) · [Exporter試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/b091548d05e39450e6c5619244af8ea3c7ee88eb/test/battle/gc_ability_hooks_export.c#L10)

ACT-002の補修。[GCの修正](https://github.com/koziyagoukei/gba-gc-extension/blob/78b3125be6efc57d710f51c730e564111bcb4b3e/gamecube/battle/battle_core.c#L973) · [GCの試験](https://github.com/koziyagoukei/gba-gc-extension/blob/78b3125be6efc57d710f51c730e564111bcb4b3e/gamecube/battle/battle_test.c#L8867) · [Expansion参照試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/329ceb28ad48f52c60803e6beb4686260bca1f3f/test/battle/gc_recharge_item_reference.c#L24)

根拠（初版）。[行動中断](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_core.c#L952) · [V19対応試験](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_test.c#L8230) · [ゲンシ天候の対応外](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/docs/weather.md#L111) · [Exporterのフラグ検査](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/frontier_gc_battle.c#L630)
