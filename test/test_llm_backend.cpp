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

TEST(llm_backend_parameters, routes_lmstudio_model_management_with_json_and_encoded_job_path)
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
        { "load", "/api/v1/models/load", Hermit::public_api::http_method::post,
            R"({"context_length":8192,"flash_attention":true,"model":"catalog/model"})" },
        { "unload", "/api/v1/models/unload", Hermit::public_api::http_method::post,
            R"({"instance_id":"instance-1"})" },
        { "download", "/api/v1/models/download", Hermit::public_api::http_method::post,
            R"({"model":"catalog/model","quantization":"Q4_K_M"})" },
        { "download-status", "/api/v1/models/download/status/job%2Fpart", Hermit::public_api::http_method::get, "" }
    };
    for (const action_case& action : actions)
    {
        Hermit::config cfg;
        cfg.llm.backend_name = "lmstudio";
        cfg.llm.model_action = action.action;
        cfg.llm.model_action_options = action.action == "load" ? action.body
            : action.action == "unload" ? action.body
            : action.action == "download" ? action.body : R"({"job_id":"job/part"})";
        cfg.llm.host = "lmstudio-host";
        cfg.llm.port = "1234";
        cfg.llm.api_key = "lm-token";
        cfg.timeout_connect = 2;
        cfg.timeout_request = 7;
        const Hermit::public_api::http_executor executor{ [&action](const Hermit::public_api::http_request_data& request)
        {
            EXPECT_EQ(request.method, action.method);
            EXPECT_EQ(request.target, action.target);
            EXPECT_EQ(request.options.authorization, "Bearer lm-token");
            EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 2 });
            EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 7 });
            EXPECT_EQ(request.body, action.body);
            return Hermit::public_api::http_response_data{ 200, "{\"status\":\"ok\"}" };
        } };
        EXPECT_EQ(Hermit::llm::manage_lmstudio_model(cfg, executor), "{\"status\":\"ok\"}") << action.action;
    }
}

TEST(llm_backend_parameters, rejects_invalid_lmstudio_model_management_options)
{
    Hermit::config cfg;
    cfg.llm.model_action = "load";
    cfg.llm.model_action_options = R"({"context_length":8192})";
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "Executor must not be called for invalid LM Studio input";
        return Hermit::public_api::http_response_data{ 200, "{}" };
    } };
    EXPECT_THROW(Hermit::llm::manage_lmstudio_model(cfg, executor), Hermit::command_line_exception);
    cfg.llm.model_action = "unload";
    cfg.llm.model_action_options = "[]";
    EXPECT_THROW(Hermit::llm::manage_lmstudio_model(cfg, executor), Hermit::command_line_exception);
    cfg.llm.model_action = "download-status";
    cfg.llm.model_action_options = R"({"job_id":"job-1","extra":true})";
    EXPECT_THROW(Hermit::llm::manage_lmstudio_model(cfg, executor), Hermit::command_line_exception);
    cfg.llm.model_action_options = R"({"job_id":".."})";
    EXPECT_THROW(Hermit::llm::manage_lmstudio_model(cfg, executor), Hermit::command_line_exception);
    cfg.llm.model_action = "unknown";
    cfg.llm.model_action_options = "{}";
    EXPECT_THROW(Hermit::llm::manage_lmstudio_model(cfg, executor), Hermit::command_line_exception);
}

TEST(llm_backend_parameters, sends_lmstudio_responses_request_and_validates_json_contract)
{
    Hermit::config cfg;
    cfg.llm.host = "lmstudio-host";
    cfg.llm.port = "1234";
    cfg.llm.api_key = "lm-token";
    cfg.llm.api_options = R"({"model":"test-model","input":[{"role":"user","content":"hello"}],"previous_response_id":"resp-previous"})";
    cfg.timeout_connect = 2;
    cfg.timeout_request = 7;
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.host, "lmstudio-host");
        EXPECT_EQ(request.port, "1234");
        EXPECT_EQ(request.method, Hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/responses");
        EXPECT_EQ(nlohmann::json::parse(request.body), nlohmann::json::parse(
            R"({"model":"test-model","input":[{"role":"user","content":"hello"}],"previous_response_id":"resp-previous"})"));
        EXPECT_EQ(request.content_type, "application/json");
        EXPECT_EQ(request.options.authorization, "Bearer lm-token");
        EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 2 });
        EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 7 });
        return Hermit::public_api::http_response_data{ 200,
            R"({"id":"resp-1","object":"response","status":"completed","output":[]})", "application/json" };
    } };
    EXPECT_EQ(Hermit::llm::create_lmstudio_response(cfg, executor),
        R"({"id":"resp-1","object":"response","status":"completed","output":[]})");
}

TEST(llm_backend_parameters, sends_lmstudio_native_chat_request_and_validates_response)
{
    Hermit::config cfg;
    cfg.llm.host = "lmstudio-host";
    cfg.llm.port = "1234";
    cfg.llm.api_key = "lm-token";
    cfg.llm.api_options = R"({"model":"ibm/granite-4-micro","input":"hello","store":false,"context_length":4096})";
    cfg.timeout_connect = 3;
    cfg.timeout_request = 8;
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.host, "lmstudio-host");
        EXPECT_EQ(request.port, "1234");
        EXPECT_EQ(request.method, Hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/api/v1/chat");
        EXPECT_EQ(request.content_type, "application/json");
        EXPECT_EQ(request.options.authorization, "Bearer lm-token");
        EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 3 });
        EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 8 });
        EXPECT_EQ(nlohmann::json::parse(request.body).at("context_length"), 4096);
        return Hermit::public_api::http_response_data{ 200,
            R"({"model_instance_id":"ibm/granite-4-micro","output":[{"type":"message","content":"hello"}],"stats":{"input_tokens":1},"response_id":"resp-1"})",
            "application/json" };
    } };
    EXPECT_NE(Hermit::llm::create_lmstudio_chat(cfg, executor).find("resp-1"), std::string::npos);
}

TEST(llm_backend_parameters, rejects_invalid_lmstudio_native_chat_request_or_response)
{
    Hermit::config cfg;
    const Hermit::public_api::http_executor unused_executor{ [](const Hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "invalid native chat request must be rejected before transport";
        return Hermit::public_api::http_response_data{ 200, R"({"model_instance_id":"m","output":[]})" };
    } };
    for (const std::string options : { "[]", R"({"input":"hello"})", R"({"model":"m"})",
        R"({"model":"m","input":1})", R"({"model":"m","input":"hello","stream":"yes"})",
        R"({"model":"m","input":"hello","stream":true})" })
    {
        cfg.llm.api_options = options;
        EXPECT_THROW(Hermit::llm::create_lmstudio_chat(cfg, unused_executor), Hermit::command_line_exception) << options;
    }
    cfg.llm.api_options = R"({"model":"m","input":"hello"})";
    const Hermit::public_api::http_executor invalid_response_executor{ [](const Hermit::public_api::http_request_data&)
    {
        return Hermit::public_api::http_response_data{ 200, R"({"model_instance_id":"m","output":1})" };
    } };
    EXPECT_THROW(Hermit::llm::create_lmstudio_chat(cfg, invalid_response_executor), Hermit::command_line_exception);
}

TEST(llm_backend_parameters, streams_lmstudio_native_chat_events_as_received)
{
    Hermit::config cfg;
    cfg.llm.api_options = R"({"model":"m","input":"hello","stream":true})";
    std::string received;
    const Hermit::public_api::http_executor unused_executor{ [](const Hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "non-stream executor must not be used";
        return Hermit::public_api::http_response_data{ 200, "{}" };
    } };
    const Hermit::public_api::http_stream_executor stream_executor{ [](const Hermit::public_api::http_request_data& request,
        const Hermit::public_api::http_stream_handler& handler)
    {
        EXPECT_EQ(request.target, "/api/v1/chat");
        EXPECT_EQ(request.content_type, "application/json");
        const std::string first{ "event: chat.start\ndata: {\"type\":\"chat.start\"}\n\n" };
        const std::string last{ "event: chat.end\ndata: {\"type\":\"chat.end\"}\n\n" };
        EXPECT_TRUE(handler(first));
        EXPECT_TRUE(handler(last));
        return Hermit::public_api::http_stream_response_data{ 200, "text/event-stream" };
    } };
    const Hermit::public_api::http_stream_handler writer{ [&received](std::string_view chunk)
    {
        received.append(chunk);
        return true;
    } };
    EXPECT_TRUE(Hermit::llm::create_lmstudio_chat(cfg, unused_executor, stream_executor, writer).empty());
    EXPECT_NE(received.find("chat.start"), std::string::npos);
    EXPECT_NE(received.find("chat.end"), std::string::npos);
}

TEST(llm_backend_parameters, rejects_invalid_or_streaming_lmstudio_responses_requests_before_transport)
{
    Hermit::config cfg;
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "invalid Responses input must be rejected before transport";
        return Hermit::public_api::http_response_data{ 200, R"({"id":"resp-1","output":[]})" };
    } };
    for (const std::string options : { "[]", R"({"input":"hello"})", R"({"model":"m"})",
        R"({"model":"m","input":1})", R"({"model":"m","input":"hello","stream":true})",
        R"({"model":"m","input":"hello","stream":"yes"})" })
    {
        cfg.llm.api_options = options;
        EXPECT_THROW(Hermit::llm::create_lmstudio_response(cfg, executor), Hermit::command_line_exception) << options;
    }

    cfg.llm.api_options = R"({"model":"m","input":"hello"})";
    const Hermit::public_api::http_executor invalid_response_executor{ [](const Hermit::public_api::http_request_data&)
    {
        return Hermit::public_api::http_response_data{ 200, R"({"id":"resp-1"})" };
    } };
    EXPECT_THROW(Hermit::llm::create_lmstudio_response(cfg, invalid_response_executor), Hermit::command_line_exception);
}

TEST(llm_backend_parameters, streams_lmstudio_responses_as_received_sse_chunks)
{
    Hermit::config cfg;
    cfg.llm.api_options = R"({"model":"m","input":"hello","stream":true})";
    std::string received;
    int chunk_count{};
    const Hermit::public_api::http_executor unused_executor{ [](const Hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "non-stream executor must not be used";
        return Hermit::public_api::http_response_data{ 200, "{}" };
    } };
    const Hermit::public_api::http_stream_executor stream_executor{ [&chunk_count](const Hermit::public_api::http_request_data& request,
        const Hermit::public_api::http_stream_handler& handler)
    {
        EXPECT_EQ(request.method, Hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/responses");
        EXPECT_EQ(request.content_type, "application/json");
        const std::string first{ "event: response.created\ndata: {\"type\":\"response.created\"}\n\n" };
        const std::string second{ "event: response.completed\ndata: {\"type\":\"response.completed\"}\n\n" };
        EXPECT_TRUE(handler(first));
        EXPECT_TRUE(handler(second));
        chunk_count = 2;
        return Hermit::public_api::http_stream_response_data{ 200, "text/event-stream" };
    } };
    const Hermit::public_api::http_stream_handler writer{ [&received](std::string_view chunk)
    {
        received.append(chunk);
        return true;
    } };
    EXPECT_TRUE(Hermit::llm::create_lmstudio_response(cfg, unused_executor, stream_executor, writer).empty());
    EXPECT_EQ(chunk_count, 2);
    EXPECT_NE(received.find("response.created"), std::string::npos);
    EXPECT_NE(received.find("response.completed"), std::string::npos);
}

TEST(llm_backend_parameters, sends_lmstudio_decisions_request_and_validates_answers)
{
    Hermit::config cfg;
    cfg.llm.host = "lmstudio-host";
    cfg.llm.port = "1234";
    cfg.llm.api_key = "lm-token";
    cfg.llm.api_options = R"({"model":"decision-model","input":"I was charged twice.","questions":[{"type":"predicate","name":"angry","instructions":"Is the customer angry?"}]})";
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, Hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/decisions");
        EXPECT_EQ(request.content_type, "application/json");
        EXPECT_EQ(request.options.authorization, "Bearer lm-token");
        EXPECT_EQ(nlohmann::json::parse(request.body).at("questions").at(0).at("name"), "angry");
        return Hermit::public_api::http_response_data{ 200, R"({"model":"decision-model","answers":[{"type":"predicate","name":"angry","probability":0.98}],"usage":{}})" };
    } };
    EXPECT_NE(Hermit::llm::create_lmstudio_decision(cfg, executor).find("\"answers\""), std::string::npos);
}

TEST(llm_backend_parameters, rejects_invalid_lmstudio_decisions_before_transport)
{
    Hermit::config cfg;
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "invalid Decisions input must be rejected before transport";
        return Hermit::public_api::http_response_data{ 200, R"({"answers":[]})" };
    } };
    for (const std::string options : { "[]", R"({"input":"text","questions":[]})",
        R"({"model":"m","input":4,"questions":[{}]})",
        R"({"model":"m","input":"text","questions":[{"type":2,"instructions":"test"}]})" })
    {
        cfg.llm.api_options = options;
        EXPECT_THROW(Hermit::llm::create_lmstudio_decision(cfg, executor), Hermit::command_line_exception) << options;
    }
    cfg.llm.api_options = R"({"model":"m","input":"text","questions":[{"type":"predicate","instructions":"test"}]})";
    const Hermit::public_api::http_executor invalid_response_executor{ [](const Hermit::public_api::http_request_data&)
    {
        return Hermit::public_api::http_response_data{ 200, R"({"model":"m"})" };
    } };
    EXPECT_THROW(Hermit::llm::create_lmstudio_decision(cfg, invalid_response_executor), Hermit::command_line_exception);
}

TEST(llm_backend_parameters, sends_lmstudio_anthropic_messages_request_and_validates_response)
{
    Hermit::config cfg;
    cfg.llm.host = "lmstudio-host";
    cfg.llm.port = "1234";
    cfg.llm.api_key = "lm-token";
    cfg.llm.api_options = R"({"model":"m","max_tokens":64,"messages":[{"role":"user","content":"hello"}],"stream":false})";
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, Hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/messages");
        EXPECT_EQ(request.content_type, "application/json");
        EXPECT_FALSE(request.options.authorization.has_value());
        EXPECT_EQ(request.headers.at("x-api-key"), "lm-token");
        const nlohmann::json sent{ nlohmann::json::parse(request.body) };
        EXPECT_EQ(sent.at("max_tokens"), 64);
        EXPECT_EQ(sent.at("messages").at(0).at("content"), "hello");
        return Hermit::public_api::http_response_data{ 200,
            R"({"id":"msg_1","type":"message","role":"assistant","content":[{"type":"text","text":"hello"}],"model":"m","stop_reason":"end_turn"})",
            "application/json" };
    } };
    EXPECT_NE(Hermit::llm::create_lmstudio_messages(cfg, executor).find("msg_1"), std::string::npos);
}

TEST(llm_backend_parameters, rejects_invalid_lmstudio_anthropic_messages_request_or_response)
{
    Hermit::config cfg;
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "invalid Anthropic Messages request must be rejected before transport";
        return Hermit::public_api::http_response_data{ 200, R"({"type":"message","role":"assistant","content":[]})" };
    } };
    for (const std::string options : { "[]", R"({"model":"m","messages":[]})",
        R"({"model":"m","max_tokens":0,"messages":[{"role":"user","content":"x"}]})",
        R"({"model":"m","max_tokens":2,"messages":[{"role":"system","content":"x"}]})",
        R"({"model":"m","max_tokens":2,"messages":[{"role":"user","content":2}]})",
        R"({"model":"m","max_tokens":2,"messages":[{"role":"user","content":"x"}],"stream":"yes"})",
        R"({"model":"m","max_tokens":2,"messages":[{"role":"user","content":"x"}],"stream":true})" })
    {
        cfg.llm.api_options = options;
        EXPECT_THROW(Hermit::llm::create_lmstudio_messages(cfg, executor), Hermit::command_line_exception) << options;
    }
    cfg.llm.api_options = R"({"model":"m","max_tokens":2,"messages":[{"role":"user","content":"x"}]})";
    const Hermit::public_api::http_executor invalid_response{ [](const Hermit::public_api::http_request_data&)
    {
        return Hermit::public_api::http_response_data{ 200, R"({"type":"not_message","content":[]})" };
    } };
    EXPECT_THROW(Hermit::llm::create_lmstudio_messages(cfg, invalid_response), Hermit::command_line_exception);
}

TEST(llm_backend_parameters, streams_lmstudio_anthropic_messages_sse_as_received)
{
    Hermit::config cfg;
    cfg.llm.api_key = "secret";
    cfg.llm.api_options = R"({"model":"m","max_tokens":10,"messages":[{"role":"user","content":"x"}],"stream":true})";
    std::string received;
    const Hermit::public_api::http_executor unused_executor{ [](const Hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "streaming must use stream executor";
        return Hermit::public_api::http_response_data{ 200, "{}" };
    } };
    const Hermit::public_api::http_stream_executor stream_executor{ [](const Hermit::public_api::http_request_data& request,
        const Hermit::public_api::http_stream_handler& handler)
    {
        EXPECT_EQ(request.target, "/v1/messages");
        EXPECT_EQ(request.headers.at("x-api-key"), "secret");
        const std::string chunk{ "event: message_start\ndata: {\"type\":\"message_start\"}\n\n" };
        EXPECT_TRUE(handler(chunk));
        return Hermit::public_api::http_stream_response_data{ 200, "text/event-stream" };
    } };
    const Hermit::public_api::http_stream_handler writer{ [&received](std::string_view chunk)
    {
        received.append(chunk);
        return true;
    } };
    EXPECT_TRUE(Hermit::llm::create_lmstudio_messages(cfg, unused_executor, stream_executor, writer).empty());
    EXPECT_NE(received.find("message_start"), std::string::npos);
}

TEST(llm_backend_parameters, sends_lmstudio_jev_systemone_request_and_validates_answers)
{
    Hermit::config cfg;
    cfg.llm.host = "lmstudio-host";
    cfg.llm.port = "1234";
    cfg.llm.api_key = "jev-token";
    cfg.llm.api_options = R"({"model":"decision-model","state":"charged twice","questions":{"angry":{"type":"noul","instructions":"Is the customer angry?"},"severity":{"type":"score","instructions":"Rate severity","criteria":["low","medium","high"]},"cause":{"type":"choice","instructions":"Choose cause","criteria":{"billing":"billing issue","other":"other"}}}})";
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, Hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/systemone");
        EXPECT_EQ(request.content_type, "application/json");
        EXPECT_EQ(request.options.authorization, "Bearer jev-token");
        const nlohmann::json sent{ nlohmann::json::parse(request.body) };
        EXPECT_EQ(sent.at("state"), "charged twice");
        EXPECT_EQ(sent.at("questions").at("cause").at("type"), "choice");
        return Hermit::public_api::http_response_data{ 200,
            R"({"answers":{"angry":{"value":true},"severity":{"value":0.8},"cause":{"value":"billing"}}})", "application/json" };
    } };
    EXPECT_NE(Hermit::llm::create_lmstudio_systemone(cfg, executor).find("severity"), std::string::npos);
}

TEST(llm_backend_parameters, rejects_invalid_lmstudio_jev_systemone_request_or_response)
{
    Hermit::config cfg;
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "invalid System One request must be rejected before transport";
        return Hermit::public_api::http_response_data{ 200, R"({"answers":{}})" };
    } };
    for (const std::string options : { "[]", R"({"model":"m","state":"x","questions":[]})",
        R"({"model":"m","state":"x","questions":{"q":{"type":"noul"}}})",
        R"({"model":"m","state":"x","questions":{"q":{"type":"unknown","instructions":"x"}}})",
        R"({"model":"m","state":"x","questions":{"q":{"type":"score","instructions":"x","criteria":[]}}})" })
    {
        cfg.llm.api_options = options;
        EXPECT_THROW(Hermit::llm::create_lmstudio_systemone(cfg, executor), Hermit::command_line_exception) << options;
    }
    cfg.llm.api_options = R"({"model":"m","state":"x","questions":{"q":{"type":"noul","instructions":"x"}}})";
    const Hermit::public_api::http_executor missing_answer{ [](const Hermit::public_api::http_request_data&)
    {
        return Hermit::public_api::http_response_data{ 200, R"({"answers":{}})" };
    } };
    EXPECT_THROW(Hermit::llm::create_lmstudio_systemone(cfg, missing_answer), Hermit::command_line_exception);
}

TEST(llm_backend_parameters, sends_vllm_batched_chat_and_checks_choice_count)
{
    Hermit::config cfg;
    cfg.llm.host = "vllm-host";
    cfg.llm.port = "8000";
    cfg.llm.api_key = "vllm-token";
    cfg.llm.api_options = R"({"model":"test-model","messages":[[{"role":"user","content":"one"}],[{"role":"user","content":"two"}]],"max_tokens":12})";
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, Hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/chat/completions/batch");
        EXPECT_EQ(request.content_type, "application/json");
        EXPECT_EQ(request.options.authorization, "Bearer vllm-token");
        EXPECT_EQ(nlohmann::json::parse(request.body).at("messages").size(), 2U);
        return Hermit::public_api::http_response_data{ 200,
            R"({"choices":[{"index":0,"message":{"role":"assistant","content":"first"}},{"index":1,"message":{"role":"assistant","content":"second"}}]})",
            "application/json" };
    } };
    EXPECT_NE(Hermit::llm::create_vllm_batch_chat(cfg, executor).find("second"), std::string::npos);
}

TEST(llm_backend_parameters, rejects_invalid_vllm_batch_chat_request_or_response)
{
    Hermit::config cfg;
    const Hermit::public_api::http_executor unused_executor{ [](const Hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "invalid batch chat must be rejected before transport";
        return Hermit::public_api::http_response_data{ 200, R"({"choices":[]})" };
    } };
    for (const std::string options : { "[]", R"({"messages":[[{"role":"user","content":"x"}]]})",
        R"({"model":"m","messages":[]})", R"({"model":"m","messages":[[]]})",
        R"({"model":"m","messages":[[{"role":"user","content":2}]]})",
        R"({"model":"m","messages":[[{"role":"user","content":"x"}]],"stream":true})",
        R"({"model":"m","messages":[[{"role":"user","content":"x"}]],"tools":[]})",
        R"({"model":"m","messages":[[{"role":"user","content":"x"}]],"n":2})" })
    {
        cfg.llm.api_options = options;
        EXPECT_THROW(Hermit::llm::create_vllm_batch_chat(cfg, unused_executor), Hermit::command_line_exception) << options;
    }
    cfg.llm.api_options = R"({"model":"m","messages":[[{"role":"user","content":"x"}],[{"role":"user","content":"y"}]]})";
    const Hermit::public_api::http_executor wrong_count{ [](const Hermit::public_api::http_request_data&)
    {
        return Hermit::public_api::http_response_data{ 200, R"({"choices":[{"index":0}]})" };
    } };
    EXPECT_THROW(Hermit::llm::create_vllm_batch_chat(cfg, wrong_count), Hermit::command_line_exception);
}

TEST(llm_backend_parameters, sends_generic_backend_api_request_with_method_body_auth_and_headers)
{
    Hermit::config cfg;
    cfg.llm.host = "backend-host";
    cfg.llm.port = "8080";
    cfg.llm.api_key = "test-token";
    cfg.llm.api_method = "POST";
    cfg.llm.api_path = "/v1/responses";
    cfg.llm.api_options = R"({"model":"test-model","input":"hello"})";
    cfg.llm.api_content_type = "application/json";
    cfg.llm.api_headers = R"({"X-Trace":"trace-1"})";
    cfg.timeout_connect = 2;
    cfg.timeout_request = 7;
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.host, "backend-host");
        EXPECT_EQ(request.port, "8080");
        EXPECT_EQ(request.method, Hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/responses");
        EXPECT_EQ(nlohmann::json::parse(request.body), nlohmann::json::parse(R"({"model":"test-model","input":"hello"})"));
        EXPECT_EQ(request.content_type, "application/json");
        EXPECT_EQ(request.options.authorization, "Bearer test-token");
        EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 2 });
        EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 7 });
        EXPECT_EQ(request.headers.at("X-Trace"), "trace-1");
        return Hermit::public_api::http_response_data{ 200, R"({"id":"resp-1"})", "application/json", false };
    } };
    const Hermit::public_api::http_response_data response{ Hermit::llm::make_api_request(cfg, executor) };
    EXPECT_EQ(response.body, R"({"id":"resp-1"})");
}

TEST(llm_backend_parameters, rejects_unsafe_generic_backend_api_requests_before_transport)
{
    Hermit::config cfg;
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "invalid request must be rejected before transport";
        return Hermit::public_api::http_response_data{ 200, "{}" };
    } };
    for (const std::string path : { "", "relative", "//remote/path", "/../admin", "/api/%2e%2e/admin", "/api#fragment", "/api\\admin" })
    {
        cfg.llm.api_path = path;
        EXPECT_THROW(Hermit::llm::make_api_request(cfg, executor), std::invalid_argument) << path;
    }
    cfg.llm.api_path = "/v1/responses";
    cfg.llm.api_method = "TRACE";
    EXPECT_THROW(Hermit::llm::make_api_request(cfg, executor), std::invalid_argument);
    cfg.llm.api_method = "POST";
    cfg.llm.api_options = "[]";
    cfg.llm.api_body_file = "payload.bin";
    EXPECT_THROW(Hermit::llm::make_api_request(cfg, executor), std::invalid_argument);
    cfg.llm.api_options.clear();
    cfg.llm.api_body_file.clear();
    cfg.llm.api_headers = R"({"Host":"remote.example"})";
    EXPECT_THROW(Hermit::llm::make_api_request(cfg, executor), std::invalid_argument);
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
