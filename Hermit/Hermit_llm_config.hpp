#ifndef HERMIT_LLM_CONFIG_HPP
#define HERMIT_LLM_CONFIG_HPP

#include <string>

#include "Hermit_llm_types.hpp"

namespace Hermit
{
    struct llm_parameters
    {
        std::string prompt, prompt_file, output_file, chat_file;
        std::string generation_prefix, generation_suffix, paragraphs_file, image_file;
        std::string host, port, api_key;
        std::string completions_target, token_count_target, chat_completions_target;
        int min_completion_tokens{};
        int max_completion_iterations{};
        std::string reasoning_prefix, reasoning_suffix;
        bool code_block_extract{};
        llm_backend_parameters* backend{};
        llm_mode mode;
    };
}

#endif
