#ifndef HERMIT_APP_TYPES_HPP
#define HERMIT_APP_TYPES_HPP

#include <string>
#include <string_view>
#include <vector>

namespace Hermit
{
    struct item
    {
        std::string head;
        std::vector<std::string> descriptions;
    };

    enum class command_mode
    {
        tg, kc, lmstudio, llama_cpp, vllm, ollama, sd, sb, cu, list_models, list_running_models, show_model, get_ollama_version,
        manage_ollama_model, manage_comfyui_api, manage_sd_api,
        create_embeddings, tokenize, detokenize,
        extract_png_parameters, create_process, terminate_process
    };

    command_mode string_to_command_mode(std::string_view name);
}

#endif
