#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include "Hermit_parser.hpp"
#include "Hermit_parser_internal.hpp"
#include "Hermit_parser_grammar_impl.hpp"
#include "Hermit_parser_evaluation_internal.hpp"
#include "Hermit_builtin.hpp"
#include "Hermit_errors.hpp"
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
                const bool parsed{ boost::spirit::qi::parse(iter, end, grammar, abstract_syntax_tree) };
                if (!parsed || iter != end)
                {
                    std::ostringstream description;
                    description << "Parse failed at: " << std::string{ iter, end };
                    HERMIT_LOG(error) << description.str();
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
            return evaluate_ast(abstract_syntax_tree, cfg, ctx);
        }

    } // namespace parser

} // namespace Hermit

