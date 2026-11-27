#ifndef HERMIT_LLM_HPP
#define HERMIT_LLM_HPP

#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"
#include "Hermit_config.hpp"
#include "Hermit_tcp.hpp"
#include "Hermit_public_types.hpp"
#include "Hermit_filesystem.hpp"

namespace Hermit
{
    namespace llm
    {
        void read_paragraphs_file(config& cfg);
        std::string list_models(const config& cfg, const public_api::http_executor& executor);
        std::string list_running_models(const config& cfg, const public_api::http_executor& executor);
        std::string get_ollama_version(const config& cfg, const public_api::http_executor& executor);
        std::string show_model(const config& cfg, const public_api::http_executor& executor);
        std::string manage_ollama_model(const config& cfg, const public_api::http_executor& executor);
        std::string create_embeddings(const config& cfg, const public_api::http_executor& executor);
        std::string tokenize(const config& cfg, const public_api::http_executor& executor);
        std::string detokenize(const config& cfg, const public_api::http_executor& executor);
        void init_chat_mode(config& cfg);
        std::vector<item> parse_item_list(std::string_view str);
        void write_item_list(const config& cfg, std::string_view task);
        std::string send_completions_request(const config& cfg, std::string_view prompt, const llm_backend_parameters& params, int max_tokens);
        std::string send_completions_request(const config& cfg, std::string_view prompt, const llm_backend_parameters& params, int max_tokens, const tcp::request_executor& executor);
        std::string send_completions_request(const config& cfg, std::string_view prompt, const llm_backend_parameters& params, int max_tokens, const public_api::http_executor& executor);
        std::string send_chat_completions_request(const config& cfg, const llm_backend_parameters& params, const nlohmann::json& messages);
        std::string send_chat_completions_request(const config& cfg, const llm_backend_parameters& params, const nlohmann::json& messages, const tcp::request_executor& executor);
        std::string send_chat_completions_request(const config& cfg, const llm_backend_parameters& params, const nlohmann::json& messages, const public_api::http_executor& executor);
        int send_token_count_request(const config& cfg, std::string_view prompt, const tcp::request_executor& executor);
        int send_token_count_request(const config& cfg, std::string_view prompt, const public_api::http_executor& executor);
        std::string completions(const config& cfg, std::string_view prompt, const context& ctx);
        std::string completions(const config& cfg, std::string_view prompt, const context& ctx, const tcp::request_executor& executor);
        std::string completions(const config& cfg, std::string_view prompt, const context& ctx, const public_api::http_executor& executor);
        std::string chat_completions(const config& cfg, const context& ctx, const nlohmann::json& messages);
        void completions_and_write_file(const config& cfg, std::string_view prompt, const context& ctx);
        void completions_and_write_file(const config& cfg, std::string_view prompt, const context& ctx, const filesystem::file_writer& writer);
        void completions_and_write_file(const config& cfg, std::string_view prompt, const context& ctx,
            const filesystem::file_writer& writer, const tcp::request_executor& executor);
        void completions_and_write_file(const config& cfg, std::string_view prompt, const context& ctx,
            const filesystem::file_writer& writer, const public_api::http_executor& executor);
        std::string generate_uuid_v4();
        std::string generate_chat_filename();
        void chat_completions_and_write_file(const config& cfg, std::string_view prompt, const context& ctx);
        void chat_completions_and_write_file(const config& cfg, std::string_view prompt, const context& ctx, const filesystem::file_writer& writer);
        void chat_completions_and_write_file(const config& cfg, std::string_view prompt, const context& ctx,
            const filesystem::file_writer& writer, const public_api::http_executor& executor, const filesystem::file_reader& reader);
        void chat_completions_and_write_file(const config& cfg, std::string_view prompt, const context& ctx,
            const filesystem::file_writer& writer, const tcp::request_executor& executor, const filesystem::file_reader& reader);
        void write_code_block(const config& cfg, std::string_view markdown);
        void write_code_block(const config& cfg, std::string_view markdown, const filesystem::file_writer& writer);
        using code_blocks = string_unordered_map<std::string>;
        code_blocks extract_code_block_from_markdown(std::string_view markdown_content);
        std::string remove_reasoning(std::string_view response, std::string_view prefix, std::string_view suffix);
    } // namespace llm

}

#endif
