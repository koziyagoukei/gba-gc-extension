# Frontier集計と実装順

## ABL-PLAN-001 Frontierの頻度

既存のExporter実測基準はSingleの全2,040セットを各先発、相手を持ち物なし・たいあたりのカビゴンとした **Request 1,996／アイテム正規化なし992／厳密108**。この監査のheadless ELFでも総計を観測したが、固定SHAからのビルド証明はない。[計測条件](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/test/battle/gc_consumable_export.c#L243) · [持ち物監査の基準](https://github.com/koziyagoukei/gba-gc-extension/blob/6550adcaa763d3651a88a0fef549a299d41c746c/docs/item-mechanics/implementation-roadmap.md#L5)

`SOURCE_CONFIRMED`（入力経路）。 `CreateFacilityMon` は指定があれば `SetCorrectAbilityNum` で種族スロットを探し、存在しない指定では初期値を保持。指定なしの生成では個体生成時に通常スロットから選ぶ経路を使う。隠れ特性は適切に指定された場合と通常選出を分ける。 `GetAbilityBySpecies` は現在の種族と保存されたスロット、空欄のフォールバックを使う。 `GcPokemon_BuildRecord` 側で `GetMonAbility` を読み、Frontierの `ResolveAbility` がレコードを変換。開始後の変更／コピーやフォーム再解決は別。[CreateFacilityMon](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/battle_frontier.c#L652) · [SetCorrectAbilityNum](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/trainer_util.c#L80) · [通常スロット選択](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/pokemon.c#L1036) · [GetAbilityBySpecies](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/pokemon.c#L3190) · [実際の特性取得](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/gc_companion_pokemon.c#L251) · [Exporterの変換](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/frontier_gc_battle.c#L1842)

`INFERRED`（静的集計）。全セットのソースを走査し、指定が全取得分岐のスロットにある1,948、条件次第で指定または通常枠86、指定なしの通常枠6を記録した。指定された特性は249種類、通常枠の代替も含む静的候補は257種類。条件付き定義は候補の和集合なので、1セットが複数行の頻度に現れる。戦闘中にトレース等で取得する特性は頻度から除外。[全分岐のセットCSV](frontier.csv) と [全分岐の頻度CSV](frequency.csv) に通常2枠、隠れ、指定、候補、変換経路を収録。

固定チェックアウトの設定ヘッダーをGCCでプリプロセスした場合、**2,034セットは固定指定が全てスロットに存在し、6セットは指定なし**。未指定はゲンガー3セット（のろわれボディ／ふゆう）とイーブイ3セット（にげあし／てきおうりょく）。乱数の実測分布は仮定しない。既定候補は250種類。下表はこの投影から計算し、技・持ち物ゲートは静的な上限に用いた。[既定設定のセットCSV](default.csv) · [既定設定の頻度CSV](default-frequency.csv)

| 特性 | 指定セット数 | 静的候補セット数 | 特性単独で追加できる上限 | 既知の品・技ゲートを残した厳密上限 |
|---|---:|---:|---:|---:|
| ふゆう | 77 | 80 | ―（既存Hook） | ― |
| プレッシャー | 58 | 58 | 58 | 13 |
| いかく | 54 | 54 | ―（既存Hook） | ― |
| さいせいりょく | 50 | 50 | 50 | 5 |
| がんじょう | 40 | 40 | ―（既存Hook） | ― |
| いたずらごころ | 31 | 31 | 31 | 2 |
| ビーストブースト | 30 | 30 | 30 | 6 |
| クォークチャージ | 30 | 30 | 30 | 7 |
| ちからずく | 19 | 19 | 19 | 3 |
| かたやぶり | 12 | 12 | 12 | 3 |
| かるわざ | 12 | 12 | 12 | 0 |
| マジックガード | 8 | 8 | 8 | 0 |
| えんかく | 3 | 3 | 3 | 1 |
| ぶきよう／じゅくせい | 0／0 | 0／0 | 0／0 | 0／0 |

厳密上限は現在 `unsupported` の1特性だけを置き換えた前提。既存の品目ゲートを超えられないセット、品関連特性で全品を止めるセット、技の大分類が `ResolveEffects` にないことを検出したセットを外した。技の個別フラグ、初手が構築できるか、例外処理、複数ロスターは追加制約。0はその静的条件で残った候補が0という意味で、同時に品・技を拡大すれば変わる。厳密の上限を現在の108に加えて実測値として扱わず、セット間の重複も考慮する。[技大分類](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/frontier_gc_battle.c#L639) · [持ち物側特性ゲート](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/frontier_gc_battle.c#L1700)

## ABL-PLAN-002 実装順

| 段階 | 実装対象・状態・Hook | Exporterと通信・試験 | コストと効果 |
|---|---|---|---|
| B1c-0 | いのちのたま。最終補正と技単位のHP減少を一組にし、技の成否、実際の被害・みがわり、処理済み状態を保持。厳密対戦のマジックガード・ちからずく・ぶきよう、特性変更や無視を未再現のまま受理しない | 新版で最終補正の品レコードと技末尾Hook、既存V19の意味を維持。ロスター双方・控えの検査、非厳密では明示した正規化を適用。対応技、接触順、反動、ACT-002試験を固定 | 中。既存持ち物監査ではいのちのたま142セット、厳密静的上限56。特性側の追加対応数とは別 |
| B1c-1以降 | 技全体の実被害の集約をかいがらのすずで検証。その後メットの接触資格と攻撃側の生存、さめはだ／てつのトゲとの順。ひんし通知を切り離す必要を試験で確定 | 被弾・各打の原因、対象状態、接触の判定集約。複合Hookの互換性を整理し、予期した実行または明示拒否。Triple／RotationのルールはGC側で試験 | 中〜大。メット73セット、Long Reach単独の厳密静的上限1。持ち物と同時追加した場合は再集計 |
| 特性独立段階 | ①間接ダメージの発生源と対象判定、②技文脈のbreakable利用、③場・個体の抑制と装備再評価、④選択／順序、退場、撃破等 | マジックガードは原因ごとの試験。ちからずくは技・追加効果の正規化フィールド、順序スキップ範囲を新契約。かたやぶりの有効範囲を照会 API に持つ。退場・順序はHookと合法行動の両立 | 中〜大。プレッシャー、さいせいりょく等の頻度上位を候補にしつつ、PP・交代・場の競合を分離 |
| 特性置換・フォルム段階 | 開始／現在／実効の参照を導入し、コピー・置換・フォーム、復元、登場通知を状態遷移として実装。変身前後のタイプ・特性・最大HP等を解決 | 種族・品の固有ID解釈はGBAで正規化し、GCは一時状態を管理。能力交換の両者、入替先特性の宣言、旧Hook清掃、差分結果。V1〜V19のcodec・固定seed回帰 | 大。出現数だけで早期実装しない。B2の移動・抑制、ギミック基盤と依存関係を定義 |

`INFERRED`。追加は新しい意味を持つレコードのversionとcapabilityで交渉する。V19の4 hook・各8バイトの枠を維持すれば、既存最大容量の式を直ちに増やす必要はない。1個体あたり1件の差は12個体で96 B。フォルム・場の全体定義や技ごとの追加タグは別途積算する。既存V19を自動修正する変更は今回の対象外。[Hookのサイズと上限](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/protocol/battle_protocol.h#L420) · [最大Request](https://github.com/koziyagoukei/gba-gc-extension/blob/44c51538c7e5efbeb27bee91d5cc8a0a89cab14b/protocol/battle_protocol.h#L659)

[試験計画](open-questions.md) · [持ち物側の計画](../item-mechanics/implementation-roadmap.md)
