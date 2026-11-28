#include <filesystem>
#include <ios>
#include <string>

#include <gtest/gtest.h>

#include "llm.hpp"
#include "llm_types.hpp"
#include "config.hpp"
#include "tcp.hpp"

TEST(llm, parses_markdown_code_blocks_and_removes_reasoning)
{
    const hermit::llm::code_blocks blocks{ hermit::llm::extract_code_block_from_markdown("```cpp\nint main() {}\n```\n```text\nhello\n```") };
    ASSERT_EQ(blocks.size(), 2U);
    EXPECT_EQ(blocks.at("cpp"), "int main() {}\n");
    EXPECT_EQ(blocks.at("text"), "hello\n");
    const hermit::llm::code_blocks unix_unsafe_name{ hermit::llm::extract_code_block_from_markdown("```../../outside\npayload\n```") };
    ASSERT_EQ(unix_unsafe_name.size(), 1U);
    EXPECT_EQ(unix_unsafe_name.begin()->first, "....outside");
    const hermit::llm::code_blocks windows_unsafe_name{ hermit::llm::extract_code_block_from_markdown("```..\\..\\outside\npayload\n```") };
    ASSERT_EQ(windows_unsafe_name.size(), 1U);
    EXPECT_EQ(windows_unsafe_name.begin()->first, "....outside");
    EXPECT_EQ(hermit::llm::remove_reasoning("before<think>internal</think>after", "<think>", "</think>"), "beforeafter");
    EXPECT_EQ(hermit::llm::remove_reasoning("unchanged", "", "</think>"), "unchanged");
}

TEST(llm, parses_item_list_with_nested_descriptions)
{
    const std::vector<hermit::Item> items{ hermit::llm::parse_item_list("- first\n  - detail one\n  * detail two\n2. second\n") };
    ASSERT_EQ(items.size(), 2U);
    EXPECT_EQ(items[0].head, "first");
    ASSERT_EQ(items[0].descriptions.size(), 2U);
    EXPECT_EQ(items[0].descriptions[1], "detail two");
    EXPECT_EQ(items[1].head, "second");
}

TEST(llm, sends_named_code_blocks_to_injected_writer)
{
    hermit::config cfg;
    cfg.base_path = "output";
    cfg.llm.code_block_extract = true;
    std::filesystem::path written_path;
    std::string written_data;
    const hermit::filesystem::file_writer writer{ [&written_path, &written_data](const std::filesystem::path& path,
        const char* data, std::size_t size, std::ios_base::openmode)
    {
        written_path = path;
        written_data.assign(data, size);
    } };

    hermit::llm::write_code_block(cfg, "```cpp\nint main() {}\n```", writer);

    EXPECT_EQ(written_path, std::filesystem::path{ "output/cpp.cpp" });
    EXPECT_EQ(written_data, "int main() {}\n");
}

TEST(llm, injects_request_executor_for_completions)
{
    hermit::config cfg;
    cfg.llm.host = "localhost";
    cfg.llm.port = "8080";
    cfg.llm.completions_target = "/completion";
    cfg.llm.api_key = "test-key";
    hermit::tg_parameters parameters;
    const hermit::tcp::request_executor executor{ [](std::string_view host, std::string_view port, const hermit::tcp::request_type& request)
    {
        EXPECT_EQ(host, "localhost");
        EXPECT_EQ(port, "8080");
        EXPECT_EQ(request.target(), "/completion");
        EXPECT_EQ(request[boost::beast::http::field::authorization], "Bearer test-key");
        hermit::tcp::response_type response{ boost::beast::http::status::ok, 11 };
        response.body() = R"({"choices":[{"text":"mock completion"}]})";
        response.prepare_payload();
        return response;
    } };
    EXPECT_EQ(hermit::llm::send_completions_request(cfg, "prompt", parameters, 16, executor), "mock completion");
}

TEST(llm, injects_type_erased_executor_for_completions)
{
    hermit::config cfg;
    cfg.llm.host = "localhost";
    cfg.llm.port = "8080";
    cfg.llm.completions_target = "/completion";
    cfg.llm.api_key = "test-key";
    hermit::tg_parameters parameters;
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.host, "localhost");
        EXPECT_EQ(request.port, "8080");
        EXPECT_EQ(request.target, "/completion");
        EXPECT_EQ(request.options.authorization, "Bearer test-key");
        return hermit::public_api::http_response_data{ 200, R"({"choices":[{"text":"typed completion"}]})", "application/json" };
    } };
    EXPECT_EQ(hermit::llm::send_completions_request(cfg, "prompt", parameters, 16, executor), "typed completion");
}

TEST(llm, injects_type_erased_executor_for_chat_completions)
{
    hermit::config cfg;
    cfg.llm.host = "localhost";
    cfg.llm.port = "8080";
    cfg.llm.chat_completions_target = "/chat";
    hermit::tg_parameters parameters;
    const nlohmann::json messages{ nlohmann::json::array({ {{"role", "user"}, {"content", "hello"}} }) };
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.target, "/chat");
        return hermit::public_api::http_response_data{ 200, R"({"choices":[{"message":{"content":"typed chat"}}]})", "application/json" };
    } };
    EXPECT_EQ(hermit::llm::send_chat_completions_request(cfg, parameters, messages, executor), "typed chat");
}
