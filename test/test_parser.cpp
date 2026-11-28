#include <limits>

#include <gtest/gtest.h>
#include <boost/spirit/include/qi.hpp>

#include "config.hpp"
#include "context.hpp"
#include "parser.hpp"
#include "parser_internal.hpp"
#include "public.hpp"
#include "value.hpp"

namespace parser_attribute_test
{
    struct wrapped_integer
    {
        int value{};
    };
}

namespace boost::spirit::traits
{
    template<>
    struct transform_attribute<parser_attribute_test::wrapped_integer, int, boost::spirit::qi::domain>
    {
        using type = int;

        static int pre(parser_attribute_test::wrapped_integer& value)
        {
            return value.value;
        }

        static void post(parser_attribute_test::wrapped_integer& value, int attribute)
        {
            value.value = attribute;
        }

        static void fail(parser_attribute_test::wrapped_integer&)
        {
        }
    };
}

TEST(parser_grammar, qi_attribute_transform_adapts_rule_output_to_ast_wrapper)
{
    namespace qi = boost::spirit::qi;
    const std::string input{ "42" };
    auto first{ input.begin() };
    parser_attribute_test::wrapped_integer result;
    const bool parsed{ qi::parse(first, input.end(), qi::attr_cast<parser_attribute_test::wrapped_integer>(qi::int_), result) };
    EXPECT_TRUE(parsed);
    EXPECT_EQ(first, input.end());
    EXPECT_EQ(result.value, 42);
}

namespace parser_attribute_test
{
    struct expression
    {
        int value{};
    };

    struct placeholder
    {
        expression value;
    };

    struct statement
    {
        int value{};
    };

    using expression_or_statement = boost::variant<expression, statement>;

    struct content_placeholder
    {
        expression_or_statement content;
    };

    using actual_statement = std::vector<hermit::parser::expression_type>;

    using actual_expression_or_statement = boost::variant<hermit::parser::expression_type, actual_statement>;

    struct actual_content_placeholder
    {
        actual_expression_or_statement content;
    };

}

BOOST_FUSION_ADAPT_STRUCT(parser_attribute_test::content_placeholder, content)
BOOST_FUSION_ADAPT_STRUCT(parser_attribute_test::actual_content_placeholder, content)

namespace boost::spirit::traits
{
    template<>
    struct transform_attribute<parser_attribute_test::content_placeholder, parser_attribute_test::expression_or_statement, boost::spirit::qi::domain>
    {
        using type = parser_attribute_test::expression_or_statement;

        static type pre(parser_attribute_test::content_placeholder& value)
        {
            return value.content;
        }

        static void post(parser_attribute_test::content_placeholder& value, type attribute)
        {
            value.content = std::move(attribute);
        }

        static void fail(parser_attribute_test::content_placeholder&)
        {
        }
    };

    template<>
    struct transform_attribute<parser_attribute_test::actual_content_placeholder, parser_attribute_test::actual_expression_or_statement, boost::spirit::qi::domain>
    {
        using type = parser_attribute_test::actual_expression_or_statement;

        static type pre(parser_attribute_test::actual_content_placeholder& value)
        {
            return value.content;
        }

        static void post(parser_attribute_test::actual_content_placeholder& value, type attribute)
        {
            value.content = std::move(attribute);
        }

        static void fail(parser_attribute_test::actual_content_placeholder&)
        {
        }
    };
}

namespace boost::spirit::traits
{
    template<>
    struct transform_attribute<parser_attribute_test::placeholder, int, boost::spirit::qi::domain>
    {
        using type = int;

        static int pre(parser_attribute_test::placeholder&)
        {
            return {};
        }

        static void post(parser_attribute_test::placeholder& value, int attribute)
        {
            value.value.value = attribute;
        }

        static void fail(parser_attribute_test::placeholder&)
        {
        }
    };
}

TEST(parser_grammar, placeholder_attribute_cast_supports_document_alternative_backtracking)
{
    namespace qi = boost::spirit::qi;
    using iterator_type = std::string::const_iterator;
    qi::rule<iterator_type, parser_attribute_test::placeholder()> placeholder;
    qi::rule<iterator_type, std::string()> plain_text;
    qi::rule<iterator_type, boost::variant<parser_attribute_test::placeholder, std::string>()> node;
    placeholder = qi::lit("{{") >> qi::attr_cast<parser_attribute_test::placeholder>(qi::int_) >> qi::lit("}}");
    plain_text = +qi::char_;
    node = placeholder | plain_text;

    const std::string valid{ "{{42}}" };
    auto valid_first{ valid.begin() };
    boost::variant<parser_attribute_test::placeholder, std::string> valid_result;
    ASSERT_TRUE(qi::parse(valid_first, valid.end(), node, valid_result));
    EXPECT_EQ(valid_first, valid.end());
    const auto* parsed_placeholder{ boost::get<parser_attribute_test::placeholder>(&valid_result) };
    ASSERT_NE(parsed_placeholder, nullptr);
    EXPECT_EQ(parsed_placeholder->value.value, 42);

    const std::string invalid{ "{{x}}" };
    auto invalid_first{ invalid.begin() };
    boost::variant<parser_attribute_test::placeholder, std::string> invalid_result;
    ASSERT_TRUE(qi::parse(invalid_first, invalid.end(), node, invalid_result));
    EXPECT_EQ(invalid_first, invalid.end());
    const auto* parsed_text{ boost::get<std::string>(&invalid_result) };
    ASSERT_NE(parsed_text, nullptr);
    EXPECT_EQ(*parsed_text, invalid);
}

TEST(parser_grammar, expression_statement_variant_propagates_through_placeholder_sequence)
{
    namespace qi = boost::spirit::qi;
    using iterator_type = std::string::const_iterator;
    qi::rule<iterator_type, parser_attribute_test::expression()> expression;
    qi::rule<iterator_type, parser_attribute_test::statement()> statement;
    qi::rule<iterator_type, parser_attribute_test::expression_or_statement()> content;
    qi::rule<iterator_type, parser_attribute_test::content_placeholder()> placeholder;
    expression = qi::int_;
    statement = qi::int_;
    content = expression | statement;
    placeholder = qi::lit("{{") >> content >> qi::lit("}}");

    const std::string input{ "{{42}}" };
    auto first{ input.begin() };
    parser_attribute_test::content_placeholder result;
    ASSERT_TRUE(qi::parse(first, input.end(), placeholder, result));
    EXPECT_EQ(first, input.end());
    const auto* parsed_expression{ boost::get<parser_attribute_test::expression>(&result.content) };
    ASSERT_NE(parsed_expression, nullptr);
    EXPECT_EQ(parsed_expression->value, 42);
}

TEST(parser_grammar, expression_statement_variant_cast_wraps_complete_placeholder)
{
    namespace qi = boost::spirit::qi;
    using iterator_type = std::string::const_iterator;
    qi::rule<iterator_type, parser_attribute_test::expression()> expression;
    qi::rule<iterator_type, parser_attribute_test::statement()> statement;
    qi::rule<iterator_type, parser_attribute_test::expression_or_statement()> content;
    expression = qi::int_;
    statement = qi::int_;
    content = expression | statement;

    const std::string input{ "{{42}}" };
    auto first{ input.begin() };
    parser_attribute_test::content_placeholder result;
    const bool parsed{ qi::parse(first, input.end(), qi::attr_cast<parser_attribute_test::content_placeholder>(qi::lit("{{") >> content >> qi::lit("}}")), result) };
    ASSERT_TRUE(parsed);
    EXPECT_EQ(first, input.end());
    const auto* parsed_expression{ boost::get<parser_attribute_test::expression>(&result.content) };
    ASSERT_NE(parsed_expression, nullptr);
    EXPECT_EQ(parsed_expression->value, 42);
}

TEST(parser_grammar, production_expression_ast_variant_propagates_into_placeholder)
{
    namespace qi = boost::spirit::qi;
    using iterator_type = std::string::const_iterator;
    qi::rule<iterator_type, hermit::parser::expression_type()> expression;
    qi::rule<iterator_type, parser_attribute_test::actual_statement()> statement;
    qi::rule<iterator_type, parser_attribute_test::actual_expression_or_statement()> content;
    qi::rule<iterator_type, parser_attribute_test::actual_content_placeholder()> placeholder;
    const hermit::parser::expression_type expression_ast{};
    const parser_attribute_test::actual_statement statement_ast{ expression_ast };
    expression = qi::lit('e') >> qi::attr(expression_ast);
    statement = qi::lit('s') >> qi::attr(statement_ast);
    content = expression | statement;
    placeholder = qi::lit("{{") >> content >> qi::lit("}}");

    const std::string input{ "{{e}}" };
    auto first{ input.begin() };
    parser_attribute_test::actual_content_placeholder result;
    ASSERT_TRUE(qi::parse(first, input.end(), placeholder, result));
    EXPECT_EQ(first, input.end());
    const auto* parsed_expression{ boost::get<hermit::parser::expression_type>(&result.content) };
    ASSERT_NE(parsed_expression, nullptr);
    EXPECT_TRUE(parsed_expression->empty());

    const std::string statement_input{ "{{s}}" };
    auto statement_first{ statement_input.begin() };
    parser_attribute_test::actual_content_placeholder statement_result;
    ASSERT_TRUE(qi::parse(statement_first, statement_input.end(), placeholder, statement_result));
    EXPECT_EQ(statement_first, statement_input.end());
    const auto* parsed_statement{ boost::get<parser_attribute_test::actual_statement>(&statement_result.content) };
    ASSERT_NE(parsed_statement, nullptr);
    ASSERT_EQ(parsed_statement->size(), 1U);
}

#if defined(HERMIT_ENABLE_EXPERIMENTAL_STAGE8_STATEMENTS)
TEST(parser_grammar, expression_statement_fusion_attribute_propagates_expression_and_semicolon)
{
    namespace qi = boost::spirit::qi;
    using iterator_type = std::string::const_iterator;
    qi::rule<iterator_type, Hermit::parser::expression_type()> expression;
    qi::rule<iterator_type, Hermit::parser::expression_statement_type()> expression_statement;
    expression = qi::lit('e') >> qi::attr(Hermit::parser::expression_type{});
    expression_statement = expression >> qi::lit(';');

    const std::string input{ "e;" };
    auto first{ input.begin() };
    Hermit::parser::expression_statement_type result;
    ASSERT_TRUE(qi::parse(first, input.end(), expression_statement, result));
    EXPECT_EQ(first, input.end());
    EXPECT_TRUE(result.expression.empty());

    qi::rule<iterator_type, Hermit::parser::statement_type()> statement;
    statement = expression_statement;
    const std::string statement_input{ "e;" };
    auto statement_first{ statement_input.begin() };
    Hermit::parser::statement_type statement_result;
    ASSERT_TRUE(qi::parse(statement_first, statement_input.end(), statement, statement_result));
    EXPECT_EQ(statement_first, statement_input.end());
    EXPECT_NE(boost::get<Hermit::parser::expression_statement_type>(&statement_result), nullptr);

    Hermit::parser::grammar production_grammar;
    Hermit::parser::expression_statement_type production_expression_statement;
    EXPECT_TRUE(production_grammar.parse_expression_statement("1 + 2;", production_expression_statement));
    Hermit::parser::statement_type production_statement;
    ASSERT_TRUE(production_grammar.parse_statement("1 + 2;", production_statement));
    EXPECT_NE(boost::get<Hermit::parser::expression_statement_type>(&production_statement), nullptr);
    Hermit::parser::placeholder_result_type production_placeholder;
    ASSERT_TRUE(production_grammar.parse_statement_placeholder("{{1 + 2;}}", production_placeholder));
    const auto* statements{ boost::get<std::vector<Hermit::parser::statement_type>>(&production_placeholder) };
    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 1U);
    EXPECT_NE(boost::get<Hermit::parser::expression_statement_type>(&statements->front()), nullptr);

}

TEST(parser, experimental_all_statement_alternatives_parse_and_evaluate_together)
{
    Hermit::config cfg;
    Hermit::context ctx;
    Hermit::parser::grammar grammar;
    Hermit::parser::statement_type statement;
    Hermit::parser::placeholder_result_type prefix_placeholder;
    Hermit::parser::placeholder_result_type placeholder;

    EXPECT_TRUE(grammar.parse_statement("value=1", statement));
    EXPECT_TRUE(grammar.parse_statement("value+=2", statement));
    EXPECT_TRUE(grammar.parse_statement("{value*=3}", statement));
    EXPECT_TRUE(grammar.parse_statement("if(1)value++; else value=9", statement));
    EXPECT_TRUE(grammar.parse_statement("while(0)value=2", statement));
    EXPECT_TRUE(grammar.parse_statement("value--;", statement));
    EXPECT_TRUE(grammar.parse_statement_placeholder("{{value=1 value+=2}}", prefix_placeholder));
    EXPECT_TRUE(grammar.parse_statement_placeholder("{{value=1 value+=2 {value*=3}}}", prefix_placeholder));
    EXPECT_TRUE(grammar.parse_statement_placeholder("{{value=1 value+=2 {value*=3} if(1)value++; else value=9}}", prefix_placeholder));
    EXPECT_TRUE(grammar.parse_statement_placeholder("{{value=1 value+=2 {value*=3} if(1)value++; else value=9 while(0)value=2}}", prefix_placeholder));
    ASSERT_TRUE(grammar.parse_statement_placeholder("{{value=1 value+=2 {value*=3} if(1)value++; else value=9 while(0)value=2 value--;}}", placeholder));
    const auto* statements{ boost::get<std::vector<Hermit::parser::statement_type>>(&placeholder) };
    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 6U);
    EXPECT_NE(boost::get<Hermit::parser::simple_assignment_statement_type>(&(*statements)[0]), nullptr);
    EXPECT_NE(boost::get<Hermit::parser::simple_assignment_statement_type>(&(*statements)[1]), nullptr);
    EXPECT_NE(boost::get<Hermit::parser::compound_statement_type>(&(*statements)[2]), nullptr);
    EXPECT_NE(boost::get<Hermit::parser::if_statement_type>(&(*statements)[3]), nullptr);
    EXPECT_NE(boost::get<Hermit::parser::while_statement_type>(&(*statements)[4]), nullptr);
    EXPECT_NE(boost::get<Hermit::parser::expression_statement_type>(&(*statements)[5]), nullptr);

    EXPECT_EQ(Hermit::parser::evaluate("{{value=1 value+=2 {value*=3} if(1)value++; else value=9 while(0)value=2 value--;}}", cfg, ctx), "");
    ASSERT_NE(ctx.get("value"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("value")), "9");
}
#endif

#if defined(HERMIT_ENABLE_EXPERIMENTAL_EXPRESSION_STATEMENTS)
TEST(parser, experimental_expression_statement_is_parsed_and_discarded)
{
    Hermit::config cfg;
    Hermit::context ctx;
    const auto ast{ Hermit::parser::parse("before{{ 1 + 2; }}after", cfg, ctx) };
    ASSERT_EQ(ast.size(), 3U);
    const auto* placeholder{ boost::get<Hermit::parser::placeholder_type>(&ast[1]) };
    ASSERT_NE(placeholder, nullptr);
    const auto* statements{ boost::get<std::vector<Hermit::parser::statement_type>>(&placeholder->content) };
    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 1U);
    const auto* expression_statement{ boost::get<Hermit::parser::expression_statement_type>(&statements->front()) };
    ASSERT_NE(expression_statement, nullptr);
    ASSERT_EQ(expression_statement->expression.size(), 1U);
    EXPECT_EQ(Hermit::parser::evaluate("before{{ 1 + 2; }}after", cfg, ctx), "beforeafter");
    EXPECT_THROW(Hermit::parser::parse("{{ 1 + 2; trailing }}", cfg, ctx), std::exception);
}

TEST(parser_grammar, experimental_expression_statement_helper_requires_complete_input)
{
    Hermit::parser::grammar grammar;
    Hermit::parser::expression_statement_type expression_statement;
    EXPECT_TRUE(grammar.parse_expression_statement("1 + 2;", expression_statement));
    EXPECT_FALSE(grammar.parse_expression_statement("1 + 2; trailing", expression_statement));
}
#endif

#if defined(HERMIT_ENABLE_EXPERIMENTAL_ASSIGNMENTS)
TEST(parser, experimental_simple_assignment_initializes_and_updates_variable)
{
    Hermit::config cfg;
    Hermit::context ctx;

    EXPECT_EQ(Hermit::parser::evaluate("before{{answer=40+2}}after", cfg, ctx), "beforeafter");
    ASSERT_NE(ctx.get("answer"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("answer")), "42");
    EXPECT_EQ(Hermit::parser::evaluate("{{answer=7}}", cfg, ctx), "");
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("answer")), "7");
    EXPECT_EQ(Hermit::parser::evaluate("{{answer=\"wrong type\"}}", cfg, ctx), "");
    EXPECT_NE(boost::get<int>(ctx.get("answer")), nullptr);
    EXPECT_EQ(*boost::get<int>(ctx.get("answer")), 7);
    EXPECT_EQ(Hermit::parser::evaluate("{{uninitialized=missing}}", cfg, ctx), "");
    EXPECT_EQ(ctx.get("uninitialized"), nullptr);
    EXPECT_EQ(Hermit::parser::evaluate("{{first=1 second=2}}", cfg, ctx), "");
    ASSERT_NE(ctx.get("first"), nullptr);
    ASSERT_NE(ctx.get("second"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("first")), "1");
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("second")), "2");
}

TEST(parser_grammar, experimental_assignment_requires_identifier_and_complete_statement)
{
    Hermit::parser::grammar grammar;
    Hermit::parser::statement_type statement;
    ASSERT_TRUE(grammar.parse_statement("value=1+2", statement));
    const auto* assignment{ boost::get<Hermit::parser::simple_assignment_statement_type>(&statement) };
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->name, "value");
    EXPECT_EQ(assignment->operator_, Hermit::parser::assignment_operator::assign);
    EXPECT_FALSE(grammar.parse_statement("1=value", statement));
    EXPECT_FALSE(grammar.parse_statement("value=1 trailing", statement));
    Hermit::parser::placeholder_result_type placeholder;
    EXPECT_TRUE(grammar.parse_statement_placeholder("{{value=1}}", placeholder));
    EXPECT_FALSE(grammar.parse_statement_placeholder("{{value=1 trailing}}", placeholder));
}
#endif

#if defined(HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_ASSIGNMENTS)
TEST(parser, experimental_compound_assignments_reuse_checked_assignment_operators)
{
    Hermit::config cfg;
    Hermit::context ctx;

    EXPECT_EQ(Hermit::parser::evaluate("{{value=5 value+=2 value-=1 value*=3 value/=2 value%=5}}", cfg, ctx), "");
    ASSERT_NE(ctx.get("value"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("value")), "4");
    EXPECT_EQ(Hermit::parser::evaluate("{{bits=1 bits<<=2 bits>>=1 bits|=1 bits&=5 bits^=4}}", cfg, ctx), "");
    ASSERT_NE(ctx.get("bits"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("bits")), "5");
}

TEST(parser, experimental_compound_assignment_errors_preserve_variable)
{
    Hermit::config cfg;
    Hermit::context ctx;

    EXPECT_EQ(Hermit::parser::evaluate("{{value=8 value/=0}}", cfg, ctx), "");
    ASSERT_NE(ctx.get("value"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("value")), "8");
    EXPECT_EQ(Hermit::parser::evaluate("{{value%=0}}", cfg, ctx), "");
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("value")), "8");
    EXPECT_EQ(Hermit::parser::evaluate("{{value+=\"wrong type\"}}", cfg, ctx), "");
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("value")), "8");
    EXPECT_EQ(Hermit::parser::evaluate("{{undefined+=1}}", cfg, ctx), "");
    EXPECT_EQ(ctx.get("undefined"), nullptr);
}

TEST(parser_grammar, experimental_compound_assignment_keeps_operator_in_ast_and_requires_complete_input)
{
    Hermit::parser::grammar grammar;
    Hermit::parser::statement_type statement;
    ASSERT_TRUE(grammar.parse_statement("value<<=2", statement));
    const auto* assignment{ boost::get<Hermit::parser::simple_assignment_statement_type>(&statement) };
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->operator_, Hermit::parser::assignment_operator::shift_left_assign);
    EXPECT_FALSE(grammar.parse_statement("1+=2", statement));
    EXPECT_FALSE(grammar.parse_statement("value+=2 trailing", statement));
}
#endif

#if defined(HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_STATEMENTS)
TEST(parser, experimental_compound_statements_preserve_nested_order_and_apply_side_effects)
{
    Hermit::config cfg;
    Hermit::context ctx;

    EXPECT_EQ(Hermit::parser::evaluate("{{value=0 {value+=1 {value*=2} value+=3}{value+=4}}}", cfg, ctx), "");
    ASSERT_NE(ctx.get("value"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("value")), "9");
}

TEST(parser_grammar, experimental_compound_statements_build_nested_ast_and_accept_empty_blocks)
{
    Hermit::parser::grammar grammar;
    Hermit::parser::statement_type statement;
    ASSERT_TRUE(grammar.parse_statement("{first=1 {second=2} first+=3}", statement));
    const auto* block{ boost::get<Hermit::parser::compound_statement_type>(&statement) };
    ASSERT_NE(block, nullptr);
    ASSERT_EQ(block->statements.size(), 3U);
    EXPECT_NE(boost::get<Hermit::parser::simple_assignment_statement_type>(&block->statements[0]), nullptr);
    const auto* nested_block{ boost::get<Hermit::parser::compound_statement_type>(&block->statements[1]) };
    ASSERT_NE(nested_block, nullptr);
    ASSERT_EQ(nested_block->statements.size(), 1U);
    EXPECT_NE(boost::get<Hermit::parser::simple_assignment_statement_type>(&nested_block->statements.front()), nullptr);
    EXPECT_NE(boost::get<Hermit::parser::simple_assignment_statement_type>(&block->statements[2]), nullptr);
    EXPECT_TRUE(grammar.parse_statement("{}", statement));
    EXPECT_EQ(boost::get<Hermit::parser::compound_statement_type>(&statement)->statements.size(), 0U);
}

TEST(parser_grammar, experimental_compound_statement_requires_complete_input)
{
    Hermit::parser::grammar grammar;
    Hermit::parser::statement_type statement;
    Hermit::parser::placeholder_result_type placeholder;
    EXPECT_FALSE(grammar.parse_statement("{value=1", statement));
    EXPECT_FALSE(grammar.parse_statement("{value=1} trailing", statement));
    ASSERT_TRUE(grammar.parse_statement_placeholder("{{{value=1}{value+=2}}}", placeholder));
    const auto* statements{ boost::get<std::vector<Hermit::parser::statement_type>>(&placeholder) };
    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 2U);
}
#endif

#if defined(HERMIT_ENABLE_EXPERIMENTAL_IF_STATEMENTS)
TEST(parser, experimental_if_else_runs_only_the_selected_branch)
{
    Hermit::config cfg;
    Hermit::context ctx;

    EXPECT_EQ(Hermit::parser::evaluate("{{if(1){value=1}else{notSelected=2} if(0){notSelected=3}else{value+=3} if(0){notSelected=4}}}", cfg, ctx), "");
    ASSERT_NE(ctx.get("value"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("value")), "4");
    EXPECT_EQ(ctx.get("notSelected"), nullptr);
}

TEST(parser_grammar, experimental_if_else_binds_dangling_else_to_nearest_if)
{
    Hermit::parser::grammar grammar;
    Hermit::parser::statement_type statement;
    ASSERT_TRUE(grammar.parse_statement("if(1) if(0) first=1 else second=2", statement));
    const auto* outer_if{ boost::get<Hermit::parser::if_statement_type>(&statement) };
    ASSERT_NE(outer_if, nullptr);
    EXPECT_FALSE(outer_if->else_statement);
    const auto* inner_if{ boost::get<Hermit::parser::if_statement_type>(&outer_if->statement) };
    ASSERT_NE(inner_if, nullptr);
    EXPECT_TRUE(inner_if->else_statement);
    EXPECT_NE(boost::get<Hermit::parser::simple_assignment_statement_type>(&*inner_if->else_statement), nullptr);
}

TEST(parser_grammar, experimental_if_statement_requires_complete_input)
{
    Hermit::parser::grammar grammar;
    Hermit::parser::statement_type statement;
    Hermit::parser::placeholder_result_type placeholder;
    EXPECT_FALSE(grammar.parse_statement("if(1)value=1 trailing", statement));
    ASSERT_TRUE(grammar.parse_statement_placeholder("{{if(1)value=1 else value=2}}", placeholder));
    const auto* statements{ boost::get<std::vector<Hermit::parser::statement_type>>(&placeholder) };
    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 1U);
}
#endif

#if defined(HERMIT_ENABLE_EXPERIMENTAL_WHILE_STATEMENTS)
TEST(parser, experimental_while_rechecks_condition_and_supports_nested_loops)
{
    Hermit::config cfg;
    Hermit::context ctx;
    ctx.set("counter", 0);

    EXPECT_EQ(Hermit::parser::evaluate("{{while(0)counter+=1 while(counter<3){counter+=1} while(counter<5)counter+=1}}", cfg, ctx), "");
    ASSERT_NE(ctx.get("counter"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("counter")), "5");

    ctx.set("outer", 0);
    ctx.set("inner", 0);
    ctx.set("total", 0);
    EXPECT_EQ(Hermit::parser::evaluate("{{while(outer<3){inner=0 while(inner<2){total+=1 inner+=1} outer+=1}}}", cfg, ctx), "");
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("total")), "6");
}

TEST(parser, experimental_while_iteration_budget_is_shared_and_allows_exact_limit)
{
    Hermit::config cfg;
    Hermit::context ctx;
    ctx.set("counter", 0);

    EXPECT_EQ(Hermit::parser::evaluate("{{while(counter<5000)counter+=1 while(counter<10000)counter+=1}}", cfg, ctx), "");
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("counter")), "10000");
}

TEST(parser, experimental_while_stops_after_budget_and_preserves_completed_side_effects)
{
    Hermit::config cfg;
    Hermit::context ctx;
    ctx.set("counter", 0);

    EXPECT_EQ(Hermit::parser::evaluate("{{while(counter<10000)counter+=1 while(1)counter+=1}}", cfg, ctx), "");
    ASSERT_NE(ctx.get("counter"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("counter")), "10000");
}

TEST(parser_grammar, experimental_while_builds_nested_ast_and_requires_complete_input)
{
    Hermit::parser::grammar grammar;
    Hermit::parser::statement_type statement;
    ASSERT_TRUE(grammar.parse_statement("while(1){while(0)value+=1}", statement));
    const auto* outer_while{ boost::get<Hermit::parser::while_statement_type>(&statement) };
    ASSERT_NE(outer_while, nullptr);
    const auto* body{ boost::get<Hermit::parser::compound_statement_type>(&outer_while->statement) };
    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->statements.size(), 1U);
    EXPECT_NE(boost::get<Hermit::parser::while_statement_type>(&body->statements.front()), nullptr);
    EXPECT_FALSE(grammar.parse_statement("while(1)value+=1 trailing", statement));
}
#endif

#if defined(HERMIT_ENABLE_EXPERIMENTAL_INCREMENT_DECREMENT)
TEST(parser, experimental_increment_and_decrement_preserve_prefix_postfix_values)
{
    Hermit::config cfg;
    Hermit::context ctx;
    ctx.set("value", 5);

    EXPECT_EQ(Hermit::parser::evaluate("{{++value}}", cfg, ctx), "6");
    EXPECT_EQ(Hermit::parser::evaluate("{{value++}}", cfg, ctx), "6");
    EXPECT_EQ(Hermit::parser::evaluate("{{--value}}", cfg, ctx), "6");
    EXPECT_EQ(Hermit::parser::evaluate("{{value--}}", cfg, ctx), "6");
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("value")), "5");

    EXPECT_EQ(Hermit::parser::evaluate("{{value=1 old=value++ new=++value}}", cfg, ctx), "");
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("old")), "1");
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("new")), "3");
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("value")), "3");
}

TEST(parser, experimental_increment_and_decrement_reject_invalid_targets_and_types)
{
    Hermit::config cfg;
    Hermit::context ctx;
    ctx.set("integer", 1);
    ctx.set("boolean", true);
    ctx.set("floating", 1.5);

    EXPECT_EQ(Hermit::parser::evaluate("{{++(integer)}}", cfg, ctx), "");
    EXPECT_EQ(Hermit::parser::evaluate("{{1++}}", cfg, ctx), "");
    EXPECT_EQ(Hermit::parser::evaluate("{{integer++++}}", cfg, ctx), "");
    EXPECT_EQ(Hermit::parser::evaluate("{{++missing}}", cfg, ctx), "");
    EXPECT_EQ(Hermit::parser::evaluate("{{boolean++}}", cfg, ctx), "");
    EXPECT_EQ(Hermit::parser::evaluate("{{--floating}}", cfg, ctx), "");
    EXPECT_EQ(ctx.get("missing"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("integer")), "1");
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("boolean")), "1");
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("floating")), "1.5");
}

TEST(parser, experimental_increment_and_decrement_reject_integer_overflow_without_mutation)
{
    Hermit::config cfg;
    Hermit::context ctx;
    ctx.set("maximum", (std::numeric_limits<int>::max)());
    ctx.set("minimum", (std::numeric_limits<int>::min)());

    EXPECT_EQ(Hermit::parser::evaluate("{{maximum++}}", cfg, ctx), "");
    EXPECT_EQ(Hermit::parser::evaluate("{{--minimum}}", cfg, ctx), "");
    EXPECT_EQ(*boost::get<int>(ctx.get("maximum")), (std::numeric_limits<int>::max)());
    EXPECT_EQ(*boost::get<int>(ctx.get("minimum")), (std::numeric_limits<int>::min)());
}

TEST(parser, experimental_increment_and_decrement_work_in_loop_conditions_and_bodies)
{
    Hermit::config cfg;
    Hermit::context ctx;
    ctx.set("counter", 0);

    EXPECT_EQ(Hermit::parser::evaluate("{{while(counter++<3)counter+=1}}", cfg, ctx), "");
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("counter")), "5");
}

TEST(parser_grammar, experimental_increment_and_decrement_require_complete_expression_input)
{
    Hermit::parser::grammar grammar;
    Hermit::parser::placeholder_result_type placeholder;
    EXPECT_TRUE(grammar.parse_statement_placeholder("{{value=++value}}", placeholder));
    EXPECT_FALSE(grammar.parse_statement_placeholder("{{value=++value trailing}}", placeholder));
}
#endif

namespace boost::spirit::traits
{
    template<>
    struct transform_attribute<hermit::parser::placeholder_type, hermit::parser::expression_type, boost::spirit::qi::domain>
    {
        using type = hermit::parser::expression_type;

        static type pre(hermit::parser::placeholder_type&)
        {
            return {};
        }

        static void post(hermit::parser::placeholder_type& value, type attribute)
        {
            value.content = std::move(attribute);
        }

        static void fail(hermit::parser::placeholder_type&)
        {
        }
    };
}

TEST(parser_grammar, ast_attribute_cast_wraps_the_complete_placeholder_parser)
{
    namespace qi = boost::spirit::qi;
    const std::string input{ "{{}}" };
    auto first{ input.begin() };
    const hermit::parser::expression_type expression{};
    hermit::parser::placeholder_type result;
    const bool parsed{ qi::parse(first, input.end(), qi::attr_cast<hermit::parser::placeholder_type>(qi::lit("{{") >> qi::attr(expression) >> qi::lit("}}")), result) };
    EXPECT_TRUE(parsed);
    EXPECT_EQ(first, input.end());
    const auto* parsed_expression{ boost::get<hermit::parser::expression_type>(&result.content) };
    ASSERT_NE(parsed_expression, nullptr);
    EXPECT_TRUE(parsed_expression->empty());
}

TEST(parser, evaluates_literal_and_variable_macros)
{
    hermit::config cfg;
    hermit::context ctx;
    ctx.set("name", std::string{ "Hermit" });
    EXPECT_EQ(hermit::parser::evaluate_recursive("value={{ 1 + 2 }}", cfg, 4, ctx), "value=3");
    EXPECT_EQ(hermit::parser::evaluate_recursive("hello {{ name }}", cfg, 4, ctx), "hello Hermit");
}

TEST(parser, non_template_evaluation_boundary)
{
    hermit::config cfg;
    hermit::context ctx;
    const auto handle{ hermit::parser::make_grammar_handle() };
    EXPECT_EQ(hermit::parser::parse("plain", cfg, handle, ctx).size(), 1U);
    EXPECT_EQ(hermit::parser::evaluate("{{ 2 + 5 }}", cfg, ctx), "7");
    EXPECT_EQ(hermit::parser::parse("plain", cfg, ctx).size(), 1U);
}

TEST(parser_grammar, parses_variable_macro_as_an_unterminated_placeholder)
{
    hermit::config cfg;
    hermit::context ctx;
    const auto ast{ hermit::parser::parse("{{ name }}", cfg, ctx) };
    ASSERT_EQ(ast.size(), 1U);
    const auto* placeholder{ boost::get<hermit::parser::placeholder_type>(&ast.front()) };
    ASSERT_NE(placeholder, nullptr);
    const auto* expression{ boost::get<hermit::parser::expression_type>(&placeholder->content) };
    ASSERT_NE(expression, nullptr);
    ASSERT_EQ(expression->size(), 1U);
}

TEST(parser_grammar, parses_function_macro_with_nested_expression_arguments)
{
    hermit::config cfg;
    hermit::context ctx;
    const auto ast{ hermit::parser::parse("{{ string(1 + int(2)) }}", cfg, ctx) };
    ASSERT_EQ(ast.size(), 1U);
    const auto* placeholder{ boost::get<hermit::parser::placeholder_type>(&ast.front()) };
    ASSERT_NE(placeholder, nullptr);
    const auto* expression{ boost::get<hermit::parser::expression_type>(&placeholder->content) };
    ASSERT_NE(expression, nullptr);
    ASSERT_EQ(expression->size(), 1U);
}

TEST(parser_grammar, parses_comma_separated_macro_arguments)
{
    hermit::config cfg;
    hermit::context ctx;
    const auto ast{ hermit::parser::parse("{{ choice(1, 2) }}", cfg, ctx) };
    ASSERT_EQ(ast.size(), 1U);
    const auto* placeholder{ boost::get<hermit::parser::placeholder_type>(&ast.front()) };
    ASSERT_NE(placeholder, nullptr);
    const auto* expression{ boost::get<hermit::parser::expression_type>(&placeholder->content) };
    ASSERT_NE(expression, nullptr);
    EXPECT_EQ(expression->size(), 1U);
}

TEST(parser_grammar, rejects_unbalanced_macro_delimiters)
{
    hermit::config cfg;
    hermit::context ctx;
    EXPECT_THROW(hermit::parser::parse("{{ string(1)", cfg, ctx), std::exception);
    EXPECT_THROW(hermit::parser::parse("{{ string(1 }", cfg, ctx), std::exception);
}

#if defined(HERMIT_ENABLE_EXPERIMENTAL_STATEMENTS)
TEST(parser_grammar, parses_if_statement_into_condition_and_compound_body_fields)
{
    Hermit::config cfg;
    Hermit::context ctx;
    const auto ast{ Hermit::parser::parse("{{if(1){selected=7}}}", cfg, ctx) };
    ASSERT_EQ(ast.size(), 1U);
    const auto* placeholder{ boost::get<Hermit::parser::placeholder_type>(&ast.front()) };
    ASSERT_NE(placeholder, nullptr);
    const auto* statements{ boost::get<std::vector<Hermit::parser::statement_type>>(&placeholder->content) };
    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 1U);
    const auto* if_statement{ boost::get<Hermit::parser::if_statement_type>(&statements->front()) };
    ASSERT_NE(if_statement, nullptr);
    ASSERT_EQ(if_statement->condition.size(), 1U);
    const auto* body{ boost::get<Hermit::parser::compound_statement_type>(&if_statement->statement) };
    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->statements.size(), 1U);
    EXPECT_NE(boost::get<Hermit::parser::assignment_statement_type>(&body->statements.front()), nullptr);
}

TEST(parser_grammar, identifies_wrong_control_body_alternative_for_unbraced_adjacent_statements)
{
    Hermit::config cfg;
    Hermit::context ctx;
    const auto ast{ Hermit::parser::parse("{{if(1)a=1 b=2}}", cfg, ctx) };
    ASSERT_EQ(ast.size(), 1U);
    const auto* placeholder{ boost::get<Hermit::parser::placeholder_type>(&ast.front()) };
    ASSERT_NE(placeholder, nullptr);
    const auto* statements{ boost::get<std::vector<Hermit::parser::statement_type>>(&placeholder->content) };
    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 2U);
    EXPECT_NE(boost::get<Hermit::parser::if_statement_type>(&statements->front()), nullptr);
    EXPECT_NE(boost::get<Hermit::parser::assignment_statement_type>(&statements->back()), nullptr);
}

TEST(parser_grammar, records_unbraced_and_nested_if_bodies_as_distinct_ast_nodes)
{
    Hermit::config cfg;
    Hermit::context ctx;
    const auto ast{ Hermit::parser::parse("{{if(0)if(1)selected=7}}", cfg, ctx) };
    ASSERT_EQ(ast.size(), 1U);
    const auto* placeholder{ boost::get<Hermit::parser::placeholder_type>(&ast.front()) };
    ASSERT_NE(placeholder, nullptr);
    const auto* statements{ boost::get<std::vector<Hermit::parser::statement_type>>(&placeholder->content) };
    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 1U);
    const auto* outer_if{ boost::get<Hermit::parser::if_statement_type>(&statements->front()) };
    ASSERT_NE(outer_if, nullptr);
    ASSERT_EQ(outer_if->condition.size(), 1U);
    const auto* inner_if{ boost::get<Hermit::parser::if_statement_type>(&outer_if->statement) };
    ASSERT_NE(inner_if, nullptr);
    ASSERT_EQ(inner_if->condition.size(), 1U);
    EXPECT_NE(boost::get<Hermit::parser::assignment_statement_type>(&inner_if->statement), nullptr);
    EXPECT_FALSE(inner_if->else_statement);
    EXPECT_FALSE(outer_if->else_statement);
}

TEST(parser, evaluates_false_if_without_running_its_body)
{
    Hermit::config cfg;
    Hermit::context ctx;
    EXPECT_EQ(Hermit::parser::evaluate("{{if(0)selected=7}}after", cfg, ctx), "after");
    EXPECT_EQ(ctx.get("selected"), nullptr);
}

TEST(parser, evaluates_nested_if_and_while_statements)
{
    Hermit::config cfg;
    Hermit::context ctx;
    ctx.set("counter", 0);
    EXPECT_EQ(Hermit::parser::evaluate("{{if(1){if(1)counter=counter+1 while(counter<2){counter+=1}}}}", cfg, ctx), "");
    ASSERT_NE(ctx.get("counter"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("counter")), "2");
}

TEST(parser_grammar, parses_else_body_into_if_ast)
{
    Hermit::config cfg;
    Hermit::context ctx;
    const auto ast{ Hermit::parser::parse("{{if(0){selected=1}else{selected=2}}}", cfg, ctx) };
    ASSERT_EQ(ast.size(), 1U);
    const auto* placeholder{ boost::get<Hermit::parser::placeholder_type>(&ast.front()) };
    ASSERT_NE(placeholder, nullptr);
    const auto* statements{ boost::get<std::vector<Hermit::parser::statement_type>>(&placeholder->content) };
    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 1U);
    const auto* if_statement{ boost::get<Hermit::parser::if_statement_type>(&statements->front()) };
    ASSERT_NE(if_statement, nullptr);
    ASSERT_TRUE(if_statement->else_statement);
    EXPECT_NE(boost::get<Hermit::parser::compound_statement_type>(&*if_statement->else_statement), nullptr);
}

TEST(parser, executes_if_and_else_branches_exclusively)
{
    Hermit::config cfg;
    Hermit::context ctx;
    EXPECT_EQ(Hermit::parser::evaluate("{{if(1){selected=1}else{selected=2}}}", cfg, ctx), "");
    ASSERT_NE(ctx.get("selected"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("selected")), "1");
    EXPECT_EQ(Hermit::parser::evaluate("{{if(0){selected=3}else{selected=4}}}", cfg, ctx), "");
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("selected")), "4");
}

TEST(parser, binds_else_to_the_nearest_if)
{
    Hermit::config cfg;
    Hermit::context ctx;
    const auto ast{ Hermit::parser::parse("{{if(1)if(0)selected=1 else selected=2}}", cfg, ctx) };
    ASSERT_EQ(ast.size(), 1U);
    const auto* placeholder{ boost::get<Hermit::parser::placeholder_type>(&ast.front()) };
    ASSERT_NE(placeholder, nullptr);
    const auto* statements{ boost::get<std::vector<Hermit::parser::statement_type>>(&placeholder->content) };
    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 1U);
    const auto* outer_if{ boost::get<Hermit::parser::if_statement_type>(&statements->front()) };
    ASSERT_NE(outer_if, nullptr);
    EXPECT_FALSE(outer_if->else_statement);
    const auto* inner_if{ boost::get<Hermit::parser::if_statement_type>(&outer_if->statement) };
    ASSERT_NE(inner_if, nullptr);
    ASSERT_TRUE(inner_if->else_statement);
    EXPECT_NE(boost::get<Hermit::parser::assignment_statement_type>(&*inner_if->else_statement), nullptr);
    EXPECT_EQ(Hermit::parser::evaluate("{{if(1)if(0)selected=1 else selected=2}}", cfg, ctx), "");
    ASSERT_NE(ctx.get("selected"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("selected")), "2");
}

TEST(parser, executes_if_compound_and_while_statements_without_expanding_them)
{
    Hermit::config cfg;
    Hermit::context ctx;
    ctx.set("counter", 0);
    EXPECT_EQ(Hermit::parser::evaluate("before{{if(1){selected=7}}}{{while(counter<3)counter+=1}}after", cfg, ctx), "beforeafter");
    ASSERT_NE(ctx.get("selected"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("selected")), "7");
    ASSERT_NE(ctx.get("counter"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("counter")), "3");
}

TEST(parser, evaluates_assignment_and_reuses_context)
{
    Hermit::config cfg;
    Hermit::context ctx;
    EXPECT_EQ(Hermit::parser::evaluate_recursive("{{ x = 4 }}{{ x + 3 }}", cfg, 4, ctx), "7");
    ASSERT_NE(ctx.get("x"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("x")), "4");
}

TEST(parser, expands_expression_result_and_discards_statement_result)
{
    Hermit::config cfg;
    Hermit::context ctx;
    EXPECT_EQ(Hermit::parser::evaluate("{{ 1 + 2 }}", cfg, ctx), "3");
    EXPECT_EQ(Hermit::parser::evaluate("{{ assigned = 7 }}{{ assigned }}", cfg, ctx), "7");
}

TEST(parser, evaluates_expression_statement_without_expanding_its_value)
{
    Hermit::config cfg;
    Hermit::context ctx;
    ctx.set("counter", 0);
    EXPECT_EQ(Hermit::parser::evaluate("{{ 1 + 2; }}", cfg, ctx), "");
    EXPECT_EQ(Hermit::parser::evaluate("{{if(1)counter++;else counter--;}}", cfg, ctx), "");
    ASSERT_NE(ctx.get("counter"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("counter")), "1");

    const auto ast{ Hermit::parser::parse("{{counter++;}}", cfg, ctx) };
    ASSERT_EQ(ast.size(), 1U);
    const auto* placeholder{ boost::get<Hermit::parser::placeholder_type>(&ast.front()) };
    ASSERT_NE(placeholder, nullptr);
    const auto* statements{ boost::get<std::vector<Hermit::parser::statement_type>>(&placeholder->content) };
    ASSERT_NE(statements, nullptr);
    ASSERT_EQ(statements->size(), 1U);
    const auto* expression_statement{ boost::get<Hermit::parser::expression_statement_type>(&statements->front()) };
    ASSERT_NE(expression_statement, nullptr);
    EXPECT_EQ(expression_statement->expression.size(), 1U);
}

TEST(parser, backtracks_from_incomplete_statement_placeholder_to_expression)
{
    Hermit::config cfg;
    Hermit::context ctx;
    ctx.set("value", 5);

    EXPECT_EQ(Hermit::parser::evaluate("{{value + 1}}", cfg, ctx), "6");
    EXPECT_EQ(Hermit::parser::evaluate("{{value + 1;}}", cfg, ctx), "");
    EXPECT_THROW(Hermit::parser::parse("{{if(1)value + }}", cfg, ctx), std::exception);
    EXPECT_THROW(Hermit::parser::parse("{{while(1){value += 1}}", cfg, ctx), std::exception);
}

TEST(parser, supports_expression_statements_in_compound_control_bodies)
{
    Hermit::config cfg;
    Hermit::context ctx;
    ctx.set("counter", 0);

    EXPECT_EQ(Hermit::parser::evaluate("{{if(1){counter++; counter + 100;}}}", cfg, ctx), "");
    EXPECT_EQ(Hermit::parser::evaluate("{{while(counter<3){counter++;}}}", cfg, ctx), "");
    ASSERT_NE(ctx.get("counter"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("counter")), "3");
}

TEST(parser, supports_expression_statement_as_unbraced_if_body_with_else)
{
    Hermit::config cfg;
    Hermit::context ctx;
    ctx.set("counter", 0);

    EXPECT_EQ(Hermit::parser::evaluate("{{if(1)counter++;else counter--;}}", cfg, ctx), "");
    ASSERT_NE(ctx.get("counter"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("counter")), "1");
}
#endif

#if !defined(HERMIT_ENABLE_EXPERIMENTAL_STATEMENTS) && !defined(HERMIT_ENABLE_EXPERIMENTAL_STAGE8_STATEMENTS) && !defined(HERMIT_ENABLE_EXPERIMENTAL_EXPRESSION_STATEMENTS) && !defined(HERMIT_ENABLE_EXPERIMENTAL_ASSIGNMENTS) && !defined(HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_ASSIGNMENTS) && !defined(HERMIT_ENABLE_EXPERIMENTAL_COMPOUND_STATEMENTS) && !defined(HERMIT_ENABLE_EXPERIMENTAL_IF_STATEMENTS) && !defined(HERMIT_ENABLE_EXPERIMENTAL_WHILE_STATEMENTS) && !defined(HERMIT_ENABLE_EXPERIMENTAL_INCREMENT_DECREMENT)
TEST(parser, rejects_disabled_statement_syntax)
{
    hermit::config cfg;
    hermit::context ctx;
    ctx.set("value", 0);

    EXPECT_EQ(hermit::parser::evaluate("{{1 + 2}}", cfg, ctx), "3");
    EXPECT_EQ(hermit::parser::evaluate("{{string(42)}}", cfg, ctx), "42");

    EXPECT_THROW(hermit::parser::parse("{{value = 1}}", cfg, ctx), std::exception);
    EXPECT_THROW(hermit::parser::parse("{{value++;}}", cfg, ctx), std::exception);
    EXPECT_THROW(hermit::parser::parse("{{value++}}", cfg, ctx), std::exception);
    EXPECT_THROW(hermit::parser::parse("{{++value}}", cfg, ctx), std::exception);
    EXPECT_THROW(hermit::parser::parse("{{if(1){value=1}}}", cfg, ctx), std::exception);
    EXPECT_THROW(hermit::parser::parse("{{while(0)value=1}}", cfg, ctx), std::exception);
    EXPECT_THROW(hermit::parser::parse("{{{value=1}}}", cfg, ctx), std::exception);
    EXPECT_THROW(hermit::parser::parse("{{1 + 2;}}", cfg, ctx), std::exception);
    ASSERT_NE(ctx.get("value"), nullptr);
    EXPECT_EQ(hermit::value_to_string(*ctx.get("value")), "0");

    hermit::parser::grammar grammar;
    hermit::parser::expression_statement_type expression_statement;
    hermit::parser::statement_type statement;
    hermit::parser::placeholder_result_type placeholder;
    EXPECT_FALSE(grammar.parse_expression_statement("1 + 2;", expression_statement));
    EXPECT_FALSE(grammar.parse_statement("value = 1", statement));
    EXPECT_FALSE(grammar.parse_statement("{value=1}", statement));
    EXPECT_FALSE(grammar.parse_statement_placeholder("{{value = 1}}", placeholder));
}
#endif

TEST(parser_grammar, reserves_control_and_boolean_keywords_as_identifiers)
{
    hermit::config cfg;
    hermit::context ctx;
    ctx.set("if", 10);
    ctx.set("else", 20);
    ctx.set("while", 30);
    ctx.set("true", 40);
    ctx.set("false", 50);
    ctx.set("iffy", 1);
    ctx.set("elsewhere", 2);
    ctx.set("while_count", 3);
    EXPECT_THROW(hermit::parser::parse("{{if}}", cfg, ctx), std::exception);
    EXPECT_THROW(hermit::parser::parse("{{else}}", cfg, ctx), std::exception);
    EXPECT_THROW(hermit::parser::parse("{{while}}", cfg, ctx), std::exception);
    EXPECT_EQ(hermit::parser::evaluate("{{true}} {{false}}", cfg, ctx), "1 0");
    EXPECT_EQ(hermit::parser::evaluate("{{iffy}} {{elsewhere}} {{while_count}}", cfg, ctx), "1 2 3");
}

TEST(parser, stops_when_recursive_expansion_is_unchanged)
{
    hermit::config cfg;
    hermit::context ctx;
    ctx.set("loop", std::string{ "{{ loop }}" });
    EXPECT_EQ(hermit::parser::evaluate_recursive("{{ loop }}", cfg, 1, ctx), "{{ loop }}");
}

TEST(parser, evaluates_builtin_macros_inside_templates)
{
    hermit::config cfg;
    hermit::context ctx;
    EXPECT_EQ(hermit::parser::evaluate("{{ int(\"42\") + 1 }}", cfg, ctx), "43");
    EXPECT_EQ(hermit::parser::evaluate("{{ json_literal(\"a\\\"b\") }}", cfg, ctx), "a\\\"b");
}

TEST(public_api, expands_builtin_macros_without_mutating_input_context)
{
    hermit::config cfg;
    hermit::context ctx;
    ctx.set("name", std::string{ "Hermit" });
    EXPECT_EQ(hermit::expand_macro("{{ string(42) }} {{ name }}", cfg, ctx), "42 Hermit");
    EXPECT_EQ(hermit::value_to_string(*ctx.get("name")), "Hermit");
}

TEST(parser, evaluates_nested_builtin_macro_arguments)
{
    hermit::config cfg;
    hermit::context ctx;
    EXPECT_EQ(hermit::parser::evaluate("{{ string(int(42)) }}", cfg, ctx), "42");
}

TEST(parser, evaluates_expression_arguments_before_builtin_conversion)
{
    hermit::config cfg;
    hermit::context ctx;
    EXPECT_EQ(hermit::parser::evaluate("{{ string(1 + 2 * 3) }}", cfg, ctx), "7");
}

TEST(parser, resolves_context_variables_as_builtin_arguments)
{
    hermit::config cfg;
    hermit::context ctx;
    ctx.set("count", 42);
    EXPECT_EQ(hermit::parser::evaluate("{{ string(count) }}", cfg, ctx), "42");
}

TEST(parser, converts_boolean_expression_result_through_string_macro)
{
    hermit::config cfg;
    hermit::context ctx;
    EXPECT_EQ(hermit::parser::evaluate("{{ string(1 < 2) }}", cfg, ctx), "1");
}

TEST(parser, evaluates_both_conditional_expression_branches)
{
    hermit::config cfg;
    hermit::context ctx;
    EXPECT_EQ(hermit::parser::evaluate("{{ 1 < 2 ? 10 : 20 }}", cfg, ctx), "10");
    EXPECT_EQ(hermit::parser::evaluate("{{ 2 < 1 ? 10 : 20 }}", cfg, ctx), "20");
}

TEST(parser, evaluates_nested_conditional_expressions)
{
    hermit::config cfg;
    hermit::context ctx;
    EXPECT_EQ(hermit::parser::evaluate("{{ 1 < 2 ? 3 < 4 ? 10 : 20 : 30 }}", cfg, ctx), "10");
    EXPECT_EQ(hermit::parser::evaluate("{{ 2 < 1 ? 10 : 3 < 4 ? 20 : 30 }}", cfg, ctx), "20");
}

TEST(parser, evaluates_logical_conditions_in_conditional_expressions)
{
    hermit::config cfg;
    hermit::context ctx;
    EXPECT_EQ(hermit::parser::evaluate("{{ (1 < 2 && 2 < 3) ? 1 : 0 }}", cfg, ctx), "1");
    EXPECT_EQ(hermit::parser::evaluate("{{ (1 < 2 && 3 < 2) || 4 < 5 ? 1 : 0 }}", cfg, ctx), "1");
}

TEST(parser, parses_overlapping_operator_tokens_with_expected_precedence)
{
    hermit::config cfg;
    hermit::context ctx;

    EXPECT_EQ(hermit::parser::evaluate("{{ 1 | 2 || 0 }}", cfg, ctx), "1");
    EXPECT_EQ(hermit::parser::evaluate("{{ 1 || 2 | 0 }}", cfg, ctx), "1");
    EXPECT_EQ(hermit::parser::evaluate("{{ 1 & 3 && 4 }}", cfg, ctx), "1");
    EXPECT_EQ(hermit::parser::evaluate("{{ 1 && 3 & 1 }}", cfg, ctx), "1");
    EXPECT_EQ(hermit::parser::evaluate("{{ 1 << 2 }}", cfg, ctx), "4");
    EXPECT_EQ(hermit::parser::evaluate("{{ 1 <= 2 }}", cfg, ctx), "1");
    EXPECT_EQ(hermit::parser::evaluate("{{ 1 >= 2 }}", cfg, ctx), "0");
    EXPECT_EQ(hermit::parser::evaluate("{{ 1, 2 }}", cfg, ctx), "2");
    EXPECT_EQ(hermit::parser::evaluate("{{ (1, 2) }}", cfg, ctx), "2");
}

#if defined(HERMIT_ENABLE_EXPERIMENTAL_STATEMENTS)
TEST(parser, evaluates_only_the_selected_conditional_branch)
{
    Hermit::config cfg;
    Hermit::context ctx;
    EXPECT_EQ(Hermit::parser::evaluate("{{ 1 ? (selected = 7) : (unselected = 8) }}{{ selected }}", cfg, ctx), "7");
    EXPECT_EQ(ctx.get("unselected"), nullptr);
}

#endif

TEST(parser_grammar, rejects_conditional_expression_without_else_branch)
{
    hermit::config cfg;
    hermit::context ctx;
    EXPECT_THROW(hermit::parser::parse("{{ 1 ? 2 }}", cfg, ctx), std::exception);
}

