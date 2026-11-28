#ifndef HERMIT_LLM_HPP
#define HERMIT_LLM_HPP

#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"
#include "config.hpp"
#include "tcp.hpp"
#include "public_types.hpp"
#include "filesystem.hpp"

namespace hermit
{
    namespace llm
    {
        void read_paragraphs_file(config& cfg);
        std::string list_models(const config& cfg, const public_api::http_executor& executor);
        std::string list_running_models(const config& cfg, const public_api::http_executor& executor);
        std::string get_ollama_version(const config& cfg, const public_api::http_executor& executor);
        std::string show_model(const config& cfg, const public_api::http_executor& executor);
        std::string manage_ollama_model(const config& cfg, const public_api::http_executor& executor);
        std::string manage_lmstudio_model(const config& cfg, const public_api::http_executor& executor);
        std::string create_lmstudio_chat(const config& cfg, const public_api::http_executor& executor);
        std::string create_lmstudio_chat(const config& cfg, const public_api::http_executor& executor,
            const public_api::http_stream_executor& stream_executor, const public_api::http_stream_handler& handler);
        std::string create_lmstudio_response(const config&, const public_api::http_executor&);
        std::string create_lmstudio_response(const config&, const public_api::http_executor&,
            const public_api::http_stream_executor&, const public_api::http_stream_handler&);
        public_api::http_stream_response_data create_lmstudio_response_stream(const config&,
            const public_api::http_stream_executor&, const public_api::http_stream_handler&);
        std::string create_lmstudio_decision(const config&, const public_api::http_executor&);
        std::string create_lmstudio_messages(const config&, const public_api::http_executor&);
        std::string create_lmstudio_messages(const config&, const public_api::http_executor&,
            const public_api::http_stream_executor&, const public_api::http_stream_handler&);
        std::string create_lmstudio_systemone(const config&, const public_api::http_executor&);
        std::string create_vllm_batch_chat(const config&, const public_api::http_executor&);
        std::string create_tg_moderations(const config&, const public_api::http_executor&);
        std::string check_tg_health(const config&, const public_api::http_executor&);
        std::string get_tg_model_info(const config&, const public_api::http_executor&);
        std::string manage_tg_model(const config&, const public_api::http_executor&);
        std::string stop_tg_generation(const config&, const public_api::http_executor&);
        std::string get_tg_logits(const config&, const public_api::http_executor&);
        std::string create_tg_chat_prompt(const config&, const public_api::http_executor&);
        std::string list_tg_loras(const config&, const public_api::http_executor&);
        std::string manage_tg_loras(const config&, const public_api::http_executor&);
        std::string encode_tg_tokens(const config&, const public_api::http_executor&);
        std::string decode_tg_tokens(const config&, const public_api::http_executor&);
        public_api::http_response_data make_api_request(const config&, const public_api::http_executor&);
        public_api::http_response_data make_api_request(const config&, const public_api::http_executor&, const filesystem::file_reader&);
        std::string create_embeddings(const config& cfg, const public_api::http_executor& executor);
        std::string tokenize(const config& cfg, const public_api::http_executor& executor);
        std::string detokenize(const config& cfg, const public_api::http_executor& executor);
        void init_chat_mode(config& cfg);
        std::vector<Item> parse_item_list(std::string_view str);
        void write_item_list(const config& cfg, std::string_view task);
        std::string send_completions_request(const config& cfg, std::string_view prompt, const LlmBackendParameters& params, int max_tokens);
        std::string send_completions_request(const config& cfg, std::string_view prompt, const LlmBackendParameters& params, int max_tokens, const tcp::request_executor& executor);
        std::string send_completions_request(const config& cfg, std::string_view prompt, const LlmBackendParameters& params, int max_tokens, const public_api::http_executor& executor);
        std::string send_chat_completions_request(const config& cfg, const LlmBackendParameters& params, const nlohmann::json& messages);
        std::string send_chat_completions_request(const config& cfg, const LlmBackendParameters& params, const nlohmann::json& messages, const tcp::request_executor& executor);
        std::string send_chat_completions_request(const config& cfg, const LlmBackendParameters& params, const nlohmann::json& messages, const public_api::http_executor& executor);
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
