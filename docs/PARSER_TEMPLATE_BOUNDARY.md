# Parser テンプレート境界設計

## 目的

`Hermit_parser_impl.hpp` のテンプレート定義を複数の翻訳単位で解析・実体化しない。公開 API は AST、設定、評価コンテキストだけを受け取り、Boost.Spirit の文法規則と評価用テンプレートを実装境界へ閉じ込める。

## 内部 API

## 構文カテゴリ別の実体化方針

Spirit の全 rule を 1 翻訳単位へ集約するのではなく、依存方向が一方向になる次のカテゴリへ分ける。

1. **字句・基本値**: `plain_text`、`name`、`character`、`string`、`value`
2. **primary／macro**: `primary`、`variable`、`placeholder`、`macro_expression`
3. **前置・後置演算**: `prefix_expression`、`suffix_expression`、`parentheses_expression`
4. **算術・比較**: multiplicative、additive、shift、relational、equality、bitwise
5. **代入・条件・文書**: assignment、conditional、expression、`abstract_syntax_tree`

各カテゴリは前段カテゴリの rule だけを参照し、カテゴリごとの constructor／rule 初期化を専用実装単位へ配置する。カテゴリ間で共有する AST 型と symbol 型は `Hermit_parser_types.hpp`／`Hermit_parser_grammar_symbols.hpp` に限定する。まず字句・基本値と算術・比較を分離して `/Bt+` を測定し、テンプレート実体化時間が増えないことを確認してから残りへ拡張する。

依存方向は次の一方向グラフに固定する。

`字句・基本値 → primary／macro → 前置・後置演算 → 算術・比較 → 代入・条件・文書`

右側のカテゴリは左側の rule と AST 型だけを参照する。左側から右側の visitor、評価 API、設定実装を参照してはならない。共有が必要な宣言は parser types／symbol header または非テンプレート評価 API へ移し、カテゴリ間の相互 include は許可しない。

## AST 属性付き rule API

各 component は `Iterator` と skip policy を受け取り、Spirit rule の属性型を省略しない。宣言は次の形を基本とする。

```cpp
template<typename Iterator>
struct lexical_rules {
    qi::rule<Iterator, std::string()> name;
    qi::rule<Iterator, char()> character;
    qi::rule<Iterator, std::string()> string;
    qi::rule<Iterator, value_type()> value;
};

template<typename Iterator>
struct primary_macro_rules {
    qi::rule<Iterator, variable_type(), qi::space_type> variable;
    qi::rule<Iterator, primary_type(), qi::space_type> primary;
    qi::rule<Iterator, macro_expression_type(), qi::space_type> macro_expression;
};

template<typename Iterator>
struct arithmetic_comparison_rules {
    qi::rule<Iterator, multiplicative_expression_type(), qi::space_type> multiplicative_expression;
    qi::rule<Iterator, additive_expression_type(), qi::space_type> additive_expression;
    qi::rule<Iterator, equality_expression_type(), qi::space_type> equality_expression;
};
```

`unary_postfix_rules` は `prefix_expression_type`／`suffix_expression_type`、`assignment_document_rules` は `assignment_expression_type`／`conditional_expression_type`／`expression_type`／`abstract_syntax_tree_type` を属性に使用する。後段 component の constructor は前段 component の rule を `const` 参照で受け取り、自身の rule の定義だけを行う。`qi::rule` の属性型、iterator、skipper が一致しない component 間の暗黙変換は禁止する。

### `parser_grammar_api.hpp`

- `grammar_handle` を非公開の文法実体への値型ハンドルとして提供する。
- `make_grammar()` は `grammar_handle` を返し、Spirit の iterator 型・rule 型を公開しない。
- `parse(input, grammar_handle, cfg, ctx)` は AST を返す。
- 現行の段階的移行では opaque な `grammar_handle`／`make_grammar_handle()`、`parse(input, cfg, ctx)`、`parse(input, cfg, grammar_handle, ctx)`、`evaluate(input, cfg, ctx)`、`evaluate(input, cfg, grammar_handle, ctx)` を非テンプレート境界として提供する。grammar 明示 overload と `make_grammar()` は削除済みである。
- 文法テンプレートの明示的実体化は `parser_grammar.cpp` の 1 翻訳単位に限定する。

### `parser_evaluation_api.hpp`

- `assign_operator` は `value_reference_type` 間の代入だけを公開する。
- `evaluation_visitor` と各 AST visitor は非公開実装型として宣言する。
- 算術・論理演算の型制約、`unwrap`、Boost.Variant の visitor 実体はこの実装境界へ移す。
- `parser_visitors.cpp` はこの API と評価関数の非テンプレート宣言だけを参照する。

現行実装では、rule 宣言を `Hermit_parser_lexical_rules.hpp`、`Hermit_parser_primary_macro_rules.hpp`、`Hermit_parser_unary_postfix_rules.hpp`、`Hermit_parser_arithmetic_comparison_rules.hpp`、`Hermit_parser_assignment_document_rules.hpp` に分割し、`document_grammar` は各 component の rule を保持して constructor から初期化する。後段 component は前段 component の rule をメンバー参照で利用する。

## 移行順序

1. `assign_operator` のテンプレート本体を `parser_evaluation_api.hpp` へ移し、既存 `parser_impl.hpp` から重複定義を除去する。
2. 文法 rule を上記 5 カテゴリへ分割し、カテゴリ単位の constructor／rule 初期化と `/Bt+` 計測を行う。全体の明示的実体化はカテゴリ分割後に再評価する。
3. visitor 構造体の宣言を同ヘッダーへ移し、実装定義を `parser_visitors.cpp` に残す。
4. 文法コンストラクターと Spirit rule 定義を `parser_grammar.cpp` へ移し、`document_grammar<std::string_view::const_iterator>` を明示的に実体化する。
5. `parser.cpp` から `Hermit_parser_impl.hpp` の直接参照を外し、`parser_grammar_api.hpp` と `parser_evaluation_api.hpp` のみを参照する。
6. `/Bt+` で parser 関連翻訳単位のフロントエンド時間を再計測し、変更前後を `build-profile` に記録する。

## 完了条件

- `Hermit_parser_impl.hpp` を直接インクルードする翻訳単位を、現状の grammar／visitor の 2 単位から非テンプレート API 境界へ段階的に縮小する。
- Spirit の rule／iterator 型が公開ヘッダーに現れない。
- Release x64 ビルドと 59 件のテストが成功する。
- parser と visitor の合計フロントエンド時間を現状（34.69 秒 + 14.37 秒）と比較し、悪化させない。
