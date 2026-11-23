#include <string>
#include <string_view>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "Hermit_public.hpp"
#include "Hermit_errors.hpp"

TEST(public_api, exposes_lightweight_macro_entrypoint)
{
    using expand_macro_type = std::string(*)(std::string_view, const Hermit::config&, const Hermit::context&);
    const expand_macro_type expand_macro{ &Hermit::expand_macro };
    EXPECT_NE(expand_macro, nullptr);
}

#include "Hermit_builtin.hpp"
#include "Hermit_config.hpp"
#include "Hermit_context.hpp"
#include "Hermit_filesystem.hpp"

TEST(builtin, injects_environment_accessors)
{
    Hermit::config cfg;
    Hermit::context ctx;
    const std::vector<Hermit::value_type> get_arguments{ std::string{"NAME"} };
    const Hermit::builtin::macro_argument_type get_args{ get_arguments, cfg, ctx };
    EXPECT_EQ(Hermit::value_to_string(Hermit::builtin::getenv(get_args, [](std::string_view key) -> std::optional<std::string> { EXPECT_EQ(key, "NAME"); return std::string{"injected"}; })), "injected");
    const std::vector<Hermit::value_type> set_arguments{ std::string{"NAME"}, std::string{"value"} };
    const Hermit::builtin::macro_argument_type set_args{ set_arguments, cfg, ctx };
    EXPECT_TRUE(Hermit::value_to_string(Hermit::builtin::setenv(set_args, [](std::string_view key, std::string_view value) { EXPECT_EQ(key, "NAME"); EXPECT_EQ(value, "value"); return true; })).empty());
}

TEST(builtin, returns_empty_for_missing_environment_and_ignores_setter_result)
{
    Hermit::config cfg;
    Hermit::context ctx;
    const std::vector<Hermit::value_type> get_arguments{ std::string{ "MISSING" } };
    const Hermit::builtin::macro_argument_type get_args{ get_arguments, cfg, ctx };
    EXPECT_TRUE(Hermit::value_to_string(Hermit::builtin::getenv(get_args, [](std::string_view) { return std::optional<std::string>{}; })).empty());

    const std::vector<Hermit::value_type> set_arguments{ std::string{ "NAME" }, std::string{ "value" } };
    const Hermit::builtin::macro_argument_type set_args{ set_arguments, cfg, ctx };
    EXPECT_TRUE(Hermit::value_to_string(Hermit::builtin::setenv(set_args, [](std::string_view, std::string_view) { return false; })).empty());
}

TEST(builtin, reads_text_file_relative_to_configured_base_path)
{
    Hermit::config cfg;
    const std::filesystem::path base_path{ std::filesystem::temp_directory_path() / "hermit_macro_file_test" };
    std::filesystem::remove_all(base_path);
    std::filesystem::create_directories(base_path);
    cfg.base_path = base_path.string();
    Hermit::filesystem::write_file(cfg, "macro content", "input.txt");

    Hermit::context ctx;
    const std::vector<Hermit::value_type> arguments{ std::string{ "input" } };
    const Hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };
    EXPECT_EQ(Hermit::value_to_string(Hermit::builtin::file(args)), "macro content");

    std::filesystem::remove_all(base_path);
}

TEST(builtin, injects_random_source)
{
    Hermit::config cfg;
    Hermit::context ctx;
    const std::vector<Hermit::value_type> random_arguments{ 1, 9 };
    const Hermit::builtin::macro_argument_type random_args{ random_arguments, cfg, ctx };
    EXPECT_EQ(Hermit::value_to_string(Hermit::builtin::random(random_args, [](std::int64_t min, std::int64_t max) { EXPECT_EQ(min, 1); EXPECT_EQ(max, 9); return std::int64_t{ 4 }; })), "4");
    const std::vector<Hermit::value_type> choice_arguments{ std::string{"first"}, std::string{"second"} };
    const Hermit::builtin::macro_argument_type choice_args{ choice_arguments, cfg, ctx };
    EXPECT_EQ(Hermit::value_to_string(Hermit::builtin::choice(choice_args, [](std::int64_t min, std::int64_t max) { EXPECT_EQ(min, 0); EXPECT_EQ(max, 1); return std::int64_t{ 1 }; })), "second");
}

TEST(builtin, injects_process_runner)
{
    Hermit::config cfg;
    Hermit::context ctx;
    const std::vector<Hermit::value_type> arguments{ std::string{"tool"}, std::string{"--flag"}, std::string{"value"} };
    const Hermit::builtin::macro_argument_type macro_args{ arguments, cfg, ctx };
    EXPECT_EQ(Hermit::value_to_string(Hermit::builtin::exec(macro_args, [](std::string_view executable, const std::vector<std::string>& process_arguments) { EXPECT_EQ(executable, "tool"); EXPECT_EQ(process_arguments.size(), 2U); EXPECT_EQ(process_arguments[0], "--flag"); EXPECT_EQ(process_arguments[1], "value"); return std::pair<int, std::string>{ 7, "output" }; })), "output");
    ASSERT_NE(ctx.get("exit_code"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("exit_code")), "7");
}

TEST(builtin, registry_exposes_supported_macros_and_rejects_unknown_names)
{
    for (const std::string_view name : { "int", "double", "char", "string", "file", "head", "tail", "head_tail", "json_literal", "getenv", "setenv", "generated", "random", "choice", "exec", "code_block", "summary", "root", "parent", "stem", "extension" })
        EXPECT_TRUE(Hermit::builtin::get_macro(name).has_value()) << name;
    EXPECT_FALSE(Hermit::builtin::get_macro("unknown_macro").has_value());
}

TEST(builtin, converts_macro_arguments_and_rejects_invalid_argument_counts)
{
    Hermit::config cfg;
    Hermit::context ctx;
    const std::vector<Hermit::value_type> int_arguments{ std::string{ "42" } };
    const std::vector<Hermit::value_type> double_arguments{ 7 };
    const Hermit::builtin::macro_argument_type int_args{ int_arguments, cfg, ctx };
    const Hermit::builtin::macro_argument_type double_args{ double_arguments, cfg, ctx };
    EXPECT_EQ(Hermit::value_to_string(Hermit::builtin::int_(int_args)), "42");
    EXPECT_EQ(Hermit::value_to_string(Hermit::builtin::double_(double_args)), "7");

    const std::vector<Hermit::value_type> char_arguments{ 'A' };
    const Hermit::builtin::macro_argument_type char_args{ char_arguments, cfg, ctx };
    EXPECT_EQ(Hermit::value_to_string(Hermit::builtin::char_(char_args)), "A");

    const std::vector<Hermit::value_type> string_arguments{ 7 };
    const Hermit::builtin::macro_argument_type string_args{ string_arguments, cfg, ctx };
    EXPECT_EQ(Hermit::value_to_string(Hermit::builtin::string_(string_args)), "7");

}

TEST(builtin, escapes_json_and_extracts_named_code_blocks)
{
    Hermit::config cfg;
    Hermit::context ctx;
    const std::vector<Hermit::value_type> json_arguments{ std::string{ "a\"b\n" } };
    const Hermit::builtin::macro_argument_type json_args{ json_arguments, cfg, ctx };
    EXPECT_EQ(Hermit::value_to_string(Hermit::builtin::json_literal(json_args)), "a\\\"b\\n");

    const std::string markdown{ "before\n```cpp\nint main() {}\n```\nafter" };
    const std::vector<Hermit::value_type> block_arguments{ markdown, std::string{ "cpp" } };
    const Hermit::builtin::macro_argument_type block_args{ block_arguments, cfg, ctx };
    EXPECT_EQ(Hermit::value_to_string(Hermit::builtin::code_block(block_args)), "int main() {}\n");
    const std::vector<Hermit::value_type> missing_arguments{ markdown, std::string{ "python" } };
    const Hermit::builtin::macro_argument_type missing_args{ missing_arguments, cfg, ctx };
    EXPECT_TRUE(Hermit::value_to_string(Hermit::builtin::code_block(missing_args)).empty());
}

TEST(builtin, applies_path_macros_to_relative_paths)
{
    Hermit::config cfg;
    Hermit::context ctx;
    const std::vector<Hermit::value_type> arguments{ std::string{ "dir/file.txt" } };
    const Hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };
    EXPECT_EQ(Hermit::value_to_string(Hermit::builtin::parent(args)), "dir");
    EXPECT_EQ(Hermit::value_to_string(Hermit::builtin::stem(args)), "file");
    EXPECT_EQ(Hermit::value_to_string(Hermit::builtin::extension(args)), ".txt");
    EXPECT_TRUE(Hermit::value_to_string(Hermit::builtin::root(args)).empty());
}

TEST(builtin, handles_extensionless_relative_paths)
{
    Hermit::config cfg;
    Hermit::context ctx;
    const std::vector<Hermit::value_type> arguments{ std::string{ "README" } };
    const Hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };
    EXPECT_TRUE(Hermit::value_to_string(Hermit::builtin::parent(args)).empty());
    EXPECT_EQ(Hermit::value_to_string(Hermit::builtin::stem(args)), "README");
    EXPECT_TRUE(Hermit::value_to_string(Hermit::builtin::extension(args)).empty());
}

TEST(builtin, uses_injected_random_bounds_for_default_arguments)
{
    Hermit::config cfg;
    Hermit::context ctx;
    const std::vector<Hermit::value_type> arguments;
    const Hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };
    EXPECT_EQ(Hermit::value_to_string(Hermit::builtin::random(args, [](std::int64_t min, std::int64_t max) {
        EXPECT_EQ(min, 0);
        EXPECT_EQ(max, static_cast<std::int64_t>((std::numeric_limits<std::uint32_t>::max)()));
        return std::int64_t{ 3 };
    })), "3");
}

TEST(builtin, choice_returns_the_only_candidate_without_random_selection)
{
    Hermit::config cfg;
    Hermit::context ctx;
    const std::vector<Hermit::value_type> arguments{ std::string{ "only" } };
    const Hermit::builtin::macro_argument_type args{ arguments, cfg, ctx };
    EXPECT_EQ(Hermit::value_to_string(Hermit::builtin::choice(args, [](std::int64_t min, std::int64_t max) {
        EXPECT_EQ(min, 0);
        EXPECT_EQ(max, 0);
        return std::int64_t{ 0 };
    })), "only");
}

TEST(builtin, truncates_head_tail_macros_using_cached_token_counts)
{
    Hermit::config cfg;
    cfg.lru_cache = Hermit::config::cache_type{ [](std::string_view value) { return static_cast<int>(value.size()); } };
    Hermit::context ctx;
    const std::vector<Hermit::value_type> head_arguments{ std::string{ "aa\nbbb\ncccc" }, 2 };
    const Hermit::builtin::macro_argument_type head_args{ head_arguments, cfg, ctx };
    EXPECT_EQ(Hermit::value_to_string(Hermit::builtin::head(head_args)), "aa");

    const std::vector<Hermit::value_type> tail_arguments{ std::string{ "aa\nbbb\ncccc" }, 4 };
    const Hermit::builtin::macro_argument_type tail_args{ tail_arguments, cfg, ctx };
    EXPECT_EQ(Hermit::value_to_string(Hermit::builtin::tail(tail_args)), "cccc");

    const std::vector<Hermit::value_type> head_tail_arguments{ std::string{ "aa\nbbb\ncccc" }, 1, 1 };
    const Hermit::builtin::macro_argument_type head_tail_args{ head_tail_arguments, cfg, ctx };
    EXPECT_EQ(Hermit::value_to_string(Hermit::builtin::head_tail(head_tail_args)), "..." );

    const std::vector<Hermit::value_type> unchanged_arguments{ std::string{ "aa\nbbb\ncccc" }, 20, 20 };
    const Hermit::builtin::macro_argument_type unchanged_args{ unchanged_arguments, cfg, ctx };
    EXPECT_EQ(Hermit::value_to_string(Hermit::builtin::head_tail(unchanged_args)), "aa\nbbb\ncccc");
}
