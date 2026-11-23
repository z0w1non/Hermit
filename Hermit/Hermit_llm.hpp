#ifndef HERMIT_LLM_HPP
#define HERMIT_LLM_HPP

#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"
#include "Hermit_config.hpp"
#include "Hermit_tcp.hpp"
#include "Hermit_public_types.hpp"

namespace Hermit
{
    namespace llm
    {
        void read_paragraphs_file(config& cfg);
        void init_chat_mode(config& cfg);
        std::vector<item> parse_item_list(std::string_view str);
        void write_item_list(const config& cfg, std::string_view task);
        std::string send_completions_request(const config& cfg, std::string_view prompt, const llm_backend_parameters& params, int max_tokens);
        std::string send_completions_request(const config& cfg, std::string_view prompt, const llm_backend_parameters& params, int max_tokens, const tcp::request_executor& executor);
        std::string send_completions_request(const config& cfg, std::string_view prompt, const llm_backend_parameters& params, int max_tokens, const public_api::http_executor& executor);
        std::string send_chat_completions_request(const config& cfg, const llm_backend_parameters& params, const nlohmann::json& messages);
        std::string send_chat_completions_request(const config& cfg, const llm_backend_parameters& params, const nlohmann::json& messages, const tcp::request_executor& executor);
        std::string send_chat_completions_request(const config& cfg, const llm_backend_parameters& params, const nlohmann::json& messages, const public_api::http_executor& executor);
        std::string completions(const config& cfg, std::string_view prompt, const context& ctx);
        std::string chat_completions(const config& cfg, const context& ctx, const nlohmann::json& messages);
        void completions_and_write_file(const config& cfg, std::string_view prompt, const context& ctx);
        std::string generate_uuid_v4();
        std::string generate_chat_filename();
        void chat_completions_and_write_file(const config& cfg, std::string_view prompt, const context& ctx);
        void write_code_block(const config& cfg, std::string_view markdown);
        using code_blocks = string_unordered_map<std::string>;
        code_blocks extract_code_block_from_markdown(std::string_view markdown_content);
        std::string remove_reasoning(std::string_view response, std::string_view prefix, std::string_view suffix);
    } // namespace llm

}

#endif
