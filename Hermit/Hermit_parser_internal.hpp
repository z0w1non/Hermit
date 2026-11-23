#ifndef HERMIT_PARSER_INTERNAL_HPP
#define HERMIT_PARSER_INTERNAL_HPP
#include "Hermit_parser_types.hpp"
#include "Hermit_parser_grammar_symbols.hpp"
#include "Hermit_parser_lexical_rules.hpp"
#include "Hermit_parser_primary_macro_rules.hpp"
#include "Hermit_parser_unary_postfix_rules.hpp"
#include "Hermit_parser_arithmetic_comparison_rules.hpp"
#include "Hermit_parser_assignment_document_rules.hpp"
namespace Hermit::parser {
template<typename Iterator> struct document_grammar : boost::spirit::qi::grammar<Iterator, abstract_syntax_tree_type()> {
    document_grammar();
private:
    assignment_symbols assignment_operator_; equality_symbols equality_operator_; relational_symbols relational_operator_; shift_symbols shift_operator_; additive_symbols additive_operator_; multiplicative_symbols multiplicative_operator_; prefix_symbols prefix_operator_; suffix_symbols suffix_operator_; escaped_chars escaped_char;
    lexical_rules<Iterator> lexical; primary_macro_rules<Iterator> primary_macro; unary_postfix_rules<Iterator> unary_postfix; arithmetic_comparison_rules<Iterator> arithmetic_comparison; assignment_document_rules<Iterator> assignment_document;
};
using grammar = document_grammar<std::string_view::const_iterator>;
}
#endif
