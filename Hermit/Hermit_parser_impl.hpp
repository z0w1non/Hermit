#ifndef HERMIT_PARSER_IMPL_HPP
#define HERMIT_PARSER_IMPL_HPP

#include "Hermit_parser.hpp"
#include "Hermit_parser_internal.hpp"
#include "Hermit_errors.hpp"
#include "Hermit_templates_core.hpp"

namespace boost::spirit::traits
{
    template<>
    struct transform_attribute<Hermit::parser::placeholder_type, Hermit::parser::placeholder_result_type, boost::spirit::qi::domain>
    {
        using type = Hermit::parser::placeholder_result_type;
        static type pre(Hermit::parser::placeholder_type&) { return {}; }
        static void post(Hermit::parser::placeholder_type& value, type content) { value.content = std::move(content); }
        static void fail(Hermit::parser::placeholder_type&) {}
    };

    template<typename Alternative>
        requires requires(Hermit::parser::statement_type& statement, Alternative&& alternative)
        {
            statement = std::forward<Alternative>(alternative);
        }
    struct transform_attribute<Hermit::parser::statement_type, Alternative, boost::spirit::qi::domain>
    {
        using type = Alternative;

        static type pre(Hermit::parser::statement_type&)
        {
            return {};
        }

        static void post(Hermit::parser::statement_type& statement, type alternative)
        {
            statement = std::move(alternative);
        }

        static void fail(Hermit::parser::statement_type&)
        {
        }
    };

}

namespace Hermit
{
    namespace parser
    {
        #ifndef HERMIT_PARSER_VISITORS_ONLY
        template<typename Iterator>
        document_grammar<Iterator>::document_grammar()
            : document_grammar::base_type(assignment_document.abstract_syntax_tree)
        {
            namespace qi = boost::spirit::qi;

            using qi::bool_;
            using qi::char_;
            using qi::int_;
            using qi::double_;
            using qi::lit;
            using qi::lexeme;
            using qi::space;
            using qi::skip;

            assignment_document.abstract_syntax_tree = *assignment_document.node;
            assignment_document.node = assignment_document.placeholder | assignment_document.plain_text;
            assignment_document.plain_text = +(!lit("{{") >> char_);
            assignment_document.placeholder = assignment_document.expression_placeholder;
            assignment_document.statement_placeholder = lit("{{") >> skip(space)[assignment_document.statements >> lit("}}")];
            assignment_document.expression_placeholder = lit("{{") >> skip(space)[assignment_document.expression >> lit("}}")];
            assignment_document.statements = *assignment_document.statement;
            assignment_document.statement = assignment_document.expression_statement
                | assignment_document.assignment_statement
                | assignment_document.compound_statement
                | assignment_document.if_statement
                | assignment_document.while_statement;
            assignment_document.expression_statement = qi::attr_cast<expression_statement_type>(assignment_document.expression >> lit(';'));
            assignment_document.assignment_statement = assignment_document.assignment_expression_node;
            assignment_document.compound_statement = lit('{') >> *assignment_document.statement >> lit('}');
            assignment_document.if_statement = lit("if") >> lit('(') >> assignment_document.expression >> lit(')') >> assignment_document.statement >> -(lit("else") >> assignment_document.statement);
            assignment_document.while_statement = lit("while") >> lit('(') >> assignment_document.expression >> lit(')') >> assignment_document.statement;
            assignment_document.expression = assignment_document.assignment_expression % lit(',');
            assignment_document.assignment_expression = assignment_document.conditional_expression;
            assignment_document.assignment_expression_node = assignment_document.conditional_expression >> assignment_operator_ >> assignment_document.assignment_expression;
            assignment_document.conditional_expression = assignment_document.conditional_expression_node | arithmetic_comparison.logical_or_expression;
            assignment_document.conditional_expression_node = arithmetic_comparison.logical_or_expression >> lit('?') >> assignment_document.expression >> lit(':') >> assignment_document.conditional_expression;
            arithmetic_comparison.logical_or_expression = arithmetic_comparison.logical_and_expression % lit("||");
            arithmetic_comparison.logical_and_expression = arithmetic_comparison.or_expression % lit("&&");
            arithmetic_comparison.or_expression = arithmetic_comparison.xor_expression % lit('|');
            arithmetic_comparison.xor_expression = arithmetic_comparison.and_expression % lit('^');
            arithmetic_comparison.and_expression = arithmetic_comparison.equality_expression % lit('&');
            arithmetic_comparison.equality_expression = arithmetic_comparison.relational_expression >> *(equality_operator_ >> arithmetic_comparison.relational_expression);
            arithmetic_comparison.relational_expression = arithmetic_comparison.shift_expression >> *(relational_operator_ >> arithmetic_comparison.shift_expression);
            arithmetic_comparison.shift_expression = arithmetic_comparison.additive_expression >> *(shift_operator_ >> arithmetic_comparison.additive_expression);
            arithmetic_comparison.additive_expression = arithmetic_comparison.multiplicative_expression >> *(additive_operator_ >> arithmetic_comparison.multiplicative_expression);
            arithmetic_comparison.multiplicative_expression = unary_postfix.prefix_expression >> *(multiplicative_operator_ >> unary_postfix.prefix_expression);
            unary_postfix.prefix_expression = unary_postfix.prefix_expression_node | unary_postfix.suffix_expression;
#if defined(HERMIT_ENABLE_EXPERIMENTAL_STATEMENTS)
            unary_postfix.prefix_expression_node = prefix_operator_ >> unary_postfix.prefix_expression;
            unary_postfix.suffix_expression = unary_postfix.parentheses_expression >> *suffix_operator_;
#else
            unary_postfix.prefix_expression_node = !(lit("++") | lit("--")) >> prefix_operator_ >> unary_postfix.prefix_expression;
            unary_postfix.suffix_expression = unary_postfix.parentheses_expression >> *((!(lit("++") | lit("--"))) >> suffix_operator_);
#endif
            unary_postfix.parentheses_expression = (lit('(') >> assignment_document.expression >> lit(')')) | primary_macro.macro_expression;
            primary_macro.macro_expression = primary_macro.macro_expression_node | primary_macro.primary;
            primary_macro.macro_expression_node = lexical.name >> primary_macro.arguments;
            primary_macro.arguments = lit('(') >> -(assignment_document.assignment_expression % ',') >> lit(')');
            primary_macro.primary = primary_macro.variable | lexical.value;
            primary_macro.variable = lexical.name;
            lexical.value = bool_ | lexical.character | int_ | double_ | lexical.string;
            lexical.name = lexeme[
                !((lit("if") | lit("else") | lit("while") | lit("true") | lit("false")) >> !char_("a-zA-Z0-9_"))
                >> char_("a-zA-Z_") >> *(char_("a-zA-Z0-9_"))];
            lexical.character = lexeme['\'' >> (('\\' >> escaped_char) | (char_ - '\'' - '\\')) >> '\''];
            lexical.string = lexeme['"' >> *(('\\' >> escaped_char) | (char_ - '"' - '\\')) >> '"'];

        }
        #endif

        namespace visitor
        {
            template<typename Result>
            struct basic_static_cast
            {
                template<typename T>
                    requires requires(const T& value) { static_cast<Result>(value); }
                Result operator ()(const T& value) const
                {
                    return static_cast<Result>(value);
                }
            };

            template<typename Result>
            struct static_cast_
                : basic_static_cast<Result>
            {
            };

            template<>
            struct static_cast_<bool>
                : basic_static_cast<bool>
            {
                using basic_static_cast<bool>::operator();

                bool operator()(const std::string& s) const
                {
                    return !s.empty();
                }

                [[noreturn]] bool operator()(const undefined_variable_type&) const
                {
                    Hermit::throw_exception(macro_exception{} << error_info::description{ "Boolean cast of an undefined variable" });
                }
            };
        } // namespace visitor

#define HERMIT_DEFINE_TRAIT_V(trait_name)                                    \
        template<typename ... Args>                                          \
        inline constexpr auto trait_name##_v{ trait_name<Args ...>::value };

#define HERMIT_DEFINE_TRAIT_T(trait_name)                           \
        template<typename ... Args>                                 \
        using trait_name##_t = typename trait_name<Args ...>::type;

#define HERMIT_DEFINE_IS_TRAIT_FROM_CONCEPT_(trait_name, concept_name)     \
        template<typename ... Args>                                        \
        struct trait_name : std::bool_constant<concept_name<Args ...>> {}; \
        HERMIT_DEFINE_TRAIT_V(trait_name)

#define HERMIT_DEFINE_IS_TRAIT_FROM_CONCEPT(concept_name)                     \
        HERMIT_DEFINE_IS_TRAIT_FROM_CONCEPT_(is_##concept_name, concept_name)

#define HERMIT_DEFINE_DECAYED_CONCEPT_FROM_CONCEPT(concept_name)                  \
        template<typename ... Args>                                               \
        concept decayed_##concept_name = concept_name<Hermit::decay_t<Args> ...>;

#define HERMIT_DEFINE_UNWRAPPED_CONCEPT_FROM_CONCEPT(concept_name)                             \
        template<typename ... Args>                                                            \
        concept unwrapped_##concept_name = concept_name<Hermit::unwrap_reference_t<Args> ...>;

#define HERMIT_DEFINE_LAZY_IS_TRAIT_FROM_IS_TRAIT(is_trait_name)           \
        struct lazy_##is_trait_name                                        \
        {                                                                  \
            template<typename ... Args>                                    \
            static constexpr bool value{ is_trait_name<Args ...>::value }; \
        };

#define HERMIT_DEFINE_FROM_CONCEPT(concept_name)                     \
        HERMIT_DEFINE_IS_TRAIT_FROM_CONCEPT(concept_name)            \
        HERMIT_DEFINE_DECAYED_CONCEPT_FROM_CONCEPT(concept_name)     \
        HERMIT_DEFINE_UNWRAPPED_CONCEPT_FROM_CONCEPT(concept_name)   \
        HERMIT_DEFINE_LAZY_IS_TRAIT_FROM_IS_TRAIT(is_##concept_name)

        template<typename A, typename B>
        concept same_boolness = !(std::same_as<A, bool> ^ std::same_as<B, bool>);
        HERMIT_DEFINE_FROM_CONCEPT(same_boolness);

        template<typename A, typename B>
        concept safe_arithmetic_assignable_to =
            std::is_arithmetic_v<A>
            && std::is_arithmetic_v<B>
            && std::convertible_to<A, B>
            && same_boolness<A, B>
            && requires(A a) { B{ a }; };
        HERMIT_DEFINE_FROM_CONCEPT(safe_arithmetic_assignable_to);

        template<typename A, typename B>
        concept safe_non_bool_arithmetic_assignable_to =
            std::is_arithmetic_v<A>
            && std::is_arithmetic_v<B>
            && std::convertible_to<A, B>
            && !std::same_as<A, bool>
            && !std::same_as<B, bool>
            && requires(A a) { B{ a }; };
        HERMIT_DEFINE_FROM_CONCEPT(safe_non_bool_arithmetic_assignable_to);

        template<typename A, typename B>
        concept safe_bitwise_assignable_to =
            std::is_integral_v<A>
            && std::is_integral_v<B>
            && std::convertible_to<A, B>
            && same_boolness<A, B>
            && requires(A a) { B{ a }; };
        HERMIT_DEFINE_FROM_CONCEPT(safe_bitwise_assignable_to);

        template<typename T>
        concept bitwise_operable = !std::same_as<T, bool>&& requires(T a, T b, int shift)
        {
            { ~a } -> std::same_as<T>;
            { a& b } -> std::same_as<T>;
            { a | b } -> std::same_as<T>;
            { a^ b } -> std::same_as<T>;
            { a << shift } -> std::same_as<T>;
            { a >> shift } -> std::same_as<T>;
        };
        HERMIT_DEFINE_FROM_CONCEPT(bitwise_operable);

        template<typename A, typename B>
        concept safe_equality_comparable_with = requires(const A & a, const B & b)
        {
            { a == b } -> std::convertible_to<bool>;
        }&& same_boolness<A, B>;
        HERMIT_DEFINE_FROM_CONCEPT(safe_equality_comparable_with);

        template<typename T>
        concept safe_equality_comparable = safe_equality_comparable_with<T, T>;
        HERMIT_DEFINE_FROM_CONCEPT(safe_equality_comparable);

        template<typename A, typename B>
        concept safe_totally_ordered_with
            = std::totally_ordered_with<A, B>
            && same_boolness<A, B>;
        HERMIT_DEFINE_FROM_CONCEPT(safe_totally_ordered_with);

        template<typename A>
        concept safe_unary_arithmetic = requires(const A & a)
        {
            { +a };
            { -a };
        } && !std::same_as<A, bool>;
        HERMIT_DEFINE_FROM_CONCEPT(safe_unary_arithmetic);

        template<typename A>
        concept safe_logical_not_operable = requires(const A & a)
        {
            { !a };
        };
        HERMIT_DEFINE_FROM_CONCEPT(safe_logical_not_operable);


        template<typename A>
        concept safe_bitwise_not_operable = requires(const A & a)
        {
            { ~a };
        } && !std::same_as<A, bool>;
        HERMIT_DEFINE_FROM_CONCEPT(safe_bitwise_not_operable);

        namespace visitor
        {
            struct assign_operator
            {
                context& ctx;
                assign_operator(context& ctx)
                    :ctx{ ctx }
                {
                }

                template<typename LHS, typename RHS>
                value_reference_type operator ()(LHS& lhs, const RHS& rhs) const
                {
                    using LHS_ = Hermit::decay_t<LHS>;
                    using RHS_ = Hermit::decay_t<RHS>;
                    if constexpr (std::is_same_v<LHS_, undefined_variable_type> && !std::is_same_v<RHS_, undefined_variable_type>)
                    {
                        const value_type value{ unwrap(rhs) };
                        ctx.set(lhs.name, value);
                        if (value_type* ptr{ ctx.get(lhs.name) }; ptr)
                        {
                            return value_as_reference_to_value_reference(*ptr);
                        }
                    }
                    else if constexpr (std::is_same_v<LHS_, RHS_> && !std::is_same_v<LHS_, bool>)
                    {
                        if constexpr (requires { unwrap(lhs) = unwrap(rhs); })
                        {
                            return unwrap(lhs) = unwrap(rhs);
                        }
                    }
                    else if constexpr (safe_arithmetic_assignable_to<RHS_, LHS_>)
                    {
                        if constexpr (requires { unwrap(lhs) = static_cast<LHS_>(unwrap(rhs)); })
                        {
                            return unwrap(lhs) = static_cast<LHS_>(unwrap(rhs));
                        }
                    }
                    Hermit::throw_exception(macro_exception{});
                }
            };
        }

#define HERMIT_SFINAE_FORWARD_RETURN(...)                                                \
        noexcept(noexcept(__VA_ARGS__)) -> decltype(__VA_ARGS__) { return __VA_ARGS__; }

#define HERMIT_DEFINE_BINARY_OPERATOR_FUNCTOR(operator_, opecode)                                     \
        namespace operators                                                                           \
        {                                                                                             \
            struct opecode                                                                            \
            {                                                                                         \
                template<typename LHS, typename RHS>                                                  \
                auto operator()(LHS&& lhs, RHS&& rhs) const                                           \
                HERMIT_SFINAE_FORWARD_RETURN(std::forward<LHS>(lhs) operator_ std::forward<RHS>(rhs)) \
            };                                                                                        \
        }

#define HERMIT_DEFINE_PREFIX_OPERATOR_FUNCTOR(operator_, opecode)                      \
        namespace operators                                                            \
        {                                                                              \
            struct opecode                                                             \
            {                                                                          \
                template<typename Operand>                                             \
                auto operator()(Operand&& opearnd) const                               \
                HERMIT_SFINAE_FORWARD_RETURN(operator_ std::forward<Operand>(opearnd)) \
            };                                                                         \
        }

#define HERMIT_DEFINE_SUFFIX_OPERATOR_FUNCTOR(operator_, opecode)                      \
        namespace operators                                                            \
        {                                                                              \
            struct opecode                                                             \
            {                                                                          \
                template<typename Operand>                                             \
                auto operator()(Operand&& opearnd) const                               \
                HERMIT_SFINAE_FORWARD_RETURN(std::forward<Operand>(opearnd) operator_) \
            };                                                                         \
        }

        template<typename Visitor>
        struct basic_visit
        {
            template<typename ... Args>
            auto operator()(Args && ... args) const
                HERMIT_SFINAE_FORWARD_RETURN(boost::apply_visitor(Visitor{}, std::forward<Args>(args) ...))
        };

        namespace visitor
        {
            template<typename Operator, typename Trait, bool ZeroCheck>
            struct basic_assign_operator
            {
                template<typename LHS, typename RHS>
                value_reference_type operator ()(LHS& lhs, const RHS& rhs) const
                {
                    using LHS_ = Hermit::decay_t<LHS>;
                    using RHS_ = Hermit::decay_t<RHS>;
                    if constexpr (std::is_same_v<LHS_, RHS_> && !std::is_same_v<LHS_, bool>)
                    {
                        if constexpr (requires { Operator{}(unwrap(lhs), unwrap(rhs)); })
                        {
                            if constexpr (ZeroCheck)
                            {
                                if (unwrap(rhs) == RHS_{})
                                {
                                    Hermit::throw_exception(macro_exception{});
                                }
                            }
                            return Operator{}(unwrap(lhs), unwrap(rhs));
                        }
                    }
                    else if constexpr (Trait::template value<RHS_, LHS_>)
                    {
                        if constexpr (requires { Operator{}(unwrap(lhs), static_cast<LHS_>(unwrap(rhs))); })
                        {
                            return Operator{}(unwrap(lhs), static_cast<LHS_>(unwrap(rhs)));
                        }
                    }
                    Hermit::throw_exception(macro_exception{});
                }
            };
        }

#define HERMIT_DEFINE_FUNCTOR(operator_, opecode, trait, zero_check)                               \
        HERMIT_DEFINE_BINARY_OPERATOR_FUNCTOR(operator_, opecode);                                 \
        using opecode = basic_visit<visitor::basic_assign_operator<operators::opecode, trait, zero_check>>;

        HERMIT_DEFINE_FUNCTOR(+=, plus_assign, lazy_is_safe_non_bool_arithmetic_assignable_to, false);
        HERMIT_DEFINE_FUNCTOR(-=, minus_assign, lazy_is_safe_non_bool_arithmetic_assignable_to, false);
        HERMIT_DEFINE_FUNCTOR(*=, multiplies_assign, lazy_is_safe_non_bool_arithmetic_assignable_to, false);
        HERMIT_DEFINE_FUNCTOR(<<=, shift_left_assign, lazy_is_safe_non_bool_arithmetic_assignable_to, false);
        HERMIT_DEFINE_FUNCTOR(>>=, shift_right_assign, lazy_is_safe_non_bool_arithmetic_assignable_to, false);
        HERMIT_DEFINE_FUNCTOR(&=, and_assign, lazy_is_safe_bitwise_assignable_to, false);
        HERMIT_DEFINE_FUNCTOR(^=, xor_assign, lazy_is_safe_bitwise_assignable_to, false);
        HERMIT_DEFINE_FUNCTOR(|=, or_assign, lazy_is_safe_bitwise_assignable_to, false);
        HERMIT_DEFINE_FUNCTOR(/=, divides_assign, lazy_is_safe_non_bool_arithmetic_assignable_to, true);
        HERMIT_DEFINE_FUNCTOR(%=, modulus_assign, lazy_is_safe_non_bool_arithmetic_assignable_to, true);
#undef HERMIT_DEFINE_FUNCTOR

        namespace visitor
        {
            template<typename Operator>
            struct basic_bitwise_operator
            {
                template<typename LHS, typename RHS>
                    requires (unwrapped_bitwise_operable<LHS>&& unwrapped_bitwise_operable<RHS>)
                value_reference_type operator ()(const LHS& lhs, const RHS& rhs) const
                {
                    return Operator{}(unwrap(lhs), unwrap(rhs));
                }

                template<typename LHS, typename RHS>
                    requires (!(unwrapped_bitwise_operable<LHS>&& unwrapped_bitwise_operable<RHS>))
                [[noreturn]] value_reference_type operator ()(const LHS& lhs, const RHS& rhs) const
                {
                    Hermit::throw_exception(macro_exception{});
                }
            };
        }

#define HERMIT_DEFINE_FUNCTOR(operator_, opecode)                                \
        HERMIT_DEFINE_BINARY_OPERATOR_FUNCTOR(operator_, opecode);               \
        using opecode = basic_visit<visitor::basic_bitwise_operator<operators::opecode>>;

        HERMIT_DEFINE_FUNCTOR(| , or_);
        HERMIT_DEFINE_FUNCTOR(^, xor_);
        HERMIT_DEFINE_FUNCTOR(&, and_);
        HERMIT_DEFINE_FUNCTOR(<< , shift_left);
        HERMIT_DEFINE_FUNCTOR(>> , shift_right);
#undef HERMIT_DEFINE_FUNCTOR

        namespace visitor
        {
            template<typename Operator>
            struct basic_equality_operator
            {
                template<typename LHS, typename RHS>
                    requires (unwrapped_safe_equality_comparable_with<LHS, RHS>)
                value_reference_type operator ()(const LHS& lhs, const RHS& rhs) const
                {
                    return Operator{}(unwrap(lhs), unwrap(rhs));
                }

                template<typename LHS, typename RHS>
                    requires (!(unwrapped_safe_equality_comparable_with<LHS, RHS>))
                [[noreturn]] value_reference_type operator ()(const LHS& lhs, const RHS& rhs) const
                {
                    Hermit::throw_exception(macro_exception{});
                }
            };
        }
#define HERMIT_DEFINE_FUNCTOR(operator_, opecode)                                 \
        HERMIT_DEFINE_BINARY_OPERATOR_FUNCTOR(operator_, opecode);                \
        using opecode = basic_visit<visitor::basic_equality_operator<operators::opecode>>;

        HERMIT_DEFINE_FUNCTOR(== , equal);
        HERMIT_DEFINE_FUNCTOR(!= , not_equal);
#undef HERMIT_DEFINE_FUNCTOR

        namespace visitor
        {
            template<typename Operator>
            struct basic_relational_operator
            {
                template<typename LHS, typename RHS>
                    requires (unwrapped_safe_totally_ordered_with<LHS, RHS>)
                value_reference_type operator ()(const LHS& lhs, const RHS& rhs) const
                {
                    return Operator{}(unwrap(lhs), unwrap(rhs));
                }

                template<typename LHS, typename RHS>
                    requires (!(unwrapped_safe_totally_ordered_with<LHS, RHS>))
                value_reference_type operator ()(const LHS& lhs, const RHS& rhs) const
                {
                    Hermit::throw_exception(macro_exception{});
                }
            };
        }

#define HERMIT_DEFINE_FUNCTOR(operator_, opecode)                                   \
        HERMIT_DEFINE_BINARY_OPERATOR_FUNCTOR(operator_, opecode);                  \
        using opecode = basic_visit<visitor::basic_relational_operator<operators::opecode>>;

        HERMIT_DEFINE_FUNCTOR(< , less);
        HERMIT_DEFINE_FUNCTOR(> , greater);
        HERMIT_DEFINE_FUNCTOR(<= , less_equal);
        HERMIT_DEFINE_FUNCTOR(>= , greater_equal);
#undef HERMIT_DEFINE_FUNCTOR

        namespace visitor
        {
            template<typename Operator, bool ZeroCheck>
            struct basic_arithmetic_operator
            {
                template<typename LHS, typename RHS>
                static constexpr bool operable_v
                {
                    requires(const LHS & lhs, const RHS & rhs) { Operator{}(unwrap(lhs), unwrap(rhs)); }
                        && !std::same_as<Hermit::decay_t<LHS>, bool>
                        && !std::same_as<Hermit::decay_t<RHS>, bool>
                };

                template<typename LHS, typename RHS>
                    requires (operable_v<LHS, RHS>)
                value_reference_type operator ()(const LHS& lhs, const RHS& rhs) const
                {
                    if constexpr (ZeroCheck)
                    {
                        using RHS_ = Hermit::decay_t<RHS>;
                        if constexpr (safe_equality_comparable<RHS_>)
                        {
                            if (unwrap(rhs) == RHS_{})
                            {
                                Hermit::throw_exception(macro_exception{});
                            }
                        }
                    }
                    return Operator{}(unwrap(lhs), unwrap(rhs));
                }

                template<typename LHS, typename RHS>
                    requires (!operable_v<LHS, RHS>)
                [[noreturn]] value_reference_type operator ()(const LHS& lhs, const RHS& rhs) const
                {
                    Hermit::throw_exception(macro_exception{});
                }
            };
        }

#define HERMIT_DEFINE_FUNCTOR(operator_, opecode, zero_check)                                   \
        HERMIT_DEFINE_BINARY_OPERATOR_FUNCTOR(operator_, opecode);                              \
        using opecode = basic_visit<visitor::basic_arithmetic_operator<operators::opecode, zero_check>>;

        HERMIT_DEFINE_FUNCTOR(+, plus, false);
        HERMIT_DEFINE_FUNCTOR(-, minus, false);
        HERMIT_DEFINE_FUNCTOR(*, multiplies, false);
        HERMIT_DEFINE_FUNCTOR(/ , divides, true);
        HERMIT_DEFINE_FUNCTOR(%, modulus, true);
#undef HERMIT_DEFINE_FUNCTOR

        namespace visitor
        {
            template<typename Operator>
            struct basic_prefix_operator
            {
                template<typename Operand>
                    requires requires(Operand& operand) { Operator{}(unwrap(operand)); }
                value_reference_type operator ()(Operand& operand) const
                {
                    return Operator{}(unwrap(operand));
                }

                template<typename Operand>
                    requires (!requires(Operand& operand) { Operator{}(unwrap(operand)); })
                [[noreturn]] value_reference_type operator ()(Operand& operand) const
                {
                    Hermit::throw_exception(macro_exception{});
                }
            };
        }

#define HERMIT_DEFINE_FUNCTOR(operator_, opecode)                               \
        HERMIT_DEFINE_PREFIX_OPERATOR_FUNCTOR(operator_, opecode);              \
        using opecode = basic_visit<visitor::basic_prefix_operator<operators::opecode>>;

        HERMIT_DEFINE_FUNCTOR(++, prefix_increment);
        HERMIT_DEFINE_FUNCTOR(--, prefix_decrement);
#undef HERMIT_DEFINE_FUNCTOR

        namespace visitor
        {
            template<typename Operator, typename Trait>
            struct basic_prefix_operator_
            {
                template<typename Operand>
                    requires (Trait::template value<Hermit::decay_t<Operand>>)
                value_reference_type operator ()(const Operand& operand) const
                {
                    return Operator{}(unwrap(operand));
                }

                template<typename Operand>
                    requires (!(Trait::template value<Hermit::decay_t<Operand>>))
                [[noreturn]] value_reference_type operator ()(const Operand& operand) const
                {
                    Hermit::throw_exception(macro_exception{});
                }
            };
        }

#define HERMIT_DEFINE_FUNCTOR(operator_, opecode, trait)                                \
        HERMIT_DEFINE_PREFIX_OPERATOR_FUNCTOR(operator_, opecode);                      \
        using opecode = basic_visit<visitor::basic_prefix_operator_<operators::opecode, trait>>;

        HERMIT_DEFINE_FUNCTOR(+, prefix_plus, lazy_is_safe_unary_arithmetic);
        HERMIT_DEFINE_FUNCTOR(-, prefix_minus, lazy_is_safe_unary_arithmetic);
        HERMIT_DEFINE_FUNCTOR(!, logical_not, lazy_is_safe_logical_not_operable);
        HERMIT_DEFINE_FUNCTOR(~, bitwise_not, lazy_is_safe_bitwise_not_operable);
#undef HERMIT_DEFINE_FUNCTOR

        namespace visitor
        {
            template<typename Operator>
            struct basic_suffix_operator
            {
                template<typename Operand>
                    requires requires(Operand& operand) { Operator{}(unwrap(operand)); }
                value_reference_type operator ()(Operand& operand) const
                {
                    return Operator{}(unwrap(operand));
                }

                template<typename Operand>
                    requires (!requires(Operand& operand) { Operator{}(unwrap(operand)); })
                [[noreturn]] value_reference_type operator ()(Operand& operand) const
                {
                    Hermit::throw_exception(macro_exception{});
                }
            };
        }

#define HERMIT_DEFINE_FUNCTOR(operator_, opecode)                               \
        HERMIT_DEFINE_SUFFIX_OPERATOR_FUNCTOR(operator_, opecode);              \
        using opecode = basic_visit<visitor::basic_suffix_operator<operators::opecode>>;

        HERMIT_DEFINE_FUNCTOR(++, suffix_increment);
        HERMIT_DEFINE_FUNCTOR(--, suffix_decrement);
#undef HERMIT_DEFINE_FUNCTOR

#undef HERMIT_DEFINE_TRAIT_V
#undef HERMIT_DEFINE_TRAIT_T
#undef HERMIT_DEFINE_DECAYED_CONCEPT_FROM_CONCEPT
#undef HERMIT_DEFINE_UNWRAPPED_CONCEPT_FROM_CONCEPT
#undef HERMIT_DEFINE_IS_TRAIT_FROM_CONCEPT_
#undef HERMIT_DEFINE_IS_TRAIT_FROM_CONCEPT
#undef HERMIT_DEFINE_LAZY_IS_TRAIT_FROM_IS_TRAIT
#undef HERMIT_DEFINE_FROM_CONCEPT
#undef HERMIT_SFINAE_FORWARD_RETURN
#undef HERMIT_DEFINE_BINARY_OPERATOR_FUNCTOR
#undef HERMIT_DEFINE_PREFIX_OPERATOR_FUNCTOR
#undef HERMIT_DEFINE_SUFFIX_OPERATOR_FUNCTOR

        template<typename Result>
        struct static_cast_
        {
            template<typename A>
            Result operator ()(const A& a) const
            {
                return boost::apply_visitor(visitor::static_cast_<Result>{}, a);
            }
        };

        template<typename ForwardIterator, typename Evaluator, typename BinaryOperator, typename ... Args>
        decltype(auto) accumulate_expression(ForwardIterator first, ForwardIterator last, Evaluator evaluator, BinaryOperator binary_operator, Args && ... args)
        {
            if (first == last)
            {
                Hermit::throw_exception(logic_error{});
            }
            auto accumulated{ evaluator(*first, std::forward<Args>(args) ...) };
            ++first;
            if (first == last)
            {
                return accumulated;
            }
            for (; first != last; ++first)
            {
                accumulated = binary_operator(accumulated, evaluator(*first, std::forward<Args>(args) ...));
            }
            return accumulated;
        }

        struct evaluation_visitor
        {
            evaluation_visitor(const config& cfg, context& ctx)
                : cfg{ cfg }
                , ctx{ ctx }
            {
            }

            evaluation_visitor(const evaluation_visitor&) = default;
            evaluation_visitor(evaluation_visitor&&) = default;

            const config& cfg;
            context& ctx;
        };

        struct node_visitor
            : evaluation_visitor
            , boost::static_visitor<std::string>
        {
            node_visitor(const config& cfg, context& ctx)
                : evaluation_visitor{ cfg, ctx }
                , boost::static_visitor<std::string>{}
            {
            }

            std::string operator()(const std::string& str) const;
            std::string operator()(const placeholder_type& value) const;
        };

        struct assignment_expression_visitor
            : boost::static_visitor<value_reference_type>
        {
            const config& cfg; context& ctx;
            assignment_expression_visitor(const config& cfg, context& ctx) : cfg{ cfg }, ctx{ ctx } {}

            value_reference_type operator()(const assignment_expression_node_type& expr) const;
            value_reference_type operator()(const conditional_expression_type& expr) const;
            value_reference_type operator()(const assignment_expression_type& expr) const;
        };

        struct conditional_expression_visitor
            : evaluation_visitor
            , boost::static_visitor<value_reference_type>
        {
            conditional_expression_visitor(const config& cfg, context& ctx)
                : evaluation_visitor{ cfg, ctx }
                , boost::static_visitor<value_reference_type>{}
            {
            }

            value_reference_type operator()(const conditional_expression_node_type& value) const;
            value_reference_type operator()(const logical_or_expression_type& value) const;
        };

        struct prefix_expression_visitor
            : evaluation_visitor
            , boost::static_visitor<value_reference_type>
        {
            prefix_expression_visitor(const config& cfg, context& ctx)
                : evaluation_visitor{ cfg, ctx }
                , boost::static_visitor<value_reference_type>{}
            {
            }

            value_reference_type operator()(const prefix_expression_node_type& expr) const;
            value_reference_type operator()(const suffix_expression_type& expr) const;
            value_reference_type operator()(const prefix_expression_type& expr) const;
        };

        struct parentheses_expression_visitor
            : evaluation_visitor
            , boost::static_visitor<value_reference_type>
        {
            parentheses_expression_visitor(const config& cfg, context& ctx)
                : evaluation_visitor{ cfg, ctx }
                , boost::static_visitor<value_reference_type>{}
            {
            }

            value_reference_type operator()(const macro_expression_type& expr) const;
            value_reference_type operator()(const expression_type& expr) const;
        };

        struct primary_visitor
            : evaluation_visitor
            , boost::static_visitor<value_reference_type>
        {
            primary_visitor(const config& cfg, context& ctx)
                : evaluation_visitor{ cfg, ctx }
                , boost::static_visitor<value_reference_type>{}
            {
            }

            value_reference_type operator()(const variable_type& variable) const;
            value_reference_type operator()(const value_type& value) const;
        };

        struct macro_expression_visitor
            : evaluation_visitor
            , boost::static_visitor<value_reference_type>
        {
            macro_expression_visitor(const config& cfg, context& ctx)
                : evaluation_visitor{ cfg, ctx }
                , boost::static_visitor<value_reference_type>{}
            {
            }

            value_reference_type operator()(const macro_expression_node_type& expr) const;
            value_reference_type operator()(const primary_type& primary) const;
        };

        struct value_as_reference_to_value_reference_visitor
            : boost::static_visitor<value_reference_type>
        {
            value_as_reference_to_value_reference_visitor() {}

            template<typename T>
            value_reference_type operator()(T& value) const
            {
                return std::ref(value);
            }
        };

        struct value_as_value_to_value_reference_visitor
            : boost::static_visitor<value_reference_type>
        {
            value_as_value_to_value_reference_visitor() {}

            template<typename T>
            value_reference_type operator()(const T& value) const
            {
                return value;
            }
        };

        struct value_reference_to_value_visitor
            : boost::static_visitor<value_type>
        {
            template<typename T>
            value_type operator()(const T& value_reference) const
            {
                return unwrap(value_reference);
            }

            [[noreturn]] value_type operator()(const undefined_variable_type& undefined_variable) const
            {
                Hermit::throw_exception(macro_exception{} << error_info::description{ "Undefined variable" });
            }
        };
    } // namespace parser
}

#endif // HERMIT_PARSER_IMPL_HPP
