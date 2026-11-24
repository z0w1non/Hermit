#include <vector>
#include <initializer_list>

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <boost/program_options.hpp>
#include <boost/nowide/iostream.hpp>

#include "Hermit_app_types.hpp"
#include "Hermit_builtin.hpp"
#include "Hermit_command_line.hpp"
#include "Hermit_config.hpp"
#include "Hermit_context.hpp"
#include "Hermit_core.hpp"
#include "Hermit_filesystem.hpp"
#include "Hermit_generation.hpp"
#include "Hermit_llm.hpp"
#include "Hermit_llm_types.hpp"
#include "Hermit_lru.hpp"
#include "Hermit_lru_impl.hpp"
#include "Hermit_parser.hpp"
#include "Hermit_png.hpp"
#include "Hermit_runtime.hpp"
#include "Hermit_sd_config.hpp"
#include "Hermit_string_utils.hpp"
#include "Hermit_tcp.hpp"
#include "Hermit_templates.hpp"
#include "Hermit_tg_kc_config.hpp"
#include "Hermit_value.hpp"
#include "test_support.hpp"

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

TEST(parse_command_line, set_verbose)
{
    commandline_args args{ "Hermit.exe", "--verbose" };
    Hermit::config cfg;
    boost::program_options::options_description od{ Hermit::command_line::make_options_description(cfg) };
    boost::program_options::variables_map vm;
    Hermit::command_line::parse_command_line(od, args.argc(), args.argv(), vm);
    boost::program_options::notify(vm);
    EXPECT_EQ(cfg.verbose, true);
}

TEST(parse_command_line, applies_common_defaults_and_values)
{
    commandline_args args{ "Hermit.exe", "--mode", "kc", "--number-iterations", "3", "--timeout-connect", "5", "--base-path", "data" };
    Hermit::config cfg;
    const Hermit::command_line::parse_result result{ Hermit::command_line::parse(args.argc(), args.argv(), cfg) };

    EXPECT_EQ(result, Hermit::command_line::parse_result::success);
    EXPECT_EQ(cfg.command_mode, Hermit::command_mode::kc);
    EXPECT_EQ(cfg.number_iterations, 3);
    EXPECT_EQ(cfg.timeout_connect, 5);
    EXPECT_EQ(cfg.base_path, "data");
    EXPECT_EQ(cfg.log_level, boost::log::trivial::info);
}

TEST(parse_command_line, applies_backend_options)
{
    commandline_args args{ "Hermit.exe", "--mode", "sd", "--sd-mode", "img2img", "--sd-steps", "40", "--sb-language", "JP", "--cu-port", "9000", "--kc-bypass-eos" };
    Hermit::config cfg;
    const Hermit::command_line::parse_result result{ Hermit::command_line::parse(args.argc(), args.argv(), cfg) };

    EXPECT_EQ(result, Hermit::command_line::parse_result::success);
    EXPECT_EQ(cfg.command_mode, Hermit::command_mode::sd);
    EXPECT_EQ(cfg.sd.mode, Hermit::sd_mode::img2img);
    EXPECT_EQ(cfg.sd.common.steps, 40);
    EXPECT_EQ(cfg.sb.language, "JP");
    EXPECT_EQ(cfg.cu.port, "9000");
    EXPECT_TRUE(cfg.kc.common.bypass_eos);
    EXPECT_TRUE(cfg.kc.common.trim_stop);
}

struct help_option_test : testing::TestWithParam<const char*> {};

TEST_P(help_option_test, help)
{
    const char* option{ GetParam() };
    commandline_args args{ "Hermit.exe", option };
    const scoped_ostream_redirect cout{ boost::nowide::cout };

    Hermit::command_line::parse_result result{};
    Hermit::config cfg;
    EXPECT_NO_THROW({
        result = Hermit::command_line::parse(args.argc(), args.argv(), cfg);
        });
    EXPECT_EQ(result, Hermit::command_line::parse_result::help);

    const std::string cout_output{ cout.str() };
    EXPECT_THAT(cout_output, testing::HasSubstr("Allowed options"));
}

INSTANTIATE_TEST_SUITE_P(
    parse,
    help_option_test,
    testing::Values("--help", "-h")
);

TEST(parse, unrecognised_options)
{
    commandline_args args{ "Hermit.exe", "--unrecognised-option" };
    const scoped_ostream_redirect cerr{ boost::nowide::cerr };

    Hermit::command_line::parse_result result{};
    Hermit::config cfg;
    result = Hermit::command_line::parse(args.argc(), args.argv(), cfg);
    EXPECT_EQ(result, Hermit::command_line::parse_result::program_options_error);

    const std::string cerr_output{ cerr.str() };
    EXPECT_THAT(cerr_output, testing::HasSubstr("unrecognised option"));
    EXPECT_THAT(cerr_output, testing::HasSubstr("--unrecognised-option"));
}

TEST(parse, rejects_invalid_backend_modes)
{
    commandline_args args{ "Hermit.exe", "--llm-mode", "invalid", "--sd-mode", "invalid" };
    const scoped_ostream_redirect cerr{ boost::nowide::cerr };
    Hermit::config cfg;

    EXPECT_THROW(Hermit::command_line::parse(args.argc(), args.argv(), cfg), std::exception);
}

TEST(test_exception_safe_main, unrecognised_option)
{
    commandline_args args{ "Hermit.exe", "--unrecognised-option" };
    const scoped_ostream_redirect cerr{ boost::nowide::cerr };

    int result{};
    EXPECT_NO_THROW({
        result = Hermit::exception_safe_main(args.argc(), args.argv());
        });
    EXPECT_NE(result, 0);
}

TEST(exception_safe_main, help)
{
    commandline_args args{ "Hermit.exe", "--help" };
    const scoped_ostream_redirect cout{ boost::nowide::cout };

    int result{};
    EXPECT_NO_THROW({
        result = Hermit::exception_safe_main(args.argc(), args.argv());
        });
    EXPECT_EQ(result, 0);
}

TEST(exception_safe_main, help_short)
{
    commandline_args args{ "Hermit.exe", "-h" };
    const scoped_ostream_redirect cout{ boost::nowide::cout };

    int result{};
    EXPECT_NO_THROW({
        result = Hermit::exception_safe_main(args.argc(), args.argv());
        });
    EXPECT_EQ(result, 0);
}

TEST(lru_cache, get_tokens)
{
    using cache_type = Hermit::lru_cache<1>;
    constexpr int factor{ 2 };
    const auto callback{ [](std::string_view str) -> int { return static_cast<int>(str.size()) * factor; } };

    cache_type cache{ callback };
    const std::string str{ "test" };
    EXPECT_EQ(cache.get_tokens(str), callback(str));
    EXPECT_EQ(cache.get_tokens(str), callback(str));
}

TEST(lru_cache, lru)
{
    using cache_type = Hermit::lru_cache<3>;
    int count{};
    const auto callback{ [&count](std::string_view str) -> int { return count++; } };

    cache_type cache{ callback };
    EXPECT_EQ(cache.get_tokens("a"), 0);
    EXPECT_EQ(cache.get_tokens("a"), 0);
    EXPECT_EQ(cache.get_tokens("b"), 1);
    EXPECT_EQ(cache.get_tokens("b"), 1);
    EXPECT_EQ(cache.get_tokens("a"), 0);
    EXPECT_EQ(cache.get_tokens("b"), 1);
    EXPECT_EQ(cache.get_tokens("c"), 2);
    EXPECT_EQ(cache.get_tokens("c"), 2);
    EXPECT_EQ(cache.get_tokens("d"), 3);
    EXPECT_EQ(cache.get_tokens("a"), 4);
}

TEST(context, inherited_values_and_override)
{
    Hermit::context base;
    base.set("name", std::string{"base"});
    Hermit::context child{ base.make_pushed() };

    ASSERT_NE(child.get("name"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*child.get("name")), "base");
    child.set("name", std::string{"child"});
    EXPECT_EQ(Hermit::value_to_string(*child.get("name")), "child");
    EXPECT_EQ(child.get("missing"), nullptr);
}

TEST(context, erases_only_values_owned_by_current_scope)
{
    Hermit::context base;
    base.set("name", std::string{ "base" });
    Hermit::context child{ base.make_pushed() };
    child.set("temporary", std::string{ "value" });

    child.erase("temporary");
    child.erase("name");

    EXPECT_EQ(child.get("temporary"), nullptr);
    ASSERT_NE(child.get("name"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*child.get("name")), "base");
}

TEST(runtime, clears_stale_previous_and_next_phase_variables)
{
    Hermit::context ctx;
    ctx.set("prev_phase", std::string{ "stale previous" });
    ctx.set("next_phase", std::string{ "stale next" });
    const std::vector<std::string> phases{ "first", "last" };

    Hermit::set_phase_variables(phases, 0, ctx);
    EXPECT_EQ(ctx.get("prev_phase"), nullptr);
    ASSERT_NE(ctx.get("next_phase"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("next_phase")), "last");

    Hermit::set_phase_variables(phases, 1, ctx);
    ASSERT_NE(ctx.get("prev_phase"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("prev_phase")), "first");
    EXPECT_EQ(ctx.get("next_phase"), nullptr);

    const std::vector<std::string> single_phase{ "only" };
    Hermit::set_phase_variables(single_phase, 0, ctx);
    EXPECT_EQ(ctx.get("prev_phase"), nullptr);
    EXPECT_EQ(ctx.get("next_phase"), nullptr);
}

TEST(command_line, split_command_line_args)
{
    const auto args{ Hermit::command_line::split_command_line_args("server.exe --model \"model name.gguf\" --flag") };
    ASSERT_EQ(args.size(), 4U);
    EXPECT_EQ(args[0], "server.exe");
    EXPECT_EQ(args[1], "--model");
    EXPECT_EQ(args[2], "model name.gguf");
    EXPECT_EQ(args[3], "--flag");
}

TEST(command_mode, string_conversion)
{
    EXPECT_EQ(Hermit::string_to_command_mode("tg"), Hermit::command_mode::tg);
    EXPECT_EQ(Hermit::string_to_command_mode("kc"), Hermit::command_mode::kc);
    EXPECT_EQ(Hermit::string_to_command_mode("cu"), Hermit::command_mode::cu);
    EXPECT_EQ(Hermit::string_to_command_mode("create-process"), Hermit::command_mode::create_process);
    EXPECT_EQ(Hermit::string_to_command_mode("terminate-process"), Hermit::command_mode::terminate_process);
    EXPECT_EQ(Hermit::string_to_command_mode("extract-png-parameters"), Hermit::command_mode::extract_png_parameters);
    EXPECT_THROW(Hermit::string_to_command_mode("unknown"), std::exception);
}

TEST(string_utils, filename_and_language_helpers)
{
    EXPECT_EQ(Hermit::sanitize_as_filename("a:b/c*?"), "abc");
    EXPECT_EQ(Hermit::sanitize_as_filename("..\\..\\secret"), "....secret");
    EXPECT_TRUE(Hermit::sanitize_as_filename("..").empty());
    EXPECT_EQ(Hermit::language_identifier_to_extension("cpp"), ".cpp");
    EXPECT_EQ(Hermit::language_identifier_to_extension("unknown-language"), "");
}

TEST(value, conversion)
{
    EXPECT_EQ(Hermit::value_to_string(Hermit::value_type{ 42 }), "42");
    EXPECT_EQ(Hermit::value_to_string(Hermit::value_type{ true }), "1");
    EXPECT_EQ(Hermit::value_to_string(Hermit::value_type{ std::string{"hello"} }), "hello");
    EXPECT_EQ(Hermit::get_optional<int>(Hermit::value_type{ 7 }), std::optional<int>{ 7 });
    EXPECT_FALSE(Hermit::get_optional<int>(Hermit::value_type{ std::string{"x"} }).has_value());
}

TEST(string_utils, encoding_and_escaping)
{
    const std::string encoded{ Hermit::string_utils::base64_encode("hello") };
    EXPECT_EQ(encoded, "aGVsbG8=");
    EXPECT_EQ(Hermit::string_utils::base64_decode(encoded), "hello");
    EXPECT_TRUE(Hermit::string_utils::is_base64(encoded, 4));
    EXPECT_EQ(Hermit::string_utils::json_escape_string("a\"b\n"), "a\\\"b\\n");
    EXPECT_EQ(Hermit::string_utils::unescape_string("a\\n"), "a\n");
}

TEST(png, validates_signature_and_bounds_chunk_lengths)
{
    std::string image{ "\x89PNG\r\n\x1A\n", 8 };
    image.append("\0\0\0\x0D", 4);
    image += "IHDR";
    image.append(13, '\0');
    image.append(4, '\0');

    const std::string metadata{ "parameters\0prompt", 17 };
    image.append("\0\0\0\x11", 4);
    image += "tEXt";
    image += metadata;
    image.append(4, '\0');

    const std::string& with_metadata{ image };
    EXPECT_EQ(Hermit::tEXt::extract_parameters(with_metadata, "parameters"), "prompt");

    std::string invalid_signature{ image };
    invalid_signature[7] = '\0';
    EXPECT_THROW(Hermit::tEXt::extract_parameters(invalid_signature, "parameters"), std::exception);

    std::string oversized_chunk{ "\x89PNG\r\n\x1A\n", 8 };
    oversized_chunk.append("\xFF\xFF\xFF\xFFtEXt", 8);
    oversized_chunk.append(4, '\0');
    EXPECT_THROW(Hermit::tEXt::extract_parameters(oversized_chunk, "parameters"), std::exception);
}

TEST(core, converts_all_public_backend_modes)
{
    EXPECT_EQ(Hermit::string_to_llm_mode("completions"), Hermit::llm_mode::completions);
    EXPECT_EQ(Hermit::string_to_llm_mode("chat_completions"), Hermit::llm_mode::chat_completions);
    EXPECT_EQ(Hermit::string_to_sd_mode("txt2img"), Hermit::sd_mode::txt2img);
    EXPECT_EQ(Hermit::string_to_sd_mode("img2img"), Hermit::sd_mode::img2img);
    EXPECT_THROW(Hermit::string_to_llm_mode("invalid"), std::exception);
    EXPECT_THROW(Hermit::string_to_sd_mode("invalid"), std::exception);
}

TEST(core, resolves_backend_request_targets)
{
    Hermit::config cfg;
    cfg.llm.completions_target = "/v1/completions";
    cfg.llm.chat_completions_target = "/v1/chat/completions";
    cfg.sd.txt2img.target = "/sdapi/v1/txt2img";
    cfg.sd.img2img.target = "/sdapi/v1/img2img";

    EXPECT_EQ(Hermit::llm_mode_to_target(Hermit::llm_mode::completions, cfg), "/v1/completions");
    EXPECT_EQ(Hermit::llm_mode_to_target(Hermit::llm_mode::chat_completions, cfg), "/v1/chat/completions");
    EXPECT_EQ(Hermit::sd_mode_to_target(Hermit::sd_mode::txt2img, cfg), "/sdapi/v1/txt2img");
    EXPECT_EQ(Hermit::sd_mode_to_target(Hermit::sd_mode::img2img, cfg), "/sdapi/v1/img2img");
}


