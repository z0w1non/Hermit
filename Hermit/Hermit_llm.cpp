#include <chrono>
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

