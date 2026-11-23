#include <gtest/gtest.h>

#include "Hermit_config.hpp"
#include "Hermit_context.hpp"
#include "Hermit_parser.hpp"
#include "Hermit_public.hpp"
#include "Hermit_value.hpp"

TEST(parser, evaluates_literal_and_variable_macros)
{
    Hermit::config cfg;
    Hermit::context ctx;
    ctx.set("name", std::string{ "Hermit" });
    EXPECT_EQ(Hermit::parser::evaluate_recursive("value={{ 1 + 2 }}", cfg, 4, ctx), "value=3");
    EXPECT_EQ(Hermit::parser::evaluate_recursive("hello {{ name }}", cfg, 4, ctx), "hello Hermit");
}

TEST(parser, non_template_evaluation_boundary)
{
    Hermit::config cfg;
    Hermit::context ctx;
    const auto handle{ Hermit::parser::make_grammar_handle() };
    EXPECT_EQ(Hermit::parser::parse("plain", cfg, handle, ctx).size(), 1U);
    EXPECT_EQ(Hermit::parser::evaluate("{{ 2 + 5 }}", cfg, ctx), "7");
    EXPECT_EQ(Hermit::parser::parse("plain", cfg, ctx).size(), 1U);
}

TEST(parser, evaluates_assignment_and_reuses_context)
{
    Hermit::config cfg;
    Hermit::context ctx;
    EXPECT_EQ(Hermit::parser::evaluate_recursive("{{ x = 4 }}{{ x + 3 }}", cfg, 4, ctx), "7");
    ASSERT_NE(ctx.get("x"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("x")), "4");
}

TEST(parser, stops_when_recursive_expansion_is_unchanged)
{
    Hermit::config cfg;
    Hermit::context ctx;
    ctx.set("loop", std::string{ "{{ loop }}" });
    EXPECT_EQ(Hermit::parser::evaluate_recursive("{{ loop }}", cfg, 1, ctx), "{{ loop }}");
}

TEST(parser, evaluates_builtin_macros_inside_templates)
{
    Hermit::config cfg;
    Hermit::context ctx;
    EXPECT_EQ(Hermit::parser::evaluate("{{ int(\"42\") + 1 }}", cfg, ctx), "43");
    EXPECT_EQ(Hermit::parser::evaluate("{{ json_literal(\"a\\\"b\") }}", cfg, ctx), "a\\\"b");
}

TEST(public_api, expands_builtin_macros_without_mutating_input_context)
{
    Hermit::config cfg;
    Hermit::context ctx;
    ctx.set("name", std::string{ "Hermit" });
    EXPECT_EQ(Hermit::expand_macro("{{ string(42) }} {{ name }}", cfg, ctx), "42 Hermit");
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("name")), "Hermit");
}

TEST(parser, evaluates_nested_builtin_macro_arguments)
{
    Hermit::config cfg;
    Hermit::context ctx;
    EXPECT_EQ(Hermit::parser::evaluate("{{ string(int(42)) }}", cfg, ctx), "42");
}

TEST(parser, evaluates_expression_arguments_before_builtin_conversion)
{
    Hermit::config cfg;
    Hermit::context ctx;
    EXPECT_EQ(Hermit::parser::evaluate("{{ string(1 + 2 * 3) }}", cfg, ctx), "7");
}

TEST(parser, resolves_context_variables_as_builtin_arguments)
{
    Hermit::config cfg;
    Hermit::context ctx;
    ctx.set("count", 42);
    EXPECT_EQ(Hermit::parser::evaluate("{{ string(count) }}", cfg, ctx), "42");
}

