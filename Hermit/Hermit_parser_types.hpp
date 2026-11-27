#ifndef HERMIT_PARSER_TYPES_HPP
#define HERMIT_PARSER_TYPES_HPP

#include <string>
#include <vector>
#include <boost/optional.hpp>
#include <boost/variant.hpp>
#include <boost/variant/recursive_variant.hpp>
#include <boost/fusion/include/adapt_struct.hpp>
#include "Hermit_context.hpp"

namespace Hermit
{
    namespace parser
    {
        enum class assignment_operator
        {
            assign,             // =
            plus_assign,        // +=
            minus_assign,       // -=
            multiplies_assign,  // *=
            divides_assign,     // /=
            modulus_assign,     // %=
            shift_left_assign,  // <<=
            shift_right_assign, // >>=
            and_assign,         // &=
            xor_assign,         // ^=
            or_assign           // |=
        };

        enum class equality_operator
        {
            equal,    // ==
            not_equal // !=
        };

        enum class relational_operator
        {
            less,         // <
            greater,      // >
            less_equal,   // <=
            greater_equal // >=
        };

        enum class shift_operator
        {
            shift_left, // <<
            shift_right // >>
        };

        enum class additive_operator
        {
            plus, // +
            minus // -
        };

        enum class multiplicative_operator
        {
            multiplies, // *
            divides,    // /
            modulus     // %
        };

        enum class prefix_operator
        {
            prefix_increment, // ++a
            prefix_decrement, // --a
            prefix_plus,      // +a
            prefix_minus,     // -a
            logical_not,      // !
            bitwise_not       // ~
        };

        enum class suffix_operator
        {
            suffix_increment, // ++
            suffix_decrement  // --
        };

        struct variable_type
        {
            std::string name;
        };
        using primary_type = boost::variant<value_type, variable_type>;

        template<typename Operand, typename Operator>
        struct operator_operand_pair
        {
            using operand_type = Operand;
            using operator_type = Operator;
            operator_type operator_;
            operand_type operand;
        };

        template<typename LowerExpression, typename Operator>
        struct basic_binary_expression
        {
            using lower_expression_type = LowerExpression;
            using operator_type = Operator;
            lower_expression_type first;
            std::vector<operator_operand_pair<lower_expression_type, operator_type>> rest;
        };

        struct macro_expression_node_type;
        using macro_expression_type = boost::variant<primary_type, boost::recursive_wrapper<macro_expression_node_type>>;
        struct expression_type;
        using parentheses_expression_type = boost::variant<macro_expression_type, boost::recursive_wrapper<expression_type>>;

        struct suffix_expression_type
        {
            parentheses_expression_type operand;
            std::vector<suffix_operator> operators;
        };

        struct prefix_expression_node_type;
        using prefix_expression_type = boost::make_recursive_variant<suffix_expression_type, prefix_expression_node_type, boost::recursive_variant_>::type;
        struct prefix_expression_node_type
        {
            prefix_operator operator_;
            boost::recursive_wrapper<prefix_expression_type> operand;
        };

        using multiplicative_expression_type = basic_binary_expression<prefix_expression_type, multiplicative_operator>;
        using additive_expression_type = basic_binary_expression<multiplicative_expression_type, additive_operator>;
        using shift_expression_type = basic_binary_expression<additive_expression_type, shift_operator>;
        using relational_expression_type = basic_binary_expression<shift_expression_type, relational_operator>;
        using equality_expression_type = basic_binary_expression<relational_expression_type, equality_operator>;
        using and_expression_type = std::vector<equality_expression_type>;
        using xor_expression_type = std::vector<and_expression_type>;
        using or_expression_type = std::vector<xor_expression_type>;
        using logical_and_expression_type = std::vector<or_expression_type>;
        using logical_or_expression_type = std::vector<logical_and_expression_type>;

        struct conditional_expression_node_type;
        using conditional_expression_type = boost::variant<logical_or_expression_type, boost::recursive_wrapper<conditional_expression_node_type>>;
        struct conditional_expression_node_type
        {
            logical_or_expression_type condition;
            boost::recursive_wrapper<expression_type> then_expr;
            conditional_expression_type else_expr;
        };

        struct assignment_expression_node_type;
        using assignment_expression_type = boost::variant<conditional_expression_type, boost::recursive_wrapper<assignment_expression_node_type>>;
        struct assignment_expression_node_type
        {
            conditional_expression_type lhs;
            assignment_operator operator_{ assignment_operator::assign };
            assignment_expression_type rhs;
        };

        struct macro_expression_node_type
        {
            std::string name;
            std::vector<assignment_expression_type> arguments;
        };

        struct expression_type
            : std::vector<assignment_expression_type>
        {
            using vector::vector;
        };

        struct assignment_statement_type
        {
            assignment_expression_type expression;
        };

        struct simple_assignment_statement_type
        {
            std::string name;
            assignment_operator operator_{ assignment_operator::assign };
            assignment_expression_type expression;
        };

        struct expression_statement_type
        {
            expression_type expression;
        };

        struct compound_statement_type;
        struct if_statement_type;
        struct while_statement_type;

        using statement_type = boost::variant<
            expression_statement_type,
            assignment_statement_type,
            simple_assignment_statement_type,
            boost::recursive_wrapper<compound_statement_type>,
            boost::recursive_wrapper<if_statement_type>,
            boost::recursive_wrapper<while_statement_type>>;

        struct compound_statement_type
        {
            std::vector<statement_type> statements;
        };

        struct if_statement_type
        {
            expression_type condition;
            statement_type statement;
            boost::optional<statement_type> else_statement;
        };

        struct while_statement_type
        {
            expression_type condition;
            statement_type statement;
        };

        using placeholder_result_type = boost::variant<std::vector<statement_type>, expression_type>;

        struct placeholder_type
        {
            placeholder_result_type content;
        };


        using node_type = boost::variant<std::string, placeholder_type>;
        using abstract_syntax_tree_type = std::vector<node_type>;

    }
}

BOOST_FUSION_ADAPT_STRUCT(Hermit::parser::suffix_expression_type, operand, operators)
BOOST_FUSION_ADAPT_STRUCT(Hermit::parser::prefix_expression_node_type, operator_, operand)
BOOST_FUSION_ADAPT_TPL_STRUCT((Operand)(Operator), (Hermit::parser::operator_operand_pair)(Operand)(Operator), operator_, operand)
BOOST_FUSION_ADAPT_TPL_STRUCT((LowerExpression)(Operator), (Hermit::parser::basic_binary_expression)(LowerExpression)(Operator), first, rest)
BOOST_FUSION_ADAPT_STRUCT(Hermit::parser::conditional_expression_node_type, condition, then_expr, else_expr)
BOOST_FUSION_ADAPT_STRUCT(Hermit::parser::macro_expression_node_type, name, arguments)
BOOST_FUSION_ADAPT_STRUCT(Hermit::parser::variable_type, name)
BOOST_FUSION_ADAPT_STRUCT(Hermit::parser::assignment_expression_node_type, lhs, operator_, rhs)
BOOST_FUSION_ADAPT_STRUCT(Hermit::parser::assignment_statement_type, expression)
BOOST_FUSION_ADAPT_STRUCT(Hermit::parser::simple_assignment_statement_type, name, operator_, expression)
BOOST_FUSION_ADAPT_STRUCT(Hermit::parser::expression_statement_type, expression)
BOOST_FUSION_ADAPT_STRUCT(Hermit::parser::compound_statement_type, statements)
BOOST_FUSION_ADAPT_STRUCT(Hermit::parser::if_statement_type, condition, statement, else_statement)
BOOST_FUSION_ADAPT_STRUCT(Hermit::parser::while_statement_type, condition, statement)
BOOST_FUSION_ADAPT_STRUCT(Hermit::parser::placeholder_type, content)

#endif
