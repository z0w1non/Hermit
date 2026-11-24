#define HERMIT_PARSER_VISITORS_ONLY
#include "Hermit_parser_impl.hpp"
#undef HERMIT_PARSER_VISITORS_ONLY
#include "Hermit_config.hpp"
#include "Hermit_log.hpp"

namespace Hermit::parser
{
    namespace
    {
        struct statement_evaluation_visitor : boost::static_visitor<void>
        {
            statement_evaluation_visitor(const config& cfg, context& ctx)
                : cfg{ cfg }
                , ctx{ ctx }
            {
            }

            const config& cfg;
            context& ctx;

            void operator()(const expression_statement_type& statement) const
            {
                evaluate_expression(statement.expression, cfg, ctx);
            }

            void operator()(const assignment_statement_type& statement) const
            {
                evaluate_assignment_expression(statement.expression, cfg, ctx);
            }

            void operator()(const compound_statement_type& statement) const
            {
                for (const statement_type& child : statement.statements)
                {
                    boost::apply_visitor(*this, child);
                }
            }

            void operator()(const if_statement_type& statement) const
            {
                const value_reference_type condition{ evaluate_expression(statement.condition, cfg, ctx) };
                if (static_cast_<bool>{}(condition))
                {
                    boost::apply_visitor(*this, statement.statement);
                }
                else if (statement.else_statement)
                {
                    boost::apply_visitor(*this, *statement.else_statement);
                }
            }

            void operator()(const while_statement_type& statement) const
            {
                while (static_cast_<bool>{}(evaluate_expression(statement.condition, cfg, ctx)))
                {
                    boost::apply_visitor(*this, statement.statement);
                }
            }
        };
    }

    value_reference_type assign::operator()(value_reference_type& lhs, const value_reference_type& rhs) const
    {
        return boost::apply_visitor(visitor::assign_operator{ ctx }, lhs, rhs);
    }

    std::string node_visitor::operator()(const std::string& str) const
    {
        return str;
    }

    std::string node_visitor::operator()(const placeholder_type& value) const
    {
        try
        {
            const auto* expression{ boost::get<expression_type>(&value.content) };
            const std::string evaluated{ expression
                ? value_reference_to_string(evaluate_expression(*expression, cfg, ctx))
                : std::string{} };
            HERMIT_LOG(trace) << "Placeholder evaluated (" << evaluated << ")";
            return evaluated;
        }
        catch (const macro_exception&)
        {
            HERMIT_LOG(warning) << "Placeholder evaluation failed";
            return std::string{};
        }
    }




    value_reference_type assignment_expression_visitor::operator()(const assignment_expression_node_type& expr) const { return evaluate_assignment_expression_node(expr, cfg, ctx); }
    value_reference_type assignment_expression_visitor::operator()(const conditional_expression_type& expr) const { return evaluate_conditional_expression(expr, cfg, ctx); }
    value_reference_type assignment_expression_visitor::operator()(const assignment_expression_type& expr) const { return evaluate_assignment_expression(expr, cfg, ctx); }

    value_reference_type conditional_expression_visitor::operator()(const conditional_expression_node_type& value) const
    {
        const value_reference_type evaluated_condition{ evaluate_logical_or_expression(value.condition, cfg, ctx) };
        if (static_cast_<bool>{}(evaluated_condition))
        {
            return evaluate_expression(value.then_expr.get(), cfg, ctx);
        }
        return evaluate_conditional_expression(value.else_expr, cfg, ctx);
    }

    value_reference_type conditional_expression_visitor::operator()(const logical_or_expression_type& value) const { return evaluate_logical_or_expression(value, cfg, ctx); }
    value_reference_type prefix_expression_visitor::operator()(const prefix_expression_node_type& expr) const { return evaluate_prefix_expression_node(expr, cfg, ctx); }
    value_reference_type prefix_expression_visitor::operator()(const suffix_expression_type& expr) const { return evaluate_suffix_expression(expr, cfg, ctx); }
    value_reference_type prefix_expression_visitor::operator()(const prefix_expression_type& expr) const { return evaluate_prefix_expression(expr, cfg, ctx); }
    value_reference_type parentheses_expression_visitor::operator()(const macro_expression_type& expr) const { return evaluate_macro_expression(expr, cfg, ctx); }
    value_reference_type parentheses_expression_visitor::operator()(const expression_type& expr) const { return evaluate_expression(expr, cfg, ctx); }
    value_reference_type primary_visitor::operator()(const variable_type& variable) const { return evaluate_variable(variable, cfg, ctx); }
    value_reference_type primary_visitor::operator()(const value_type& value) const { return value; }
    value_reference_type macro_expression_visitor::operator()(const macro_expression_node_type& expr) const { return evaluate_macro_expression_node(expr, cfg, ctx); }
    value_reference_type macro_expression_visitor::operator()(const primary_type& primary) const { return evaluate_primary(primary, cfg, ctx); }
}
