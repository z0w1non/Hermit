#ifndef HERMIT_PARSER_ASSIGNMENT_DOCUMENT_RULES_HPP
#define HERMIT_PARSER_ASSIGNMENT_DOCUMENT_RULES_HPP

#include "parser_arithmetic_comparison_rules.hpp"

namespace hermit::parser
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
        boost::spirit::qi::rule<Iterator, placeholder_result_type()> statement_placeholder;
        boost::spirit::qi::rule<Iterator, placeholder_result_type()> expression_placeholder;
        skipped_rule<statement_type()> statement;
        skipped_rule<std::vector<statement_type>()> statements;
        skipped_rule<expression_statement_type()> expression_statement;
        skipped_rule<assignment_statement_type()> assignment_statement;
        skipped_rule<simple_assignment_statement_type()> simple_assignment_statement;
        skipped_rule<compound_statement_type()> compound_statement;
        skipped_rule<if_statement_type()> if_statement;
        skipped_rule<while_statement_type()> while_statement;
        skipped_rule<expression_type()> expression;
        skipped_rule<assignment_expression_type()> assignment_expression;
        skipped_rule<assignment_expression_node_type()> assignment_expression_node;
        skipped_rule<conditional_expression_type()> conditional_expression;
        skipped_rule<conditional_expression_node_type()> conditional_expression_node;
    };
}

#endif
