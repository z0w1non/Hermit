#ifndef HERMIT_PARSER_ASSIGNMENT_DOCUMENT_RULES_HPP
#define HERMIT_PARSER_ASSIGNMENT_DOCUMENT_RULES_HPP

#include "Hermit_parser_arithmetic_comparison_rules.hpp"

namespace Hermit::parser
{
    template<typename Iterator>
    struct assignment_document_rules
    {
        template<typename ... Args>
        using skipped_rule = boost::spirit::qi::rule<Iterator, boost::spirit::qi::space_type, Args ...>;
        boost::spirit::qi::rule<Iterator, abstract_syntax_tree_type()> abstract_syntax_tree;
        boost::spirit::qi::rule<Iterator, node_type()> node;
        boost::spirit::qi::rule<Iterator, std::string()> plain_text;
        boost::spirit::qi::rule<Iterator, placeholder_type()> placeholder;
        skipped_rule<expression_type()> expression;
        skipped_rule<assignment_expression_type()> assignment_expression;
        skipped_rule<assignment_expression_node_type()> assignment_expression_node;
        skipped_rule<conditional_expression_type()> conditional_expression;
        skipped_rule<conditional_expression_node_type()> conditional_expression_node;
    };
}

#endif
