# 追加検証と未確認事項

| ID | 優先 | 比較する条件 |
|---|---|---|
| ABL-Q01 | 高 | いのちのたまの最終補正とHP減少を独立に測定。単発／範囲／連続／みがわり、0ダメージ扱い、外れ／吸収／反動待機、使用者ひんし、マジックガード、ちからずくの適用技と非適用技、ぶきよう有効／抑制中。HP・発動順で判定。[Life Orb既存試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/hold_effect/life_orb.c#L9) |
| ABL-Q02 | 高 | さめはだ＋メット。攻撃者先に倒れる、対象HP0、双方HP0、後続打ミス、対象のみがわり、えんかく／パット、シールド、マジックガード。HP0、ひんし処理、イベント、補充を別々に観測。[後続打ミス](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/hold_effect/rocky_helmet.c#L24) · [既存の接触回帰](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/gamecube/battle/battle_test.c#L2978) |
| ABL-Q03 | 高 | 同時接触Hookを旧バージョンと新バージョンで扱う方針。既存の構成1種類と複合、発動不能な状態異常、確率外れ、同じkind二つ、無効データの明示的拒否。[既存のcodec検証](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/protocol/battle_protocol.c#L428) |
| ABL-Q04 | 中 | ガス発生源が複数、いえき、シールド喪失、技中の無視。かるわざは発動時に抑制と発動後に抑制を比較。追加入場で発動が二重にならないこと。[抑制・消費の既存例](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/ability/unburden.c#L45) |
| ABL-Q05 | 中 | きのみ抑制解除と全員のMoveEnd判定、ACT-002、Pivotの既知差。 `CUD_CHEW` 時間、同じターン中の `SYMBIOSIS`、晴れの `HARVEST`、再摂食と再装備を別に確認。[ACT-002](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/gc_recharge_item_reference.c#L24) · [はんすう](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_util.c#L3965) |
| ABL-Q06 | 継続 | 既存V1〜V19とGC4形式を回帰。Expansion参照はSingle/Double。Triple／Rotationの遠距離、翼個体、Shift、ひんし・交換の判定はGC仕様として固定。 |

## 今回の補助実行

固定ソースからGCネイティブ試験に成功。追加の一時試験は、V19 Singleで接触攻撃を使用し、対象へ状態100%と1/8ダメージの接触Hookを併記する試験、および対象HP1の単一さめはだHookでイベント順を比較する試験。試験コードはリポジトリに含めていない。[観測結果](gc-compatibility.md)

既存のFrontier headless ELFでは、V19参照47/47、Exporter tallyの全17区間、Magic Guard 4/4、Sheer Force 32/32を観測。Unburden 9/11、Iron Barbsフィルタ0/1、Speed Boost 0/1では英語のメッセージ照合に失敗した。一時ディレクトリで再試行し、Rough Skin and Iron Barbs 1/1、Sturdy 1/3、Mold Breaker 4/8を観測。後二者の失敗も英語メッセージ照合。Long ReachとKlutzの専用ファイルは、このソースではTODOのみ。[Long ReachのTODO](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/ability/long_reach.c#L4) · [KlutzのTODO](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/ability/klutz.c#L4)対象外の `secret_power.c:7` のASSUME通知あり。ELFの正確な由来は未証明で、これらは固定ソースに対する新規の実行証明ではない。次回は固定SHAからビルドし、言語に依存しないHP・状態・通知順の検証を優先する。新しい戦闘ロジックの失敗としてメッセージ照合の結果を数えない。[Iron Barbs試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/ability/iron_barbs.c#L4) · [Speed Boost試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/ability/speed_boost.c#L4) · [V19のExporter tally](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/gc_consumable_export.c#L243)

`UNKNOWN`。全特性の条件付き網羅、無視・抑制と変身の交差、フォルム品・複合ルール、GCとExpansion同条件での致死接触のイベント照合は未完。索引から参照を追い、実装範囲を広げる前に比較する。
