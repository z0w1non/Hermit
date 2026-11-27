# Parser テンプレート境界

## 目的

Boost.Spirit の文法テンプレートと AST 評価テンプレートを、目的別の翻訳単位へ分離し、同一の巨大実装ヘッダーを parser と visitor の両方で解析する構成を解消する。公開 API と AST 型は従来どおり維持し、公開 grammar では statement 構文を有効化しない。

## 実装境界

- `Hermit_parser_grammar_impl.hpp` は Spirit の属性変換と document grammar 構築を定義し、`Hermit_parser.cpp` だけがインクルードする。
- `Hermit_parser_visitor_impl.hpp` は評価 visitor と演算子テンプレートを定義し、`Hermit_parser_visitors.cpp` だけがインクルードする。
- `Hermit_parser_evaluation_internal.hpp` は `evaluate_ast` の非テンプレート宣言を提供する。parser は評価実装をこの関数経由で呼び出す。
- `Hermit_parser_internal.hpp` は文法 rule / symbol の内部宣言をまとめる。grammar 実装から参照する。

依存方向は `parser.cpp → grammar_impl` および `parser.cpp → evaluation_internal ← visitors.cpp → visitor_impl` とする。文法実装から評価 visitor を参照せず、評価 visitor から Spirit grammar 構築実装を参照しない。旧共有実装ヘッダーと訪問者限定の前処理分岐は削除した。

grammar の rule 宣言は字句、primary/macro、unary/postfix、算術/比較、assignment/document の component に分かれ、後段 component は前段の rule を参照する。共有する AST と symbol 型は `Hermit_parser_types.hpp` と `Hermit_parser_grammar_symbols.hpp` に置く。

## 実測

Visual Studio 18、Release x64、`Hermit.sln /t:Rebuild /m:1`、`_CL_=/Bt+` でフルリビルドを実行した。ログは [`release-x64-parser-boundary-2026-11-26.log`](../build-profile/release-x64-parser-boundary-2026-11-26.log)、比較 CSV は [`parser-template-boundary-2026-11-27.csv`](../build-profile/parser-template-boundary-2026-11-27.csv)、グラフは [`parser-template-boundary-2026-11-27.svg`](../build-profile/parser-template-boundary-2026-11-27.svg) に保存する。

| 翻訳単位 | 変更前 c1xx | 変更後 c1xx | 変更前 c2 | 変更後 c2 |
| --- | ---: | ---: | ---: | ---: |
| `Hermit_parser.cpp` | 37.02576 秒 | 19.04035 秒 | 0.41195 秒 | 0.31342 秒 |
| `Hermit_parser_visitors.cpp` | 17.31892 秒 | 13.26766 秒 | 0.06504 秒 | 0.24231 秒 |
| 合計 | 54.34468 秒 | 32.30801 秒 | 0.47699 秒 | 0.55573 秒 |

c1xx 合計は 22.03667 秒（約 40.6%）短縮した。c2 合計は 0.07874 秒増加したため、短縮効果は主にフロントエンド解析にある。フルソリューション Rebuild は 5分02.94秒で成功し、警告・エラーは 0 件だった。Release x64 既定構成は 105/105 tests、Stage 8 実験構成は 128/128 tests 成功を確認した。

## 維持する条件

- Spirit の rule 型と iterator は公開 API に追加しない。
- parser grammar 構築と評価 visitor の実装依存は一方向に保つ。
- statement grammar は feature flag による実験構成だけで検証し、既定の公開構文は式限定とする。
- compiler macro を一時的に切り替える場合は `/t:Build` で古い中間生成物を再利用せず、`/t:Rebuild` を使う。
- 依存関係や公開型を変更した場合は本資料と `docs/SPECIFICATION.md` のファイル構成・依存表を同じ変更で更新する。
