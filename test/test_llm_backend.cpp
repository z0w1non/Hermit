#include <gtest/gtest.h>

#include "Hermit_config.hpp"
#include "Hermit_llm.hpp"
#include "Hermit_tg_kc_config.hpp"
#include "test_support.hpp"

TEST(llm_backend_parameters, builds_and_parses_text_generation_requests)
{
    Hermit::tg_parameters parameters;
    parameters.common.model = "test-model";
    parameters.common.max_tokens = 128;
    parameters.common.seed = 42;
    const nlohmann::json request{ parameters.get_request_for_completions("hello", 16) };
    EXPECT_EQ(request.at("prompt"), "hello");
    EXPECT_EQ(request.at("max_tokens"), 16);
    EXPECT_EQ(request.at("seed"), 42);
    EXPECT_EQ(parameters.parse_response_for_completions(R"({"choices":[{"text":"answer"}]})"), "answer");
    EXPECT_EQ(parameters.parse_response_for_token_count(R"({"length":7})"), 7);
    EXPECT_EQ(parameters.get_max_tokens(), 128);
}

TEST(llm_backend_parameters, builds_and_parses_kobold_requests)
{
    Hermit::kc_parameters parameters;
    parameters.common.max_length = 64;
    parameters.common.max_context_length = 2048;
    const nlohmann::json request{ parameters.get_request_for_completions("hello", 12) };
    EXPECT_EQ(request.at("prompt"), "hello");
    EXPECT_EQ(request.at("max_length"), 12);
    EXPECT_EQ(parameters.parse_response_for_completions(R"({"results":[{"text":"answer"}]})"), "answer");
    EXPECT_EQ(parameters.parse_response_for_token_count(R"({"value":9})"), 9);
    EXPECT_EQ(parameters.get_truncation_length(), 2048);
}

TEST(llm_backend_parameters, builds_token_count_requests_and_rejects_malformed_responses)
{
    Hermit::tg_parameters tg;
    Hermit::kc_parameters kc;
    EXPECT_EQ(tg.get_request_for_token_count("hello").at("text"), "hello");
    EXPECT_EQ(kc.get_request_for_token_count("hello").at("prompt"), "hello");
    EXPECT_THROW(tg.parse_response_for_completions("not-json"), std::exception);
    EXPECT_THROW(kc.parse_response_for_token_count(R"({"value":"not-an-int"})"), std::exception);
}

TEST(llm_backend_parameters, rejects_responses_with_missing_required_fields)
{
    Hermit::tg_parameters tg;
    Hermit::kc_parameters kc;
    EXPECT_THROW(tg.parse_response_for_completions(R"({"choices":[]})"), std::exception);
    EXPECT_THROW(tg.parse_response_for_chat_completions(R"({"choices":[{}]})"), std::exception);
    EXPECT_THROW(kc.parse_response_for_completions(R"({"results":[]})"), std::exception);
    EXPECT_THROW(kc.parse_response_for_chat_completions(R"({"choices":[{"message":{}}]})"), std::exception);
}

TEST(llm_backend_parameters, builds_and_parses_chat_requests)
{
    const nlohmann::json messages{ nlohmann::json::array({ {{"role", "user"}, {"content", "hello"}} }) };

    Hermit::tg_parameters tg;
    tg.common.max_tokens = 32;
    const nlohmann::json tg_request{ tg.get_request_for_chat_completions(messages) };
    EXPECT_EQ(tg_request.at("messages"), messages);
    EXPECT_EQ(tg_request.at("max_tokens"), 32);
    EXPECT_EQ(tg.parse_response_for_chat_completions(R"({"choices":[{"message":{"content":"reply"}}]})"), "reply");

    Hermit::kc_parameters kc;
    kc.common.max_length = 48;
    const nlohmann::json kc_request{ kc.get_request_for_chat_completions(messages) };
    EXPECT_EQ(kc_request.at("messages"), messages);
    EXPECT_EQ(kc_request.at("max_tokens"), 48);
    EXPECT_EQ(kc.parse_response_for_chat_completions(R"({"choices":[{"message":{"content":"reply"}}]})"), "reply");
}

TEST(llm_backend_parameters, mock_contract_supports_all_backend_operations)
{
    testing::StrictMock<MockLlmBackendParameters> backend;
    const nlohmann::json messages{ nlohmann::json::array() };
    const nlohmann::json request{ {{"prompt", "hello"}} };

    EXPECT_CALL(backend, get_request_for_completions("hello", 16)).WillOnce(testing::Return(request));
    EXPECT_CALL(backend, parse_response_for_completions("response")).WillOnce(testing::Return("text"));
    EXPECT_CALL(backend, get_request_for_token_count("hello")).WillOnce(testing::Return(request));
    EXPECT_CALL(backend, parse_response_for_token_count("tokens")).WillOnce(testing::Return(3));
    EXPECT_CALL(backend, get_request_for_chat_completions(messages)).WillOnce(testing::Return(request));
    EXPECT_CALL(backend, parse_response_for_chat_completions("chat-response")).WillOnce(testing::Return("chat"));
    EXPECT_CALL(backend, get_max_tokens()).WillOnce(testing::Return(128));
    EXPECT_CALL(backend, get_truncation_length()).WillOnce(testing::Return(4096));

    EXPECT_EQ(backend.get_request_for_completions("hello", 16), request);
    EXPECT_EQ(backend.parse_response_for_completions("response"), "text");
    EXPECT_EQ(backend.get_request_for_token_count("hello"), request);
    EXPECT_EQ(backend.parse_response_for_token_count("tokens"), 3);
    EXPECT_EQ(backend.get_request_for_chat_completions(messages), request);
    EXPECT_EQ(backend.parse_response_for_chat_completions("chat-response"), "chat");
    EXPECT_EQ(backend.get_max_tokens(), 128);
    EXPECT_EQ(backend.get_truncation_length(), 4096);
}
