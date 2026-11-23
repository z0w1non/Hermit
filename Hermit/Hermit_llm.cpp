#include "Hermit_llm.hpp"
#include "Hermit_public_http_adapter.hpp"
#include "Hermit_config.hpp"
#include "Hermit_filesystem.hpp"
#include "Hermit_builtin.hpp"
#include "Hermit_string_utils.hpp"
#include "Hermit_tcp.hpp"
#include "Hermit_log.hpp"
#include "Hermit_errors.hpp"
#include "Hermit_runtime.hpp"
#include "Hermit_templates.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/nowide/iostream.hpp>

#include "nlohmann/json.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

namespace Hermit
{
    namespace llm
    {
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
            return params.parse_response_for_chat_completions(response.body);
        }

        std::string completions(const config& cfg, std::string_view prompt, const context& ctx)
        {
            std::string expanded_prompt{ expand_macro(prompt, cfg, ctx) };
            const std::string expanded_prefix{ expand_macro(cfg.llm.generation_prefix, cfg, ctx) };
            const std::size_t initial_prompt_size{ expanded_prompt.size() };
            expanded_prompt += expanded_prefix;

            const int initial_tokens{ send_token_count_request(cfg, expanded_prompt) };

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
                const std::string response{ send_completions_request(cfg, current_prompt, *cfg.llm.backend, max_tokens) };

                if (response.empty())
                {
                    break;
                }

                current_prompt += response;
                current_tokens = send_token_count_request(cfg, current_prompt);
            }

            std::string generated;
            generated.reserve(initial_prompt_size + cfg.llm.generation_suffix.size());
            generated += current_prompt.substr(initial_prompt_size);
            generated += cfg.llm.generation_suffix;

            return generated;
        }

        std::string chat_completions(const config& cfg, const context& ctx, const nlohmann::json& messages)
        {
            return send_chat_completions_request(cfg, *cfg.llm.backend, messages);
        }

        void completions_and_write_file(const config& cfg, std::string_view prompt, const context& ctx)
        {
            const std::string truncated_prompt{ string_utils::truncate_prompt_by_config(prompt, cfg) };

            std::string response{ completions(cfg, truncated_prompt, ctx) };
            response = remove_reasoning(response, cfg.llm.reasoning_prefix, cfg.llm.reasoning_suffix);
            response += cfg.llm.generation_suffix;

            filesystem::write_file(cfg, response, cfg.llm.output_file, std::ios_base::app);

            if (!cfg.verbose)
            {
                boost::nowide::cout << response << std::flush;
            }

            write_code_block(cfg, response);
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

        void chat_completions_and_write_file(const config& cfg, std::string_view prompt, const context& ctx)
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
                const image_info_type image_info{ image_info_type::from_file(cfg.llm.image_file, cfg) };
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

            const std::string response{ chat_completions(cfg, ctx, messages) };
            messages.push_back
            (
                {
                    { "role", "assistant" },
                    { "content", response }
                }
            );

            filesystem::write_file(cfg, messages.dump(), chat_filename);

            filesystem::write_file(cfg, response, cfg.llm.output_file, std::ios_base::app);

            if (!cfg.verbose)
            {
                boost::nowide::cout << response << std::flush;
            }

            write_code_block(cfg, response);
        }

        void write_code_block(const config& cfg, std::string_view markdown)
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
                        filesystem::write_file(cfg, code, filesystem::complement_codeblock_extension(name), 0);
                    }
                }
            }
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

