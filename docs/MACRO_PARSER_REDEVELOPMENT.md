# マクロパーサー再開発方針

## 方針

マクロ parser の実装方式は Boost.Spirit Qi を維持する。現在の `Hermit_parser_impl.hpp` と `Hermit_parser_*_rules.hpp` に分散した EBNF 相当の rule 定義は、移植中の旧文法として一時退避し、公開仕様から再構成した新しい Qi grammar を段階的に導入する。

旧実装を削除して一度に置き換えるのではなく、公開 API、評価処理、既存テストを維持したまま、文法と AST 属性の境界を再設計する。

## 旧実装の退避対象

- `Hermit_parser_impl.hpp` の document／expression／statement rule 定義
- `Hermit_parser_lexical_rules.hpp`、`Hermit_parser_primary_macro_rules.hpp`、`Hermit_parser_unary_postfix_rules.hpp`、`Hermit_parser_arithmetic_comparison_rules.hpp`、`Hermit_parser_assignment_document_rules.hpp`
- Qi rule への `BOOST_FUSION_ADAPT_STRUCT` による直接属性伝播
- 制御文移植のために追加した `statement_type`、`if_statement_type`、`while_statement_type` の未完成経路

退避中の旧 rule は比較・回帰確認用に保持し、新 grammar が対応機能を満たすまで削除しない。

## 機能仕様から再構成する Qi grammar

新 grammar の優先順位は次のとおりとする。

1. 通常文字列と `{{ ... }}` の文書ノード
2. bool、整数、浮動小数点数、文字、文字列のリテラル
3. 変数参照と組み込み関数呼び出し
4. 括弧、前置・後置、算術、比較、論理演算
5. 代入・複合代入
6. カンマ区切りの式列
7. 式と statement の分離。式は文字列展開し、statement は空文字列へ展開する

Qi の各 rule はこの順序と責務に合わせて構成し、rule の属性型は新しい syntax model に明示的に対応させる。文法候補が途中まで入力を消費して失敗しても、別候補を安全に試行できる構造にする。

`if`、`while`、複合文は C 類似の statement grammar として追加する。式は評価結果を文字列展開し、statement は副作用を適用した後に空文字列を返す。statement は式 statement (`expression;`)、単一代入、複合文、制御文を含む。末尾 `;` は式を式 statement として識別し、その評価値は捨てる。`;` のない placeholder 式は文字列へ展開する。

### AST 調査で特定した原因

Qi の式／文 variant 属性や placeholder 全体の変換境界を小さな grammar で試す調査だけでは、実際の制御文 AST がどの alternative と値で埋まるかを見落としていた。原因特定では production AST を直接 parse し、構造・各フィールド・variant の選択型を検査する。

`if(c){}` に相当する再現入力を `if_statement_type` へ parse したときの AST は次の状態だった。

| AST 箇所 | Fusion 対応漏れ時 | 正常な値 |
| --- | --- | --- |
| `if_statement_type.condition` | 式 AST が入り、条件は正常 | 同じ式 AST |
| `if_statement_type.statement.statement` | `assignment_statement_type` alternative が選択された | `compound_statement_type` alternative |
| `compound_statement_type.statements` | compound alternative 自体が選択されず、空の既定 variant が残る | 空（`{}` 内に文がないため） |

誤解析の本体は条件ではなく body である。Qi の sequence が `if_statement_type` の Fusion field へ属性を分配するには `BOOST_FUSION_ADAPT_STRUCT(if_statement_type, condition, statement)` が必要だが、これが欠けていた。`statement_type` の `boost::variant` は先頭 alternative の assignment を既定構築するため、その結果、実際には `{}` である body が代入文として誤認識された。while AST にも同じ欠落があった。

production の AST 型を使い、adaptation を適用しない対照テストでは `expression=true, if=false, while=false, compound=0` となった。if と while の Fusion adaptation を加えた後、body は compound として parse され、Release x64 の AST 検査テストを通過した。この差分比較により、variant 全般や placeholder の `attr_cast` 境界ではなく、制御文ノード自身の Fusion field 対応が原因と特定できた。

追加の原因監査では `{{if(1)a=1 b=2}}` の AST を調べた。if body は assignment、後続 assignment は placeholder の別 statement となり、想定した吸い込みは起きなかった。`{{if(0)if(1)selected=7}}` も outer if → inner if → assignment の階層を保持し、false 条件では body の副作用が発生しない。`else` を含む入力は以前は制御構文を消費した後に parse が失敗していた。optional directive の属性は裸の `statement_type` として合成され、Fusion の複数 field `else_clause_type` へ直接割り当てられずビルドエラーになった。AST wrapper を statement から変換構築するようにし、Qi rule の属性型を揃えて解決した。\n\n調査を効率化する手順は、(1)最小入力を実 parser で parse、(2)AST の型・variant alternative・各フィールド値を assertion で確認、(3)誤った枝を特定、(4)疑わしい属性対応だけを外した対照と修正後を比較、(5)同じ AST を使った回帰テストを残す、である。parse 成否だけで結論せず、隣接・nested・false 条件を使って実行時の副作用も確認する。型だけを模した独立 grammar の実験は Qi の境界条件確認に限定する。

### 現在の進捗

> **公開状態（2026-11-24）:** statement の Qi 属性伝播に未解決問題があるため、通常ビルドでは statement 構文を無効化している。公開 placeholder grammar は式のみとし、単独代入・複合文・`if`／`else`／`while`・セミコロン付き式・前置/後置増減演算子を拒否する。内部 AST、grammar、visitor の実装コードは再開用に保持しているが、内部 parse helper も既定で失敗し、公開 visitor から statement 評価を呼び出さない。実験的テストは `HERMIT_ENABLE_EXPERIMENTAL_STATEMENTS` の条件下へ隔離し、既定 Release x64 では statement 拒否回帰を含む 102 tests / 25 suites が成功している。属性境界の問題が解決し実験的テストが通るまで、公開経路へ戻さない。

- 実験的な式 AST／statement AST と評価visitorを保持する。if/while AST の Fusion adaptation、`else` clause も実装済みだが、通常ビルドの公開経路から切り離している。
- statement alternative の属性伝播調査中に追加した AST・評価テストは `HERMIT_ENABLE_EXPERIMENTAL_STATEMENTS` の下へ置き、既定テストでは実行しない。
- 既定 grammar は式 placeholder だけを受け付け、statement を含む placeholder は構文エラーにする。内部 statement parse helper も既定では `false` を返す。
- 公開式の代入・前置/後置増減演算子も無効化し、副作用を公開構文から除いた。`rejects_disabled_statement_syntax` が代表入力の拒否と変数値不変を確認する。
- 実 parser では単独の式 statement は通る一方、複数の `statement` alternative に組み込むと parse が失敗する。alternative全体を `attr_cast<statement_type>` で包む試行は実行時アクセス違反を起こしたため棄却した。Qiが合成するalternative属性と再帰wrapperを含む `statement_type` の変換境界が未解決である。これが公開機能を無効化した理由である。
- `statement_type` への独自 `transform_attribute` を除去する試行、および同一型変換の場合に限って除外する試行では症状が変わらなかった。式statement枝だけに `attr_cast` を適用しても単独placeholderが失敗した。各試行後の Release x64 ビルドは成功し、対象回帰3件は失敗した。
- Phoenix semantic action で各具体ASTを `statement_type` へ代入する案も、Phoenix operator を直接includeするとRelease x64ビルドは成功したが、対象回帰3件は失敗したため採用しなかった。
- `statement_placeholder` と `expression_placeholder` で本文と終端 `}}` を同じ `qi::skip(space)` 範囲に置き、終端前の空白を許容するようにした。この変更だけでは式 statement のvariant伝播問題は解消しない。
- `transform_attribute` と `qi::attr_cast` でAST wrapperを作る最小テスト、およびplaceholder候補が不成立の場合に文書テキストalternativeへ戻るテストが成功した。
- 実 AST 型への `attr_cast` で適用範囲を比較した。式属性 rule だけを cast し delimiter の sequence の内側へ置くと、sequence が渡す属性型が `expression_type` となり、placeholder用 `transform_attribute` が要求する `placeholder_type&` と不一致でコンパイルエラーになる。
- 開始・終了 delimiter を含む placeholder parser 全体を `attr_cast<placeholder_type>` で包むと、同じ実 AST 型を使った最小grammarがコンパイル・parseとも成功する。属性変換の境界を sequence 全体に置くことが重要である。
- 実 `expression_type` と過去試作に近い `std::vector<expression_type>` の別ruleを `boost::variant` にまとめ、Fusion placeholder member へ伝播する試験が成功した。式とstatementの両alternativeが選択でき、variant／statement sequence属性自体は失敗原因でない。
- 同じ実 AST 属性を使い、placeholder parser 全体を `attr_cast` で包む試験も成功した。
- `qi::int_` を実 AST の `expression_type` ruleへ直接接続する試験も失敗したが、文法階層を飛ばして `int` を `assignment_expression_type` の深い variant へ渡していたため、この失敗は元の統合不具合の再現ではない。
- 初期調査では Qi sequence 内の属性型不一致も観測したが、production AST による追試で、制御構文の body 誤認識を直接起こす原因は if/while の Fusion adaptation 欠落と判明した。

## 移行境界

```text
公開 parse/evaluate API
        |
新 Qi grammar --- 新 syntax model --- 既存 evaluator/context/builtin
        |
旧 Qi grammar（退避・比較用）
```

- Qi の利用は parser 実装内部に限定する。
- `grammar_handle`、`parse`、`evaluate` の公開契約は維持する。
- `context`、`config`、builtin registry の仕様は変更しない。
- syntax model は Fusion の暗黙変換に依存せず、rule の属性型を明示する。
- 構文エラーには入力位置と期待 token を記録する。

## 検証順序

1. リテラルと通常の変数／関数マクロ
2. 式演算子と代入
3. カンマ区切りの式列、代入 statement、ネストした関数引数
4. 組み込み関数と再帰展開
5. `if`／`while`／複合文（別仕様として追加）

各段階で既存テストを実行し、新 grammar の単体テストを追加する。制御構文の調査では parse 成否に加えて AST alternative と値を検証する。新 grammar が仕様対象を満たすまで旧 grammar と制御文の未完成経路を削除しない。
