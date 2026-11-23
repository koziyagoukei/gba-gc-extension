# 戦闘処理リファレンス

対象Expansionの戦闘処理を、GC側の実装判断と参照試験に使える粒度で記録する。初版の重点は、ライフサイクル、行動順、ダメージ、状態異常、V19消費型持ち物、行動不能時の技終了処理。

| 基準 | 固定コミット |
|---|---|
| GC `milestone4b1b` | `ba16c9455f25b7588486d0021aa1ae9b68627e11` |
| Frontier `frontier-gc-m4b1b` | `20debd3e44fb3da4c5a6834f73883fc95a8a1747` |
| 実行契約 | Request/APP V19、release 13。旧版の意味は版ごとに照合する |
| ACT-002補修 | GC `milestone4b1b-act002`（`78b3125be6` ほか）、Frontier `frontier-gc-m4b1b-act002`（`329ceb28ad`、`2d1e107ad8`） |

## 索引

- [戦闘ライフサイクル](battle-lifecycle.md) — 初期化、登場、交代、ひんし、終了と結果
- [ターン処理](turn-order.md) — 選択、行動順、実行可否、MoveEndの再入、残留処理
- [ダメージとHP](damage-resolution.md) — 計算段階、致死判定、連続技と範囲技
- [持ち物](items.md) — しきい値、治療、消費、抑制、各チェックポイント
- [状態異常と揮発状態](status-and-volatiles.md) — 行動不能経路の比較と反動ターンの監査
- [技・特性の監査台帳](move-and-ability-audit.md) — フィールド監査の記録単位と優先項目
- [設定の逆引き](build-rules.md) — コンパイル既定値、解決箇所、GCのルール
- [GC対応台帳](compatibility-matrix.md) — 版、差分、確認範囲
- [未確認事項とテスト案](open-questions.md) — 反動の再現条件、補修候補、次段階

## 規約

- 仕様IDは節の見出しに置く。既存IDは内容を更新しても再利用・改番しない。
- 根拠リンクは固定コミットと行番号を使う。関数・スクリプトのシンボルを本文に添える。対象ExpansionのC、スクリプト、設定、テストを一次資料とする。
- `SOURCE_CONFIRMED` は対象ソースで確認、`TEST_CONFIRMED` は指定した条件を実行試験で確認、`INFERRED` は経路から推定、`UNKNOWN` は未確認。既存テストの実行結果と、ソースで判明した未試験の差分を分ける。
- GCの対応分類は [GC対応台帳](compatibility-matrix.md) に置く。未対応の拒否やルールによる無効化は、当該効果の再現と同じ扱いにしない。
- 本文のExpansionは上記Frontierコミットの設定を指す。原作再現、別世代、Triple／Rotationの全経路は個々の根拠がある範囲で扱う。

## 今回の試験

- GC固定ソースからネイティブ `battle_test.c` を GCC `-O2 -Wall -Wextra -Werror` でビルド・実行し成功。V19の `test_v19_reference`、`test_v19_more`、`test_v19_gaps`、`test_v19_wire`、`test_v19_formats` を含む。[GCのV19試験](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_test.c#L8230)
- Frontier worktreeにあったビルド済みheadless ELFを一時コピーし、`gTestRunnerArgv` を `GC V19 reference` に設定して既存ランナーで実行。対象41件がPASS。ELFは今回再リンクしていない。対象外の `secret_power.c` のASSUME通知が1件出たが、対象41件の失敗は0。[Expansion参照試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/test/battle/gc_consumable_item_reference.c#L1)
- 同じ一時ELF上の `GC V14 reference` 11/11、V15 11/11、V16 13/13、V17 13/13、V18 22/22。一般の反動テストは2/3件がPASS。残り1件は英語の反動メッセージを照合できず失敗した。持ち物と反動を組み合わせた試験とは別。[失敗したメッセージのアサーション](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/test/battle/move_effect_secondary/recharge.c#L20)
- 反動中の持ち物チェックは既存41件の対象外。今回の差分判定はコード照合。Dolphin・GC/GBA実機での新規確認なし。

## ACT-002補修の試験

- **Expansion：** 参照試験 `test/battle/gc_recharge_item_reference.c` を追加し、6件がPASS。内容は反動待機（日本語の反動メッセージ照合を含む）、持ち物なし／きんちょうかん、Double、待機前のひんし、睡眠との比較、`recharge.c` からMESSAGEを除いた版。`GC V19 reference` は47件すべてPASS。
- **GCネイティブ：** `test_v19_recharge` を追加した。補修前は「recharge」ケースが失敗し、きのみが相手の行動の後に発動した。補修後は -O2、-Werror、ASan/UBSan、presentation のすべてがPASS。固定seedトレースV12〜V19は補修前と同一。
- **ミューテーション：** 9件のうち6件を検出し、3件は等価（理由は [items](../items.md) 12.11）。
- **Dolphin：** L72で、反動、オボン、相手の行動の順に画面表示されることを確認した。L01〜L71はM4-B1bと全行一致（66戦とGBA側拒否5件）。nonceによる結果契約、JoyBus（CRC 0）、Frontierセーブの不変も確認した。
- **Frontier `make check`：** 全6,951件。PASS 4,035、FAILED 2,455。FAILEDはM4-B1bと同じ2,455件で、`recharge.c` は英語メッセージで引き続き失敗する。M4-B1b（6,945件）からの変化は反動の参照試験6件の追加だけで、すべてPASS。状態の変わった試験はない。
