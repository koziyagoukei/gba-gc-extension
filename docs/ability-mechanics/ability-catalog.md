# 全特性のカタログ

`SOURCE_CONFIRMED`。対象実コードの定義表は `src/data/abilities.h`。数値IDは `include/constants/abilities.h`。対象の既定値は `GEN_LATEST = GEN_9`。 `P_UPDATED_ABILITIES` は種族スロットの世代、`B_*` は戦闘仕様の世代・機能条件として別々に用いる。[gAbilitiesInfo](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/data/abilities.h#L1) · [Ability列挙](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/include/constants/abilities.h#L5) · [GEN_LATEST](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/include/config/general.h#L74) · [P_UPDATED_ABILITIES](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/include/config/pokemon.h#L7)

| 指標 | 数 |
|---|---:|
| 定義ID（`ABILITY_NONE`・予約枠を含む） | 320 |
| NONEと予約枠314／317を除く名称付き定義 | 317 |
| 戦闘ランタイムCの直接シンボル参照を発見 | 311 |
| 種族テーブルの静的スロット参照を発見（NONE除外、全条件分岐） | 310 |
| V19 ExporterのHook経路／NONE経路／未対応経路 | 75／5／240 |
| 調査分類 | 18分類 |

311は実装を機械的に確定した数ではない。一般化したテーブル経由、フォーム表、戦闘外・施設等は直接参照と別に記録。例としてマルチタイプ、ぎょぐん等は直接ランタイム照会の集計に出なくても、特性やフォルムの関連表を読む。Aura Guardの定義は未実装記載で戦闘Cの直接参照なし。314／317は予約枠。種族の配置と、施設で実際に生成され有効になることを区別する。[予約枠と独自特性](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/data/abilities.h#L2446) · [Aura Guard](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/data/abilities.h#L2476) · [フォーム表](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/2d1e107ad820a3b0a601af968b49856f74151ee1/src/data/pokemon/form_change_tables.h#L1)

## 分類の使い方

| 分類 | 仕様・段階 |
|---|---|
| ABL-POWER / DEFENSE / IMMUNITY | 威力・攻防値・最終補正、ダメージ軽減、吸収・直接／間接無効 |
| ABL-HIT / STATUS | 致死耐久、接触・被弾、付与と回復 |
| ABL-FIELD / ENTRY / EXIT | 天候・地形、登場、退場と交代可否 |
| ABL-ORDER / ACCURACY / SECONDARY | 技優先度・素早さ、命中、追加効果 |
| ABL-ITEM / CHANGE / FORM | 品との相互作用、実効性と置換、フォーム |
| ABL-KO / RANK / POLICY | ひんし・撃破、能力ランク、施設・戦闘外等 |
| ABL-ACTION | 技の使用可否、タイプや対象の変更、ヒット数、再行動 |

[abilities.csv](abilities.csv) は複数タグ、タイミングと常時照会・イベント・複合の目安、定義テーブルの説明文、データ上の制約フラグ、参照の種類、GCの静的分類と未確認事項を含む。英語名欄は現行ソースから取得できる場合のみ。定義の説明はゲーム内要約なので、発動条件の一次根拠はRuntime・スクリプト欄の入口。各ファイル・関数の最初の出現を記録する。詳細な読み取り状態・書き込み・世代分岐は優先特性の章を参照。残りの特性は個別条件の完全検証をしていない。

- `DIRECT_RUNTIME_REFERENCE` は静的なCの参照。呼び出しが有効な条件や読まれた結果は別に確認する。 `NO_DIRECT_RUNTIME_REFERENCE` は欠落の断定には使わない。
- `data_flags` は `breakable`、`cantBeSuppressed`、変更・コピー関連フラグ等のTRUE候補。場や技による追加条件は [特性の有効性](suppression-and-change.md)。
- `gc_exporter_route` は実行可能と宣言される入口。 `gc_v19_audit` は安全側の索引判定で、正確性の判断は [効果単位の台帳](gc-compatibility.md)。

## 再生成

Python 3.10以降で `python -B catalog.py <Frontierのローカルパス> abilities` を実行。最後の引数は `abilities`、`frontier`、`frequency`、`default`、`default-frequency`、`summary`。固定コミットのGitオブジェクトと同梱の持ち物監査セット表を読み、標準出力へCSVまたは件数を出す。ビルドやチェックアウトは不要。`frontier`／`frequency` は条件分岐全体を扱う。`default`／`default-frequency` は同じSHAの変更されていないFrontier作業ツリーをGCCでプリプロセスする（Windowsなら `--wsl CodexUbuntu` 等を指定）。ELFは作らない。いずれもROM実行結果とは区別する。[集計スクリプト](catalog.py)
