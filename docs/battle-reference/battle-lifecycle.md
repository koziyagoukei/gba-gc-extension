# 戦闘ライフサイクル

## LIFE-001

`SOURCE_CONFIRMED` — Expansionは戦闘開始時の初期化と `FORM_CHANGE_BEGIN_BATTLE` を行い、登場処理、選択、行動順、行動実行へ進む。装備で解決されるフォーム、戦闘開始時のフォーム、技前のギミック、自動登場時のゲンシカイキは入口が異なる。GCはRequestから初期ロスター・能力値・タイプ・特性を読み、その後に開始時の場・登場効果を適用する。[開始時の状態とフォーム](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_main.c#L590) · [battle_init](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_core.c#L237) · [battle_begin](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_core.c#L1028)

## LIFE-002

`SOURCE_CONFIRMED` — Expansionの交代登場処理は、設置技処理後の一般特性段階で `TryPrimalReversion` または登場時特性へ進み、状態異常免疫特性、登場時持ち物を別段階で評価する。対象が持ち物を発動すればスクリプトへ移り、親処理へ戻る。V19の対応範囲では、登場時特性の直後にその個体のきのみをチェックする。先発同士の順序とHP半分の交代登場に参照試験がある。[登場時の段階](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_switch_in.c#L274) · [GCの登場処理](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_core.c#L512) · [V19参照試験](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/test/battle/gc_consumable_item_reference.c#L1)

## LIFE-003

`SOURCE_CONFIRMED` — 行動中のひんし、ターン末の残留ダメージのひんし、交代補充は発生箇所が異なる。Expansionのターン末には複数の補充ブロックが置かれている。GCの `continue_turn` は行動ごとに戦闘終了を判定し、Pivot選択では途中で戻り、同じターンを再開する。ターン末処理の後に空いた位置を整理する。Shift／Rotationは位置の変更で、通常交代の登場処理と同一視しない。[ターン末の段階と補充](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_end_turn.c#L1614) · [GCの行動・補充・終了](https://github.com/koziyagoukei/gba-gc-extension/blob/ba16c9455f25b7588486d0021aa1ae9b68627e11/gamecube/battle/battle_core.c#L952)

## LIFE-004

`SOURCE_CONFIRMED` — Expansion本体には終了時フォーム復元・所持品復元の経路がある。GCのV19消費は一時戦闘状態。GBA側 `HandleResult` でnonceに基づく結果受付、Frontierの `OnResult` と `Finish` でポリシーと個体を検証する。現行ロスター戦は表示専用で、旧Singleの反映対象はHPとPP。消費したきのみ・タスキは保存データへ伝播しない。[ネイティブ戦闘終了](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_main.c#L5458) · [TryRestoreHeldItems](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/battle_util.c#L9585) · [HandleResult](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/gc_companion_protocol.c#L251) · [OnResult / Finish](https://github.com/koziyagoukei/PokeEm-expansion-CanuseJP/blob/20debd3e44fb3da4c5a6834f73883fc95a8a1747/src/frontier_gc_battle.c#L2995)
