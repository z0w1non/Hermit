#ifndef HERMIT_CONFIG_HPP
#define HERMIT_CONFIG_HPP

#include <string>
#include <vector>

#include "context.hpp"
#include "lru.hpp"
#include "app_types.hpp"
#include "llm_config.hpp"
#include "tg_kc_config.hpp"
#include "sd_config.hpp"
#include "media_config.hpp"
#include "aux_config.hpp"
#include "log_types.hpp"

namespace hermit
{
    struct config;

    llm_mode string_to_llm_mode(std::string_view name);

    std::string llm_mode_to_target(llm_mode mode, const config& cfg);

    struct config
    {
        using cache_type = lru_cache<1000>;

        CommandMode command_mode;
        std::string base_path;
        log::severity_level log_level{ log::severity_level::info };
        std::string log_file;
        std::string config_file;
        bool verbose{};
        bool allow_process_exec{};
        int number_iterations{};
        std::vector<std::string> user_defined_variables;
        std::vector<std::string> phases;

        int seed{};

        unsigned int timeout_connect{};
        unsigned int timeout_request{};

        LlmParameters llm;
        tg_parameters tg;
        kc_parameters kc;
        sd_parameters sd;
        sb_parameters sb;
        cu_parameters cu;
        PngParameters png;
        ProcessParameters proc;

        mutable cache_type lru_cache{ make_lru_cache_callback() };
        context ctx;

        cache_type::callback_type make_lru_cache_callback();
    };

}

#endif
