#include <chrono>

#include <gtest/gtest.h>

#include "config.hpp"
#include "errors.hpp"
#include "llm.hpp"
#include "tg_kc_config.hpp"
#include "test_support.hpp"

TEST(llm_backend_parameters, builds_and_parses_text_generation_requests)
{
    hermit::tg_parameters parameters;
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
    hermit::config cfg;
    cfg.llm.host = "localhost";
    cfg.llm.port = "1234";
    cfg.llm.models_target = "/api/v1/models";
    cfg.llm.api_key = "test-token";
    cfg.timeout_connect = 3;
    cfg.timeout_request = 8;
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::get);
        EXPECT_EQ(request.host, "localhost");
        EXPECT_EQ(request.port, "1234");
        EXPECT_EQ(request.target, "/api/v1/models");
        EXPECT_TRUE(request.body.empty());
        EXPECT_EQ(request.options.authorization, "Bearer test-token");
        EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 3 });
        EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 8 });
        return hermit::public_api::http_response_data{ 200, R"({"models":[]})", "application/json" };
    } };

    EXPECT_EQ(hermit::llm::list_models(cfg, executor), R"({"models":[]})");
}

TEST(llm_backend_parameters, lists_tg_models_with_admin_auth_and_validates_response)
{
    hermit::config cfg;
    cfg.llm.backend_name = "tg";
    cfg.llm.host = "textgen-host";
    cfg.llm.port = "5000";
    cfg.llm.models_target = "/v1/internal/model/list";
    cfg.llm.api_key = "regular-key";
    cfg.llm.admin_api_key = "admin-key";
    cfg.timeout_connect = 2;
    cfg.timeout_request = 10;
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::get);
        EXPECT_EQ(request.target, "/v1/internal/model/list");
        EXPECT_EQ(request.options.authorization, "Bearer admin-key");
        EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 2 });
        EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 10 });
        return hermit::public_api::http_response_data{ 200, R"({"model_names":["model-a","model-b"]})", "application/json" };
    } };
    EXPECT_EQ(hermit::llm::list_models(cfg, executor), R"({"model_names":["model-a","model-b"]})");

    cfg.llm.admin_api_key.clear();
    cfg.llm.api_key = "regular-key";
    const hermit::public_api::http_executor api_key_fallback{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.options.authorization, "Bearer regular-key");
        return hermit::public_api::http_response_data{ 200, R"({"model_names":[1]})" };
    } };
    EXPECT_THROW(hermit::llm::list_models(cfg, api_key_fallback), hermit::command_line_exception);
}

TEST(llm_backend_parameters, loads_and_unloads_tg_models_with_admin_auth)
{
    hermit::config cfg;
    cfg.llm.backend_name = "tg";
    cfg.llm.host = "textgen-host";
    cfg.llm.port = "5000";
    cfg.llm.model_action = "load";
    cfg.llm.model_action_options = R"({"model_name":"ExampleModel","args":{"n_gpu_layers":8},"instruction_template":"ChatML"})";
    cfg.llm.admin_api_key = "admin-key";
    cfg.llm.api_key = "regular-key";
    cfg.timeout_connect = 3;
    cfg.timeout_request = 12;
    const hermit::public_api::http_executor load_executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/internal/model/load");
        EXPECT_EQ(request.content_type, "application/json");
        EXPECT_EQ(request.options.authorization, "Bearer admin-key");
        EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 3 });
        EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 12 });
        const nlohmann::json body{ nlohmann::json::parse(request.body) };
        EXPECT_EQ(body.at("model_name"), "ExampleModel");
        EXPECT_EQ(body.at("args").at("n_gpu_layers"), 8);
        EXPECT_EQ(body.at("instruction_template"), "ChatML");
        return hermit::public_api::http_response_data{ 200, R"("OK")", "application/json" };
    } };
    EXPECT_EQ(hermit::llm::manage_tg_model(cfg, load_executor), R"("OK")");

    cfg.llm.model_action = "unload";
    cfg.llm.model_action_options.clear();
    cfg.llm.admin_api_key.clear();
    const hermit::public_api::http_executor unload_executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/internal/model/unload");
        EXPECT_TRUE(request.body.empty());
        EXPECT_EQ(request.options.authorization, "Bearer regular-key");
        return hermit::public_api::http_response_data{ 200, R"("OK")", "application/json" };
    } };
    EXPECT_EQ(hermit::llm::manage_tg_model(cfg, unload_executor), R"("OK")");

    cfg.llm.model_action = "load";
    cfg.llm.model_action_options = R"({"model_name":""})";
    const hermit::public_api::http_executor unused_executor{ [](const hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "invalid model load request must be rejected before transport";
        return hermit::public_api::http_response_data{ 200, R"("OK")" };
    } };
    EXPECT_THROW(hermit::llm::manage_tg_model(cfg, unused_executor), hermit::command_line_exception);
}

TEST(llm_backend_parameters, lists_running_ollama_models_with_get)
{
    hermit::config cfg;
    cfg.llm.host = "ollama-host";
    cfg.llm.port = "11434";
    cfg.llm.running_models_target = "/api/ps";
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::get);
        EXPECT_EQ(request.target, "/api/ps");
        EXPECT_TRUE(request.body.empty());
        return hermit::public_api::http_response_data{ 200, R"({"models":[]})", "application/json" };
    } };
    EXPECT_EQ(hermit::llm::list_running_models(cfg, executor), R"({"models":[]})");
}

TEST(llm_backend_parameters, gets_ollama_version_with_get)
{
    hermit::config cfg;
    cfg.llm.host = "ollama-host";
    cfg.llm.port = "11434";
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::get);
        EXPECT_EQ(request.target, "/api/version");
        EXPECT_TRUE(request.body.empty());
        return hermit::public_api::http_response_data{ 200, R"({"version":"0.5.1"})", "application/json" };
    } };
    EXPECT_EQ(hermit::llm::get_ollama_version(cfg, executor), R"({"version":"0.5.1"})");
}

TEST(llm_backend_parameters, shows_ollama_model_with_verbose_option)
{
    hermit::config cfg;
    cfg.llm.model = "gemma4";
    cfg.llm.model_info_target = "/api/show";
    cfg.llm.model_info_options = R"({"verbose":true})";
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/api/show");
        const nlohmann::json body{ nlohmann::json::parse(request.body) };
        EXPECT_EQ(body.at("model"), "gemma4");
        EXPECT_TRUE(body.at("verbose"));
        return hermit::public_api::http_response_data{ 200, R"({"capabilities":["completion"]})", "application/json" };
    } };
    EXPECT_EQ(hermit::llm::show_model(cfg, executor), R"({"capabilities":["completion"]})");
}

TEST(llm_backend_parameters, routes_ollama_model_lifecycle_actions_with_exact_methods_and_bodies)
{
    struct action_case
    {
        std::string action;
        std::string target;
        hermit::public_api::http_method method;
        std::string body;
    };
    const std::vector<action_case> actions
    {
        { "create", "/api/create", hermit::public_api::http_method::post, R"({"model":"new-model","from":"base","stream":false})" },
        { "copy", "/api/copy", hermit::public_api::http_method::post, R"({"source":"base","destination":"backup"})" },
        { "delete", "/api/delete", hermit::public_api::http_method::delete_, R"({"model":"old-model"})" },
        { "pull", "/api/pull", hermit::public_api::http_method::post, R"({"model":"library/model","stream":false})" },
        { "push", "/api/push", hermit::public_api::http_method::post, R"({"model":"account/model","stream":false})" }
    };
    for (const action_case& action : actions)
    {
        hermit::config cfg;
        cfg.llm.model_action = action.action;
        cfg.llm.model_action_options = action.body;
        cfg.llm.host = "ollama-host";
        cfg.llm.port = "11434";
        cfg.llm.api_key = "ollama-token";
        cfg.timeout_connect = 2;
        cfg.timeout_request = 7;
        const hermit::public_api::http_executor executor{ [&action](const hermit::public_api::http_request_data& request)
        {
            EXPECT_EQ(request.method, action.method);
            EXPECT_EQ(request.target, action.target);
            EXPECT_EQ(request.options.authorization, "Bearer ollama-token");
            EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 2 });
            EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 7 });
            EXPECT_EQ(nlohmann::json::parse(request.body), nlohmann::json::parse(action.body));
            return hermit::public_api::http_response_data{ 200, "{\"status\":\"success\"}" };
        } };
        EXPECT_EQ(hermit::llm::manage_ollama_model(cfg, executor), "{\"status\":\"success\"}") << action.action;
    }
}

TEST(llm_backend_parameters, model_action_rejects_missing_fields_and_non_objects)
{
    hermit::config cfg;
    cfg.llm.model_action = "copy";
    cfg.llm.model_action_options = R"({"source":"only-source"})";
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "Executor must not be called for invalid action input";
        return hermit::public_api::http_response_data{ 200, "{}" };
    } };
    EXPECT_THROW(hermit::llm::manage_ollama_model(cfg, executor), hermit::command_line_exception);
    cfg.llm.model_action = "delete";
    cfg.llm.model_action_options = "[]";
    EXPECT_THROW(hermit::llm::manage_ollama_model(cfg, executor), hermit::command_line_exception);
}

TEST(llm_backend_parameters, routes_lmstudio_model_management_with_json_and_encoded_job_path)
{
    struct action_case
    {
        std::string action;
        std::string target;
        hermit::public_api::http_method method;
        std::string body;
    };
    const std::vector<action_case> actions
    {
        { "load", "/api/v1/models/load", hermit::public_api::http_method::post,
            R"({"context_length":8192,"flash_attention":true,"model":"catalog/model"})" },
        { "unload", "/api/v1/models/unload", hermit::public_api::http_method::post,
            R"({"instance_id":"instance-1"})" },
        { "download", "/api/v1/models/download", hermit::public_api::http_method::post,
            R"({"model":"catalog/model","quantization":"Q4_K_M"})" },
        { "download-status", "/api/v1/models/download/status/job%2Fpart", hermit::public_api::http_method::get, "" }
    };
    for (const action_case& action : actions)
    {
        hermit::config cfg;
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
        const hermit::public_api::http_executor executor{ [&action](const hermit::public_api::http_request_data& request)
        {
            EXPECT_EQ(request.method, action.method);
            EXPECT_EQ(request.target, action.target);
            EXPECT_EQ(request.options.authorization, "Bearer lm-token");
            EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 2 });
            EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 7 });
            EXPECT_EQ(request.body, action.body);
            return hermit::public_api::http_response_data{ 200, "{\"status\":\"ok\"}" };
        } };
        EXPECT_EQ(hermit::llm::manage_lmstudio_model(cfg, executor), "{\"status\":\"ok\"}") << action.action;
    }
}

TEST(llm_backend_parameters, rejects_invalid_lmstudio_model_management_options)
{
    hermit::config cfg;
    cfg.llm.model_action = "load";
    cfg.llm.model_action_options = R"({"context_length":8192})";
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "Executor must not be called for invalid LM Studio input";
        return hermit::public_api::http_response_data{ 200, "{}" };
    } };
    EXPECT_THROW(hermit::llm::manage_lmstudio_model(cfg, executor), hermit::command_line_exception);
    cfg.llm.model_action = "unload";
    cfg.llm.model_action_options = "[]";
    EXPECT_THROW(hermit::llm::manage_lmstudio_model(cfg, executor), hermit::command_line_exception);
    cfg.llm.model_action = "download-status";
    cfg.llm.model_action_options = R"({"job_id":"job-1","extra":true})";
    EXPECT_THROW(hermit::llm::manage_lmstudio_model(cfg, executor), hermit::command_line_exception);
    cfg.llm.model_action_options = R"({"job_id":".."})";
    EXPECT_THROW(hermit::llm::manage_lmstudio_model(cfg, executor), hermit::command_line_exception);
    cfg.llm.model_action = "unknown";
    cfg.llm.model_action_options = "{}";
    EXPECT_THROW(hermit::llm::manage_lmstudio_model(cfg, executor), hermit::command_line_exception);
}

TEST(llm_backend_parameters, sends_lmstudio_responses_request_and_validates_json_contract)
{
    hermit::config cfg;
    cfg.llm.host = "lmstudio-host";
    cfg.llm.port = "1234";
    cfg.llm.api_key = "lm-token";
    cfg.llm.api_options = R"({"model":"test-model","input":[{"role":"user","content":"hello"}],"previous_response_id":"resp-previous"})";
    cfg.timeout_connect = 2;
    cfg.timeout_request = 7;
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.host, "lmstudio-host");
        EXPECT_EQ(request.port, "1234");
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/responses");
        EXPECT_EQ(nlohmann::json::parse(request.body), nlohmann::json::parse(
            R"({"model":"test-model","input":[{"role":"user","content":"hello"}],"previous_response_id":"resp-previous"})"));
        EXPECT_EQ(request.content_type, "application/json");
        EXPECT_EQ(request.options.authorization, "Bearer lm-token");
        EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 2 });
        EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 7 });
        return hermit::public_api::http_response_data{ 200,
            R"({"id":"resp-1","object":"response","status":"completed","output":[]})", "application/json" };
    } };
    EXPECT_EQ(hermit::llm::create_lmstudio_response(cfg, executor),
        R"({"id":"resp-1","object":"response","status":"completed","output":[]})");
}

TEST(llm_backend_parameters, sends_lmstudio_native_chat_request_and_validates_response)
{
    hermit::config cfg;
    cfg.llm.host = "lmstudio-host";
    cfg.llm.port = "1234";
    cfg.llm.api_key = "lm-token";
    cfg.llm.api_options = R"({"model":"ibm/granite-4-micro","input":"hello","store":false,"context_length":4096})";
    cfg.timeout_connect = 3;
    cfg.timeout_request = 8;
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.host, "lmstudio-host");
        EXPECT_EQ(request.port, "1234");
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/api/v1/chat");
        EXPECT_EQ(request.content_type, "application/json");
        EXPECT_EQ(request.options.authorization, "Bearer lm-token");
        EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 3 });
        EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 8 });
        EXPECT_EQ(nlohmann::json::parse(request.body).at("context_length"), 4096);
        return hermit::public_api::http_response_data{ 200,
            R"({"model_instance_id":"ibm/granite-4-micro","output":[{"type":"message","content":"hello"}],"stats":{"input_tokens":1},"response_id":"resp-1"})",
            "application/json" };
    } };
    EXPECT_NE(hermit::llm::create_lmstudio_chat(cfg, executor).find("resp-1"), std::string::npos);
}

TEST(llm_backend_parameters, rejects_invalid_lmstudio_native_chat_request_or_response)
{
    hermit::config cfg;
    const hermit::public_api::http_executor unused_executor{ [](const hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "invalid native chat request must be rejected before transport";
        return hermit::public_api::http_response_data{ 200, R"({"model_instance_id":"m","output":[]})" };
    } };
    for (const std::string options : { "[]", R"({"input":"hello"})", R"({"model":"m"})",
        R"({"model":"m","input":1})", R"({"model":"m","input":"hello","stream":"yes"})",
        R"({"model":"m","input":"hello","stream":true})" })
    {
        cfg.llm.api_options = options;
        EXPECT_THROW(hermit::llm::create_lmstudio_chat(cfg, unused_executor), hermit::command_line_exception) << options;
    }
    cfg.llm.api_options = R"({"model":"m","input":"hello"})";
    const hermit::public_api::http_executor invalid_response_executor{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 200, R"({"model_instance_id":"m","output":1})" };
    } };
    EXPECT_THROW(hermit::llm::create_lmstudio_chat(cfg, invalid_response_executor), hermit::command_line_exception);
}

TEST(llm_backend_parameters, streams_lmstudio_native_chat_events_as_received)
{
    hermit::config cfg;
    cfg.llm.api_options = R"({"model":"m","input":"hello","stream":true})";
    std::string received;
    const hermit::public_api::http_executor unused_executor{ [](const hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "non-stream executor must not be used";
        return hermit::public_api::http_response_data{ 200, "{}" };
    } };
    const hermit::public_api::http_stream_executor stream_executor{ [](const hermit::public_api::http_request_data& request,
        const hermit::public_api::http_stream_handler& handler)
    {
        EXPECT_EQ(request.target, "/api/v1/chat");
        EXPECT_EQ(request.content_type, "application/json");
        const std::string first{ "event: chat.start\ndata: {\"type\":\"chat.start\"}\n\n" };
        const std::string last{ "event: chat.end\ndata: {\"type\":\"chat.end\"}\n\n" };
        EXPECT_TRUE(handler(first));
        EXPECT_TRUE(handler(last));
        return hermit::public_api::http_stream_response_data{ 200, "text/event-stream" };
    } };
    const hermit::public_api::http_stream_handler writer{ [&received](std::string_view chunk)
    {
        received.append(chunk);
        return true;
    } };
    EXPECT_TRUE(hermit::llm::create_lmstudio_chat(cfg, unused_executor, stream_executor, writer).empty());
    EXPECT_NE(received.find("chat.start"), std::string::npos);
    EXPECT_NE(received.find("chat.end"), std::string::npos);
}

TEST(llm_backend_parameters, rejects_invalid_or_streaming_lmstudio_responses_requests_before_transport)
{
    hermit::config cfg;
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "invalid Responses input must be rejected before transport";
        return hermit::public_api::http_response_data{ 200, R"({"id":"resp-1","output":[]})" };
    } };
    for (const std::string options : { "[]", R"({"input":"hello"})", R"({"model":"m"})",
        R"({"model":"m","input":1})", R"({"model":"m","input":"hello","stream":true})",
        R"({"model":"m","input":"hello","stream":"yes"})" })
    {
        cfg.llm.api_options = options;
        EXPECT_THROW(hermit::llm::create_lmstudio_response(cfg, executor), hermit::command_line_exception) << options;
    }

    cfg.llm.api_options = R"({"model":"m","input":"hello"})";
    const hermit::public_api::http_executor invalid_response_executor{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 200, R"({"id":"resp-1"})" };
    } };
    EXPECT_THROW(hermit::llm::create_lmstudio_response(cfg, invalid_response_executor), hermit::command_line_exception);
}

TEST(llm_backend_parameters, streams_lmstudio_responses_as_received_sse_chunks)
{
    hermit::config cfg;
    cfg.llm.api_options = R"({"model":"m","input":"hello","stream":true})";
    std::string received;
    int chunk_count{};
    const hermit::public_api::http_executor unused_executor{ [](const hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "non-stream executor must not be used";
        return hermit::public_api::http_response_data{ 200, "{}" };
    } };
    const hermit::public_api::http_stream_executor stream_executor{ [&chunk_count](const hermit::public_api::http_request_data& request,
        const hermit::public_api::http_stream_handler& handler)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/responses");
        EXPECT_EQ(request.content_type, "application/json");
        const std::string first{ "event: response.created\ndata: {\"type\":\"response.created\"}\n\n" };
        const std::string second{ "event: response.completed\ndata: {\"type\":\"response.completed\"}\n\n" };
        EXPECT_TRUE(handler(first));
        EXPECT_TRUE(handler(second));
        chunk_count = 2;
        return hermit::public_api::http_stream_response_data{ 200, "text/event-stream" };
    } };
    const hermit::public_api::http_stream_handler writer{ [&received](std::string_view chunk)
    {
        received.append(chunk);
        return true;
    } };
    EXPECT_TRUE(hermit::llm::create_lmstudio_response(cfg, unused_executor, stream_executor, writer).empty());
    EXPECT_EQ(chunk_count, 2);
    EXPECT_NE(received.find("response.created"), std::string::npos);
    EXPECT_NE(received.find("response.completed"), std::string::npos);
}

TEST(llm_backend_parameters, sends_lmstudio_decisions_request_and_validates_answers)
{
    hermit::config cfg;
    cfg.llm.host = "lmstudio-host";
    cfg.llm.port = "1234";
    cfg.llm.api_key = "lm-token";
    cfg.llm.api_options = R"({"model":"decision-model","input":"I was charged twice.","questions":[{"type":"predicate","name":"angry","instructions":"Is the customer angry?"}]})";
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/decisions");
        EXPECT_EQ(request.content_type, "application/json");
        EXPECT_EQ(request.options.authorization, "Bearer lm-token");
        EXPECT_EQ(nlohmann::json::parse(request.body).at("questions").at(0).at("name"), "angry");
        return hermit::public_api::http_response_data{ 200, R"({"model":"decision-model","answers":[{"type":"predicate","name":"angry","probability":0.98}],"usage":{}})" };
    } };
    EXPECT_NE(hermit::llm::create_lmstudio_decision(cfg, executor).find("\"answers\""), std::string::npos);
}

TEST(llm_backend_parameters, rejects_invalid_lmstudio_decisions_before_transport)
{
    hermit::config cfg;
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "invalid Decisions input must be rejected before transport";
        return hermit::public_api::http_response_data{ 200, R"({"answers":[]})" };
    } };
    for (const std::string options : { "[]", R"({"input":"text","questions":[]})",
        R"({"model":"m","input":4,"questions":[{}]})",
        R"({"model":"m","input":"text","questions":[{"type":2,"instructions":"test"}]})" })
    {
        cfg.llm.api_options = options;
        EXPECT_THROW(hermit::llm::create_lmstudio_decision(cfg, executor), hermit::command_line_exception) << options;
    }
    cfg.llm.api_options = R"({"model":"m","input":"text","questions":[{"type":"predicate","instructions":"test"}]})";
    const hermit::public_api::http_executor invalid_response_executor{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 200, R"({"model":"m"})" };
    } };
    EXPECT_THROW(hermit::llm::create_lmstudio_decision(cfg, invalid_response_executor), hermit::command_line_exception);
}

TEST(llm_backend_parameters, sends_lmstudio_anthropic_messages_request_and_validates_response)
{
    hermit::config cfg;
    cfg.llm.host = "lmstudio-host";
    cfg.llm.port = "1234";
    cfg.llm.api_key = "lm-token";
    cfg.llm.api_options = R"({"model":"m","max_tokens":64,"messages":[{"role":"user","content":"hello"}],"stream":false})";
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/messages");
        EXPECT_EQ(request.content_type, "application/json");
        EXPECT_FALSE(request.options.authorization.has_value());
        EXPECT_EQ(request.headers.at("x-api-key"), "lm-token");
        const nlohmann::json sent{ nlohmann::json::parse(request.body) };
        EXPECT_EQ(sent.at("max_tokens"), 64);
        EXPECT_EQ(sent.at("messages").at(0).at("content"), "hello");
        return hermit::public_api::http_response_data{ 200,
            R"({"id":"msg_1","type":"message","role":"assistant","content":[{"type":"text","text":"hello"}],"model":"m","stop_reason":"end_turn"})",
            "application/json" };
    } };
    EXPECT_NE(hermit::llm::create_lmstudio_messages(cfg, executor).find("msg_1"), std::string::npos);
}

TEST(llm_backend_parameters, rejects_invalid_lmstudio_anthropic_messages_request_or_response)
{
    hermit::config cfg;
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "invalid Anthropic Messages request must be rejected before transport";
        return hermit::public_api::http_response_data{ 200, R"({"type":"message","role":"assistant","content":[]})" };
    } };
    for (const std::string options : { "[]", R"({"model":"m","messages":[]})",
        R"({"model":"m","max_tokens":0,"messages":[{"role":"user","content":"x"}]})",
        R"({"model":"m","max_tokens":2,"messages":[{"role":"system","content":"x"}]})",
        R"({"model":"m","max_tokens":2,"messages":[{"role":"user","content":2}]})",
        R"({"model":"m","max_tokens":2,"messages":[{"role":"user","content":"x"}],"stream":"yes"})",
        R"({"model":"m","max_tokens":2,"messages":[{"role":"user","content":"x"}],"stream":true})" })
    {
        cfg.llm.api_options = options;
        EXPECT_THROW(hermit::llm::create_lmstudio_messages(cfg, executor), hermit::command_line_exception) << options;
    }
    cfg.llm.api_options = R"({"model":"m","max_tokens":2,"messages":[{"role":"user","content":"x"}]})";
    const hermit::public_api::http_executor invalid_response{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 200, R"({"type":"not_message","content":[]})" };
    } };
    EXPECT_THROW(hermit::llm::create_lmstudio_messages(cfg, invalid_response), hermit::command_line_exception);
}

TEST(llm_backend_parameters, streams_lmstudio_anthropic_messages_sse_as_received)
{
    hermit::config cfg;
    cfg.llm.api_key = "secret";
    cfg.llm.api_options = R"({"model":"m","max_tokens":10,"messages":[{"role":"user","content":"x"}],"stream":true})";
    std::string received;
    const hermit::public_api::http_executor unused_executor{ [](const hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "streaming must use stream executor";
        return hermit::public_api::http_response_data{ 200, "{}" };
    } };
    const hermit::public_api::http_stream_executor stream_executor{ [](const hermit::public_api::http_request_data& request,
        const hermit::public_api::http_stream_handler& handler)
    {
        EXPECT_EQ(request.target, "/v1/messages");
        EXPECT_EQ(request.headers.at("x-api-key"), "secret");
        const std::string chunk{ "event: message_start\ndata: {\"type\":\"message_start\"}\n\n" };
        EXPECT_TRUE(handler(chunk));
        return hermit::public_api::http_stream_response_data{ 200, "text/event-stream" };
    } };
    const hermit::public_api::http_stream_handler writer{ [&received](std::string_view chunk)
    {
        received.append(chunk);
        return true;
    } };
    EXPECT_TRUE(hermit::llm::create_lmstudio_messages(cfg, unused_executor, stream_executor, writer).empty());
    EXPECT_NE(received.find("message_start"), std::string::npos);
}

TEST(llm_backend_parameters, sends_lmstudio_jev_systemone_request_and_validates_answers)
{
    hermit::config cfg;
    cfg.llm.host = "lmstudio-host";
    cfg.llm.port = "1234";
    cfg.llm.api_key = "jev-token";
    cfg.llm.api_options = R"({"model":"decision-model","state":"charged twice","questions":{"angry":{"type":"noul","instructions":"Is the customer angry?"},"severity":{"type":"score","instructions":"Rate severity","criteria":["low","medium","high"]},"cause":{"type":"choice","instructions":"Choose cause","criteria":{"billing":"billing issue","other":"other"}}}})";
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/systemone");
        EXPECT_EQ(request.content_type, "application/json");
        EXPECT_EQ(request.options.authorization, "Bearer jev-token");
        const nlohmann::json sent{ nlohmann::json::parse(request.body) };
        EXPECT_EQ(sent.at("state"), "charged twice");
        EXPECT_EQ(sent.at("questions").at("cause").at("type"), "choice");
        return hermit::public_api::http_response_data{ 200,
            R"({"answers":{"angry":{"value":true},"severity":{"value":0.8},"cause":{"value":"billing"}}})", "application/json" };
    } };
    EXPECT_NE(hermit::llm::create_lmstudio_systemone(cfg, executor).find("severity"), std::string::npos);
}

TEST(llm_backend_parameters, rejects_invalid_lmstudio_jev_systemone_request_or_response)
{
    hermit::config cfg;
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "invalid System One request must be rejected before transport";
        return hermit::public_api::http_response_data{ 200, R"({"answers":{}})" };
    } };
    for (const std::string options : { "[]", R"({"model":"m","state":"x","questions":[]})",
        R"({"model":"m","state":"x","questions":{"q":{"type":"noul"}}})",
        R"({"model":"m","state":"x","questions":{"q":{"type":"unknown","instructions":"x"}}})",
        R"({"model":"m","state":"x","questions":{"q":{"type":"score","instructions":"x","criteria":[]}}})" })
    {
        cfg.llm.api_options = options;
        EXPECT_THROW(hermit::llm::create_lmstudio_systemone(cfg, executor), hermit::command_line_exception) << options;
    }
    cfg.llm.api_options = R"({"model":"m","state":"x","questions":{"q":{"type":"noul","instructions":"x"}}})";
    const hermit::public_api::http_executor missing_answer{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 200, R"({"answers":{}})" };
    } };
    EXPECT_THROW(hermit::llm::create_lmstudio_systemone(cfg, missing_answer), hermit::command_line_exception);
}

TEST(llm_backend_parameters, sends_vllm_batched_chat_and_checks_choice_count)
{
    hermit::config cfg;
    cfg.llm.host = "vllm-host";
    cfg.llm.port = "8000";
    cfg.llm.api_key = "vllm-token";
    cfg.llm.api_options = R"({"model":"test-model","messages":[[{"role":"user","content":"one"}],[{"role":"user","content":"two"}]],"max_tokens":12})";
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/chat/completions/batch");
        EXPECT_EQ(request.content_type, "application/json");
        EXPECT_EQ(request.options.authorization, "Bearer vllm-token");
        EXPECT_EQ(nlohmann::json::parse(request.body).at("messages").size(), 2U);
        return hermit::public_api::http_response_data{ 200,
            R"({"choices":[{"index":0,"message":{"role":"assistant","content":"first"}},{"index":1,"message":{"role":"assistant","content":"second"}}]})",
            "application/json" };
    } };
    EXPECT_NE(hermit::llm::create_vllm_batch_chat(cfg, executor).find("second"), std::string::npos);
}

TEST(llm_backend_parameters, rejects_invalid_vllm_batch_chat_request_or_response)
{
    hermit::config cfg;
    const hermit::public_api::http_executor unused_executor{ [](const hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "invalid batch chat must be rejected before transport";
        return hermit::public_api::http_response_data{ 200, R"({"choices":[]})" };
    } };
    for (const std::string options : { "[]", R"({"messages":[[{"role":"user","content":"x"}]]})",
        R"({"model":"m","messages":[]})", R"({"model":"m","messages":[[]]})",
        R"({"model":"m","messages":[[{"role":"user","content":2}]]})",
        R"({"model":"m","messages":[[{"role":"user","content":"x"}]],"stream":true})",
        R"({"model":"m","messages":[[{"role":"user","content":"x"}]],"tools":[]})",
        R"({"model":"m","messages":[[{"role":"user","content":"x"}]],"n":2})" })
    {
        cfg.llm.api_options = options;
        EXPECT_THROW(hermit::llm::create_vllm_batch_chat(cfg, unused_executor), hermit::command_line_exception) << options;
    }
    cfg.llm.api_options = R"({"model":"m","messages":[[{"role":"user","content":"x"}],[{"role":"user","content":"y"}]]})";
    const hermit::public_api::http_executor wrong_count{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 200, R"({"choices":[{"index":0}]})" };
    } };
    EXPECT_THROW(hermit::llm::create_vllm_batch_chat(cfg, wrong_count), hermit::command_line_exception);
}

TEST(llm_backend_parameters, sends_tg_moderation_request_and_validates_results)
{
    hermit::config cfg;
    cfg.llm.host = "textgen-host";
    cfg.llm.port = "5000";
    cfg.llm.api_key = "test-key";
    cfg.llm.api_options = R"({"input":["first text","second text"]})";
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/moderations");
        EXPECT_EQ(request.content_type, "application/json");
        EXPECT_EQ(request.options.authorization, "Bearer test-key");
        EXPECT_EQ(nlohmann::json::parse(request.body).at("input").size(), 2U);
        return hermit::public_api::http_response_data{ 200,
            R"({"id":"moderation-1","model":"textgen","results":[{"flagged":false,"categories":{"hate":false},"category_scores":{"hate":0.01}},{"flagged":true,"categories":{"hate":true},"category_scores":{"hate":0.91}}]})",
            "application/json" };
    } };
    EXPECT_NE(hermit::llm::create_tg_moderations(cfg, executor).find("moderation-1"), std::string::npos);
}

TEST(llm_backend_parameters, rejects_invalid_tg_moderation_request_or_response)
{
    hermit::config cfg;
    const hermit::public_api::http_executor unused_executor{ [](const hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "invalid moderation input must be rejected before transport";
        return hermit::public_api::http_response_data{ 200, R"({"results":[]})" };
    } };
    for (const std::string options : { "[]", "{}", R"({"input":""})", R"({"input":[]})",
        R"({"input":["ok",2]})", R"({"input":[""]})" })
    {
        cfg.llm.api_options = options;
        EXPECT_THROW(hermit::llm::create_tg_moderations(cfg, unused_executor), hermit::command_line_exception) << options;
    }
    cfg.llm.api_options = R"({"input":"test"})";
    for (const std::string response : { "{}", R"({"results":[]})", R"({"results":[{}]})",
        R"({"results":[{"flagged":"false","categories":{},"category_scores":{}}]})" })
    {
        const hermit::public_api::http_executor invalid_response{ [&response](const hermit::public_api::http_request_data&)
        {
            return hermit::public_api::http_response_data{ 200, response };
        } };
        EXPECT_THROW(hermit::llm::create_tg_moderations(cfg, invalid_response), hermit::command_line_exception) << response;
    }
}

TEST(llm_backend_parameters, checks_tg_health_and_validates_status)
{
    hermit::config cfg;
    cfg.llm.host = "textgen-host";
    cfg.llm.port = "5000";
    cfg.llm.api_key = "health-key";
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::get);
        EXPECT_EQ(request.target, "/v1/internal/health");
        EXPECT_EQ(request.options.authorization, "Bearer health-key");
        return hermit::public_api::http_response_data{ 200, R"({"status":"ok"})", "application/json" };
    } };
    EXPECT_EQ(hermit::llm::check_tg_health(cfg, executor), R"({"status":"ok"})");

    const hermit::public_api::http_executor invalid_response{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 200, R"({"status":"starting"})" };
    } };
    EXPECT_THROW(hermit::llm::check_tg_health(cfg, invalid_response), hermit::command_line_exception);
}

TEST(llm_backend_parameters, reads_and_validates_tg_current_model_info)
{
    hermit::config cfg;
    cfg.llm.host = "textgen-host";
    cfg.llm.port = "5000";
    cfg.llm.api_key = "model-info-key";
    cfg.timeout_connect = 3;
    cfg.timeout_request = 11;
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::get);
        EXPECT_EQ(request.target, "/v1/internal/model/info");
        EXPECT_EQ(request.body, "");
        EXPECT_EQ(request.options.authorization, "Bearer model-info-key");
        EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 3 });
        EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 11 });
        return hermit::public_api::http_response_data{ 200,
            R"({"model_name":"ExampleModel","lora_names":["style-a","style-b"]})", "application/json" };
    } };
    EXPECT_EQ(hermit::llm::get_tg_model_info(cfg, executor),
        R"({"model_name":"ExampleModel","lora_names":["style-a","style-b"]})");

    const hermit::public_api::http_executor invalid_response{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 200, R"({"model_name":"ExampleModel","lora_names":[1]})" };
    } };
    EXPECT_THROW(hermit::llm::get_tg_model_info(cfg, invalid_response), hermit::command_line_exception);
}

TEST(llm_backend_parameters, stops_tg_generation_with_admin_auth)
{
    hermit::config cfg;
    cfg.llm.host = "textgen-host";
    cfg.llm.port = "5000";
    cfg.llm.api_key = "test-key";
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/internal/stop-generation");
        EXPECT_TRUE(request.body.empty());
        EXPECT_EQ(request.options.authorization, "Bearer test-key");
        return hermit::public_api::http_response_data{ 200, R"("OK")" };
    } };
    EXPECT_EQ(hermit::llm::stop_tg_generation(cfg, executor), R"("OK")");
    const hermit::public_api::http_executor invalid{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 200, R"({"result":"OK"})" };
    } };
    EXPECT_THROW(hermit::llm::stop_tg_generation(cfg, invalid), hermit::command_line_exception);
}

TEST(llm_backend_parameters, requests_and_validates_tg_logits)
{
    hermit::config cfg;
    cfg.llm.host = "textgen-host";
    cfg.llm.port = "5000";
    cfg.llm.api_key = "logits-key";
    cfg.llm.api_options = R"({"prompt":"hello","top_logits":2})";
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/internal/logits");
        EXPECT_EQ(request.content_type, "application/json");
        EXPECT_EQ(request.options.authorization, "Bearer logits-key");
        EXPECT_EQ(nlohmann::json::parse(request.body).at("top_logits"), 2);
        return hermit::public_api::http_response_data{ 200, R"({"logits":{"hello":1.25,"world":-0.5}})" };
    } };
    EXPECT_NE(hermit::llm::get_tg_logits(cfg, executor).find("logits"), std::string::npos);
    for (const std::string options : { "[]", "{}", R"({"prompt":""})", R"({"prompt":3})" })
    {
        cfg.llm.api_options = options;
        EXPECT_THROW(hermit::llm::get_tg_logits(cfg, executor), hermit::command_line_exception) << options;
    }
    cfg.llm.api_options = R"({"prompt":"hello"})";
    const hermit::public_api::http_executor invalid{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 200, R"({"logits":{"hello":"1.0"}})" };
    } };
    EXPECT_THROW(hermit::llm::get_tg_logits(cfg, invalid), hermit::command_line_exception);
}

TEST(llm_backend_parameters, requests_and_validates_tg_chat_prompt)
{
    hermit::config cfg;
    cfg.llm.host = "textgen-host";
    cfg.llm.port = "5000";
    cfg.llm.api_key = "prompt-key";
    cfg.llm.api_options = R"({"messages":[{"role":"user","content":"hello"}]})";
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/internal/chat-prompt");
        EXPECT_EQ(request.content_type, "application/json");
        EXPECT_EQ(request.options.authorization, "Bearer prompt-key");
        EXPECT_EQ(nlohmann::json::parse(request.body).at("messages").size(), 1U);
        return hermit::public_api::http_response_data{ 200, R"({"prompt":"formatted prompt"})" };
    } };
    EXPECT_EQ(hermit::llm::create_tg_chat_prompt(cfg, executor), R"({"prompt":"formatted prompt"})");
    for (const std::string options : { "[]", "{}", R"({"messages":[]})", R"({"messages":["user"]})" })
    {
        cfg.llm.api_options = options;
        EXPECT_THROW(hermit::llm::create_tg_chat_prompt(cfg, executor), hermit::command_line_exception) << options;
    }
    cfg.llm.api_options = R"({"messages":[{}]})";
    const hermit::public_api::http_executor invalid{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 200, R"({"prompt":5})" };
    } };
    EXPECT_THROW(hermit::llm::create_tg_chat_prompt(cfg, invalid), hermit::command_line_exception);
}

TEST(llm_backend_parameters, lists_tg_loras_with_admin_auth_and_validates_response)
{
    hermit::config cfg;
    cfg.llm.host = "textgen-host";
    cfg.llm.port = "5000";
    cfg.llm.api_key = "fallback-key";
    cfg.llm.admin_api_key = "admin-key";
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::get);
        EXPECT_EQ(request.target, "/v1/internal/lora/list");
        EXPECT_EQ(request.options.authorization, "Bearer admin-key");
        return hermit::public_api::http_response_data{ 200, R"({"lora_names":["style-a","style-b"]})" };
    } };
    EXPECT_EQ(hermit::llm::list_tg_loras(cfg, executor), R"({"lora_names":["style-a","style-b"]})");
    const hermit::public_api::http_executor invalid{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 200, R"({"lora_names":[1]})" };
    } };
    EXPECT_THROW(hermit::llm::list_tg_loras(cfg, invalid), hermit::command_line_exception);
}

TEST(llm_backend_parameters, loads_and_unloads_tg_loras_with_admin_auth)
{
    hermit::config cfg;
    cfg.llm.host = "textgen-host";
    cfg.llm.port = "5000";
    cfg.llm.admin_api_key = "admin-key";
    cfg.llm.model_action = "load";
    cfg.llm.model_action_options = R"({"lora_names":["style-a","style-b"]})";
    const hermit::public_api::http_executor load_executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/internal/lora/load");
        EXPECT_EQ(request.content_type, "application/json");
        EXPECT_EQ(request.options.authorization, "Bearer admin-key");
        EXPECT_EQ(nlohmann::json::parse(request.body).at("lora_names").size(), 2U);
        return hermit::public_api::http_response_data{ 200, R"("OK")" };
    } };
    EXPECT_EQ(hermit::llm::manage_tg_loras(cfg, load_executor), R"("OK")");
    cfg.llm.model_action = "unload";
    cfg.llm.model_action_options.clear();
    const hermit::public_api::http_executor unload_executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/internal/lora/unload");
        EXPECT_TRUE(request.body.empty());
        EXPECT_EQ(request.options.authorization, "Bearer admin-key");
        return hermit::public_api::http_response_data{ 200, R"("OK")" };
    } };
    EXPECT_EQ(hermit::llm::manage_tg_loras(cfg, unload_executor), R"("OK")");
    cfg.llm.model_action = "load";
    for (const std::string options : { "{}", R"({"lora_names":[3]})" })
    {
        cfg.llm.model_action_options = options;
        EXPECT_THROW(hermit::llm::manage_tg_loras(cfg, load_executor), hermit::command_line_exception) << options;
    }
    cfg.llm.model_action_options = R"({"lora_names":[]})";
    const hermit::public_api::http_executor empty_load_executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_TRUE(nlohmann::json::parse(request.body).at("lora_names").empty());
        return hermit::public_api::http_response_data{ 200, R"("OK")" };
    } };
    EXPECT_EQ(hermit::llm::manage_tg_loras(cfg, empty_load_executor), R"("OK")");
}

TEST(llm_backend_parameters, sends_generic_backend_api_request_with_method_body_auth_and_headers)
{
    hermit::config cfg;
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
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.host, "backend-host");
        EXPECT_EQ(request.port, "8080");
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/responses");
        EXPECT_EQ(nlohmann::json::parse(request.body), nlohmann::json::parse(R"({"model":"test-model","input":"hello"})"));
        EXPECT_EQ(request.content_type, "application/json");
        EXPECT_EQ(request.options.authorization, "Bearer test-token");
        EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 2 });
        EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 7 });
        EXPECT_EQ(request.headers.at("X-Trace"), "trace-1");
        return hermit::public_api::http_response_data{ 200, R"({"id":"resp-1"})", "application/json", false };
    } };
    const hermit::public_api::http_response_data response{ hermit::llm::make_api_request(cfg, executor) };
    EXPECT_EQ(response.body, R"({"id":"resp-1"})");
}

TEST(llm_backend_parameters, rejects_unsafe_generic_backend_api_requests_before_transport)
{
    hermit::config cfg;
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "invalid request must be rejected before transport";
        return hermit::public_api::http_response_data{ 200, "{}" };
    } };
    for (const std::string path : { "", "relative", "//remote/path", "/../admin", "/api/%2e%2e/admin", "/api#fragment", "/api\\admin" })
    {
        cfg.llm.api_path = path;
        EXPECT_THROW(hermit::llm::make_api_request(cfg, executor), std::invalid_argument) << path;
    }
    cfg.llm.api_path = "/v1/responses";
    cfg.llm.api_method = "TRACE";
    EXPECT_THROW(hermit::llm::make_api_request(cfg, executor), std::invalid_argument);
    cfg.llm.api_method = "POST";
    cfg.llm.api_options = "[]";
    cfg.llm.api_body_file = "payload.bin";
    EXPECT_THROW(hermit::llm::make_api_request(cfg, executor), std::invalid_argument);
    cfg.llm.api_options.clear();
    cfg.llm.api_body_file.clear();
    cfg.llm.api_headers = R"({"Host":"remote.example"})";
    EXPECT_THROW(hermit::llm::make_api_request(cfg, executor), std::invalid_argument);
}

TEST(llm_backend_parameters, model_list_rejects_http_errors)
{
    hermit::config cfg;
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 401, R"({"error":"unauthorized"})" };
    } };

    EXPECT_THROW(hermit::llm::list_models(cfg, executor), hermit::http_status_exception);
}

TEST(llm_backend_parameters, creates_openai_compatible_embeddings)
{
    hermit::config cfg;
    cfg.llm.backend_name = "vllm";
    cfg.llm.host = "embedding-host";
    cfg.llm.port = "8000";
    cfg.llm.model = "embed-model";
    cfg.llm.prompt = "text to embed";
    cfg.llm.embedding_target = "/v1/embeddings";
    cfg.llm.api_key = "secret-token";
    cfg.timeout_connect = 2;
    cfg.timeout_request = 9;
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
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
        return hermit::public_api::http_response_data{ 200, R"({"data":[{"embedding":[0.1]}]})", "application/json" };
    } };

    EXPECT_EQ(hermit::llm::create_embeddings(cfg, executor), R"({"data":[{"embedding":[0.1]}]})");
}

TEST(llm_backend_parameters, creates_ollama_embeddings_without_openai_fields)
{
    hermit::config cfg;
    cfg.llm.backend_name = "ollama";
    cfg.llm.port = "11434";
    cfg.llm.model = "embeddinggemma";
    cfg.llm.prompt = "text to embed";
    cfg.llm.embedding_target = "/api/embed";
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
        EXPECT_EQ(request.port, "11434");
        EXPECT_EQ(request.target, "/api/embed");
        const nlohmann::json body{ nlohmann::json::parse(request.body) };
        EXPECT_EQ(body.at("input"), "text to embed");
        EXPECT_EQ(body.at("model"), "embeddinggemma");
        EXPECT_FALSE(body.contains("encoding_format"));
        return hermit::public_api::http_response_data{ 200, R"({"embeddings":[[0.2]]})", "application/json" };
    } };

    EXPECT_EQ(hermit::llm::create_embeddings(cfg, executor), R"({"embeddings":[[0.2]]})");
}

TEST(llm_backend_parameters, creates_batch_embeddings_with_backend_options)
{
    hermit::config cfg;
    cfg.llm.backend_name = "ollama";
    cfg.llm.model = "embeddinggemma";
    cfg.llm.embedding_target = "/api/embed";
    cfg.llm.embedding_inputs = { "first", "second" };
    cfg.llm.embedding_options = R"({"truncate":false,"dimensions":256,"keep_alive":"5m"})";
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        const nlohmann::json body{ nlohmann::json::parse(request.body) };
        EXPECT_EQ(body.at("input"), nlohmann::json({ "first", "second" }));
        EXPECT_EQ(body.at("model"), "embeddinggemma");
        EXPECT_FALSE(body.at("truncate"));
        EXPECT_EQ(body.at("dimensions"), 256);
        EXPECT_EQ(body.at("keep_alive"), "5m");
        return hermit::public_api::http_response_data{ 200, R"({"embeddings":[[0.1],[0.2]]})", "application/json" };
    } };

    EXPECT_EQ(hermit::llm::create_embeddings(cfg, executor), R"({"embeddings":[[0.1],[0.2]]})");
}

TEST(llm_backend_parameters, embedding_options_must_be_object_and_cannot_replace_required_fields)
{
    hermit::config cfg;
    cfg.llm.backend_name = "vllm";
    cfg.llm.model = "embed-model";
    cfg.llm.embedding_options = R"({"input":"override"})";
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 200, "{}" };
    } };
    EXPECT_THROW(hermit::llm::create_embeddings(cfg, executor), hermit::command_line_exception);

    cfg.llm.embedding_options = "[]";
    EXPECT_THROW(hermit::llm::create_embeddings(cfg, executor), hermit::command_line_exception);
}

TEST(llm_backend_parameters, tokenizes_with_llama_cpp_options_auth_and_timeouts)
{
    hermit::config cfg;
    cfg.llm.host = "token-host";
    cfg.llm.port = "8080";
    cfg.llm.tokenize_target = "/tokenize";
    cfg.llm.prompt = "hello";
    cfg.llm.api_key = "token-key";
    cfg.llm.tokenize_options = R"({"add_special":true,"parse_special":false,"with_pieces":true})";
    cfg.timeout_connect = 4;
    cfg.timeout_request = 12;
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/tokenize");
        EXPECT_EQ(request.options.authorization, "Bearer token-key");
        EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 4 });
        EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 12 });
        const nlohmann::json body{ nlohmann::json::parse(request.body) };
        EXPECT_EQ(body.at("content"), "hello");
        EXPECT_TRUE(body.at("add_special"));
        EXPECT_FALSE(body.at("parse_special"));
        EXPECT_TRUE(body.at("with_pieces"));
        return hermit::public_api::http_response_data{ 200, R"({"tokens":[1,2]})", "application/json" };
    } };
    EXPECT_EQ(hermit::llm::tokenize(cfg, executor), R"({"tokens":[1,2]})");
}

TEST(llm_backend_parameters, detokenizes_token_ids_and_rejects_http_errors)
{
    hermit::config cfg;
    cfg.llm.detokenize_target = "/detokenize";
    cfg.llm.token_ids = { 1, 2, 3 };
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        const nlohmann::json body{ nlohmann::json::parse(request.body) };
        EXPECT_EQ(body.at("tokens"), nlohmann::json({ 1, 2, 3 }));
        return hermit::public_api::http_response_data{ 200, R"({"content":"hello"})", "application/json" };
    } };
    EXPECT_EQ(hermit::llm::detokenize(cfg, executor), R"({"content":"hello"})");

    const hermit::public_api::http_executor error_executor{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 500, "failure" };
    } };
    EXPECT_THROW(hermit::llm::detokenize(cfg, error_executor), hermit::http_status_exception);
}

TEST(llm_backend_parameters, encodes_and_decodes_text_generation_web_ui_tokens)
{
    hermit::config cfg;
    cfg.llm.host = "tg-host";
    cfg.llm.port = "5000";
    cfg.llm.prompt = "hello";
    cfg.llm.api_key = "tg-key";
    cfg.llm.token_ids = { 7, 8 };
    cfg.timeout_connect = 2;
    cfg.timeout_request = 9;
    const hermit::public_api::http_executor encode_executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.target, "/v1/internal/encode");
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
        EXPECT_EQ(request.content_type, "application/json");
        EXPECT_EQ(request.options.authorization, "Bearer tg-key");
        EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 2 });
        EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 9 });
        EXPECT_EQ(nlohmann::json::parse(request.body), nlohmann::json({ { "text", "hello" } }));
        return hermit::public_api::http_response_data{ 200, R"({"tokens":[7,8],"length":2})", "application/json" };
    } };
    EXPECT_EQ(hermit::llm::encode_tg_tokens(cfg, encode_executor), R"({"tokens":[7,8],"length":2})");

    const hermit::public_api::http_executor decode_executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.target, "/v1/internal/decode");
        EXPECT_EQ(nlohmann::json::parse(request.body), nlohmann::json({ { "tokens", { 7, 8 } } }));
        return hermit::public_api::http_response_data{ 200, R"({"text":"hello"})", "application/json" };
    } };
    EXPECT_EQ(hermit::llm::decode_tg_tokens(cfg, decode_executor), R"({"text":"hello"})");
}

TEST(llm_backend_parameters, rejects_malformed_text_generation_web_ui_token_responses)
{
    hermit::config cfg;
    const hermit::public_api::http_executor invalid_encode{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 200, R"({"tokens":[1],"length":2})" };
    } };
    EXPECT_THROW(hermit::llm::encode_tg_tokens(cfg, invalid_encode), hermit::command_line_exception);

    const hermit::public_api::http_executor invalid_decode{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 200, R"({"tokens":[1]})" };
    } };
    EXPECT_THROW(hermit::llm::decode_tg_tokens(cfg, invalid_decode), hermit::command_line_exception);
}

TEST(llm_backend_parameters, embedding_requires_model_and_rejects_http_errors)
{
    hermit::config cfg;
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 503, "unavailable" };
    } };

    EXPECT_THROW(hermit::llm::create_embeddings(cfg, executor), hermit::command_line_exception);
    cfg.llm.model = "embed-model";
    EXPECT_THROW(hermit::llm::create_embeddings(cfg, executor), hermit::http_status_exception);
}

TEST(llm_backend_parameters, builds_and_parses_openai_compatible_requests)
{
    hermit::tg_parameters parameters;
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
    hermit::kc_parameters parameters;
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
    hermit::kc_parameters parameters;
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
    hermit::tg_parameters tg;
    hermit::kc_parameters kc;
    EXPECT_EQ(tg.get_request_for_token_count("hello").at("text"), "hello");
    EXPECT_EQ(kc.get_request_for_token_count("hello").at("prompt"), "hello");
    EXPECT_THROW(tg.parse_response_for_completions("not-json"), std::exception);
    EXPECT_THROW(kc.parse_response_for_token_count(R"({"value":"not-an-int"})"), std::exception);
}

TEST(llm_backend_parameters, sends_tg_token_count_to_internal_route_with_api_auth)
{
    hermit::config cfg;
    cfg.llm.backend = &cfg.tg;
    cfg.llm.backend_name = "tg";
    cfg.llm.host = "textgen-host";
    cfg.llm.port = "5000";
    cfg.llm.token_count_target = "/v1/internal/token-count";
    cfg.llm.api_key = "api-key";
    cfg.timeout_connect = 2;
    cfg.timeout_request = 9;

    const hermit::public_api::http_executor http_executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/v1/internal/token-count");
        EXPECT_EQ(request.content_type, "application/json");
        EXPECT_EQ(request.options.authorization, "Bearer api-key");
        EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 2 });
        EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 9 });
        EXPECT_EQ(nlohmann::json::parse(request.body).at("text"), "hello");
        return hermit::public_api::http_response_data{ 200, R"({"length":5})", "application/json" };
    } };
    EXPECT_EQ(hermit::llm::send_token_count_request(cfg, "hello", http_executor), 5);

    const hermit::tcp::request_executor tcp_executor{ [](std::string_view host, std::string_view port,
        const hermit::tcp::request_type& request)
    {
        EXPECT_EQ(host, "textgen-host");
        EXPECT_EQ(port, "5000");
        EXPECT_EQ(request.method(), boost::beast::http::verb::post);
        EXPECT_EQ(request.target(), "/v1/internal/token-count");
        EXPECT_EQ(request[boost::beast::http::field::authorization], "Bearer api-key");
        EXPECT_EQ(nlohmann::json::parse(request.body()).at("text"), "world");
        hermit::tcp::response_type response{ boost::beast::http::status::ok, 11 };
        response.body() = R"({"length":7})";
        response.prepare_payload();
        return response;
    } };
    EXPECT_EQ(hermit::llm::send_token_count_request(cfg, "world", tcp_executor), 7);
}

TEST(llm_backend_parameters, rejects_responses_with_missing_required_fields)
{
    hermit::tg_parameters tg;
    hermit::kc_parameters kc;
    EXPECT_THROW(tg.parse_response_for_completions(R"({"choices":[]})"), std::exception);
    EXPECT_THROW(tg.parse_response_for_chat_completions(R"({"choices":[{}]})"), std::exception);
    EXPECT_THROW(kc.parse_response_for_completions(R"({"results":[]})"), std::exception);
    EXPECT_THROW(kc.parse_response_for_chat_completions(R"({"choices":[{"message":{}}]})"), std::exception);
}

TEST(llm_backend_parameters, builds_and_parses_chat_requests)
{
    const nlohmann::json messages{ nlohmann::json::array({ {{"role", "user"}, {"content", "hello"}} }) };

    hermit::tg_parameters tg;
    tg.common.max_tokens = 32;
    const nlohmann::json tg_request{ tg.get_request_for_chat_completions(messages) };
    EXPECT_EQ(tg_request.at("messages"), messages);
    EXPECT_EQ(tg_request.at("max_tokens"), 32);
    EXPECT_EQ(tg.parse_response_for_chat_completions(R"({"choices":[{"message":{"content":"reply"}}]})"), "reply");

    hermit::kc_parameters kc;
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
    hermit::config cfg;
    hermit::tg_parameters parameters;
    const nlohmann::json messages{ nlohmann::json::array() };
    const hermit::tcp::request_executor tcp_executor{ [](std::string_view, std::string_view, const hermit::tcp::request_type&)
    {
        return hermit::tcp::response_type{ boost::beast::http::status::internal_server_error, 0 };
    } };
    const hermit::public_api::http_executor http_executor{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 500, "server error" };
    } };

    EXPECT_THROW(hermit::llm::send_completions_request(cfg, "prompt", parameters, 16, tcp_executor), hermit::http_status_exception);
    EXPECT_THROW(hermit::llm::send_chat_completions_request(cfg, parameters, messages, tcp_executor), hermit::http_status_exception);
    EXPECT_THROW(hermit::llm::send_completions_request(cfg, "prompt", parameters, 16, http_executor), hermit::http_status_exception);
    EXPECT_THROW(hermit::llm::send_chat_completions_request(cfg, parameters, messages, http_executor), hermit::http_status_exception);
}
