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
    bool parse_expression_statement(std::string_view input, expression_statement_type& result) const {
#if defined(HERMIT_ENABLE_EXPERIMENTAL_STATEMENTS)
        Iterator first{ input.begin() };
        const Iterator last{ input.end() };
        return boost::spirit::qi::phrase_parse(first, last, assignment_document.expression_statement, boost::spirit::qi::space, result) && first == last;
#else
        static_cast<void>(input);
        static_cast<void>(result);
        return false;
#endif
    }
    bool parse_statement(std::string_view input, statement_type& result) const {
#if defined(HERMIT_ENABLE_EXPERIMENTAL_STATEMENTS)
        Iterator first{ input.begin() };
        const Iterator last{ input.end() };
        return boost::spirit::qi::phrase_parse(first, last, assignment_document.statement, boost::spirit::qi::space, result) && first == last;
#else
        static_cast<void>(input);
        static_cast<void>(result);
        return false;
#endif
    }
    bool parse_statement_placeholder(std::string_view input, placeholder_result_type& result) const {
#if defined(HERMIT_ENABLE_EXPERIMENTAL_STATEMENTS)
        Iterator first{ input.begin() };
        const Iterator last{ input.end() };
        return boost::spirit::qi::phrase_parse(first, last, assignment_document.statement_placeholder, boost::spirit::qi::space, result) && first == last;
#else
        static_cast<void>(input);
        static_cast<void>(result);
        return false;
#endif
    }
private:
    assignment_symbols assignment_operator_; equality_symbols equality_operator_; relational_symbols relational_operator_; shift_symbols shift_operator_; additive_symbols additive_operator_; multiplicative_symbols multiplicative_operator_; prefix_symbols prefix_operator_; suffix_symbols suffix_operator_; escaped_chars escaped_char;
    lexical_rules<Iterator> lexical; primary_macro_rules<Iterator> primary_macro; unary_postfix_rules<Iterator> unary_postfix; arithmetic_comparison_rules<Iterator> arithmetic_comparison; assignment_document_rules<Iterator> assignment_document;
};
using grammar = document_grammar<std::string_view::const_iterator>;
}

#endif
