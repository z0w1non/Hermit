#ifndef HERMIT_PARSER_GRAMMAR_IMPL_HPP
#define HERMIT_PARSER_GRAMMAR_IMPL_HPP

#include <utility>

#include "Hermit_parser_internal.hpp"

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

#if defined(HERMIT_ENABLE_EXPERIMENTAL_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_STAGE8_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_ASSIGNMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_ASSIGNMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_IF_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_WHILE_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_INCREMENT_DECREMENT)
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
#endif

}

namespace Hermit
{
    namespace parser
    {
        
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
#if defined(HERMIT_ENABLE_EXPERIMENTAL_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_STAGE8_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_ASSIGNMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_ASSIGNMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_IF_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_WHILE_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_INCREMENT_DECREMENT)
            assignment_document.placeholder = assignment_document.statement_placeholder | assignment_document.expression_placeholder;
#elif defined(HERMIT_ENABLE_EXPERIMENTAL_EXPRESSION_STATEMENTS)
            assignment_document.placeholder = assignment_document.statement_placeholder | assignment_document.expression_placeholder;
#else
            assignment_document.placeholder = assignment_document.expression_placeholder;
#endif
            assignment_document.expression_placeholder = lit("{{") >> skip(space)[assignment_document.expression >> lit("}}")];
            assignment_document.expression = assignment_document.assignment_expression % lit(',');
            assignment_document.assignment_expression = assignment_document.conditional_expression;
            assignment_document.conditional_expression = assignment_document.conditional_expression_node | arithmetic_comparison.logical_or_expression;
            assignment_document.conditional_expression_node = arithmetic_comparison.logical_or_expression >> lit('?') >> assignment_document.expression >> lit(':') >> assignment_document.conditional_expression;
#if defined(HERMIT_ENABLE_EXPERIMENTAL_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_STAGE8_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_EXPRESSION_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_ASSIGNMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_ASSIGNMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_IF_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_WHILE_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_INCREMENT_DECREMENT)
            assignment_document.statement_placeholder = lit("{{") >> skip(space)[assignment_document.statements >> lit("}}")];
            assignment_document.statements = *assignment_document.statement;
            assignment_document.expression_statement = assignment_document.expression >> lit(';');
#endif
#if defined(HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_ASSIGNMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_IF_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_WHILE_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_INCREMENT_DECREMENT)
            assignment_document.simple_assignment_statement = lexical.name >> assignment_operator_ >> assignment_document.assignment_expression;
#elif defined(HERMIT_ENABLE_EXPERIMENTAL_ASSIGNMENTS)
            assignment_document.simple_assignment_statement = lexical.name >> qi::attr(assignment_operator::assign) >> lit('=') >> assignment_document.assignment_expression;
#endif
#if defined(HERMIT_ENABLE_EXPERIMENTAL_STAGE8_STATEMENTS)
            assignment_document.statement = assignment_document.simple_assignment_statement
                | assignment_document.compound_statement
                | assignment_document.if_statement
                | assignment_document.while_statement
                | assignment_document.expression_statement;
            assignment_document.compound_statement = lit('{') >> *assignment_document.statement >> lit('}');
            assignment_document.if_statement = lit("if") >> lit('(') >> assignment_document.expression >> lit(')') >> assignment_document.statement >> -(lit("else") >> assignment_document.statement);
            assignment_document.while_statement = lit("while") >> lit('(') >> assignment_document.expression >> lit(')') >> assignment_document.statement;
#elif defined(HERMIT_ENABLE_EXPERIMENTAL_INCREMENT_DECREMENT)
            assignment_document.statement = assignment_document.simple_assignment_statement | assignment_document.compound_statement | assignment_document.if_statement | assignment_document.while_statement;
            assignment_document.compound_statement = lit('{') >> *assignment_document.statement >> lit('}');
            assignment_document.if_statement = lit("if") >> lit('(') >> assignment_document.expression
                >> lit(')') >> assignment_document.statement >> -(lit("else") >> assignment_document.statement);
            assignment_document.while_statement = lit("while") >> lit('(') >> assignment_document.expression
                >> lit(')') >> assignment_document.statement;
#elif defined(HERMIT_ENABLE_EXPERIMENTAL_WHILE_STATEMENTS)
            assignment_document.statement = assignment_document.simple_assignment_statement | assignment_document.compound_statement | assignment_document.if_statement | assignment_document.while_statement;
            assignment_document.compound_statement = lit('{') >> *assignment_document.statement >> lit('}');
            assignment_document.if_statement = lit("if") >> lit('(') >> assignment_document.expression >> lit(')') >> assignment_document.statement >> -(lit("else") >> assignment_document.statement);
            assignment_document.while_statement = lit("while") >> lit('(') >> assignment_document.expression >> lit(')') >> assignment_document.statement;
#elif defined(HERMIT_ENABLE_EXPERIMENTAL_IF_STATEMENTS)
            assignment_document.statement = assignment_document.simple_assignment_statement | assignment_document.compound_statement | assignment_document.if_statement;
            assignment_document.compound_statement = lit('{') >> *assignment_document.statement >> lit('}');
            assignment_document.if_statement = lit("if") >> lit('(') >> assignment_document.expression >> lit(')') >> assignment_document.statement >> -(lit("else") >> assignment_document.statement);
#elif defined(HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_STATEMENTS)
            assignment_document.statement = assignment_document.simple_assignment_statement | assignment_document.compound_statement;
            assignment_document.compound_statement = lit('{') >> *assignment_document.statement >> lit('}');
#elif defined(HERMIT_ENABLE_EXPERIMENTAL_EXPRESSION_STATEMENTS)
            assignment_document.statement = assignment_document.expression_statement;
#elif defined(HERMIT_ENABLE_EXPERIMENTAL_ASSIGNMENTS)
            assignment_document.statement = assignment_document.simple_assignment_statement;
#elif defined(HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_ASSIGNMENTS)
            assignment_document.statement = assignment_document.simple_assignment_statement;
#endif
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
#if defined(HERMIT_ENABLE_EXPERIMENTAL_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_WHILE_STATEMENTS) || defined(HERMIT_ENABLE_EXPERIMENTAL_INCREMENT_DECREMENT)
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

    } // namespace parser
}

#endif // HERMIT_PARSER_GRAMMAR_IMPL_HPP

