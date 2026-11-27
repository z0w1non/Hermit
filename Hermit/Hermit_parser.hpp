#ifndef HERMIT_PARSER_HPP
#define HERMIT_PARSER_HPP

#include <memory>

#include "Hermit_parser_types.hpp"

namespace Hermit
{
    struct config;

    namespace parser
    {
        struct assign
        {
            context& ctx;
            explicit assign(context& ctx) : ctx{ ctx } {}
            value_reference_type operator()(value_reference_type& lhs, const value_reference_type& rhs) const;
        };

        struct grammar_handle
        {
            std::shared_ptr<void> storage;
        };

        grammar_handle make_grammar_handle();

        abstract_syntax_tree_type parse(std::string_view input, const config& cfg, context& ctx);
        abstract_syntax_tree_type parse(std::string_view input, const config& cfg, const grammar_handle& handle, context& ctx);

        std::string evaluate_recursive(std::string input, const config& cfg, unsigned int max_depth, context& ctx);
        std::string evaluate(std::string_view input, const config& cfg, context& ctx);
        std::string evaluate(std::string_view input, const config& cfg, const grammar_handle& handle, context& ctx);

        value_reference_type evaluate_expression(const expression_type& expr, const config& cfg, context& ctx);
        value_reference_type evaluate_assignment_expression(const assignment_expression_type& expr, const config& cfg, context& ctx);
        value_reference_type apply_assignment_operator(assignment_operator operator_, value_reference_type& lhs, const value_reference_type& rhs, context& ctx);
        value_reference_type evaluate_assignment_expression_node(const assignment_expression_node_type& expr, const config& cfg, context& ctx);
        value_reference_type evaluate_conditional_expression(const conditional_expression_type& expr, const config& cfg, context& ctx);
        value_reference_type evaluate_logical_or_expression(const logical_or_expression_type& expr, const config& cfg, context& ctx);
        value_reference_type evaluate_logical_and_expression(const logical_and_expression_type& expr, const config& cfg, context& ctx);
        value_reference_type evaluate_or_expression(const or_expression_type& expr, const config& cfg, context& ctx);
        value_reference_type evaluate_xor_expression(const xor_expression_type& expr, const config& cfg, context& ctx);
        value_reference_type evaluate_and_expression(const and_expression_type& expr, const config& cfg, context& ctx);
        value_reference_type evaluate_equality_expression(const equality_expression_type& expr, const config& cfg, context& ctx);
        value_reference_type evaluate_relational_expression(const relational_expression_type& expr, const config& cfg, context& ctx);
        value_reference_type evaluate_shift_expression(const shift_expression_type& expr, const config& cfg, context& ctx);
        value_reference_type evaluate_additive_expression(const additive_expression_type& expr, const config& cfg, context& ctx);
        value_reference_type evaluate_multiplicative_expression(const multiplicative_expression_type& expr, const config& cfg, context& ctx);
        value_reference_type evaluate_prefix_expression(const prefix_expression_type& expr, const config& cfg, context& ctx);
        value_reference_type evaluate_prefix_expression_node(const prefix_expression_node_type& expr, const config& cfg, context& ctx);
        value_reference_type evaluate_suffix_expression(const suffix_expression_type& expr, const config& cfg, context& ctx);
        value_reference_type evaluate_parentheses_expression(const parentheses_expression_type& expr, const config& cfg, context& ctx);
        value_reference_type evaluate_macro_expression(const macro_expression_type& expr, const config& cfg, context& ctx);
        value_reference_type evaluate_macro_expression_node(const macro_expression_node_type& expr, const config& cfg, context& ctx);
        value_reference_type evaluate_primary(const primary_type& primary, const config& cfg, context& ctx);
        value_reference_type evaluate_variable(const variable_type& symbol, const config& cfg, context& ctx);
        value_reference_type value_as_reference_to_value_reference(value_type& value);
        value_reference_type value_as_value_to_value_reference(const value_type& value);

        value_type value_reference_to_value(const value_reference_type& value_reference);
    } // namespace parser
}

#endif
