#include <chrono>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <utility>

#include <boost/algorithm/string.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/nowide/iostream.hpp>
#include <boost/url.hpp>
#include "nlohmann/json.hpp"

#include "Hermit_builtin.hpp"
#include "Hermit_config.hpp"
#include "Hermit_errors.hpp"
#include "Hermit_filesystem.hpp"
#include "Hermit_llm.hpp"
#include "Hermit_log.hpp"
#include "Hermit_public_http_adapter.hpp"
#include "Hermit_runtime.hpp"
#include "Hermit_string_utils.hpp"
#include "Hermit_tcp.hpp"
#include "Hermit_templates.hpp"

namespace Hermit
{
    namespace llm
    {
        std::string list_models(const config& cfg, const public_api::http_executor& executor)
        {
            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            if (!cfg.llm.api_key.empty()) options.authorization = "Bearer " + cfg.llm.api_key;

            public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.llm.host, cfg.llm.port, cfg.llm.models_target, {}, std::move(options)) };
            request.method = public_api::http_method::get;
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            return response.body;
        }

        std::string list_running_models(const config& cfg, const public_api::http_executor& executor)
        {
            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            if (!cfg.llm.api_key.empty()) options.authorization = "Bearer " + cfg.llm.api_key;
            public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.llm.host, cfg.llm.port, cfg.llm.running_models_target, {}, std::move(options)) };
            request.method = public_api::http_method::get;
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            return response.body;
        }

        std::string get_ollama_version(const config& cfg, const public_api::http_executor& executor)
        {
            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            if (!cfg.llm.api_key.empty()) options.authorization = "Bearer " + cfg.llm.api_key;
            public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.llm.host, cfg.llm.port, "/api/version", {}, std::move(options)) };
            request.method = public_api::http_method::get;
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            return response.body;
        }

        std::string show_model(const config& cfg, const public_api::http_executor& executor)
        {
            if (cfg.llm.model.empty())
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{ "models show requires --llm.model" });
            }
            nlohmann::json body{ { "model", cfg.llm.model } };
            if (!cfg.llm.model_info_options.empty())
            {
                const nlohmann::json extra_options{ nlohmann::json::parse(cfg.llm.model_info_options) };
                if (!extra_options.is_object() || extra_options.contains("model"))
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "--llm.model-info-options must be a JSON object without the reserved model field" });
                }
                body.update(extra_options);
            }
            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            if (!cfg.llm.api_key.empty()) options.authorization = "Bearer " + cfg.llm.api_key;
            const public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.llm.host, cfg.llm.port, cfg.llm.model_info_target, body.dump(), std::move(options)) };
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            return response.body;
        }

        std::string manage_ollama_model(const config& cfg, const public_api::http_executor& executor)
        {
            const nlohmann::json body{ nlohmann::json::parse(cfg.llm.model_action_options) };
            if (!body.is_object())
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "--llm.model-action-options must be a JSON object" });
            }

            auto require_string = [&body](std::string_view key)
            {
                if (!body.contains(key) || !body.at(key).is_string() || body.at(key).get<std::string>().empty())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        std::string{ "model-action-options requires a non-empty string field: " } + std::string{ key } });
                }
            };

            public_api::http_request_data request;
            if (cfg.llm.model_action == "create")
            {
                require_string("model");
                request = public_api::make_http_request_data(cfg.llm.host, cfg.llm.port, "/api/create", body.dump());
            }
            else if (cfg.llm.model_action == "copy")
            {
                require_string("source");
                require_string("destination");
                request = public_api::make_http_request_data(cfg.llm.host, cfg.llm.port, "/api/copy", body.dump());
            }
            else if (cfg.llm.model_action == "delete")
            {
                require_string("model");
                request = public_api::make_http_request_data(cfg.llm.host, cfg.llm.port, "/api/delete", body.dump());
                request.method = public_api::http_method::delete_;
            }
            else if (cfg.llm.model_action == "pull" || cfg.llm.model_action == "push")
            {
                require_string("model");
                const std::string target{ cfg.llm.model_action_target.empty()
                    ? cfg.llm.model_action == "pull" ? "/api/pull" : "/api/push"
                    : cfg.llm.model_action_target };
                request = public_api::make_http_request_data(cfg.llm.host, cfg.llm.port, target, body.dump());
            }
            else
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "unsupported Ollama model action: " + cfg.llm.model_action });
            }

            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            if (!cfg.llm.api_key.empty()) options.authorization = "Bearer " + cfg.llm.api_key;
            request.options = std::move(options);
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            return response.body;
        }

        std::string manage_lmstudio_model(const config& cfg, const public_api::http_executor& executor)
        {
            nlohmann::json body{ nlohmann::json::parse(cfg.llm.model_action_options) };
            if (!body.is_object())
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "--llm.model-action-options must be a JSON object" });
            }

            public_api::http_request_data request;
            if (cfg.llm.model_action == "load" || cfg.llm.model_action == "download")
            {
                if (!body.contains("model") || !body.at("model").is_string() || body.at("model").get<std::string>().empty())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "LM Studio model load/download options require a non-empty model field" });
                }
                request = public_api::make_http_request_data(cfg.llm.host, cfg.llm.port,
                    cfg.llm.model_action == "load" ? "/api/v1/models/load" : "/api/v1/models/download", body.dump());
            }
            else if (cfg.llm.model_action == "unload")
            {
                if (!body.contains("instance_id") || !body.at("instance_id").is_string()
                    || body.at("instance_id").get<std::string>().empty())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "LM Studio model unload options require a non-empty instance_id field" });
                }
                request = public_api::make_http_request_data(cfg.llm.host, cfg.llm.port,
                    "/api/v1/models/unload", body.dump());
            }
            else if (cfg.llm.model_action == "download-status")
            {
                if (!body.contains("job_id") || !body.at("job_id").is_string()
                    || body.at("job_id").get<std::string>().empty() || body.size() != 1)
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "LM Studio download-status options require only a non-empty job_id field" });
                }
                const std::string job_id{ body.at("job_id").get<std::string>() };
                if (job_id == "." || job_id == "..")
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "LM Studio download-status job_id must not be a dot path segment" });
                }
                boost::urls::url url{};
                url.set_path("/");
                url.segments().push_back("api");
                url.segments().push_back("v1");
                url.segments().push_back("models");
                url.segments().push_back("download");
                url.segments().push_back("status");
                url.segments().push_back(job_id);
                request = public_api::make_http_request_data(cfg.llm.host, cfg.llm.port, url.encoded_path(), {});
                request.method = public_api::http_method::get;
            }
            else
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "unsupported LM Studio model action: " + cfg.llm.model_action });
            }

            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            if (!cfg.llm.api_key.empty()) options.authorization = "Bearer " + cfg.llm.api_key;
            request.options = std::move(options);
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            return response.body;
        }

        std::string create_lmstudio_chat(const config& cfg, const public_api::http_executor& executor)
        {
            const nlohmann::json body{ nlohmann::json::parse(cfg.llm.api_options) };
            if (!body.is_object() || !body.contains("model") || !body.at("model").is_string()
                || body.at("model").get<std::string>().empty() || !body.contains("input")
                || (!body.at("input").is_string() && !body.at("input").is_array()))
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "LM Studio native chat requires a non-empty model and string or array input" });
            }
            if (body.contains("stream") && !body.at("stream").is_boolean())
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "LM Studio native chat stream must be a boolean" });
            }
            if (body.value("stream", false))
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "LM Studio native chat streaming requires the streaming executor" });
            }
            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            if (!cfg.llm.api_key.empty()) options.authorization = "Bearer " + cfg.llm.api_key;
            public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.llm.host, cfg.llm.port, "/api/v1/chat", body.dump(), std::move(options)) };
            request.content_type = "application/json";
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            const nlohmann::json response_json{ nlohmann::json::parse(response.body) };
            if (!response_json.is_object() || !response_json.contains("model_instance_id")
                || !response_json.at("model_instance_id").is_string() || !response_json.contains("output")
                || !response_json.at("output").is_array())
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "LM Studio native chat API returned an invalid response object" });
            }
            return response.body;
        }

        std::string create_lmstudio_chat(const config& cfg, const public_api::http_executor& executor,
            const public_api::http_stream_executor& stream_executor, const public_api::http_stream_handler& handler)
        {
            const nlohmann::json body{ nlohmann::json::parse(cfg.llm.api_options) };
            if (body.is_object() && body.contains("stream") && body.at("stream").is_boolean() && body.at("stream").get<bool>())
            {
                if (!body.contains("model") || !body.at("model").is_string() || body.at("model").get<std::string>().empty()
                    || !body.contains("input") || (!body.at("input").is_string() && !body.at("input").is_array()))
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "LM Studio native chat streaming requires model and string or array input" });
                }
                if (!stream_executor || !handler) throw std::invalid_argument{ "LM Studio native chat stream executor and handler are required" };
                public_api::http_transport_options options;
                options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
                options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
                if (!cfg.llm.api_key.empty()) options.authorization = "Bearer " + cfg.llm.api_key;
                public_api::http_request_data request{ public_api::make_http_request_data(
                    cfg.llm.host, cfg.llm.port, "/api/v1/chat", body.dump(), std::move(options)) };
                request.content_type = "application/json";
                const public_api::http_stream_response_data response{ stream_executor(request, handler) };
                public_api::ensure_success({ response.status_code, {}, response.content_type, false });
                if (response.content_type.find("text/event-stream") == std::string::npos)
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "LM Studio native chat streaming API did not return text/event-stream" });
                }
                return {};
            }
            return create_lmstudio_chat(cfg, executor);
        }

        std::string create_lmstudio_response(const config& cfg, const public_api::http_executor& executor)
        {
            const nlohmann::json body{ nlohmann::json::parse(cfg.llm.api_options) };
            if (!body.is_object() || !body.contains("model") || !body.at("model").is_string()
                || body.at("model").get<std::string>().empty() || !body.contains("input")
                || (!body.at("input").is_string() && !body.at("input").is_array()))
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "LM Studio Responses request requires a non-empty model and string or array input" });
            }
            if (body.contains("stream") && !body.at("stream").is_boolean())
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "LM Studio Responses stream must be a boolean" });
            }
            if (body.value("stream", false))
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "LM Studio Responses streaming is not supported by this command" });
            }
            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            if (!cfg.llm.api_key.empty()) options.authorization = "Bearer " + cfg.llm.api_key;
            public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.llm.host, cfg.llm.port, "/v1/responses", body.dump(), std::move(options)) };
            request.content_type = "application/json";
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            const nlohmann::json response_json{ nlohmann::json::parse(response.body) };
            if (!response_json.is_object() || !response_json.contains("id") || !response_json.at("id").is_string()
                || !response_json.contains("output") || !response_json.at("output").is_array())
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "LM Studio Responses API returned an invalid response object" });
            }
            return response.body;
        }

        public_api::http_stream_response_data create_lmstudio_response_stream(const config& cfg,
            const public_api::http_stream_executor& executor, const public_api::http_stream_handler& handler)
        {
            const nlohmann::json body{ nlohmann::json::parse(cfg.llm.api_options) };
            if (!body.is_object() || !body.contains("model") || !body.at("model").is_string()
                || body.at("model").get<std::string>().empty() || !body.contains("input")
                || (!body.at("input").is_string() && !body.at("input").is_array())
                || !body.contains("stream") || !body.at("stream").is_boolean() || !body.at("stream").get<bool>())
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "LM Studio Responses streaming requires model, string or array input, and stream:true" });
            }
            if (!executor || !handler) throw std::invalid_argument{ "LM Studio Responses stream executor and handler are required" };
            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            if (!cfg.llm.api_key.empty()) options.authorization = "Bearer " + cfg.llm.api_key;
            public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.llm.host, cfg.llm.port, "/v1/responses", body.dump(), std::move(options)) };
            request.content_type = "application/json";
            const public_api::http_stream_response_data response{ executor(request, handler) };
            public_api::ensure_success({ response.status_code, {}, response.content_type, false });
            if (response.content_type.find("text/event-stream") == std::string::npos)
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "LM Studio Responses streaming API did not return text/event-stream" });
            }
            return response;
        }

        std::string create_lmstudio_response(const config& cfg, const public_api::http_executor& executor,
            const public_api::http_stream_executor& stream_executor, const public_api::http_stream_handler& handler)
        {
            const nlohmann::json body{ nlohmann::json::parse(cfg.llm.api_options) };
            if (body.is_object() && body.contains("stream") && body.at("stream").is_boolean() && body.at("stream").get<bool>())
            {
                create_lmstudio_response_stream(cfg, stream_executor, handler);
                return {};
            }
            return create_lmstudio_response(cfg, executor);
        }

        std::string create_lmstudio_decision(const config& cfg, const public_api::http_executor& executor)
        {
            const nlohmann::json body{ nlohmann::json::parse(cfg.llm.api_options) };
            if (!body.is_object() || !body.contains("model") || !body.at("model").is_string()
                || body.at("model").get<std::string>().empty() || !body.contains("input")
                || (!body.at("input").is_string() && !body.at("input").is_array())
                || !body.contains("questions") || !body.at("questions").is_array()
                || body.at("questions").empty())
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "LM Studio Decisions request requires a non-empty model, string or array input, and non-empty questions array" });
            }
            const nlohmann::json& questions{ body.at("questions") };
            if (std::any_of(questions.begin(), questions.end(), [](const nlohmann::json& question)
                { return !question.is_object() || !question.contains("type") || !question.at("type").is_string()
                    || !question.contains("instructions") || (!question.at("instructions").is_string() && !question.at("instructions").is_array()); }))
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "LM Studio Decisions questions require type and string or array instructions" });
            }
            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            if (!cfg.llm.api_key.empty()) options.authorization = "Bearer " + cfg.llm.api_key;
            public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.llm.host, cfg.llm.port, "/v1/decisions", body.dump(), std::move(options)) };
            request.content_type = "application/json";
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            const nlohmann::json response_json{ nlohmann::json::parse(response.body) };
            if (!response_json.is_object() || !response_json.contains("answers") || !response_json.at("answers").is_array())
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "LM Studio Decisions API returned an invalid response object" });
            }
            return response.body;
        }

        std::string create_lmstudio_messages(const config& cfg, const public_api::http_executor& executor)
        {
            const nlohmann::json body{ nlohmann::json::parse(cfg.llm.api_options) };
            if (!body.is_object() || !body.contains("model") || !body.at("model").is_string()
                || body.at("model").get<std::string>().empty() || !body.contains("max_tokens")
                || !body.at("max_tokens").is_number_integer() || body.at("max_tokens").get<std::int64_t>() <= 0
                || !body.contains("messages") || !body.at("messages").is_array() || body.at("messages").empty())
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "LM Studio Anthropic Messages requires model, positive max_tokens, and a non-empty messages array" });
            }
            for (const nlohmann::json& message : body.at("messages"))
            {
                if (!message.is_object() || !message.contains("role") || !message.at("role").is_string()
                    || (message.at("role") != "user" && message.at("role") != "assistant")
                    || !message.contains("content") || (!message.at("content").is_string() && !message.at("content").is_array()))
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "LM Studio Anthropic Messages entries require user/assistant role and string or array content" });
                }
            }
            if (body.contains("stream") && !body.at("stream").is_boolean())
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "LM Studio Anthropic Messages stream must be a boolean" });
            }
            if (body.value("stream", false))
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "LM Studio Anthropic Messages streaming requires the streaming executor" });
            }
            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.llm.host, cfg.llm.port, "/v1/messages", body.dump(), std::move(options)) };
            request.content_type = "application/json";
            if (!cfg.llm.api_key.empty()) request.headers.emplace("x-api-key", cfg.llm.api_key);
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            const nlohmann::json response_json{ nlohmann::json::parse(response.body) };
            if (!response_json.is_object() || !response_json.contains("type") || response_json.at("type") != "message"
                || !response_json.contains("role") || response_json.at("role") != "assistant"
                || !response_json.contains("content") || !response_json.at("content").is_array())
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "LM Studio Anthropic Messages API returned an invalid response object" });
            }
            return response.body;
        }

        std::string create_lmstudio_messages(const config& cfg, const public_api::http_executor& executor,
            const public_api::http_stream_executor& stream_executor, const public_api::http_stream_handler& handler)
        {
            const nlohmann::json body{ nlohmann::json::parse(cfg.llm.api_options) };
            if (body.is_object() && body.contains("stream") && body.at("stream").is_boolean() && body.at("stream").get<bool>())
            {
                if (!body.contains("model") || !body.at("model").is_string() || body.at("model").get<std::string>().empty()
                    || !body.contains("max_tokens") || !body.at("max_tokens").is_number_integer()
                    || body.at("max_tokens").get<std::int64_t>() <= 0 || !body.contains("messages")
                    || !body.at("messages").is_array() || body.at("messages").empty())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "LM Studio Anthropic Messages streaming requires model, positive max_tokens, and messages" });
                }
                if (!stream_executor || !handler) throw std::invalid_argument{ "Anthropic Messages stream executor and handler are required" };
                public_api::http_transport_options options;
                options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
                options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
                public_api::http_request_data request{ public_api::make_http_request_data(
                    cfg.llm.host, cfg.llm.port, "/v1/messages", body.dump(), std::move(options)) };
                request.content_type = "application/json";
                if (!cfg.llm.api_key.empty()) request.headers.emplace("x-api-key", cfg.llm.api_key);
                const public_api::http_stream_response_data response{ stream_executor(request, handler) };
                public_api::ensure_success({ response.status_code, {}, response.content_type, false });
                if (response.content_type.find("text/event-stream") == std::string::npos)
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "LM Studio Anthropic Messages streaming API did not return text/event-stream" });
                }
                return {};
            }
            return create_lmstudio_messages(cfg, executor);
        }

        std::string create_lmstudio_systemone(const config& cfg, const public_api::http_executor& executor)
        {
            const nlohmann::json body{ nlohmann::json::parse(cfg.llm.api_options) };
            if (!body.is_object() || !body.contains("model") || !body.at("model").is_string()
                || body.at("model").get<std::string>().empty() || !body.contains("state")
                || (!body.at("state").is_string() && !body.at("state").is_object() && !body.at("state").is_array())
                || !body.contains("questions") || !body.at("questions").is_object() || body.at("questions").empty())
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "LM Studio System One requires model, state, and a non-empty questions object" });
            }
            for (const auto& [question_id, question] : body.at("questions").items())
            {
                if (question_id.empty() || !question.is_object() || !question.contains("type")
                    || !question.at("type").is_string()
                    || (question.at("type") != "noul" && question.at("type") != "choice" && question.at("type") != "score")
                    || !question.contains("instructions") || !question.at("instructions").is_string())
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "LM Studio System One questions require IDs, supported type, and string instructions" });
                }
                if ((question.at("type") == "choice" && (!question.contains("criteria")
                        || !question.at("criteria").is_object() || question.at("criteria").empty()))
                    || (question.at("type") == "score" && (!question.contains("criteria")
                        || !question.at("criteria").is_array() || question.at("criteria").empty())))
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "LM Studio System One choice/score questions require non-empty criteria" });
                }
            }
            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            if (!cfg.llm.api_key.empty()) options.authorization = "Bearer " + cfg.llm.api_key;
            public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.llm.host, cfg.llm.port, "/v1/systemone", body.dump(), std::move(options)) };
            request.content_type = "application/json";
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            const nlohmann::json response_json{ nlohmann::json::parse(response.body) };
            if (!response_json.is_object() || !response_json.contains("answers") || !response_json.at("answers").is_object())
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "LM Studio System One API returned an invalid response object" });
            }
            for (const auto& [question_id, question] : body.at("questions").items())
            {
                if (!response_json.at("answers").contains(question_id))
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "LM Studio System One response is missing an answer for question: " + question_id });
                }
            }
            return response.body;
        }

        std::string create_vllm_batch_chat(const config& cfg, const public_api::http_executor& executor)
        {
            const nlohmann::json body{ nlohmann::json::parse(cfg.llm.api_options) };
            if (!body.is_object() || !body.contains("model") || !body.at("model").is_string()
                || body.at("model").get<std::string>().empty() || !body.contains("messages")
                || !body.at("messages").is_array() || body.at("messages").empty())
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "vLLM batch chat requires a non-empty model and conversations array" });
            }
            for (const nlohmann::json& conversation : body.at("messages"))
            {
                if (!conversation.is_array() || conversation.empty()
                    || std::any_of(conversation.begin(), conversation.end(), [](const nlohmann::json& message)
                    {
                        return !message.is_object() || !message.contains("role") || !message.at("role").is_string()
                            || !message.contains("content") || (!message.at("content").is_string() && !message.at("content").is_array());
                    }))
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "vLLM batch chat conversations must be non-empty arrays of role/content messages" });
                }
            }
            if ((body.contains("stream") && (!body.at("stream").is_boolean() || body.at("stream").get<bool>()))
                || body.contains("tools") || (body.contains("n") && (!body.at("n").is_number_integer() || body.at("n") != 1)))
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "vLLM batch chat does not support streaming/tools and requires n=1" });
            }
            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            if (!cfg.llm.api_key.empty()) options.authorization = "Bearer " + cfg.llm.api_key;
            public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.llm.host, cfg.llm.port, "/v1/chat/completions/batch", body.dump(), std::move(options)) };
            request.content_type = "application/json";
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            const nlohmann::json response_json{ nlohmann::json::parse(response.body) };
            if (!response_json.is_object() || !response_json.contains("choices") || !response_json.at("choices").is_array()
                || response_json.at("choices").size() != body.at("messages").size())
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{
                    "vLLM batch chat response must contain one choice for each conversation" });
            }
            return response.body;
        }

        public_api::http_response_data make_api_request(const config& cfg, const public_api::http_executor& executor)
        {
            return make_api_request(cfg, executor, [](const std::filesystem::path& path, std::ios_base::openmode mode)
            {
                return filesystem::read_file_to_string(path, mode);
            });
        }

        public_api::http_response_data make_api_request(const config& cfg, const public_api::http_executor& executor,
            const filesystem::file_reader& reader)
        {
            using public_api::http_method;
            http_method method;
            const std::string& method_name{ cfg.llm.api_method };
            if (method_name == "GET") method = http_method::get;
            else if (method_name == "POST") method = http_method::post;
            else if (method_name == "PUT") method = http_method::put;
            else if (method_name == "PATCH") method = http_method::patch;
            else if (method_name == "DELETE") method = http_method::delete_;
            else if (method_name == "HEAD") method = http_method::head;
            else if (method_name == "OPTIONS") method = http_method::options;
            else throw std::invalid_argument{ "backend-api method must be GET, POST, PUT, PATCH, DELETE, HEAD, or OPTIONS" };

            const std::string& path{ cfg.llm.api_path };
            const std::string_view path_only{ path.substr(0, path.find('?')) };
            const std::string decoded_path{ boost::urls::pct_string_view{ path_only }.decode() };
            const bool has_dot_segment{ [&decoded_path]
            {
                std::size_t segment_start{};
                while (segment_start <= decoded_path.size())
                {
                    const std::size_t segment_end{ decoded_path.find('/', segment_start) };
                    const std::string_view segment{ decoded_path.data() + segment_start,
                        (segment_end == std::string::npos ? decoded_path.size() : segment_end) - segment_start };
                    if (segment == "." || segment == "..") return true;
                    if (segment_end == std::string::npos) break;
                    segment_start = segment_end + 1;
                }
                return false;
            }() };
            if (path.empty() || path.front() != '/' || path.starts_with("//") || path.find('#') != std::string::npos
                || path.find('\\') != std::string::npos || decoded_path.starts_with("//") || decoded_path.find('\\') != std::string::npos
                || has_dot_segment
                || std::any_of(path.begin(), path.end(), [](unsigned char character) { return character <= 0x20 || character == 0x7f; }))
            {
                throw std::invalid_argument{ "backend-api path must be a local absolute path without traversal, fragment, or whitespace" };
            }
            if (!cfg.llm.api_options.empty() && !cfg.llm.api_body_file.empty())
            {
                throw std::invalid_argument{ "backend-api accepts either --llm.api-options or --llm.api-body-file, not both" };
            }
            std::string body;
            if (!cfg.llm.api_body_file.empty())
            {
                const std::filesystem::path body_path{ filesystem::string_to_path_by_config(cfg.llm.api_body_file, cfg) };
                body = filesystem::read_file_to_string(body_path, std::ios::binary, reader);
            }
            else if (!cfg.llm.api_options.empty())
            {
                const nlohmann::json json_body{ nlohmann::json::parse(cfg.llm.api_options) };
                if (!json_body.is_object() && !json_body.is_array())
                {
                    throw std::invalid_argument{ "backend-api JSON body must be an object or array" };
                }
                body = json_body.dump();
            }
            public_api::http_transport_options transport_options;
            transport_options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            transport_options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            if (!cfg.llm.api_key.empty()) transport_options.authorization = "Bearer " + cfg.llm.api_key;
            public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.llm.host, cfg.llm.port, path, std::move(body), std::move(transport_options)) };
            request.method = method;
            if (std::any_of(cfg.llm.api_content_type.begin(), cfg.llm.api_content_type.end(), [](unsigned char character)
                { return character < 0x20 || character == 0x7f; }))
            {
                throw std::invalid_argument{ "backend-api Content-Type contains a control character" };
            }
            request.content_type = cfg.llm.api_content_type;
            if (!cfg.llm.api_headers.empty())
            {
                const nlohmann::json headers{ nlohmann::json::parse(cfg.llm.api_headers) };
                if (!headers.is_object()) throw std::invalid_argument{ "backend-api headers must be a JSON object" };
                const std::string token_characters{ "!#$%&'*+-.^_`|~" };
                for (const auto& [name, value] : headers.items())
                {
                    const bool valid_name{ !name.empty() && std::all_of(name.begin(), name.end(), [&](unsigned char character)
                    {
                        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')
                            || (character >= '0' && character <= '9') || token_characters.find(static_cast<char>(character)) != std::string::npos;
                    }) };
                    std::string normalized_name{ name };
                    std::transform(normalized_name.begin(), normalized_name.end(), normalized_name.begin(), [](unsigned char character)
                    {
                        return static_cast<char>(std::tolower(character));
                    });
                    if (!valid_name || !value.is_string() || normalized_name == "host" || normalized_name == "content-length"
                        || normalized_name == "transfer-encoding" || normalized_name == "connection" || normalized_name == "content-type")
                    {
                        throw std::invalid_argument{ "backend-api contains a reserved or non-string header" };
                    }
                    const std::string header_value{ value.get<std::string>() };
                    if (std::any_of(header_value.begin(), header_value.end(), [](unsigned char character)
                            { return (character < 0x20 && character != '\t') || character == 0x7f; }))
                    {
                        throw std::invalid_argument{ "backend-api header contains an invalid control character" };
                    }
                    request.headers.emplace(name, header_value);
                }
            }
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            return response;
        }

        std::string create_embeddings(const config& cfg, const public_api::http_executor& executor)
        {
            if (cfg.llm.model.empty())
            {
                Hermit::throw_exception(command_line_exception{} << error_info::description{ "embeddings create requires --llm.model" });
            }

            nlohmann::json input = cfg.llm.prompt;
            if (!cfg.llm.embedding_inputs.empty()) input = nlohmann::json::array();
            for (const std::string& text : cfg.llm.embedding_inputs) input.push_back(text);

            nlohmann::json body
            {
                { "input", std::move(input) },
                { "model", cfg.llm.model }
            };
            if (cfg.llm.backend_name != "ollama")
            {
                body["encoding_format"] = "float";
            }
            if (!cfg.llm.embedding_options.empty())
            {
                const nlohmann::json extra_options{ nlohmann::json::parse(cfg.llm.embedding_options) };
                if (!extra_options.is_object() || extra_options.contains("input") || extra_options.contains("model"))
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "--llm.embedding-options must be a JSON object without reserved input/model fields" });
                }
                body.update(extra_options);
            }

            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            if (!cfg.llm.api_key.empty()) options.authorization = "Bearer " + cfg.llm.api_key;

            const public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.llm.host, cfg.llm.port, cfg.llm.embedding_target, body.dump(), std::move(options)) };
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            return response.body;
        }

        std::string tokenize(const config& cfg, const public_api::http_executor& executor)
        {
            nlohmann::json body{ { "content", cfg.llm.prompt } };
            if (!cfg.llm.tokenize_options.empty())
            {
                const nlohmann::json extra_options{ nlohmann::json::parse(cfg.llm.tokenize_options) };
                if (!extra_options.is_object() || extra_options.contains("content"))
                {
                    Hermit::throw_exception(command_line_exception{} << error_info::description{
                        "--llm.tokenize-options must be a JSON object without the reserved content field" });
                }
                body.update(extra_options);
            }
            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            if (!cfg.llm.api_key.empty()) options.authorization = "Bearer " + cfg.llm.api_key;
            const public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.llm.host, cfg.llm.port, cfg.llm.tokenize_target, body.dump(), std::move(options)) };
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            return response.body;
        }

        std::string detokenize(const config& cfg, const public_api::http_executor& executor)
        {
            const nlohmann::json body{ { "tokens", cfg.llm.token_ids } };
            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            if (!cfg.llm.api_key.empty()) options.authorization = "Bearer " + cfg.llm.api_key;
            const public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.llm.host, cfg.llm.port, cfg.llm.detokenize_target, body.dump(), std::move(options)) };
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            return response.body;
        }

        void read_paragraphs_file(config& cfg)
        {
            if (!cfg.llm.paragraphs_file.empty())
            {
                cfg.phases.clear();
                const std::string content{ filesystem::read_text_file_to_string(cfg.llm.paragraphs_file, cfg) };
                std::vector<item> paragraphs{ parse_item_list(content) };
                set_paragraphs_to_phases(paragraphs, cfg.phases);
            }
        }

        void init_chat_mode(config& cfg)
        {
            if (cfg.phases.empty())
            {
                cfg.phases = { "{{user}}", "{{char}}" };
            }
            if (cfg.llm.generation_prefix.empty())
            {
                cfg.llm.generation_prefix = "\\n{{phase}}: ";
            }
        }

        std::vector<item> parse_item_list(std::string_view str)
        {
            std::vector<item> result;

            const std::regex item_regex{ R"(^(?:[-*+]|[0-9a-zA-Z]+[.\)]) (.+))", std::regex_constants::ECMAScript };
            const std::regex sub_item_regex{ R"(^(?:[ \t]+)(?:[-*+]|[0-9a-zA-Z]+[.\)]) (.+))", std::regex_constants::ECMAScript };

            std::istringstream iss{ std::string{ str } };
            std::string line;
            bool is_prev_line_item{};
            while (std::getline(iss, line))
            {
                if (line.empty())
                {
                    continue;
                }
                if (std::smatch match; std::regex_match(line, match, item_regex))
                {
                    const std::string trimmed{ boost::trim_copy(match[1].str()) };
                    if (!trimmed.empty())
                    {
                        result.push_back({ trimmed });
                        is_prev_line_item = true;
                    }
                }
                else if (std::smatch match; is_prev_line_item && std::regex_match(line, match, sub_item_regex))
                {
                    const std::string trimmed{ boost::trim_copy(match[1].str()) };
                    if (!trimmed.empty())
                    {
                        result.back().descriptions.push_back(trimmed);
                    }
                }
                else
                {
                    is_prev_line_item = false;
                }
            }

            return result;
        }

        void write_item_list(const config& cfg, std::string_view task)
        {
            const std::vector<item> items{ parse_item_list(task) };

            for (const item& item : items)
            {
                std::string descriptions;
                for (const std::string& description : item.descriptions)
                {
                    descriptions += description;
                }
                filesystem::write_file(cfg, descriptions, item.head, std::ios::binary);
            }
        }

        std::string send_completions_request(const config& cfg, std::string_view prompt, const llm_backend_parameters& params, int max_tokens)
        {
            tcp transport;
            return send_completions_request(cfg, prompt, params, max_tokens,
                [&transport, &cfg](std::string_view host, std::string_view port, const tcp::request_type& request)
                {
                    transport.expires_after(std::chrono::seconds{ cfg.timeout_connect }).connect(host, port);
                    return transport.expires_after(std::chrono::seconds{ cfg.timeout_request }).request(request);
                });
        }

        std::string send_completions_request(const config& cfg, std::string_view prompt, const llm_backend_parameters& params, int max_tokens, const tcp::request_executor& executor)
        {
            const std::string_view host{ cfg.llm.host };
            const std::string_view port{ cfg.llm.port };
            const std::string_view target{ cfg.llm.completions_target };

            const std::string request_body{ params.get_request_for_completions(prompt, max_tokens).dump() };
            HERMIT_LOG(info) << "Send JSON\n```\n" << request_body << "\n```";

            tcp::request_type request{ tcp::make_post_json_request(host, target, request_body) };
            if (!cfg.llm.api_key.empty())
            {
                request.set(boost::beast::http::field::authorization, ("Bearer ") + cfg.llm.api_key);
            }
            request.prepare_payload();

            const tcp::response_type response{ executor(host, port, request) };
            tcp::ensure_success(response);
            HERMIT_LOG(trace) << "Receive JSON\n```\n" << response.body() << "\n```";

            return params.parse_response_for_completions(response.body());
        }

        std::string send_completions_request(const config& cfg, std::string_view prompt, const llm_backend_parameters& params, int max_tokens, const public_api::http_executor& executor)
        {
            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            if (!cfg.llm.api_key.empty()) options.authorization = "Bearer " + cfg.llm.api_key;
            const std::string body{ params.get_request_for_completions(prompt, max_tokens).dump() };
            const public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.llm.host, cfg.llm.port, cfg.llm.completions_target, body, std::move(options)) };
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            return params.parse_response_for_completions(response.body);
        }

        std::string send_chat_completions_request(const config& cfg, const llm_backend_parameters& params, const nlohmann::json& messages)
        {
            tcp transport;
            return send_chat_completions_request(cfg, params, messages,
                [&transport, &cfg](std::string_view host, std::string_view port, const tcp::request_type& request)
                {
                    transport.expires_after(std::chrono::seconds{ cfg.timeout_connect }).connect(host, port);
                    return transport.expires_after(std::chrono::seconds{ cfg.timeout_request }).request(request);
                });
        }

        std::string send_chat_completions_request(const config& cfg, const llm_backend_parameters& params, const nlohmann::json& messages, const tcp::request_executor& executor)
        {
            const std::string_view host{ cfg.llm.host };
            const std::string_view port{ cfg.llm.port };
            const std::string_view target{ cfg.llm.chat_completions_target };

            const nlohmann::json request_body_json{ params.get_request_for_chat_completions(messages) };
            const std::string request_body{ request_body_json.dump() };

            constexpr std::size_t threshold{ 64 };
            if (string_utils::has_base64(request_body_json, threshold))
            {
                HERMIT_LOG(info) << "Send JSON";
            }
            else
            {
                HERMIT_LOG(info) << "Send JSON\n```\n" << request_body << "\n```";
            }

            tcp::request_type request{ tcp::make_post_json_request(host, target, request_body) };
            if (!cfg.llm.api_key.empty())
            {
                request.set(boost::beast::http::field::authorization, ("Bearer ") + cfg.llm.api_key);
            }
            request.prepare_payload();

            const tcp::response_type response{ executor(host, port, request) };
            tcp::ensure_success(response);
            HERMIT_LOG(trace) << "Receive JSON\n```\n" << response.body() << "\n```";

            return params.parse_response_for_chat_completions(response.body());
        }

        std::string send_chat_completions_request(const config& cfg, const llm_backend_parameters& params, const nlohmann::json& messages, const public_api::http_executor& executor)
        {
            public_api::http_transport_options options;
            options.connect_timeout = std::chrono::seconds{ cfg.timeout_connect };
            options.request_timeout = std::chrono::seconds{ cfg.timeout_request };
            if (!cfg.llm.api_key.empty()) options.authorization = "Bearer " + cfg.llm.api_key;
            const std::string body{ params.get_request_for_chat_completions(messages).dump() };
            const public_api::http_request_data request{ public_api::make_http_request_data(
                cfg.llm.host, cfg.llm.port, cfg.llm.chat_completions_target, body, std::move(options)) };
            const public_api::http_response_data response{ executor(request) };
            public_api::ensure_success(response);
            return params.parse_response_for_chat_completions(response.body);
        }

        static std::string completions_impl(const config& cfg, std::string_view prompt, const context& ctx,
            const tcp::request_executor* tcp_executor, const public_api::http_executor* http_executor)
        {
            std::string expanded_prompt{ expand_macro(prompt, cfg, ctx) };
            const std::string expanded_prefix{ expand_macro(cfg.llm.generation_prefix, cfg, ctx) };
            const std::size_t initial_prompt_size{ expanded_prompt.size() };
            expanded_prompt += expanded_prefix;

            const auto count_tokens = [&cfg, tcp_executor, http_executor](std::string_view value)
            {
                return http_executor ? send_token_count_request(cfg, value, *http_executor)
                    : tcp_executor ? send_token_count_request(cfg, value, *tcp_executor)
                    : Hermit::send_token_count_request(cfg, value);
            };
            const int initial_tokens{ count_tokens(expanded_prompt) };

            HERMIT_LOG(info) << "Prompt created.\n```\n" << expanded_prompt << "\n```";

            std::string current_prompt{ expanded_prompt };
            int current_tokens{ initial_tokens };
            for (int completion_iterations{}; completion_iterations < cfg.llm.max_completion_iterations; ++completion_iterations)
            {
                HERMIT_LOG(trace) << "completion_iterations: " << completion_iterations;

                if (current_tokens - initial_tokens >= cfg.llm.min_completion_tokens)
                {
                    break;
                }

                const int remaining_tokens{ cfg.llm.backend->get_truncation_length() - current_tokens };
                if (remaining_tokens <= 0)
                {
                    HERMIT_LOG(warning) << "Context window full. Cannot generate more tokens";
                    break;
                }

                const int tokens_to_generate{ std::min(cfg.llm.backend->get_max_tokens(), remaining_tokens) };
                if (tokens_to_generate <= 0)
                {
                    HERMIT_LOG(warning) << "No tokens left to generate. Aborting";
                    break;
                }

                const int max_tokens{ tokens_to_generate };
                const std::string response{ http_executor
                    ? send_completions_request(cfg, current_prompt, *cfg.llm.backend, max_tokens, *http_executor)
                    : tcp_executor ? send_completions_request(cfg, current_prompt, *cfg.llm.backend, max_tokens, *tcp_executor)
                    : send_completions_request(cfg, current_prompt, *cfg.llm.backend, max_tokens) };

                if (response.empty())
                {
                    break;
                }

                current_prompt += response;
                current_tokens = count_tokens(current_prompt);
            }

            std::string generated;
            generated.reserve(initial_prompt_size + cfg.llm.generation_suffix.size());
            generated += current_prompt.substr(initial_prompt_size);
            generated += cfg.llm.generation_suffix;

            return generated;
        }

        std::string completions(const config& cfg, std::string_view prompt, const context& ctx)
        {
            return completions_impl(cfg, prompt, ctx, nullptr, nullptr);
        }

        std::string completions(const config& cfg, std::string_view prompt, const context& ctx,
            const tcp::request_executor& executor)
        {
            return completions_impl(cfg, prompt, ctx, &executor, nullptr);
        }

        std::string completions(const config& cfg, std::string_view prompt, const context& ctx,
            const public_api::http_executor& executor)
        {
            return completions_impl(cfg, prompt, ctx, nullptr, &executor);
        }

        std::string chat_completions(const config& cfg, const context& ctx, const nlohmann::json& messages)
        {
            return send_chat_completions_request(cfg, *cfg.llm.backend, messages);
        }

        static void completions_and_write_file_impl(const config& cfg, std::string_view prompt, const context& ctx,
            const filesystem::file_writer* writer, const tcp::request_executor* tcp_executor,
            const public_api::http_executor* http_executor)
        {
            const auto count_tokens = [&cfg, tcp_executor, http_executor](std::string_view value)
            {
                return http_executor ? send_token_count_request(cfg, value, *http_executor)
                    : tcp_executor ? send_token_count_request(cfg, value, *tcp_executor)
                    : Hermit::send_token_count_request(cfg, value);
            };
            const std::string truncated_prompt{ http_executor || tcp_executor
                ? string_utils::truncate_prompt_by_config(prompt, cfg, count_tokens)
                : string_utils::truncate_prompt_by_config(prompt, cfg) };

            std::string response{ http_executor ? completions(cfg, truncated_prompt, ctx, *http_executor)
                : tcp_executor ? completions(cfg, truncated_prompt, ctx, *tcp_executor)
                : completions(cfg, truncated_prompt, ctx) };
            response = remove_reasoning(response, cfg.llm.reasoning_prefix, cfg.llm.reasoning_suffix);

            if (writer)
            {
                filesystem::write_file(cfg, response, cfg.llm.output_file, std::ios_base::app, *writer);
            }
            else
            {
                filesystem::write_file(cfg, response, cfg.llm.output_file, std::ios_base::app);
            }

            if (!cfg.verbose)
            {
                boost::nowide::cout << response << std::flush;
            }

            if (writer)
            {
                write_code_block(cfg, response, *writer);
            }
            else
            {
                write_code_block(cfg, response);
            }
        }

        void completions_and_write_file(const config& cfg, std::string_view prompt, const context& ctx)
        {
            completions_and_write_file_impl(cfg, prompt, ctx, nullptr, nullptr, nullptr);
        }

        void completions_and_write_file(const config& cfg, std::string_view prompt, const context& ctx,
            const filesystem::file_writer& writer)
        {
            completions_and_write_file_impl(cfg, prompt, ctx, &writer, nullptr, nullptr);
        }

        void completions_and_write_file(const config& cfg, std::string_view prompt, const context& ctx,
            const filesystem::file_writer& writer, const tcp::request_executor& executor)
        {
            completions_and_write_file_impl(cfg, prompt, ctx, &writer, &executor, nullptr);
        }

        void completions_and_write_file(const config& cfg, std::string_view prompt, const context& ctx,
            const filesystem::file_writer& writer, const public_api::http_executor& executor)
        {
            completions_and_write_file_impl(cfg, prompt, ctx, &writer, nullptr, &executor);
        }

        std::string generate_uuid_v4()
        {
            std::ostringstream oss;
            oss << std::hex << std::setfill('0');
            oss << std::setw(8) << random<std::uint32_t>() << '-'
                << std::setw(4) << (random<std::uint32_t>() & UINT32_C(0xFFFF)) << '-'
                << '4' << std::setw(3) << (random<std::uint32_t>() & UINT32_C(0xFFF)) << '-'
                << std::setw(4) << ((random<std::uint32_t>() & UINT32_C(0x3FFF)) | UINT32_C(0x8000)) << '-'
                << std::setw(12) << random<std::uint64_t>()
                ;
            return oss.str();
        }

        std::string generate_chat_filename()
        {
            const boost::posix_time::ptime local_time{ boost::posix_time::second_clock::local_time() };
            const boost::posix_time::time_facet* facet{ new boost::posix_time::time_facet("%Y%m%d_%H%M%S") };
            std::ostringstream oss;
            oss.imbue(std::locale(oss.getloc(), facet));
            oss << "chat_" << local_time << "_" << generate_uuid_v4() << ".json";
            return oss.str();
        }

        static void chat_completions_and_write_file_impl(const config& cfg, std::string_view prompt, const context& ctx,
            const filesystem::file_writer* writer, const public_api::http_executor* http_executor,
            const tcp::request_executor* tcp_executor, const filesystem::file_reader* reader)
        {
            std::string chat_file_content;
            try
            {
                chat_file_content = filesystem::read_text_file_to_string(cfg.llm.chat_file, cfg);
            }
            catch (const file_open_exception&)
            {
                ;
            }

            nlohmann::json messages;
            if (chat_file_content.empty())
            {
                messages = nlohmann::json::array();
            }
            else
            {
                messages = nlohmann::json::parse(chat_file_content);
            }

            const std::string expanded_prompt{ expand_macro(prompt, cfg, ctx) };
            HERMIT_LOG(info) << "Prompt created.\n```\n" << expanded_prompt << "\n```";

            nlohmann::json content{ nlohmann::json::array() };
            content.push_back
            (
                {
                    { "type", "text" },
                    { "text", expanded_prompt }
                }
            );

            if (!cfg.llm.image_file.empty())
            {
                const image_info_type image_info{ reader
                    ? image_info_type::from_file(cfg.llm.image_file, cfg, *reader)
                    : image_info_type::from_file(cfg.llm.image_file, cfg) };
                const std::string image_url{ filesystem::base64_image_to_url(image_info.base64_image, image_info.mime_type) };
                content.push_back
                (
                    {
                        { "type", "image_url" },
                        { "image_url", { { "url", image_url } } }
                    }
                );
            }

            messages.push_back
            (
                {
                    { "role", "user" },
                    { "content", std::move(content) }
                }
            );

            const std::string chat_filename{ cfg.llm.chat_file.empty() ? generate_chat_filename() : cfg.llm.chat_file };

            const std::string response{ http_executor
                ? send_chat_completions_request(cfg, *cfg.llm.backend, messages, *http_executor)
                : tcp_executor ? send_chat_completions_request(cfg, *cfg.llm.backend, messages, *tcp_executor)
                : chat_completions(cfg, ctx, messages) };
            messages.push_back
            (
                {
                    { "role", "assistant" },
                    { "content", response }
                }
            );

            if (writer)
            {
                filesystem::write_file(cfg, messages.dump(), chat_filename, 0, *writer);
                filesystem::write_file(cfg, response, cfg.llm.output_file, std::ios_base::app, *writer);
            }
            else
            {
                filesystem::write_file(cfg, messages.dump(), chat_filename);
                filesystem::write_file(cfg, response, cfg.llm.output_file, std::ios_base::app);
            }

            if (!cfg.verbose)
            {
                boost::nowide::cout << response << std::flush;
            }

            if (writer)
            {
                write_code_block(cfg, response, *writer);
            }
            else
            {
                write_code_block(cfg, response);
            }
        }

        void chat_completions_and_write_file(const config& cfg, std::string_view prompt, const context& ctx)
        {
            chat_completions_and_write_file_impl(cfg, prompt, ctx, nullptr, nullptr, nullptr, nullptr);
        }

        void chat_completions_and_write_file(const config& cfg, std::string_view prompt, const context& ctx,
            const filesystem::file_writer& writer)
        {
            chat_completions_and_write_file_impl(cfg, prompt, ctx, &writer, nullptr, nullptr, nullptr);
        }

        void chat_completions_and_write_file(const config& cfg, std::string_view prompt, const context& ctx,
            const filesystem::file_writer& writer, const public_api::http_executor& executor, const filesystem::file_reader& reader)
        {
            chat_completions_and_write_file_impl(cfg, prompt, ctx, &writer, &executor, nullptr, &reader);
        }

        void chat_completions_and_write_file(const config& cfg, std::string_view prompt, const context& ctx,
            const filesystem::file_writer& writer, const tcp::request_executor& executor, const filesystem::file_reader& reader)
        {
            chat_completions_and_write_file_impl(cfg, prompt, ctx, &writer, nullptr, &executor, &reader);
        }

        static void write_code_block_impl(const config& cfg, std::string_view markdown,
            const filesystem::file_writer* writer)
        {
            if (cfg.llm.code_block_extract)
            {
                const code_blocks blocks{ extract_code_block_from_markdown(markdown) };
                for (const auto& [name, code] : blocks)
                {
                    if (name == "stdout")
                    {
                        boost::nowide::cout << code << std::flush;
                    }
                    else
                    {
                        if (writer)
                        {
                            filesystem::write_file(cfg, code, filesystem::complement_codeblock_extension(name), 0, *writer);
                        }
                        else
                        {
                            filesystem::write_file(cfg, code, filesystem::complement_codeblock_extension(name), 0);
                        }
                    }
                }
            }
        }

        void write_code_block(const config& cfg, std::string_view markdown)
        {
            write_code_block_impl(cfg, markdown, nullptr);
        }

        void write_code_block(const config& cfg, std::string_view markdown, const filesystem::file_writer& writer)
        {
            write_code_block_impl(cfg, markdown, &writer);
        }

        code_blocks extract_code_block_from_markdown(std::string_view markdown_content)
        {
            code_blocks result;
            const std::regex code_block_regex{ R"(```(\S+)\s*\n([\s\S]*?)```)" };

            for (std::cregex_iterator iter{ markdown_content.data(), markdown_content.data() + markdown_content.size(), code_block_regex }; iter != std::cregex_iterator{}; ++iter)
            {
                const std::cmatch match{ *iter };
                const std::string name{ sanitize_as_filename(match[1].str()) };
                const std::string code{ match[2].str() };
                result[name] = code;
            }

            return result;
        }

        std::string remove_reasoning(std::string_view response, std::string_view prefix, std::string_view suffix)
        {
            std::string result{ response };

            if (prefix.empty() || suffix.empty())
            {
                return result;
            }

            std::string::size_type first{};
            while ((first = result.find(prefix, first)) != std::string::npos)
            {
                const std::string::size_type last{ result.find(suffix, first + prefix.size()) };
                if (last != std::string::npos)
                {
                    const std::string::size_type remove_length{ (last + suffix.size()) - first };
                    HERMIT_LOG(info) << "Reasoning removed.\n```\n" << result.substr(first, remove_length) << "\n```\n";
                    result.erase(first, remove_length);
                }
                else
                {
                    break;
                }
            }

            if (result != response)
            {
                HERMIT_LOG(info) << "Reasoning removed.\n```\n" << result << "\n```\n";
            }

            return result;
        }
    } // namespace llm

} // namespace Hermit

