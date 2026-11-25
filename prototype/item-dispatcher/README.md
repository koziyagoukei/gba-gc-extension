# Item Hookの独立試作

B1c-0b向けに、既存 `bc_item_*` へ追加する技終了判定の形をC11で試す。シェルスクリプトからのみビルドされる。Battle Coreの実行、Request/APPのエンコード、JoyBus、Exporter、Dolphinはこのモデルの対象外。

## 実行

Linux／WSLで、C11、AddressSanitizer、UndefinedBehaviorSanitizerを備えるGCCまたはClangを使用する。

```sh
sh prototype/item-dispatcher/test.sh
# コンパイラーを指定する場合
CC=clang sh prototype/item-dispatcher/test.sh
```

`-O2` とサニタイザー付き `-O1` を別々に実行する。両方で `-Wall -Wextra -Werror -pedantic` を使用する。実行ファイルは一時ディレクトリに置く。

## 試作の境界

- `item_hook.c` は正規化済みの一時入力を検査し、既存最終補正段階へ渡す係数、または技終了の自己ダメージ量を返す。生存中の元使用者、実ダメージ成立、現在有効な持ち物を呼び出し元が渡す。試作の列挙値にwire上の意味はない。
- `MoveItemProgress` は技1回分の再呼び出し試験用。処理時点を評価した時点で訪問済みにする。判定が無効果でも後から同一技の終了点を再実行しない。不正レコードと未対応時点は判定前に返す。
- 試験の丸め、持ち物消費のモック、イベント記録はアダプター。マジックガード相当の間接ダメージ無効、ちからずく相当の終了処理スキップ、ぶきよう相当の効果抑制も解決済みフラグとして与える。アルゴリズムの比較対象は [既存Modifier](../../docs/item-dispatcher-design.md)。
- 参照結果の代表例は55→71、51→66、46→60、675→877（四捨五入の5325では878）、みがわり24→31、範囲の各対象69→90、連続技の各ヒット42→55。最大HP300で300→270を1回。**TEST_CONFIRMED（試作）** は同じ数値をモデルで確認した意味。[Expansion参照結果 §13.2](../../docs/items.md#132-reference-tests-testbattlegc_life_orb_referencec-22-tests)

## 今回の検証

| 試験 | 結果と対象 |
| --- | --- |
| 独立C試作 | `-O2` とASan/UBSanで各5試験群成功。5324の補正と丸め、減少量の下限、みがわり、連続・範囲各1回、ひんし・退場・無効化、現在品を失った後の二重発動防止、呼び出し側の順序、無効レコードと未対応時点の拒否。 |
| 既存GC | Native戦闘試験は `-O2` とASan/UBSanで成功。V19の4形式240戦、ACT-002、ABL-G04/G05を含む。Native表示、`wire_test`、`message_test` も成功。Requestの版・記録・フラグは既存戦闘試験で検証。Frontierの接続・結果反映のNative試験も成功。接続試験のビルドでは現行Send関数の符号比較の警告が1件。 |
| 固定seed | GC基準コミットと試作ツリーのトレースを比較し、V12〜V19がそれぞれ4形式×300 seedで一致。V19は1,200戦の集計ハッシュ `65a3b885`。 |
| V20失敗先行 | `sh tools/v20-tests.sh` は基準通り14件全て開始前のRequest拒否、終了コード1。効果のアサーションは未到達。 |
| Expansion・接続環境 | 既存のExpansion参照試験22件の記録を参照。ここでの再実行、V20の実戦統合、DolphinとJoyBusの新規動作確認は未実施。 |

[設計比較](../../docs/item-dispatcher-design.md) · [B1c-0bへの統合計画](../../docs/item-dispatcher-integration.md)
