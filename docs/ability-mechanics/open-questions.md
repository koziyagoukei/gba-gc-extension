# 追加検証と未確認事項

| ID | 優先 | 比較する条件 |
|---|---|---|
| ABL-Q01 | 高 | いのちのたま。**実行済み（B1c-0a）**：単発／範囲／連続／みがわり、外れ／吸収／反動待機、使用者ひんし、マジックガード（補正維持・減少なし）、ちからずくの適用技（減少なし）と非適用技、ぶきよう有効／いえきで抑制。**残り**：0ダメージ扱い、相手全滅後、わるあがき、ダイマックス。[参照試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/9601bc98afc3ae96d9df3b4b76b04d316f1062a5/test/battle/gc_life_orb_reference.c#L462) · [仕様と設計](../items.md#13-m4-b1c-0a-life-orb-specification-and-reference-no-implementation) |
| ABL-Q02 | 高 | さめはだ＋メット。**実行済み（ABL-G04）**: 攻撃者先に倒れる、対象HP0、双方HP0、対象のみがわり、外れ、連続技の打ち切り、Double。**残り**: メット、後続打ミス（GCは命中判定が対象ごとに1回で、各ヒットの命中判定を持たないため条件を作れない）、えんかく／パット、シールド、マジックガード。HP0、ひんし処理、イベント、補充を別々に観測。[参照試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/b091548d05e39450e6c5619244af8ea3c7ee88eb/test/battle/gc_contact_faint_reference.c#L12) · [後続打ミス](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/hold_effect/rocky_helmet.c#L24) · [既存の接触回帰](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/gamecube/battle/battle_test.c#L2978) |
| ABL-Q03 | 済 | 同時接触Hookの方針は**拒否**（ABL-G05）。既存の構成1種類と複合、発動不能な状態異常、確率1/1と1/2、同じkind二つ（先頭のみ）、strict、無効データの明示的拒否をV12〜V19で試験。複合を実行する版は、Expansionに同じ構成の特性がなく順序の根拠がないため未定。[拒否条件](https://github.com/koziyagoukei/gba-gc-extension/blob/1664fb45a7aee928c32c90430073c7bea0711ee5/protocol/battle_protocol.c#L445) · [GC試験](https://github.com/koziyagoukei/gba-gc-extension/blob/1664fb45a7aee928c32c90430073c7bea0711ee5/gamecube/battle/battle_test.c#L9010) |
| ABL-Q04 | 中 | ガス発生源が複数、いえき、シールド喪失、技中の無視。かるわざは発動時に抑制と発動後に抑制を比較。追加入場で発動が二重にならないこと。[抑制・消費の既存例](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/ability/unburden.c#L45) |
| ABL-Q05 | 中 | きのみ抑制解除と全員のMoveEnd判定、ACT-002、Pivotの既知差。 `CUD_CHEW` 時間、同じターン中の `SYMBIOSIS`、晴れの `HARVEST`、再摂食と再装備を別に確認。[ACT-002](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/gc_recharge_item_reference.c#L24) · [はんすう](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_util.c#L3965) |
| ABL-Q06 | 継続 | 既存V1〜V19とGC4形式を回帰。Expansion参照はSingle/Double。Triple／Rotationの遠距離、翼個体、Shift、ひんし・交換の判定はGC仕様として固定。 |

## ABL-G04／G05の検証（実行済み）

Expansion側は `frontier-gc-m4b1b-act002`（2d1e107）に試験2ファイルを加えた木から `make check` がheadless ELFを生成し、`GC V19 reference: contact KO` 8/8、`ability hooks` 2/2がPASS。GCの順序に書き換えた一時試験3件（ひんし→さめはだ、双方で対象が先、ひんし前の命中回数）は3/3がFAILし、判別できることを確認（一時ファイルは削除）。ひんしの文は日本語で照合し、名前の制御コードにより `{JPN}{JPN}相手の {JPN}キバニアは 倒れた！` となる。メッセージを使わない順序試験（HP0→さめはだ→攻撃者HP0）も別にPASS。

GC側は `test_abl_g04` で同じ8条件を実行し、結果の一致と対象 `EV_FAINT` の位置の差を固定。`test_abl_g05` は修正前に `!battle_ability_valid(&pair, …)` で失敗し、修正後にPASS。ミューテーション13件はすべて検出。ExpansionのStaticは確率を他の条件より先に引き、GCは状態を付与できる場合だけ引く。確率は同じで乱数列は共有しないため、結果の差にはならない。

## 今回の補助実行

固定ソースからGCネイティブ試験に成功。追加の一時試験は、V19 Singleで接触攻撃を使用し、対象へ状態100%と1/8ダメージの接触Hookを併記する試験、および対象HP1の単一さめはだHookでイベント順を比較する試験。試験コードはリポジトリに含めていない。[観測結果](gc-compatibility.md)

既存のFrontier headless ELFでは、V19参照47/47、Exporter tallyの全17区間、Magic Guard 4/4、Sheer Force 32/32を観測。Unburden 9/11、Iron Barbsフィルタ0/1、Speed Boost 0/1では英語のメッセージ照合に失敗した。一時ディレクトリで再試行し、Rough Skin and Iron Barbs 1/1、Sturdy 1/3、Mold Breaker 4/8を観測。後二者の失敗も英語メッセージ照合。Long ReachとKlutzの専用ファイルは、このソースではTODOのみ。[Long ReachのTODO](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/ability/long_reach.c#L4) · [KlutzのTODO](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/ability/klutz.c#L4)対象外の `secret_power.c:7` のASSUME通知あり。ELFの正確な由来は未証明で、これらは固定ソースに対する新規の実行証明ではない。次回は固定SHAからビルドし、言語に依存しないHP・状態・通知順の検証を優先する。新しい戦闘ロジックの失敗としてメッセージ照合の結果を数えない。[Iron Barbs試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/ability/iron_barbs.c#L4) · [Speed Boost試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/ability/speed_boost.c#L4) · [V19のExporter tally](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/gc_consumable_export.c#L243)

`UNKNOWN`。全特性の条件付き網羅、無視・抑制と変身の交差、フォルム品・複合ルールは未完（同条件での致死接触のイベント照合はABL-G04で実施）。索引から参照を追い、実装範囲を広げる前に比較する。
