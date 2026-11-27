# マクロパーサー再開発方針

## 方針

マクロ parser の実装方式は Boost.Spirit Qi を維持する。現在の `Hermit_parser_grammar_impl.hpp` と `Hermit_parser_*_rules.hpp` に分散した EBNF 相当の rule 定義は、移植中の旧文法として一時退避し、公開仕様から再構成した新しい Qi grammar を段階的に導入する。

旧実装を削除して一度に置き換えるのではなく、公開 API、評価処理、既存テストを維持したまま、文法と AST 属性の境界を再設計する。

## 旧実装の退避対象

- `Hermit_parser_grammar_impl.hpp` の document／expression／statement rule 定義
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

### 調査知見の参照

Qiの属性伝播、AST alternative、失敗したrule構成、依存注入callbackの状態契約と再現条件は [開発知見台帳](DEVELOPMENT_KNOWLEDGE.md) に表形式で管理する。制御文の属性漏れと修正、placeholder全体の変換境界、単独代入のvariant制約、次に必要な検証を同台帳のIDから確認する。ここでは段階計画と現在の公開/実験境界を管理し、個別試行の詳細は重複して記載しない。

### 構文の段階導入計画

公開 grammar は計画の全段階を結合検証するまで式 placeholder のみとする。各段階は専用の実験用前処理フラグでビルドし、次段階が未完成でも既定構成の構文・動作へ影響させない。

段階番号は構文導入順を示し、`MPR-STG-NNN` は段階項目を外部参照する固定IDです。順序変更後もIDを維持します。
| 項目ID | 段階 | 追加する構文と意味 | 主な検証 | 状態 |
| --- | --- | --- | --- | --- |
| MPR-STG-001 | 1 | 式 statement (`expression;`)。statement の値を展開結果にせず、1つの `expression_statement_type` としてASTへ格納して評価する。 | 実placeholder parse、AST alternative、終端 `}}` の完全消費、値を捨てる評価、不正な後続文字の拒否。 | 実験フラグ `HERMIT_ENABLE_EXPERIMENTAL_EXPRESSION_STATEMENTS` で実装済み。2026-11-25 Release x64実験build成功、2 tests成功。 |
| MPR-STG-002 | 2 | 単独代入 (`=`)。左辺変数・右辺式をASTで区別し、型変換とコンテキスト更新を定義する。 | identifier以外の左辺拒否、未定義変数の初期化、既存値更新、型・評価エラー、placeholder内の隣接statement。 | 実験フラグ `HERMIT_ENABLE_EXPERIMENTAL_ASSIGNMENTS` で実装済み。単一statement variant rule、実験Rebuild、106/106 tests成功（2026-11-26）。 |
| MPR-STG-003 | 3 | 複合代入 (`+=`, `-=`, `*=`, `/=`, `%=`, `<<=`, `>>=`, `&=`, `^=`, `\|=`)。単独代入の型・演算境界を再利用する。 | 各演算子のASTと評価、識別子限定、完全入力、型不一致、ゼロ除算・剰余0、未定義左辺拒否、既定grammarでの拒否。 | `HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_ASSIGNMENTS` で実装済み。`assignment_operator` をASTに保持し既存演算子visitorへ接続。実験Release x64 Rebuild、109/109 tests成功（2026-11-26）。 |
| MPR-STG-004 | 4 | 複合文 (`{ ... }`) と複数statement列。 | 空block、入れ子、隣接文、AST順序、実行順序、途中エラー。 | `HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_STATEMENTS` で実装済み。単独代入と複合文の2 alternativeをproduction Qi grammarで構成。Release x64実験Rebuild、112/112 tests成功（2026-11-26）。 |
| MPR-STG-005 | 5 | `if`／`else`。 | 真偽両枝、未選択枝の副作用なし、nested if、dangling `else` の最近接if結合。 | `HERMIT_ENABLE_EXPERIMENTAL_IF_STATEMENTS` で実装済み。Stage 4までの実験grammarへ3つ目のstatement alternativeとして統合。Release x64実験Rebuild、115/115 tests成功（2026-11-26）。 |
| MPR-STG-006 | 6 | `while`。 | 条件再評価、0回/複数回実行、入れ子、実行予算到達時の明確なエラー、無限実行しないこと。 | `HERMIT_ENABLE_EXPERIMENTAL_WHILE_STATEMENTS` で実装済み。placeholder単位で合計10,000回を共有し、10,000回後に条件がfalseなら成功、10,001回目のbodyへ入る直前に停止する。副作用は完了済み反復分を保持する。Stage 2–6実験Release x64 Rebuild、119/119 tests成功（2026-11-26）。 |
| MPR-STG-007 | 7 | 前置/後置 `++`／`--`。 | prefixは更新後、postfixは更新前の整数値を返す。整数変数だけを対象にし、非識別子・未定義・非整数・overflow/underflow・複数postfixを拒否する。 | `HERMIT_ENABLE_EXPERIMENTAL_INCREMENT_DECREMENT` で実装済み。Stage 2–7実験構成124/124 tests成功（2026-11-26）。 |
| MPR-STG-008 | 8 | 全statement構文の統合grammar。式statement、単独/複合代入、複合文、if/else、while、増減演算子を一つのproduction grammarで結合し、全 alternative の属性伝播・優先順位・backtrackingを検証する。 | 全AST alternativeと再帰field、式statementと制御文・代入の相互作用、dangling else、完全入力、実行予算、通常構成での拒否、公開grammarへ接続する可否。 | 完了。式statementの末尾`;`を要求する正しい入力へ統合テストを修正。Stage 8実験Release x64で128/128 tests、通常構成で105/105 tests成功（2026-11-26）。通常のdocument grammarではstatement placeholderを候補から外し、公開構文は段階計画どおり式限定を維持する。 |

各段階のテストと既定構成の全回帰テストが通ってから次段階へ進む。statement全体の属性伝播問題が残る間は、単独ruleで検証できた構文だけを実験フラグで隔離し、公開経路に接続しない。

### 現在の進捗

> **公開状態（2026-11-26）:** Stage 8の全statement grammar統合は実験構成で検証済み（128/128 tests）。式statementは式と`;`を直接Fusion属性伝播する。通常ビルドでは段階導入方針によりstatement構文を無効化し、公開placeholder grammarは式のみとする。単独/複合代入、複合文、`if`／`else`／`while`、セミコロン付き式、前置/後置増減演算子は通常構成で拒否する。Stage 1–7は各専用flag、全grammar統合は `HERMIT_ENABLE_EXPERIMENTAL_STAGE8_STATEMENTS` を使う。Stage 2–7は124 tests、Stage 8は128 tests、既定構成は105 tests（全て25 suites）が成功している。

- 実験的な式 AST／statement AST と評価visitorを保持する。if/while AST の Fusion adaptation、`else` clause も実装済みだが、通常ビルドの公開経路から切り離している。
- statement alternative の属性伝播調査中に追加した AST・評価テストは `HERMIT_ENABLE_EXPERIMENTAL_STATEMENTS` の下へ置き、既定テストでは実行しない。
- 既定 grammar は式 placeholder だけを受け付け、statement を含む placeholder は構文エラーにする。statement rule・属性変換は通常ビルドで構築せず、内部 statement parse helper も既定では `false` を返す。
- 公開式の代入・前置/後置増減演算子も無効化し、副作用を公開構文から除いた。`rejects_disabled_statement_syntax` が代表入力の拒否と変数値不変を確認する。
- Stage 1の式statementは単一 `expression_statement` alternative の専用grammarでparseし、`vector<statement_type>` に格納する。通常buildではこのruleとvisitor分岐も定義せず、実験flag時のみ評価値を捨てる。
- Stage 2の初回試作では右辺にvector派生の `expression_type` を使いQiのvector属性変換で失敗した。AST右辺を単一の `assignment_expression_type` にし、専用ASTを `statement_type` variantへ登録、単一alternativeのstatement ruleから合成する形へ絞ったところ、実験Rebuildが通過した。評価visitorは既存の `assign` 処理を使い、未定義変数の初期化と既存変数の型検証を保つ。ASTのalternative、識別子限定、完全入力、初期化・更新、型不一致・未定義右辺を実験テストで検証する。
- Stage 3ではASTへ `assignment_operator` を追加し、複合代入10演算子を専用フラグ下で有効にする。右辺を一度評価した後、式代入と共有する `apply_assignment_operator` へ渡す。整数の算術・bit演算、AST operator、完全入力、非識別子左辺、型不一致、除算/剰余の0、未定義左辺の拒否を実験テストし、公開grammarは引き続き無効である。
- Stage 4では `statement` を単独代入と複合文の2 alternativeにし、block内のstatement列を再帰的にparseする。Qiの `transform_attribute<statement_type, Alternative>` によるvariant代入でproduction grammarを構成し、空block、入れ子block、statement順序、placeholder内の隣接blockと完全入力を検証した。これは2 alternativeの範囲での確認であり、全statement grammarの複数alternative結合を解決したことにはしない。
- Stage 5ではif文を代入・複合文に続く3つ目のalternativeとして追加する。Qiの再帰rule順によりdangling `else` は最も近い`if`へ結合する。既存visitorで真偽両枝を実行し、未選択枝のassignmentがcontextへ現れないこと、nested-if AST、完全入力を実験テストする。statement全体のgrammarへ一般化せず、通常構成では継続して無効とする。
- Stage 6ではwhileを4つ目のalternativeとして追加し、placeholder評価開始時に10,000回の共有予算を設定する。条件を毎回body前に評価し、body実行直前に予算を消費するため、予算ちょうどの反復後にfalseとなる条件は成功し、さらに反復が必要ならそのbodyを実行せず `macro_exception` で停止する。上限到達時は既存のplaceholder境界が結果を空文字にし、完了済み反復のcontext副作用は保持する。逐次・nested loopは同じ予算を消費する。Stage 2–6のRebuildと119件、通常構成のRebuildと105件が成功した。
- Stage 7では既存のprefix/postfix ASTを実験flag下で有効にし、整数変数だけを更新対象とする。prefixは更新後、postfixは更新前の値を返し、未定義変数・非整数・識別子以外・複数postfix・整数限界値では変更せずmacro errorにする。Stage 2–7の実験構成で124件が成功した。通常構成も次のflagなしRebuildで回帰確認する。
- Stage 8では式statementを `expression >> lit(';')` のFusion属性伝播でstatement variantへ格納する。最初の統合テスト失敗は `if(1)value++ else value=9` の式statementに`;`がなかったためで、Qi属性伝播問題ではなかった。`if(1)value++; else value=9` に修正後、各statement単体、段階prefix、全alternativeのparse/evaluateが成功し、Stage 8実験128/128、既定105/105 testsを確認した。通常grammarは公開仕様どおり式placeholderのみとする。
- Qi属性境界の個々の試行、AST検査、採否は [開発知見台帳](DEVELOPMENT_KNOWLEDGE.md) の `QI-*` 行に記録する。実験条件や結論が変わった場合は証拠と状態を同じ行へ追記し、ここには詳細を重複させない。

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
