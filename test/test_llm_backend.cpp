#include <chrono>

#include <gtest/gtest.h>

#include "Hermit_config.hpp"
#include "Hermit_errors.hpp"
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

TEST(llm_backend_parameters, lists_models_with_get_auth_and_timeout_options)
{
    Hermit::config cfg;
    cfg.llm.host = "localhost";
    cfg.llm.port = "1234";
    cfg.llm.models_target = "/api/v1/models";
    cfg.llm.api_key = "test-token";
    cfg.timeout_connect = 3;
    cfg.timeout_request = 8;
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, Hermit::public_api::http_method::get);
        EXPECT_EQ(request.host, "localhost");
        EXPECT_EQ(request.port, "1234");
        EXPECT_EQ(request.target, "/api/v1/models");
        EXPECT_TRUE(request.body.empty());
        EXPECT_EQ(request.options.authorization, "Bearer test-token");
        EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 3 });
        EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 8 });
        return Hermit::public_api::http_response_data{ 200, R"({"models":[]})", "application/json" };
    } };

    EXPECT_EQ(Hermit::llm::list_models(cfg, executor), R"({"models":[]})");
}

TEST(llm_backend_parameters, lists_running_ollama_models_with_get)
{
    Hermit::config cfg;
    cfg.llm.host = "ollama-host";
    cfg.llm.port = "11434";
    cfg.llm.running_models_target = "/api/ps";
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, Hermit::public_api::http_method::get);
        EXPECT_EQ(request.target, "/api/ps");
        EXPECT_TRUE(request.body.empty());
        return Hermit::public_api::http_response_data{ 200, R"({"models":[]})", "application/json" };
    } };
    EXPECT_EQ(Hermit::llm::list_running_models(cfg, executor), R"({"models":[]})");
}

TEST(llm_backend_parameters, gets_ollama_version_with_get)
{
    Hermit::config cfg;
    cfg.llm.host = "ollama-host";
    cfg.llm.port = "11434";
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, Hermit::public_api::http_method::get);
        EXPECT_EQ(request.target, "/api/version");
        EXPECT_TRUE(request.body.empty());
        return Hermit::public_api::http_response_data{ 200, R"({"version":"0.5.1"})", "application/json" };
    } };
    EXPECT_EQ(Hermit::llm::get_ollama_version(cfg, executor), R"({"version":"0.5.1"})");
}

TEST(llm_backend_parameters, shows_ollama_model_with_verbose_option)
{
    Hermit::config cfg;
    cfg.llm.model = "gemma4";
    cfg.llm.model_info_target = "/api/show";
    cfg.llm.model_info_options = R"({"verbose":true})";
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, Hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/api/show");
        const nlohmann::json body{ nlohmann::json::parse(request.body) };
        EXPECT_EQ(body.at("model"), "gemma4");
        EXPECT_TRUE(body.at("verbose"));
        return Hermit::public_api::http_response_data{ 200, R"({"capabilities":["completion"]})", "application/json" };
    } };
    EXPECT_EQ(Hermit::llm::show_model(cfg, executor), R"({"capabilities":["completion"]})");
}

TEST(llm_backend_parameters, routes_ollama_model_lifecycle_actions_with_exact_methods_and_bodies)
{
    struct action_case
    {
        std::string action;
        std::string target;
        Hermit::public_api::http_method method;
        std::string body;
    };
    const std::vector<action_case> actions
    {
        { "create", "/api/create", Hermit::public_api::http_method::post, R"({"model":"new-model","from":"base","stream":false})" },
        { "copy", "/api/copy", Hermit::public_api::http_method::post, R"({"source":"base","destination":"backup"})" },
        { "delete", "/api/delete", Hermit::public_api::http_method::delete_, R"({"model":"old-model"})" },
        { "pull", "/api/pull", Hermit::public_api::http_method::post, R"({"model":"library/model","stream":false})" },
        { "push", "/api/push", Hermit::public_api::http_method::post, R"({"model":"account/model","stream":false})" }
    };
    for (const action_case& action : actions)
    {
        Hermit::config cfg;
        cfg.llm.model_action = action.action;
        cfg.llm.model_action_options = action.body;
        cfg.llm.host = "ollama-host";
        cfg.llm.port = "11434";
        cfg.llm.api_key = "ollama-token";
        cfg.timeout_connect = 2;
        cfg.timeout_request = 7;
        const Hermit::public_api::http_executor executor{ [&action](const Hermit::public_api::http_request_data& request)
        {
            EXPECT_EQ(request.method, action.method);
            EXPECT_EQ(request.target, action.target);
            EXPECT_EQ(request.options.authorization, "Bearer ollama-token");
            EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 2 });
            EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 7 });
            EXPECT_EQ(nlohmann::json::parse(request.body), nlohmann::json::parse(action.body));
            return Hermit::public_api::http_response_data{ 200, "{\"status\":\"success\"}" };
        } };
        EXPECT_EQ(Hermit::llm::manage_ollama_model(cfg, executor), "{\"status\":\"success\"}") << action.action;
    }
}

TEST(llm_backend_parameters, model_action_rejects_missing_fields_and_non_objects)
{
    Hermit::config cfg;
    cfg.llm.model_action = "copy";
    cfg.llm.model_action_options = R"({"source":"only-source"})";
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "Executor must not be called for invalid action input";
        return Hermit::public_api::http_response_data{ 200, "{}" };
    } };
    EXPECT_THROW(Hermit::llm::manage_ollama_model(cfg, executor), Hermit::command_line_exception);
    cfg.llm.model_action = "delete";
    cfg.llm.model_action_options = "[]";
    EXPECT_THROW(Hermit::llm::manage_ollama_model(cfg, executor), Hermit::command_line_exception);
}

TEST(llm_backend_parameters, model_list_rejects_http_errors)
{
    Hermit::config cfg;
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data&)
    {
        return Hermit::public_api::http_response_data{ 401, R"({"error":"unauthorized"})" };
    } };

    EXPECT_THROW(Hermit::llm::list_models(cfg, executor), Hermit::http_status_exception);
}

TEST(llm_backend_parameters, creates_openai_compatible_embeddings)
{
    Hermit::config cfg;
    cfg.llm.backend_name = "vllm";
    cfg.llm.host = "embedding-host";
    cfg.llm.port = "8000";
    cfg.llm.model = "embed-model";
    cfg.llm.prompt = "text to embed";
    cfg.llm.embedding_target = "/v1/embeddings";
    cfg.llm.api_key = "secret-token";
    cfg.timeout_connect = 2;
    cfg.timeout_request = 9;
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, Hermit::public_api::http_method::post);
        EXPECT_EQ(request.host, "embedding-host");
        EXPECT_EQ(request.port, "8000");
        EXPECT_EQ(request.target, "/v1/embeddings");
        EXPECT_EQ(request.options.authorization, "Bearer secret-token");
        EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 2 });
        EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 9 });
        const nlohmann::json body{ nlohmann::json::parse(request.body) };
        EXPECT_EQ(body.at("input"), "text to embed");
        EXPECT_EQ(body.at("model"), "embed-model");
        EXPECT_EQ(body.at("encoding_format"), "float");
        return Hermit::public_api::http_response_data{ 200, R"({"data":[{"embedding":[0.1]}]})", "application/json" };
    } };

    EXPECT_EQ(Hermit::llm::create_embeddings(cfg, executor), R"({"data":[{"embedding":[0.1]}]})");
}

TEST(llm_backend_parameters, creates_ollama_embeddings_without_openai_fields)
{
    Hermit::config cfg;
    cfg.llm.backend_name = "ollama";
    cfg.llm.port = "11434";
    cfg.llm.model = "embeddinggemma";
    cfg.llm.prompt = "text to embed";
    cfg.llm.embedding_target = "/api/embed";
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, Hermit::public_api::http_method::post);
        EXPECT_EQ(request.port, "11434");
        EXPECT_EQ(request.target, "/api/embed");
        const nlohmann::json body{ nlohmann::json::parse(request.body) };
        EXPECT_EQ(body.at("input"), "text to embed");
        EXPECT_EQ(body.at("model"), "embeddinggemma");
        EXPECT_FALSE(body.contains("encoding_format"));
        return Hermit::public_api::http_response_data{ 200, R"({"embeddings":[[0.2]]})", "application/json" };
    } };

    EXPECT_EQ(Hermit::llm::create_embeddings(cfg, executor), R"({"embeddings":[[0.2]]})");
}

TEST(llm_backend_parameters, creates_batch_embeddings_with_backend_options)
{
    Hermit::config cfg;
    cfg.llm.backend_name = "ollama";
    cfg.llm.model = "embeddinggemma";
    cfg.llm.embedding_target = "/api/embed";
    cfg.llm.embedding_inputs = { "first", "second" };
    cfg.llm.embedding_options = R"({"truncate":false,"dimensions":256,"keep_alive":"5m"})";
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        const nlohmann::json body{ nlohmann::json::parse(request.body) };
        EXPECT_EQ(body.at("input"), nlohmann::json({ "first", "second" }));
        EXPECT_EQ(body.at("model"), "embeddinggemma");
        EXPECT_FALSE(body.at("truncate"));
        EXPECT_EQ(body.at("dimensions"), 256);
        EXPECT_EQ(body.at("keep_alive"), "5m");
        return Hermit::public_api::http_response_data{ 200, R"({"embeddings":[[0.1],[0.2]]})", "application/json" };
    } };

    EXPECT_EQ(Hermit::llm::create_embeddings(cfg, executor), R"({"embeddings":[[0.1],[0.2]]})");
}

TEST(llm_backend_parameters, embedding_options_must_be_object_and_cannot_replace_required_fields)
{
    Hermit::config cfg;
    cfg.llm.backend_name = "vllm";
    cfg.llm.model = "embed-model";
    cfg.llm.embedding_options = R"({"input":"override"})";
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data&)
    {
        return Hermit::public_api::http_response_data{ 200, "{}" };
    } };
    EXPECT_THROW(Hermit::llm::create_embeddings(cfg, executor), Hermit::command_line_exception);

    cfg.llm.embedding_options = "[]";
    EXPECT_THROW(Hermit::llm::create_embeddings(cfg, executor), Hermit::command_line_exception);
}

TEST(llm_backend_parameters, tokenizes_with_llama_cpp_options_auth_and_timeouts)
{
    Hermit::config cfg;
    cfg.llm.host = "token-host";
    cfg.llm.port = "8080";
    cfg.llm.tokenize_target = "/tokenize";
    cfg.llm.prompt = "hello";
    cfg.llm.api_key = "token-key";
    cfg.llm.tokenize_options = R"({"add_special":true,"parse_special":false,"with_pieces":true})";
    cfg.timeout_connect = 4;
    cfg.timeout_request = 12;
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, Hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/tokenize");
        EXPECT_EQ(request.options.authorization, "Bearer token-key");
        EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 4 });
        EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 12 });
        const nlohmann::json body{ nlohmann::json::parse(request.body) };
        EXPECT_EQ(body.at("content"), "hello");
        EXPECT_TRUE(body.at("add_special"));
        EXPECT_FALSE(body.at("parse_special"));
        EXPECT_TRUE(body.at("with_pieces"));
        return Hermit::public_api::http_response_data{ 200, R"({"tokens":[1,2]})", "application/json" };
    } };
    EXPECT_EQ(Hermit::llm::tokenize(cfg, executor), R"({"tokens":[1,2]})");
}

TEST(llm_backend_parameters, detokenizes_token_ids_and_rejects_http_errors)
{
    Hermit::config cfg;
    cfg.llm.detokenize_target = "/detokenize";
    cfg.llm.token_ids = { 1, 2, 3 };
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        const nlohmann::json body{ nlohmann::json::parse(request.body) };
        EXPECT_EQ(body.at("tokens"), nlohmann::json({ 1, 2, 3 }));
        return Hermit::public_api::http_response_data{ 200, R"({"content":"hello"})", "application/json" };
    } };
    EXPECT_EQ(Hermit::llm::detokenize(cfg, executor), R"({"content":"hello"})");

    const Hermit::public_api::http_executor error_executor{ [](const Hermit::public_api::http_request_data&)
    {
        return Hermit::public_api::http_response_data{ 500, "failure" };
    } };
    EXPECT_THROW(Hermit::llm::detokenize(cfg, error_executor), Hermit::http_status_exception);
}

TEST(llm_backend_parameters, embedding_requires_model_and_rejects_http_errors)
{
    Hermit::config cfg;
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data&)
    {
        return Hermit::public_api::http_response_data{ 503, "unavailable" };
    } };

    EXPECT_THROW(Hermit::llm::create_embeddings(cfg, executor), Hermit::command_line_exception);
    cfg.llm.model = "embed-model";
    EXPECT_THROW(Hermit::llm::create_embeddings(cfg, executor), Hermit::http_status_exception);
}

TEST(llm_backend_parameters, builds_and_parses_openai_compatible_requests)
{
    Hermit::tg_parameters parameters;
    parameters.openai_compatible = true;
    parameters.common.model = "local-model";
    parameters.common.max_tokens = 80;
    parameters.common.temperature = 0.4;
    parameters.common.top_p = 0.9;
    parameters.common.stop = { "END" };
    parameters.common.seed = 12;

    const nlohmann::json completion{ parameters.get_request_for_completions("prompt", 20) };
    EXPECT_EQ(completion.at("model"), "local-model");
    EXPECT_EQ(completion.at("prompt"), "prompt");
    EXPECT_EQ(completion.at("max_tokens"), 20);
    EXPECT_EQ(completion.at("stop").at(0), "END");
    EXPECT_FALSE(completion.contains("stop_sequence"));
    EXPECT_EQ(parameters.parse_response_for_completions(R"({"choices":[{"text":"answer"}]})"), "answer");

    const nlohmann::json count{ parameters.get_request_for_token_count("prompt") };
    EXPECT_EQ(count.at("max_tokens"), 0);
    EXPECT_TRUE(count.at("echo"));
    EXPECT_EQ(parameters.parse_response_for_token_count(R"({"usage":{"prompt_tokens":5}})"), 5);

    const nlohmann::json messages{ nlohmann::json::array({ {{"role", "user"}, {"content", "hello"}} }) };
    const nlohmann::json chat{ parameters.get_request_for_chat_completions(messages) };
    EXPECT_EQ(chat.at("model"), "local-model");
    EXPECT_EQ(chat.at("messages"), messages);
    EXPECT_FALSE(chat.contains("stop_sequence"));
    EXPECT_EQ(parameters.parse_response_for_chat_completions(R"({"choices":[{"message":{"content":"reply"}}]})"), "reply");
}

TEST(llm_backend_parameters, builds_and_parses_ollama_native_requests)
{
    Hermit::kc_parameters parameters;
    parameters.ollama_compatible = true;
    parameters.model = "llama3.2:latest";
    parameters.common.max_length = 64;
    parameters.common.temperature = 0.3;
    parameters.common.top_p = 0.8;
    parameters.common.stop_sequence = { "END" };
    parameters.common.sampler_seed = 19;

    const nlohmann::json completion{ parameters.get_request_for_completions("prompt", 16) };
    EXPECT_EQ(completion.at("model"), "llama3.2:latest");
    EXPECT_EQ(completion.at("prompt"), "prompt");
    EXPECT_FALSE(completion.at("stream"));
    EXPECT_TRUE(completion.at("raw"));
    EXPECT_EQ(completion.at("options").at("num_predict"), 16);
    EXPECT_EQ(parameters.parse_response_for_completions(R"({"response":"answer"})"), "answer");

    const nlohmann::json count{ parameters.get_request_for_token_count("prompt") };
    EXPECT_EQ(count.at("options").at("num_predict"), 0);
    EXPECT_EQ(parameters.parse_response_for_token_count(R"({"prompt_eval_count":7})"), 7);

    const nlohmann::json messages{ nlohmann::json::array({ {{"role", "user"}, {"content", "hello"}} }) };
    const nlohmann::json chat{ parameters.get_request_for_chat_completions(messages) };
    EXPECT_EQ(chat.at("model"), "llama3.2:latest");
    EXPECT_EQ(chat.at("messages"), messages);
    EXPECT_EQ(chat.at("options").at("num_predict"), 64);
    EXPECT_EQ(chat.at("options").at("stop").at(0), "END");
    EXPECT_EQ(chat.at("options").at("seed"), 19);
    EXPECT_EQ(parameters.parse_response_for_chat_completions(R"({"message":{"content":"reply"}})"), "reply");
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

TEST(llm_backend_parameters, rejects_non_success_status_from_injected_executors)
{
    Hermit::config cfg;
    Hermit::tg_parameters parameters;
    const nlohmann::json messages{ nlohmann::json::array() };
    const Hermit::tcp::request_executor tcp_executor{ [](std::string_view, std::string_view, const Hermit::tcp::request_type&)
    {
        return Hermit::tcp::response_type{ boost::beast::http::status::internal_server_error, 0 };
    } };
    const Hermit::public_api::http_executor http_executor{ [](const Hermit::public_api::http_request_data&)
    {
        return Hermit::public_api::http_response_data{ 500, "server error" };
    } };

    EXPECT_THROW(Hermit::llm::send_completions_request(cfg, "prompt", parameters, 16, tcp_executor), Hermit::http_status_exception);
    EXPECT_THROW(Hermit::llm::send_chat_completions_request(cfg, parameters, messages, tcp_executor), Hermit::http_status_exception);
    EXPECT_THROW(Hermit::llm::send_completions_request(cfg, "prompt", parameters, 16, http_executor), Hermit::http_status_exception);
    EXPECT_THROW(Hermit::llm::send_chat_completions_request(cfg, parameters, messages, http_executor), Hermit::http_status_exception);
}
