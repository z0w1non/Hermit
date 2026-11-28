#ifndef HERMIT_PARSER_UNARY_POSTFIX_RULES_HPP
#define HERMIT_PARSER_UNARY_POSTFIX_RULES_HPP

#include "parser_primary_macro_rules.hpp"

namespace hermit::parser
{
    template<typename Iterator>
    struct unary_postfix_rules
    {
        template<typename ... Args>
        using skipped_rule = boost::spirit::qi::rule<Iterator, boost::spirit::qi::space_type, Args ...>;
        skipped_rule<prefix_expression_type()> prefix_expression;
        skipped_rule<prefix_expression_node_type()> prefix_expression_node;
        skipped_rule<suffix_expression_type()> suffix_expression;
        skipped_rule<parentheses_expression_type()> parentheses_expression;
    };
}

#endif
