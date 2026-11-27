#ifndef HERMIT_LLM_CONFIG_HPP
#define HERMIT_LLM_CONFIG_HPP

#include <string>
#include <vector>

#include "Hermit_llm_types.hpp"

namespace Hermit
{
    struct llm_parameters
    {
        std::string prompt, prompt_file, output_file, chat_file;
        std::string generation_prefix, generation_suffix, paragraphs_file, image_file;
        std::string host, port, api_key, model, backend_name;
        std::string completions_target, token_count_target, chat_completions_target, models_target, embedding_target;
        std::string tokenize_target, detokenize_target, tokenize_options;
        std::string running_models_target, model_info_target, model_info_options;
        std::string model_action, model_action_target, model_action_options;
        std::vector<std::string> embedding_inputs;
        std::string embedding_options;
        std::vector<int> token_ids;
        int min_completion_tokens{};
        int max_completion_iterations{};
        std::string reasoning_prefix, reasoning_suffix;
        bool code_block_extract{};
        llm_backend_parameters* backend{};
        llm_mode mode;
    };
}

#endif
