#ifndef HERMIT_PARSER_PRIMARY_MACRO_RULES_HPP
#define HERMIT_PARSER_PRIMARY_MACRO_RULES_HPP

#include "parser_lexical_rules.hpp"

namespace hermit::parser
{
    template<typename Iterator>
    struct primary_macro_rules
    {
        template<typename ... Args>
        using skipped_rule = boost::spirit::qi::rule<Iterator, boost::spirit::qi::space_type, Args ...>;
        skipped_rule<macro_expression_type()> macro_expression;
        skipped_rule<macro_expression_node_type()> macro_expression_node;
        skipped_rule<primary_type()> primary;
        skipped_rule<variable_type()> variable;
        skipped_rule<std::vector<assignment_expression_type>()> arguments;
    };
}

#endif
