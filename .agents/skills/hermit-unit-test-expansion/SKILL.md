---
name: hermit-unit-test-expansion
description: Hermit の公開 API と再利用可能な処理に対する Google Test を追加し、実装契約と資料の整合を確認する。
metadata:
  short-description: Hermit の単体テスト拡充
---

# Hermit 単体テスト拡充

実装と公開宣言を根拠に、`test/` の決定的な単体テストを拡充する。公開関数・クラス、設定型、parser の AST／評価 visitor、filesystem・文字列・ネットワーク補助、生成バックエンドの純粋な変換処理を対象にする。

## 手順

1. `AGENTS.md`、`docs/REQUIREMENTS.md`、`docs/ISSUES.md`、`docs/SPECIFICATION.md`、公開ヘッダー、`test/` の既存テストを確認する。`third_party/` と `vcpkg_installed/` は対象外とする。
2. 宣言と実装を照合し、未テストの関数・クラス・分岐を列挙する。正常系、境界値、失敗系、状態遷移の順に優先し、重複する既存テストは避ける。
3. `test/test.cpp` または責務に対応する `test/*.cpp` に追加する。既存の Google Test 規約と `test/Hermit.cpp` のテスト専用実装構成を保つ。
4. 外部 I/O は一時ファイル、インメモリ入力、依存注入、決定的なスタブで隔離する。実サービス、実ネットワーク、プロセス、時刻、乱数、ユーザー環境に依存させない。既存 TCP 経路の runtime 検証が必要な場合は `hermit-build-test` の loopback 手順に従う。
5. テスト名と assertion が API の実際の戻り値、例外、エラー伝播、副作用を示すようにする。回帰テストを修正より先に追加できる場合は、修正前に失敗を確認する。
6. テスト構成・検証対象に関する `docs/SPECIFICATION.md` の記載を更新し、実装・テスト一覧と照合する。公開仕様や実行方法も変更した場合は `readme.md` を更新する。
7. Release x64 の差分ビルドと全テストを実行する場合は `hermit-build-test` の手順・制約・報告形式に従う。依頼または作業上の必要がない限り、検証を追加実行しない。

## 完了条件

- 追加したテストが実装の契約を検証し、重複を避けている。
- テストが決定的で、ユーザー環境や外部サービスに依存しない。必要な loopback 検証は指定手順に従う。
- テスト構成・対象範囲・資料が一致している。
- 実行したビルド・テストの結果、または未実行の理由を報告できる。
