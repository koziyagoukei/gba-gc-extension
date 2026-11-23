# 技・特性のフィールド監査

## MOVE-001

`SOURCE_CONFIRMED` — M4-B1a+以降の基準Exporterは `shadowMove`、`ignoresTargetAbility`、`ignoresTargetDefenseEvasionStages`、`ignoreTypeIfFlyingAndUngrounded` を検出し、該当する条件で未対応とする。通常ヒットとしての誤受理を防ぐ処理。GCがそれぞれの効果を実行する範囲とは別に記録する。[MoveInfo](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/include/move.h#L97) · [既存の拒否条件](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/frontier_gc_battle.c#L630)

| 優先フィールド | 調べる読み取り箇所・相互作用 |
|---|---|
| `shadowMove` | Shadow専用の相性、実行可否とギミック |
| `ignoresTargetAbility` | ダメージ・無効化・耐久Hookに渡る有効特性。汎用特性無視と個別例外 |
| `ignoresTargetDefenseEvasionStages` | 命中・回避と防御側能力ランク。急所・てんねんとの交差 |
| `ignoreTypeIfFlyingAndUngrounded` | 接地、ひこうタイプ、地面判定、フィールドの対象判定 |

## 全件台帳の記録単位

1. 基準コミット・ビルド設定・`MoveInfo`／`AdditionalEffect` のフィールド名と既定値。
2. 定義した技を列挙し、直接参照とgetterを経由した参照を、選択・取消・命中・計算・Hit・MoveEnd・ターン末・AIへ対応付ける。
3. Exporterの扱いを「転送」「正規化」「明示拒否」「未表現」「演出専用」に記録する。フラグ単体だけでなく発動主体、対象、時点、組み合わせを記録する。
4. 支援判定を通る正例・拒否例と、効果の再現を比較する試験を個別に登録する。未調査フィールドを既定値として処理済みにしない。

特性台帳は、登場／退場、選択・行動前、相性・命中・能力値、状態付与前後、接触、ひんし、ターン末、場の維持を記録単位とする。特性IDごとの網羅判定は保留。[AdditionalEffect](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/include/move.h#L34) · [ResolveAdditionalEffects](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/frontier_gc_battle.c#L535) · [技終了時の分類](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L5500) · [GCの能力Hook取得](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_abilities.c#L19)
