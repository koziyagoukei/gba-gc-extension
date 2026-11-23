# ターン処理

## TURN-001

`SOURCE_CONFIRMED` — Expansionは行動選択後、行動種別と技優先度、現在の素早さ等で順序を決定する。交代・バッグ等の後、最初の技の前にギミックと集中等の技前処理を実行する。おいうちの割り込みには条件がある。第7世代以降の設定では技前ギミックの後に速度順を更新し、動的素早さ設定では行動の合間にも更新する。GCは `battle_run_turn` / `update_action_order` で行動候補と順序を管理する。ギミックなしの現行範囲で使う。[SetActionsAndBattlersTurnOrder](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_main.c#L4858) · [RunTurnActionsFunctions](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_main.c#L5270) · [update_action_order](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_core.c#L856)

## TURN-002

`SOURCE_CONFIRMED` — 基準ビルドの主要な経路は次の通り。支援する技や特性により途中の段階が増減する。

```text
選択時の拒否 → 選び直し（行動順の決定前）
行動枠を取り出す
  ├─ 行動者不在・ひんし → 行動枠終了
  └─ 技実行へ → DoAttackCanceler
       ├─ 実行不能 → 該当スクリプト → BattleScript_MoveEnd
       └─ 実行継続 → 実行時の対象決定・PP消費 → 成否・命中
            ├─ ミス・失敗 → 対応スクリプト → MoveEndまたは部分MoveEnd
            └─ 命中 → 計算・HP適用・効果 → MoveEnd
               （対象ループ、追加ヒット、スクリプトの割り込みと再入）
全行動後 → ターン終了処理 → 次ターンまたは戦闘終了
```

`DoAttackCanceler` の主要順は反動待機 → ねむり／こおり → 実行時PP確認 → ひるみ → 制限技 → 混乱 → まひ → 対象決定 → PP消費 → 失敗判定・命中・計算。失敗を受けると `unableToUseMove` を立てる。技終了の有無は遷移先スクリプトで確認する。[実行可否とPP消費のディスパッチ](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L3242) · [DoAttackCanceler](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L3304) · [行動不能の経路別監査](status-and-volatiles.md)

## TURN-003

`SOURCE_CONFIRMED` — `Cmd_moveend` は `DoMoveEnd` にモードと範囲を渡す。通常の `BattleScript_MoveEnd` は `moveendall`。技スクリプトには部分実行もある。各段階は状態番号を進めるか、スクリプト実行・次の対象・次のヒットに処理を引き渡す。スクリプトが返ると持ち物や特性を取り直すため、直前の消費・交代が反映される。`MoveEndItemsEffectsAll` は個体位置の走査を再開し、同じ位置で二重に発動しない。[Cmd_moveend](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_script_commands.c#L3156) · [DoMoveEnd](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L5539) · [全員の持ち物検査](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_move_resolution.c#L5099)

## TURN-004

`SOURCE_CONFIRMED` — Expansionのターン末は、順序確定・場の処理 → 天候経過／残留 → 補充 → 先読み技・ねがいごと等 → 最初の個体別ブロック → 補充 → どく、やけど、しもやけを含む残留 → 場の経過や後半の特性・補充。確定済み勝敗で多くの段階を飛ばす。最初の個体別ブロックではグラス回復 → 状態回復特性 → たべのこし等の順になる。GC V19の対応範囲は、天候 → グラス・状態回復特性・残留持ち物 → 状態ダメージ → フィールド経過 → 後段の対応特性。Expansionにある複数の補充点を全て実行する構造ではない。[個体別の回復と持ち物](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_end_turn.c#L424) · [ターン末ディスパッチ](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_end_turn.c#L1614) · [DoEndTurnEffects](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_end_turn.c#L1691) · [GCのターン末処理](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_core.c#L919)
