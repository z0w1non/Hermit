#include <string>
#include <string_view>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include "public.hpp"
#include "errors.hpp"

#include "builtin.hpp"
#include "config.hpp"
#include "context.hpp"
#include "filesystem.hpp"

TEST(builtin, injects_environment_accessors)
{
    hermit::config cfg;
    hermit::context ctx;
    const std::vector<hermit::value_type> get_arguments{ std::string{"NAME"} };
    const hermit::builtin::macro_argument_type get_args{ get_arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::getenv(get_args, [](std::string_view key) -> std::optional<std::string> { EXPECT_EQ(key, "NAME"); return std::string{"injected"}; })), "injected");
    const std::vector<hermit::value_type> set_arguments{ std::string{"NAME"}, std::string{"value"} };
    const hermit::builtin::macro_argument_type set_args{ set_arguments, cfg, ctx };
    EXPECT_TRUE(hermit::value_to_string(hermit::builtin::setenv(set_args, [](std::string_view key, std::string_view value) { EXPECT_EQ(key, "NAME"); EXPECT_EQ(value, "value"); return true; })).empty());
}

TEST(builtin, returns_empty_for_missing_environment_and_ignores_setter_result)
{
    hermit::config cfg;
    hermit::context ctx;
    const std::vector<hermit::value_type> get_arguments{ std::string{ "MISSING" } };
    const hermit::builtin::macro_argument_type get_args{ get_arguments, cfg, ctx };
    EXPECT_TRUE(hermit::value_to_string(hermit::builtin::getenv(get_args, [](std::string_view) { return std::optional<std::string>{}; })).empty());

    const std::vector<hermit::value_type> set_arguments{ std::string{ "NAME" }, std::string{ "value" } };
    const hermit::builtin::macro_argument_type set_args{ set_arguments, cfg, ctx };
    EXPECT_TRUE(hermit::value_to_string(hermit::builtin::setenv(set_args, [](std::string_view, std::string_view) { return false; })).empty());
}

TEST(builtin, injects_text_file_reader_with_configured_path)
{
    hermit::config cfg;
    cfg.base_path = "configured-base";
    hermit::context ctx;
    const std::vector<hermit::value_type> arguments{ std::string{ "input" } };
    const hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::file(args, [](std::string_view path, const hermit::config& reader_cfg) {
        EXPECT_EQ(path, "input");
        EXPECT_EQ(reader_cfg.base_path, "configured-base");
        return std::string{ "macro content" };
    })), "macro content");
}

TEST(builtin, propagates_text_file_reader_failure)
{
    hermit::config cfg;
    hermit::context ctx;
    const std::vector<hermit::value_type> arguments{ std::string{ "missing.txt" } };
    const hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };
    EXPECT_THROW(hermit::builtin::file(args, [](std::string_view, const hermit::config&) -> std::string { throw std::runtime_error{ "read failed" }; }), std::runtime_error);
}

TEST(builtin, file_macro_default_reader_complements_extension_and_base_path)
{
    const std::filesystem::path base_path{ std::filesystem::temp_directory_path() / "hermit_builtin_file_macro_test" };
    std::filesystem::remove_all(base_path);
    std::filesystem::create_directories(base_path);
    const std::filesystem::path input_path{ base_path / "input.txt" };
    {
        std::ofstream output{ input_path };
        ASSERT_TRUE(output.is_open());
        output << "default file content";
    }

    hermit::config cfg;
    cfg.base_path = base_path.string();
    hermit::context ctx;
    const std::vector<hermit::value_type> arguments{ std::string{ "input" } };
    const hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };

    EXPECT_EQ(hermit::value_to_string(hermit::builtin::file(args)), "default file content");

    std::filesystem::remove_all(base_path);
}

TEST(builtin, generated_macro_injects_completion_requests)
{
    hermit::config cfg;
    cfg.llm.backend = &cfg.tg;
    cfg.llm.host = "localhost";
    cfg.llm.port = "8080";
    cfg.llm.completions_target = "/completion";
    cfg.llm.token_count_target = "/token-count";
    cfg.llm.max_completion_iterations = 2;
    cfg.llm.min_completion_tokens = 5;
    cfg.llm.generation_suffix = "!";
    cfg.tg.common.max_tokens = 16;
    cfg.tg.common.truncation_length = 100;
    hermit::context ctx;
    const std::vector<hermit::value_type> arguments{ std::string{ "generated prompt" } };
    const hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };
    int request_count{};
    const hermit::public_api::http_executor executor{ [&request_count](const hermit::public_api::http_request_data& request)
    {
        ++request_count;
        if (request.target == "/token-count")
        {
            const int length{ request_count == 1 ? 0 : 5 };
            return hermit::public_api::http_response_data{ 200, "{\"length\":" + std::to_string(length) + "}" };
        }
        EXPECT_EQ(request.target, "/completion");
        EXPECT_EQ(nlohmann::json::parse(request.body).at("prompt"), "generated prompt");
        return hermit::public_api::http_response_data{ 200, R"({"choices":[{"text":"mock result"}]})" };
    } };

    EXPECT_EQ(hermit::value_to_string(hermit::builtin::generated(args, executor)), "mock result!");
    EXPECT_EQ(request_count, 3);
}

TEST(builtin, generated_macro_propagates_injected_http_failure)
{
    hermit::config cfg;
    cfg.llm.backend = &cfg.tg;
    cfg.llm.host = "localhost";
    cfg.llm.port = "8080";
    cfg.llm.token_count_target = "/token-count";
    hermit::context ctx;
    const std::vector<hermit::value_type> arguments{ std::string{ "generated prompt" } };
    const hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 503, "unavailable" };
    } };

    EXPECT_THROW(hermit::builtin::generated(args, executor), hermit::http_status_exception);
}

TEST(builtin, summary_macro_injects_completion_and_expands_temporary_variables)
{
    hermit::config cfg;
    cfg.llm.backend = &cfg.tg;
    cfg.llm.host = "localhost";
    cfg.llm.port = "8080";
    cfg.llm.completions_target = "/completion";
    cfg.llm.token_count_target = "/token-count";
    cfg.llm.max_completion_iterations = 1;
    cfg.llm.min_completion_tokens = 1;
    cfg.llm.reasoning_prefix = "[reason]";
    cfg.llm.reasoning_suffix = "[/reason]";
    cfg.tg.common.max_tokens = 16;
    cfg.tg.common.truncation_length = 100;
    cfg.lru_cache = hermit::config::cache_type{ [](std::string_view value) { return static_cast<int>(value.size()); } };
    hermit::context ctx;
    const std::vector<hermit::value_type> arguments{ std::string{ "{{target}}\nmax={{max_tokens}}" },
        std::string{ "source text" }, 5 };
    const hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };
    std::vector<std::string> targets;
    const hermit::public_api::http_executor executor{ [&targets](const hermit::public_api::http_request_data& request)
    {
        targets.push_back(request.target);
        if (request.target == "/token-count")
        {
            return hermit::public_api::http_response_data{ 200, R"({"length":0})" };
        }
        EXPECT_EQ(nlohmann::json::parse(request.body).at("prompt"), "source text\nmax=5");
        return hermit::public_api::http_response_data{ 200,
            R"({"choices":[{"text":"[reason]private[/reason]one\nsummary words"}]})" };
    } };

    EXPECT_EQ(hermit::value_to_string(hermit::builtin::summary(args, executor)), "one");
    ASSERT_EQ(targets.size(), 3U);
    EXPECT_EQ(targets[0], "/token-count");
    EXPECT_EQ(targets[1], "/completion");
    EXPECT_EQ(targets[2], "/token-count");
    EXPECT_EQ(ctx.get("target"), nullptr);
    EXPECT_EQ(ctx.get("max_tokens"), nullptr);
}

TEST(builtin, summary_macro_propagates_injected_http_failure)
{
    hermit::config cfg;
    cfg.llm.backend = &cfg.tg;
    cfg.llm.host = "localhost";
    cfg.llm.port = "8080";
    cfg.llm.token_count_target = "/token-count";
    hermit::context ctx;
    const std::vector<hermit::value_type> arguments{ std::string{ "{{target}}" }, std::string{ "source" }, 5 };
    const hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 503, "unavailable" };
    } };

    EXPECT_THROW(hermit::builtin::summary(args, executor), hermit::http_status_exception);
    EXPECT_EQ(ctx.get("target"), nullptr);
    EXPECT_EQ(ctx.get("max_tokens"), nullptr);
}

TEST(builtin, injects_random_source)
{
    hermit::config cfg;
    hermit::context ctx;
    const std::vector<hermit::value_type> random_arguments{ 1, 9 };
    const hermit::builtin::macro_argument_type random_args{ random_arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::random(random_args, [](std::int64_t min, std::int64_t max) { EXPECT_EQ(min, 1); EXPECT_EQ(max, 9); return std::int64_t{ 4 }; })), "4");
    const std::vector<hermit::value_type> choice_arguments{ std::string{"first"}, std::string{"second"} };
    const hermit::builtin::macro_argument_type choice_args{ choice_arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::choice(choice_args, [](std::int64_t min, std::int64_t max) { EXPECT_EQ(min, 0); EXPECT_EQ(max, 1); return std::int64_t{ 1 }; })), "second");
}

TEST(builtin, injects_process_runner)
{
    hermit::config cfg;
    cfg.allow_process_exec = true;
    hermit::context ctx;
    const std::vector<hermit::value_type> arguments{ std::string{"tool"}, std::string{"--flag"}, std::string{"value"} };
    const hermit::builtin::macro_argument_type macro_args{ arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::exec(macro_args, [](std::string_view executable, const std::vector<std::string>& process_arguments) { EXPECT_EQ(executable, "tool"); EXPECT_EQ(process_arguments.size(), 2U); EXPECT_EQ(process_arguments[0], "--flag"); EXPECT_EQ(process_arguments[1], "value"); return std::pair<int, std::string>{ 7, "output" }; })), "output");
    ASSERT_NE(ctx.get("exit_code"), nullptr);
    EXPECT_EQ(hermit::value_to_string(*ctx.get("exit_code")), "7");
}

TEST(builtin, blocks_exec_macro_by_default_before_process_launch)
{
    hermit::config cfg;
    hermit::context ctx;
    const std::vector<hermit::value_type> arguments{ std::string{ "tool" } };
    const hermit::builtin::macro_argument_type macro_args{ arguments, cfg, ctx };
    bool runner_invoked{};

    EXPECT_THROW(hermit::builtin::exec(macro_args,
        [&runner_invoked](std::string_view, const std::vector<std::string>&)
        {
            runner_invoked = true;
            return std::pair<int, std::string>{ 0, "" };
        }), hermit::macro_exception);
    EXPECT_FALSE(runner_invoked);
}

TEST(builtin, registry_exposes_supported_macros_and_rejects_unknown_names)
{
    for (const std::string_view name : { "int", "double", "char", "string", "file", "head", "tail", "head_tail", "json_literal", "getenv", "setenv", "generated", "random", "choice", "exec", "code_block", "summary", "root", "parent", "stem", "extension" })
        EXPECT_TRUE(hermit::builtin::get_macro(name).has_value()) << name;
    EXPECT_FALSE(hermit::builtin::get_macro("unknown_macro").has_value());
}

TEST(builtin, invokes_registry_macro_through_type_erased_callable)
{
    hermit::config cfg;
    hermit::context ctx;
    const std::vector<hermit::value_type> arguments{ 123 };
    const hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };
    const std::optional<hermit::builtin::macro_type> macro{ hermit::builtin::get_macro("string") };
    ASSERT_TRUE(macro.has_value());
    EXPECT_EQ(hermit::value_to_string((*macro)(args)), "123");
}

TEST(builtin, converts_macro_arguments_and_rejects_invalid_argument_counts)
{
    hermit::config cfg;
    hermit::context ctx;
    const std::vector<hermit::value_type> int_arguments{ std::string{ "42" } };
    const std::vector<hermit::value_type> double_arguments{ 7 };
    const hermit::builtin::macro_argument_type int_args{ int_arguments, cfg, ctx };
    const hermit::builtin::macro_argument_type double_args{ double_arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::int_(int_args)), "42");
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::double_(double_args)), "7");

    const std::vector<hermit::value_type> char_arguments{ 'A' };
    const hermit::builtin::macro_argument_type char_args{ char_arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::char_(char_args)), "A");

    const std::vector<hermit::value_type> string_arguments{ 7 };
    const hermit::builtin::macro_argument_type string_args{ string_arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::string_(string_args)), "7");

}

TEST(builtin, rejects_missing_required_arguments_before_side_effects)
{
    hermit::config cfg;
    hermit::context ctx;
    const std::vector<hermit::value_type> no_arguments;
    const hermit::builtin::macro_argument_type args{ no_arguments, cfg, ctx };

    EXPECT_THROW(hermit::builtin::file(args), hermit::macro_exception);
    EXPECT_THROW(hermit::builtin::getenv(args), hermit::macro_exception);
    EXPECT_THROW(hermit::builtin::setenv(args), hermit::macro_exception);
    EXPECT_THROW(hermit::builtin::generated(args), hermit::macro_exception);
    EXPECT_THROW(hermit::builtin::exec(args), hermit::macro_exception);
    EXPECT_THROW(hermit::builtin::choice(args), hermit::macro_exception);
    EXPECT_THROW(hermit::builtin::json_literal(args), hermit::macro_exception);
    EXPECT_THROW(hermit::builtin::code_block(args), hermit::macro_exception);

    const std::vector<hermit::value_type> too_many_arguments{ std::string{ "a" }, std::string{ "b" }, std::string{ "c" } };
    const hermit::builtin::macro_argument_type too_many_args{ too_many_arguments, cfg, ctx };
    EXPECT_THROW(hermit::builtin::file(too_many_args), hermit::macro_exception);
    EXPECT_THROW(hermit::builtin::random(too_many_args), hermit::macro_exception);
    EXPECT_THROW(hermit::builtin::code_block(too_many_args), hermit::macro_exception);
}

TEST(builtin, rejects_invalid_value_types_before_invoking_injected_boundaries)
{
    hermit::config cfg;
    hermit::context ctx;
    const std::vector<hermit::value_type> invalid_file_arguments{ 7 };
    const hermit::builtin::macro_argument_type invalid_file_args{ invalid_file_arguments, cfg, ctx };
    bool reader_invoked{};
    EXPECT_THROW(hermit::builtin::file(invalid_file_args,
        [&reader_invoked](std::string_view, const hermit::config&)
        {
            reader_invoked = true;
            return std::string{};
        }), std::exception);
    EXPECT_FALSE(reader_invoked);

    const std::vector<hermit::value_type> invalid_getenv_arguments{ 7 };
    const hermit::builtin::macro_argument_type invalid_getenv_args{ invalid_getenv_arguments, cfg, ctx };
    bool getter_invoked{};
    EXPECT_THROW(hermit::builtin::getenv(invalid_getenv_args,
        [&getter_invoked](std::string_view)
        {
            getter_invoked = true;
            return std::optional<std::string>{};
        }), std::exception);
    EXPECT_FALSE(getter_invoked);

    const std::vector<hermit::value_type> invalid_setenv_arguments{ std::string{ "NAME" }, 7 };
    const hermit::builtin::macro_argument_type invalid_setenv_args{ invalid_setenv_arguments, cfg, ctx };
    bool setter_invoked{};
    EXPECT_THROW(hermit::builtin::setenv(invalid_setenv_args,
        [&setter_invoked](std::string_view, std::string_view)
        {
            setter_invoked = true;
            return true;
        }), std::exception);
    EXPECT_FALSE(setter_invoked);

    const std::vector<hermit::value_type> invalid_exec_arguments{ 7 };
    const hermit::builtin::macro_argument_type invalid_exec_args{ invalid_exec_arguments, cfg, ctx };
    bool runner_invoked{};
    EXPECT_THROW(hermit::builtin::exec(invalid_exec_args,
        [&runner_invoked](std::string_view, const std::vector<std::string>&)
        {
            runner_invoked = true;
            return std::pair<int, std::string>{};
        }), std::exception);
    EXPECT_FALSE(runner_invoked);

    const std::vector<hermit::value_type> invalid_path_arguments{ 7 };
    const hermit::builtin::macro_argument_type invalid_path_args{ invalid_path_arguments, cfg, ctx };
    EXPECT_THROW(hermit::builtin::parent(invalid_path_args), std::exception);

    const std::vector<hermit::value_type> invalid_json_arguments{ 7 };
    const hermit::builtin::macro_argument_type invalid_json_args{ invalid_json_arguments, cfg, ctx };
    EXPECT_THROW(hermit::builtin::json_literal(invalid_json_args), std::exception);

    const std::vector<hermit::value_type> invalid_code_block_arguments{ 7, std::string{ "cpp" } };
    const hermit::builtin::macro_argument_type invalid_code_block_args{ invalid_code_block_arguments, cfg, ctx };
    EXPECT_THROW(hermit::builtin::code_block(invalid_code_block_args), std::exception);

    const hermit::public_api::http_executor executor{ [&runner_invoked](const hermit::public_api::http_request_data&)
    {
        runner_invoked = true;
        return hermit::public_api::http_response_data{};
    } };
    const std::vector<hermit::value_type> invalid_generated_arguments{ 7 };
    const hermit::builtin::macro_argument_type invalid_generated_args{ invalid_generated_arguments, cfg, ctx };
    EXPECT_THROW(hermit::builtin::generated(invalid_generated_args, executor), std::exception);
    EXPECT_FALSE(runner_invoked);

    const std::vector<hermit::value_type> invalid_summary_arguments{ std::string{ "prompt" }, 7, 5 };
    const hermit::builtin::macro_argument_type invalid_summary_args{ invalid_summary_arguments, cfg, ctx };
    EXPECT_THROW(hermit::builtin::summary(invalid_summary_args, executor), std::exception);
    EXPECT_FALSE(runner_invoked);
}

TEST(builtin, escapes_json_and_extracts_named_code_blocks)
{
    hermit::config cfg;
    hermit::context ctx;
    const std::vector<hermit::value_type> json_arguments{ std::string{ "a\"b\n" } };
    const hermit::builtin::macro_argument_type json_args{ json_arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::json_literal(json_args)), "a\\\"b\\n");

    const std::string markdown{ "before\n```cpp\nint main() {}\n```\nafter" };
    const std::vector<hermit::value_type> block_arguments{ markdown, std::string{ "cpp" } };
    const hermit::builtin::macro_argument_type block_args{ block_arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::code_block(block_args)), "int main() {}\n");
    const std::vector<hermit::value_type> missing_arguments{ markdown, std::string{ "python" } };
    const hermit::builtin::macro_argument_type missing_args{ missing_arguments, cfg, ctx };
    EXPECT_TRUE(hermit::value_to_string(hermit::builtin::code_block(missing_args)).empty());
}

TEST(builtin, escapes_json_control_characters_and_backslashes)
{
    hermit::config cfg;
    hermit::context ctx;
    const std::vector<hermit::value_type> arguments{ std::string{ "\\\"\t\r" } };
    const hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::json_literal(args)), "\\\\\\\"\\t\\r");
}

TEST(builtin, selects_the_requested_block_from_multiple_code_blocks)
{
    hermit::config cfg;
    hermit::context ctx;
    const std::string markdown{ "```cpp\nint cpp_value;\n```\n```python\npython_value = 1\n```" };
    const std::vector<hermit::value_type> arguments{ markdown, std::string{ "python" } };
    const hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::code_block(args)), "python_value = 1\n");
}

TEST(builtin, applies_path_macros_to_relative_paths)
{
    hermit::config cfg;
    hermit::context ctx;
    const std::vector<hermit::value_type> arguments{ std::string{ "dir/file.txt" } };
    const hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::parent(args)), "dir");
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::stem(args)), "file");
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::extension(args)), ".txt");
    EXPECT_TRUE(hermit::value_to_string(hermit::builtin::root(args)).empty());
}

TEST(builtin, handles_extensionless_relative_paths)
{
    hermit::config cfg;
    hermit::context ctx;
    const std::vector<hermit::value_type> arguments{ std::string{ "README" } };
    const hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };
    EXPECT_TRUE(hermit::value_to_string(hermit::builtin::parent(args)).empty());
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::stem(args)), "README");
    EXPECT_TRUE(hermit::value_to_string(hermit::builtin::extension(args)).empty());
}

TEST(builtin, returns_platform_root_for_absolute_paths)
{
    hermit::config cfg;
    hermit::context ctx;
    const std::filesystem::path absolute_path{ std::filesystem::temp_directory_path() / "macro.txt" };
    const std::vector<hermit::value_type> arguments{ absolute_path.string() };
    const hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::root(args)), absolute_path.root_path().string());
}

TEST(builtin, returns_empty_components_for_empty_path)
{
    hermit::config cfg;
    hermit::context ctx;
    const std::vector<hermit::value_type> arguments{ std::string{} };
    const hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };
    EXPECT_TRUE(hermit::value_to_string(hermit::builtin::root(args)).empty());
    EXPECT_TRUE(hermit::value_to_string(hermit::builtin::parent(args)).empty());
    EXPECT_TRUE(hermit::value_to_string(hermit::builtin::stem(args)).empty());
    EXPECT_TRUE(hermit::value_to_string(hermit::builtin::extension(args)).empty());
}

TEST(builtin, preserves_compound_stem_before_final_extension)
{
    hermit::config cfg;
    hermit::context ctx;
    const std::vector<hermit::value_type> arguments{ std::string{ "archive.tar.gz" } };
    const hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::stem(args)), "archive.tar");
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::extension(args)), ".gz");
}

TEST(builtin, uses_injected_random_bounds_for_default_arguments)
{
    hermit::config cfg;
    hermit::context ctx;
    const std::vector<hermit::value_type> arguments;
    const hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::random(args, [](std::int64_t min, std::int64_t max) {
        EXPECT_EQ(min, 0);
        EXPECT_EQ(max, static_cast<std::int64_t>((std::numeric_limits<std::uint32_t>::max)()));
        return std::int64_t{ 3 };
    })), "3");
}

TEST(builtin, choice_returns_the_only_candidate_without_random_selection)
{
    hermit::config cfg;
    hermit::context ctx;
    const std::vector<hermit::value_type> arguments{ std::string{ "only" } };
    const hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::choice(args, [](std::int64_t min, std::int64_t max) {
        EXPECT_EQ(min, 0);
        EXPECT_EQ(max, 0);
        return std::int64_t{ 0 };
    })), "only");
}

TEST(builtin, supports_partial_random_bounds_and_non_string_choice_values)
{
    hermit::config cfg;
    hermit::context ctx;
    const std::vector<hermit::value_type> random_arguments{ 5 };
    const hermit::builtin::macro_argument_type random_args{ random_arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::random(random_args, [](std::int64_t min, std::int64_t max) {
        EXPECT_EQ(min, 5);
        EXPECT_EQ(max, static_cast<std::int64_t>((std::numeric_limits<std::uint32_t>::max)()));
        return std::int64_t{ 6 };
    })), "6");

    const std::vector<hermit::value_type> choice_arguments{ 11, true };
    const hermit::builtin::macro_argument_type choice_args{ choice_arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::choice(choice_args, [](std::int64_t, std::int64_t) { return std::int64_t{ 1 }; })), "1");
}

TEST(builtin, rejects_injected_random_results_outside_requested_bounds)
{
    hermit::config cfg;
    hermit::context ctx;
    const std::vector<hermit::value_type> random_arguments{ 2, 4 };
    const hermit::builtin::macro_argument_type random_args{ random_arguments, cfg, ctx };
    EXPECT_THROW(hermit::builtin::random(random_args, [](std::int64_t, std::int64_t) { return std::int64_t{ 5 }; }), std::exception);

    const std::vector<hermit::value_type> choice_arguments{ std::string{ "only" } };
    const hermit::builtin::macro_argument_type choice_args{ choice_arguments, cfg, ctx };
    EXPECT_THROW(hermit::builtin::choice(choice_args, [](std::int64_t, std::int64_t) { return std::int64_t{ 1 }; }), std::exception);

    const std::vector<hermit::value_type> reversed_arguments{ 5, 2 };
    const hermit::builtin::macro_argument_type reversed_args{ reversed_arguments, cfg, ctx };
    bool source_invoked{};
    EXPECT_THROW(hermit::builtin::random(reversed_args, [&source_invoked](std::int64_t, std::int64_t) {
        source_invoked = true;
        return std::int64_t{ 3 };
    }), std::exception);
    EXPECT_FALSE(source_invoked);
}

TEST(builtin, propagates_injected_process_runner_failure_without_changing_exit_code)
{
    hermit::config cfg;
    cfg.allow_process_exec = true;
    hermit::context ctx;
    ctx.set("exit_code", 3);
    const std::vector<hermit::value_type> arguments{ std::string{ "tool" } };
    const hermit::builtin::macro_argument_type macro_args{ arguments, cfg, ctx };

    EXPECT_THROW(hermit::builtin::exec(macro_args, [](std::string_view, const std::vector<std::string>&) -> std::pair<int, std::string> {
        throw std::runtime_error{ "runner failed" };
    }), std::runtime_error);
    ASSERT_NE(ctx.get("exit_code"), nullptr);
    EXPECT_EQ(hermit::value_to_string(*ctx.get("exit_code")), "3");
}

TEST(builtin, truncates_head_tail_macros_using_cached_token_counts)
{
    hermit::config cfg;
    cfg.lru_cache = hermit::config::cache_type{ [](std::string_view value) { return static_cast<int>(value.size()); } };
    hermit::context ctx;
    const std::vector<hermit::value_type> head_arguments{ std::string{ "aa\nbbb\ncccc" }, 2 };
    const hermit::builtin::macro_argument_type head_args{ head_arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::head(head_args)), "aa");

    const std::vector<hermit::value_type> tail_arguments{ std::string{ "aa\nbbb\ncccc" }, 4 };
    const hermit::builtin::macro_argument_type tail_args{ tail_arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::tail(tail_args)), "cccc");

    const std::vector<hermit::value_type> head_tail_arguments{ std::string{ "aa\nbbb\ncccc" }, 1, 1 };
    const hermit::builtin::macro_argument_type head_tail_args{ head_tail_arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::head_tail(head_tail_args)), "..." );

    const std::vector<hermit::value_type> unchanged_arguments{ std::string{ "aa\nbbb\ncccc" }, 20, 20 };
    const hermit::builtin::macro_argument_type unchanged_args{ unchanged_arguments, cfg, ctx };
    EXPECT_EQ(hermit::value_to_string(hermit::builtin::head_tail(unchanged_args)), "aa\nbbb\ncccc");
}

TEST(builtin, preserves_empty_input_in_truncation_macros)
{
    hermit::config cfg;
    cfg.lru_cache = hermit::config::cache_type{ [](std::string_view value) { return static_cast<int>(value.size()); } };
    hermit::context ctx;
    const std::vector<hermit::value_type> head_arguments{ std::string{}, 1 };
    const hermit::builtin::macro_argument_type head_args{ head_arguments, cfg, ctx };
    EXPECT_TRUE(hermit::value_to_string(hermit::builtin::head(head_args)).empty());

    const std::vector<hermit::value_type> tail_arguments{ std::string{}, 1 };
    const hermit::builtin::macro_argument_type tail_args{ tail_arguments, cfg, ctx };
    EXPECT_TRUE(hermit::value_to_string(hermit::builtin::tail(tail_args)).empty());

    const std::vector<hermit::value_type> head_tail_arguments{ std::string{}, 1, 1 };
    const hermit::builtin::macro_argument_type head_tail_args{ head_tail_arguments, cfg, ctx };
    EXPECT_TRUE(hermit::value_to_string(hermit::builtin::head_tail(head_tail_args)).empty());
}
