#ifndef HERMIT_PARSER_ARITHMETIC_COMPARISON_RULES_HPP
#define HERMIT_PARSER_ARITHMETIC_COMPARISON_RULES_HPP

#include "parser_unary_postfix_rules.hpp"

namespace hermit::parser
{
    template<typename Iterator>
    struct arithmetic_comparison_rules
    {
        template<typename ... Args>
        using skipped_rule = boost::spirit::qi::rule<Iterator, boost::spirit::qi::space_type, Args ...>;
        skipped_rule<multiplicative_expression_type()> multiplicative_expression;
        skipped_rule<additive_expression_type()> additive_expression;
        skipped_rule<shift_expression_type()> shift_expression;
        skipped_rule<relational_expression_type()> relational_expression;
        skipped_rule<equality_expression_type()> equality_expression;
        skipped_rule<and_expression_type()> and_expression;
        skipped_rule<xor_expression_type()> xor_expression;
        skipped_rule<or_expression_type()> or_expression;
        skipped_rule<logical_and_expression_type()> logical_and_expression;
        skipped_rule<logical_or_expression_type()> logical_or_expression;
    };
}

#endif
