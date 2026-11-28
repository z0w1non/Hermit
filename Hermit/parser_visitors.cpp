#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "parser_visitor_impl.hpp"
#include "parser_evaluation_internal.hpp"
#include "builtin.hpp"
#include "config.hpp"
#include "log.hpp"

namespace hermit::parser
{
    namespace
    {
        constexpr std::size_t maximum_statement_loop_iterations{ 10'000 };

        struct statement_evaluation_visitor : boost::static_visitor<void>
        {
            statement_evaluation_visitor(const config& cfg, context& ctx, std::size_t& remaining_loop_iterations)
                : cfg{ cfg }
                , ctx{ ctx }
                , remaining_loop_iterations{ remaining_loop_iterations }
            {
            }

            const config& cfg;
            context& ctx;
            std::size_t& remaining_loop_iterations;

            void operator()(const expression_statement_type& statement) const
            {
                evaluate_expression(statement.expression, cfg, ctx);
            }

            void operator()(const assignment_statement_type& statement) const
            {
                evaluate_assignment_expression(statement.expression, cfg, ctx);
            }

            void operator()(const simple_assignment_statement_type& statement) const
            {
                value_reference_type lhs{ undefined_variable_type{ statement.name } };
                if (value_type* value{ ctx.get(statement.name) })
                {
                    lhs = value_as_reference_to_value_reference(*value);
                }
                const value_reference_type rhs{ evaluate_assignment_expression(statement.expression, cfg, ctx) };
                apply_assignment_operator(statement.operator_, lhs, rhs, ctx);
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
                    if (remaining_loop_iterations == 0U)
                    {
                        hermit::throw_exception(macro_exception{} << error_info::description{ "Statement loop iteration budget exceeded" });
                    }
                    --remaining_loop_iterations;
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
#if defined(HERMIT_ENABLE_EXPERIMENTAL_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_EXPRESSION_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_ASSIGNMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_ASSIGNMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_IF_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_WHILE_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_INCREMENT_DECREMENT)
            const auto* statements{ boost::get<std::vector<statement_type>>(&value.content) };
            if (statements != nullptr)
            {
                std::size_t remaining_loop_iterations{ maximum_statement_loop_iterations };
                for (const statement_type& statement : *statements)
                {
                    boost::apply_visitor(statement_evaluation_visitor{ cfg, ctx, remaining_loop_iterations }, statement);
                }
                return std::string{};
            }
#endif
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
        std::string evaluate_ast(const abstract_syntax_tree_type& abstract_syntax_tree, const config& cfg, context& ctx)
        {
            std::string result;
            for (const node_type& node : abstract_syntax_tree)
            {
                result += boost::apply_visitor(node_visitor{ cfg, ctx }, node);
            }
            return result;
        }


        value_reference_type evaluate_expression(const expression_type& expr, const config& cfg, context& ctx)
        {
            if (expr.empty())
            {
                hermit::throw_exception(macro_exception{});
            }
            value_reference_type last{};
            for (const assignment_expression_type& assignment_expression : expr)
            {
                last = evaluate_assignment_expression(assignment_expression, cfg, ctx);
            }
            return last;
        }

        value_reference_type evaluate_assignment_expression(const assignment_expression_type& expr, const config& cfg, context& ctx)
        {
            return boost::apply_visitor(assignment_expression_visitor{ cfg, ctx }, expr);
        }

        value_reference_type apply_assignment_operator(assignment_operator operator_, value_reference_type& lhs, const value_reference_type& rhs, context& ctx)
        {
            switch (operator_)
            {
            case assignment_operator::assign:
                assign{ ctx }(lhs, rhs);
                break;
            case assignment_operator::plus_assign:
                plus_assign{}(lhs, rhs);
                break;
            case assignment_operator::minus_assign:
                minus_assign{}(lhs, rhs);
                break;
            case assignment_operator::multiplies_assign:
                multiplies_assign{}(lhs, rhs);
                break;
            case assignment_operator::divides_assign:
                divides_assign{}(lhs, rhs);
                break;
            case assignment_operator::modulus_assign:
                modulus_assign{}(lhs, rhs);
                break;
            case assignment_operator::shift_left_assign:
                shift_left_assign{}(lhs, rhs);
                break;
            case assignment_operator::shift_right_assign:
                shift_right_assign{}(lhs, rhs);
                break;
            case assignment_operator::and_assign:
                and_assign{}(lhs, rhs);
                break;
            case assignment_operator::xor_assign:
                xor_assign{}(lhs, rhs);
                break;
            case assignment_operator::or_assign:
                or_assign{}(lhs, rhs);
                break;
            default:
                hermit::throw_exception(logic_error{});
            }

            return lhs;
        }

        value_reference_type evaluate_assignment_expression_node(const assignment_expression_node_type& expr, const config& cfg, context& ctx)
        {
            value_reference_type lhs{ evaluate_conditional_expression(expr.lhs, cfg, ctx) };
            const value_reference_type rhs{ evaluate_assignment_expression(expr.rhs, cfg, ctx) };
            return apply_assignment_operator(expr.operator_, lhs, rhs, ctx);
        }

        value_reference_type evaluate_conditional_expression(const conditional_expression_type& expr, const config& cfg, context& ctx)
        {
            return boost::apply_visitor(conditional_expression_visitor{ cfg, ctx }, expr);
        }

        value_reference_type evaluate_logical_or_expression(const logical_or_expression_type& expr, const config& cfg, context& ctx)
        {
            if (expr.empty())
            {
                hermit::throw_exception(logic_error{});
            }
            value_reference_type lhs{ evaluate_logical_and_expression(expr.front(), cfg, ctx) };
            if (expr.size() == 1)
            {
                return lhs;
            }
            if (static_cast_<bool>{}(lhs))
            {
                return true;
            }
            for (std::vector<logical_and_expression_type>::const_iterator iter{ expr.begin() + 1 }; iter != expr.end(); ++iter)
            {
                const auto& rhs = *iter;
                lhs = evaluate_logical_and_expression(rhs, cfg, ctx);
                if (static_cast_<bool>{}(lhs))
                {
                    return true;
                }
            }
            return false;
        }

        value_reference_type evaluate_logical_and_expression(const logical_and_expression_type& expr, const config& cfg, context& ctx)
        {
            if (expr.empty())
            {
                hermit::throw_exception(logic_error{});
            }
            value_reference_type lhs{ evaluate_or_expression(expr.front(), cfg, ctx) };
            if (expr.size() == 1)
            {
                return lhs;
            }
            if (!static_cast_<bool>{}(lhs))
            {
                return false;
            }
            for (std::vector<or_expression_type>::const_iterator iter{ expr.begin() + 1 }; iter != expr.end(); ++iter)
            {
                const or_expression_type& rhs = *iter;
                lhs = evaluate_or_expression(rhs, cfg, ctx);
                if (!static_cast_<bool>{}(lhs))
                {
                    return false;
                }
            }
            return true;
        }

        value_reference_type evaluate_or_expression(const or_expression_type& expr, const config& cfg, context& ctx)
        {
            return accumulate_expression(expr.begin(), expr.end(), evaluate_xor_expression, or_{}, cfg, ctx);
        }

        value_reference_type evaluate_xor_expression(const xor_expression_type& expr, const config& cfg, context& ctx)
        {
            return accumulate_expression(expr.begin(), expr.end(), evaluate_and_expression, xor_{}, cfg, ctx);
        }

        value_reference_type evaluate_and_expression(const and_expression_type& expr, const config& cfg, context& ctx)
        {
            return accumulate_expression(expr.begin(), expr.end(), evaluate_equality_expression, and_{}, cfg, ctx);
        }

        value_reference_type evaluate_equality_expression(const equality_expression_type& expr, const config& cfg, context& ctx)
        {
            value_reference_type lhs{ evaluate_relational_expression(expr.first, cfg, ctx) };
            for (const auto& [operator_, operand] : expr.rest)
            {
                const value_reference_type rhs{ evaluate_relational_expression(operand, cfg, ctx) };
                switch (operator_)
                {
                case equality_operator::equal:
                    lhs = equal{}(lhs, rhs);
                    break;
                case equality_operator::not_equal:
                    lhs = not_equal{}(lhs, rhs);
                    break;
                default:
                    hermit::throw_exception(logic_error{});
                }
            }
            return lhs;
        }

        value_reference_type evaluate_relational_expression(const relational_expression_type& expr, const config& cfg, context& ctx)
        {
            value_reference_type lhs{ evaluate_shift_expression(expr.first, cfg, ctx) };
            for (const auto& [operator_, operand] : expr.rest)
            {
                const value_reference_type rhs{ evaluate_shift_expression(operand, cfg, ctx) };
                switch (operator_)
                {
                case relational_operator::less:
                    lhs = less{}(lhs, rhs);
                    break;
                case relational_operator::greater:
                    lhs = greater{}(lhs, rhs);
                    break;
                case relational_operator::less_equal:
                    lhs = less_equal{}(lhs, rhs);
                    break;
                case relational_operator::greater_equal:
                    lhs = greater_equal{}(lhs, rhs);
                    break;
                default:
                    hermit::throw_exception(logic_error{});
                }
            }
            return lhs;
        }

        value_reference_type evaluate_shift_expression(const shift_expression_type& expr, const config& cfg, context& ctx)
        {
            value_reference_type lhs{ evaluate_additive_expression(expr.first, cfg, ctx) };
            for (const auto& [operator_, operand] : expr.rest)
            {
                const value_reference_type rhs{ evaluate_additive_expression(operand, cfg, ctx) };
                switch (operator_)
                {
                case shift_operator::shift_left:
                    lhs = shift_left{}(lhs, rhs);
                    break;
                case shift_operator::shift_right:
                    lhs = shift_right{}(lhs, rhs);
                    break;
                default:
                    hermit::throw_exception(logic_error{});
                }
            }
            return lhs;
        }

        value_reference_type evaluate_additive_expression(const additive_expression_type& expr, const config& cfg, context& ctx)
        {
            value_reference_type lhs{ evaluate_multiplicative_expression(expr.first, cfg, ctx) };
            for (const auto& [operator_, operand] : expr.rest)
            {
                const value_reference_type rhs{ evaluate_multiplicative_expression(operand, cfg, ctx) };
                switch (operator_)
                {
                case additive_operator::plus:
                    lhs = plus{}(lhs, rhs);
                    break;
                case additive_operator::minus:
                    lhs = minus{}(lhs, rhs);
                    break;
                default:
                    hermit::throw_exception(logic_error{});
                }
            }
            return lhs;
        }

        value_reference_type evaluate_multiplicative_expression(const multiplicative_expression_type& expr, const config& cfg, context& ctx)
        {
            value_reference_type lhs{ evaluate_prefix_expression(expr.first, cfg, ctx) };
            for (const auto& [operator_, operand] : expr.rest)
            {
                const value_reference_type rhs{ evaluate_prefix_expression(operand, cfg, ctx) };
                switch (operator_)
                {
                case multiplicative_operator::multiplies:
                    lhs = multiplies{}(lhs, rhs);
                    break;
                case multiplicative_operator::divides:
                    lhs = divides{}(lhs, rhs);
                    break;
                case multiplicative_operator::modulus:
                    lhs = modulus{}(lhs, rhs);
                    break;
                default:
                    hermit::throw_exception(logic_error{});
                }
            }
            return lhs;
        }

        value_reference_type evaluate_prefix_expression(const prefix_expression_type& expr, const config& cfg, context& ctx)
        {
            return boost::apply_visitor(prefix_expression_visitor{ cfg, ctx }, expr);
        }

        value_reference_type evaluate_prefix_expression_node(const prefix_expression_node_type& expr, const config& cfg, context& ctx)
        {
            switch (expr.operator_)
            {
            case prefix_operator::prefix_increment:
            case prefix_operator::prefix_decrement:
            {
                const prefix_expression_type& target{ expr.operand.get() };
                const suffix_expression_type* suffix{ boost::get<suffix_expression_type>(&target) };
                const macro_expression_type* macro{ suffix && suffix->operators.empty() ? boost::get<macro_expression_type>(&suffix->operand) : nullptr };
                const primary_type* primary{ macro ? boost::get<primary_type>(macro) : nullptr };
                const variable_type* variable{ primary ? boost::get<variable_type>(primary) : nullptr };
                value_type* value{ variable ? ctx.get(variable->name) : nullptr };
                int* integer{ value ? boost::get<int>(value) : nullptr };
                if (integer == nullptr
                    || (expr.operator_ == prefix_operator::prefix_increment && *integer == (std::numeric_limits<int>::max)())
                    || (expr.operator_ == prefix_operator::prefix_decrement && *integer == (std::numeric_limits<int>::min)()))
                {
                    hermit::throw_exception(macro_exception{});
                }
                *integer += expr.operator_ == prefix_operator::prefix_increment ? 1 : -1;
                return *integer;
            }
            case prefix_operator::prefix_plus:
            {
                value_reference_type operand{ evaluate_prefix_expression(expr.operand.get(), cfg, ctx) };
                return prefix_plus{}(operand);
            }
            case prefix_operator::prefix_minus:
            {
                value_reference_type operand{ evaluate_prefix_expression(expr.operand.get(), cfg, ctx) };
                return prefix_minus{}(operand);
            }
            case prefix_operator::logical_not:
            {
                value_reference_type operand{ evaluate_prefix_expression(expr.operand.get(), cfg, ctx) };
                return logical_not{}(operand);
            }
            case prefix_operator::bitwise_not:
            {
                value_reference_type operand{ evaluate_prefix_expression(expr.operand.get(), cfg, ctx) };
                return bitwise_not{}(operand);
            }
            default:
                hermit::throw_exception(logic_error{});
            }
        }

        value_reference_type evaluate_suffix_expression(const suffix_expression_type& expr, const config& cfg, context& ctx)
        {
            if (!expr.operators.empty())
            {
                if (expr.operators.size() != 1U)
                {
                    hermit::throw_exception(macro_exception{});
                }
                const macro_expression_type* macro{ boost::get<macro_expression_type>(&expr.operand) };
                const primary_type* primary{ macro ? boost::get<primary_type>(macro) : nullptr };
                const variable_type* variable{ primary ? boost::get<variable_type>(primary) : nullptr };
                value_type* value{ variable ? ctx.get(variable->name) : nullptr };
                int* integer{ value ? boost::get<int>(value) : nullptr };
                const suffix_operator operator_{ expr.operators.front() };
                if (integer == nullptr
                    || (operator_ == suffix_operator::suffix_increment && *integer == (std::numeric_limits<int>::max)())
                    || (operator_ == suffix_operator::suffix_decrement && *integer == (std::numeric_limits<int>::min)()))
                {
                    hermit::throw_exception(macro_exception{});
                }
                const int previous_value{ *integer };
                *integer += operator_ == suffix_operator::suffix_increment ? 1 : -1;
                return previous_value;
            }
            value_reference_type operand{ evaluate_parentheses_expression(expr.operand, cfg, ctx) };
            return operand;
        }

        value_reference_type evaluate_parentheses_expression(const parentheses_expression_type& expr, const config& cfg, context& ctx)
        {
            return boost::apply_visitor(parentheses_expression_visitor{ cfg, ctx }, expr);
        }

        value_reference_type evaluate_macro_expression(const macro_expression_type& expr, const config& cfg, context& ctx)
        {
            return boost::apply_visitor(macro_expression_visitor{ cfg, ctx }, expr);
        }

        value_reference_type evaluate_macro_expression_node(const macro_expression_node_type& expr, const config& cfg, context& ctx)
        {
            std::vector<value_type> evaluated_args;
            for (const assignment_expression_type& arg : expr.arguments)
            {
                const value_reference_type evaluated_arg{ evaluate_assignment_expression(arg, cfg, ctx) };
                evaluated_args.push_back(value_reference_to_value(evaluated_arg));
            }

            if (const std::optional<builtin::macro_type> macro{ builtin::get_macro(expr.name) }; macro)
            {
                try
                {
                    value_type evaluated{ (*macro)({ evaluated_args, cfg, ctx }) };
                    HERMIT_LOG(trace) << "Macro evaluated (" << expr.name << " => " << value_to_string(evaluated) << ")";
                    return value_as_value_to_value_reference(evaluated);
                }
                catch (const boost::exception&)
                {
                    HERMIT_LOG(warning) << "Evaluation failed (" << expr.name << ")";
                    throw_nested_exception(macro_exception{});
                }
            }

            HERMIT_LOG(warning) << "Macro not found (" << expr.name << ")";
            hermit::throw_exception(macro_exception{});
        }

        value_reference_type evaluate_primary(const primary_type& primary, const config& cfg, context& ctx)
        {
            return boost::apply_visitor(primary_visitor{ cfg, ctx }, primary);
        }

        value_reference_type evaluate_variable(const variable_type& variable, const config& cfg, context& ctx)
        {
            if (value_type* variable_value_ptr{ ctx.get(variable.name) }; variable_value_ptr)
            {
                HERMIT_LOG(trace) << "Variable found (" << variable.name << "=" << value_to_string(*variable_value_ptr) << ")";
                return value_as_reference_to_value_reference(*variable_value_ptr);
            }
            HERMIT_LOG(trace) << "Variable not found (" << variable.name << ")";
            return undefined_variable_type{ variable.name };
        }

        value_reference_type value_as_reference_to_value_reference(value_type& value)
        {
            return boost::apply_visitor(value_as_reference_to_value_reference_visitor{}, value);
        }

        value_reference_type value_as_value_to_value_reference(const value_type& value)
        {
            return boost::apply_visitor(value_as_value_to_value_reference_visitor{}, value);
        }

        value_type value_reference_to_value(const value_reference_type& value_reference)
        {
            return boost::apply_visitor(value_reference_to_value_visitor(), value_reference);
        }

}
