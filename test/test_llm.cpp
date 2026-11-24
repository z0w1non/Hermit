#include <gtest/gtest.h>

#include "Hermit_llm.hpp"
#include "Hermit_llm_types.hpp"
#include "Hermit_config.hpp"
#include "Hermit_tcp.hpp"

TEST(llm, parses_markdown_code_blocks_and_removes_reasoning)
{
    const Hermit::llm::code_blocks blocks{ Hermit::llm::extract_code_block_from_markdown("```cpp\nint main() {}\n```\n```text\nhello\n```") };
    ASSERT_EQ(blocks.size(), 2U);
    EXPECT_EQ(blocks.at("cpp"), "int main() {}\n");
    EXPECT_EQ(blocks.at("text"), "hello\n");
    const Hermit::llm::code_blocks unix_unsafe_name{ Hermit::llm::extract_code_block_from_markdown("```../../outside\npayload\n```") };
    ASSERT_EQ(unix_unsafe_name.size(), 1U);
    EXPECT_EQ(unix_unsafe_name.begin()->first, "....outside");
    const Hermit::llm::code_blocks windows_unsafe_name{ Hermit::llm::extract_code_block_from_markdown("```..\\..\\outside\npayload\n```") };
    ASSERT_EQ(windows_unsafe_name.size(), 1U);
    EXPECT_EQ(windows_unsafe_name.begin()->first, "....outside");
    EXPECT_EQ(Hermit::llm::remove_reasoning("before<think>internal</think>after", "<think>", "</think>"), "beforeafter");
    EXPECT_EQ(Hermit::llm::remove_reasoning("unchanged", "", "</think>"), "unchanged");
}

TEST(llm, parses_item_list_with_nested_descriptions)
{
    const std::vector<Hermit::item> items{ Hermit::llm::parse_item_list("- first\n  - detail one\n  * detail two\n2. second\n") };
    ASSERT_EQ(items.size(), 2U);
    EXPECT_EQ(items[0].head, "first");
    ASSERT_EQ(items[0].descriptions.size(), 2U);
    EXPECT_EQ(items[0].descriptions[1], "detail two");
    EXPECT_EQ(items[1].head, "second");
}

TEST(llm, injects_request_executor_for_completions)
{
    Hermit::config cfg;
    cfg.llm.host = "localhost";
    cfg.llm.port = "8080";
    cfg.llm.completions_target = "/completion";
    cfg.llm.api_key = "test-key";
    Hermit::tg_parameters parameters;
    const Hermit::tcp::request_executor executor{ [](std::string_view host, std::string_view port, const Hermit::tcp::request_type& request)
    {
        EXPECT_EQ(host, "localhost");
        EXPECT_EQ(port, "8080");
        EXPECT_EQ(request.target(), "/completion");
        EXPECT_EQ(request[boost::beast::http::field::authorization], "Bearer test-key");
        Hermit::tcp::response_type response{ boost::beast::http::status::ok, 11 };
        response.body() = R"({"choices":[{"text":"mock completion"}]})";
        response.prepare_payload();
        return response;
    } };
    EXPECT_EQ(Hermit::llm::send_completions_request(cfg, "prompt", parameters, 16, executor), "mock completion");
}

TEST(llm, injects_type_erased_executor_for_completions)
{
    Hermit::config cfg;
    cfg.llm.host = "localhost";
    cfg.llm.port = "8080";
    cfg.llm.completions_target = "/completion";
    cfg.llm.api_key = "test-key";
    Hermit::tg_parameters parameters;
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.host, "localhost");
        EXPECT_EQ(request.port, "8080");
        EXPECT_EQ(request.target, "/completion");
        EXPECT_EQ(request.options.authorization, "Bearer test-key");
        return Hermit::public_api::http_response_data{ 200, R"({"choices":[{"text":"typed completion"}]})", "application/json" };
    } };
    EXPECT_EQ(Hermit::llm::send_completions_request(cfg, "prompt", parameters, 16, executor), "typed completion");
}

TEST(llm, injects_type_erased_executor_for_chat_completions)
{
    Hermit::config cfg;
    cfg.llm.host = "localhost";
    cfg.llm.port = "8080";
    cfg.llm.chat_completions_target = "/chat";
    Hermit::tg_parameters parameters;
    const nlohmann::json messages{ nlohmann::json::array({ {{"role", "user"}, {"content", "hello"}} }) };
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.target, "/chat");
        return Hermit::public_api::http_response_data{ 200, R"({"choices":[{"message":{"content":"typed chat"}}]})", "application/json" };
    } };
    EXPECT_EQ(Hermit::llm::send_chat_completions_request(cfg, parameters, messages, executor), "typed chat");
}
