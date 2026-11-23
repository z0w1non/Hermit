#include "Hermit_parser.hpp"
#include "Hermit_parser_internal.hpp"
#include "Hermit_parser_impl.hpp"
#include "Hermit_builtin.hpp"
#include "Hermit_log.hpp"

namespace Hermit
{
    namespace parser
    {
        assignment_symbols::assignment_symbols()
        {
            add
            ("=", assignment_operator::assign)
                ("+=", assignment_operator::plus_assign)
                ("-=", assignment_operator::minus_assign)
                ("*=", assignment_operator::multiplies_assign)
                ("/=", assignment_operator::divides_assign)
                ("%=", assignment_operator::modulus_assign)
                ("<<=", assignment_operator::shift_left_assign)
                (">>=", assignment_operator::shift_right_assign)
                ("&=", assignment_operator::and_assign)
                ("^=", assignment_operator::xor_assign)
                ("|=", assignment_operator::or_assign)
                ;
        }

        equality_symbols::equality_symbols()
        {
            add
            ("==", equality_operator::equal)
                ("!=", equality_operator::not_equal)
                ;
        }

        relational_symbols::relational_symbols()
        {
            add
            ("<", relational_operator::less)
                (">", relational_operator::greater)
                ("<=", relational_operator::less_equal)
                (">=", relational_operator::greater_equal)
                ;
        }

        shift_symbols::shift_symbols()
        {
            add
            ("<<", shift_operator::shift_left)
                (">>", shift_operator::shift_right)
                ;
        }

        additive_symbols::additive_symbols()
        {
            add
            ("+", additive_operator::plus)
                ("-", additive_operator::minus)
                ;
        }

        multiplicative_symbols::multiplicative_symbols()
        {
            add
            ("*", multiplicative_operator::multiplies)
                ("/", multiplicative_operator::divides)
                ("%", multiplicative_operator::modulus)
                ;
        }

        prefix_symbols::prefix_symbols()
        {
            add
            ("++", prefix_operator::prefix_increment)
                ("--", prefix_operator::prefix_decrement)
                ("+", prefix_operator::prefix_plus)
                ("-", prefix_operator::prefix_minus)
                ("!", prefix_operator::logical_not)
                ("~", prefix_operator::bitwise_not)
                ;
        }

        suffix_symbols::suffix_symbols()
        {
            add
            ("++", suffix_operator::suffix_increment)
                ("--", suffix_operator::suffix_decrement)
                ;
        }

        escaped_chars::escaped_chars()
        {
            add
            ("\"", '\"')
                ("\'", '\'')
                ("\\", '\\')
                ("a", '\a')
                ("b", '\b')
                ("f", '\f')
                ("n", '\n')
                ("r", '\r')
                ("t", '\t')
                ;
        }

        std::string evaluate_ast_with_grammar(const abstract_syntax_tree_type&, const config&, const grammar&, context&);

        namespace
        {
            std::unique_ptr<grammar> make_grammar()
            {
                return std::make_unique<grammar>();
            }

            abstract_syntax_tree_type parse_with_grammar(std::string_view document, const grammar& grammar, context& ctx)
            {
                abstract_syntax_tree_type abstract_syntax_tree;
                grammar::iterator_type iter{ document.begin() };
                const grammar::iterator_type end{ document.end() };
                if (boost::spirit::qi::parse(iter, end, grammar, abstract_syntax_tree) && iter != end)
                {
                    std::ostringstream description;
                    description << "Parse failed at: " << std::string{ iter, end };
                    Hermit::throw_exception(macro_exception{} << error_info::description{ description.str() });
                }
                return abstract_syntax_tree;
            }

        }

        grammar_handle make_grammar_handle()
        {
            return grammar_handle{ std::shared_ptr<void>{ make_grammar().release(), [](void* ptr) { delete static_cast<grammar*>(ptr); } } };
        }

        abstract_syntax_tree_type parse(std::string_view document, const config& cfg, context& ctx)
        {
            const grammar_handle handle{ make_grammar_handle() };
            return parse(document, cfg, handle, ctx);
        }

        abstract_syntax_tree_type parse(std::string_view document, const config& cfg, const grammar_handle& handle, context& ctx)
        {
            const grammar* grammar_ptr{ static_cast<const grammar*>(handle.storage.get()) };
            if (grammar_ptr == nullptr)
            {
                Hermit::throw_exception(macro_exception{} << error_info::description{ "Invalid grammar handle" });
            }
            return parse_with_grammar(document, *grammar_ptr, ctx);
        }

        std::string evaluate_recursive(std::string input, const config& cfg, unsigned int max_depth, context& ctx)
        {
            const grammar_handle grammar{ make_grammar_handle() };

            unsigned int depth{};
            for (; depth < max_depth; ++depth)
            {
                if (input.find("{{") == std::string_view::npos)
                {
                    return input;
                }

                std::string evaluated{ evaluate(input, cfg, grammar, ctx) };

                if (evaluated == input)
                {
                    break;
                }

                input = std::move(evaluated);
            }

            if (depth >= max_depth)
            {
                Hermit::throw_exception(macro_exception{} << error_info::description{ "Maximum recursion depth reached" });
            }

            return input;
        }

        std::string evaluate(std::string_view input, const config& cfg, context& ctx)
        {
            const grammar_handle handle{ make_grammar_handle() };
            return evaluate(input, cfg, handle, ctx);
        }

        std::string evaluate(std::string_view input, const config& cfg, const grammar_handle& handle, context& ctx)
        {
            const grammar* grammar_ptr{ static_cast<const grammar*>(handle.storage.get()) };
            if (grammar_ptr == nullptr)
            {
                Hermit::throw_exception(macro_exception{} << error_info::description{ "Invalid grammar handle" });
            }
            const abstract_syntax_tree_type abstract_syntax_tree{ parse_with_grammar(input, *grammar_ptr, ctx) };
            return evaluate_ast_with_grammar(abstract_syntax_tree, cfg, *grammar_ptr, ctx);
        }

        std::string evaluate_ast_with_grammar(const abstract_syntax_tree_type& abstract_syntax_tree, const config& cfg, const grammar& grammar, context& ctx)
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
            if (expr.expressions.empty())
            {
                Hermit::throw_exception(macro_exception{});
            }
            value_reference_type last{};
            for (const assignment_expression_type& assignment_expression : expr.expressions)
            {
                last = evaluate_assignment_expression(assignment_expression, cfg, ctx);
            }
            if (expr.terminated)
            {
                return std::string{};
            }
            return last;
        }

        value_reference_type evaluate_assignment_expression(const assignment_expression_type& expr, const config& cfg, context& ctx)
        {
            return boost::apply_visitor(assignment_expression_visitor{ cfg, ctx }, expr);
        }

        value_reference_type evaluate_assignment_expression_node(const assignment_expression_node_type& expr, const config& cfg, context& ctx)
        {
            value_reference_type lhs{ evaluate_conditional_expression(expr.lhs, cfg, ctx) };
            value_reference_type rhs{ evaluate_assignment_expression(expr.rhs, cfg, ctx) };

            switch (expr.operator_)
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
                Hermit::throw_exception(logic_error{});
            }

            return lhs;
        }

        value_reference_type evaluate_conditional_expression(const conditional_expression_type& expr, const config& cfg, context& ctx)
        {
            return boost::apply_visitor(conditional_expression_visitor{ cfg, ctx }, expr);
        }

        value_reference_type evaluate_logical_or_expression(const logical_or_expression_type& expr, const config& cfg, context& ctx)
        {
            if (expr.empty())
            {
                Hermit::throw_exception(logic_error{});
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
                Hermit::throw_exception(logic_error{});
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
                    Hermit::throw_exception(logic_error{});
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
                    Hermit::throw_exception(logic_error{});
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
                    Hermit::throw_exception(logic_error{});
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
                    Hermit::throw_exception(logic_error{});
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
                    Hermit::throw_exception(logic_error{});
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
            value_reference_type operand{ evaluate_prefix_expression(expr.operand.get(), cfg, ctx) };
            switch (expr.operator_)
            {
            case prefix_operator::prefix_increment:
                return prefix_increment{}(operand);
            case prefix_operator::prefix_decrement:
                return prefix_decrement{}(operand);
            case prefix_operator::prefix_plus:
                return prefix_plus{}(operand);
            case prefix_operator::prefix_minus:
                return prefix_minus{}(operand);
            case prefix_operator::logical_not:
                return logical_not{}(operand);
            case prefix_operator::bitwise_not:
                return bitwise_not{}(operand);
            default:
                Hermit::throw_exception(logic_error{});
            }
        }

        value_reference_type evaluate_suffix_expression(const suffix_expression_type& expr, const config& cfg, context& ctx)
        {
            value_reference_type operand{ evaluate_parentheses_expression(expr.operand, cfg, ctx) };
            for (const suffix_operator& operator_ : expr.operators)
            {
                switch (operator_)
                {
                case suffix_operator::suffix_increment:
                    return prefix_increment{}(operand);
                case suffix_operator::suffix_decrement:
                    return prefix_decrement{}(operand);
                default:
                    Hermit::throw_exception(logic_error{});
                }
            }
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
            Hermit::throw_exception(macro_exception{});
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

    } // namespace parser

} // namespace Hermit

