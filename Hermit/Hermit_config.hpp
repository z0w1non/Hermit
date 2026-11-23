#ifndef HERMIT_CONFIG_HPP
#define HERMIT_CONFIG_HPP

#include <string>
#include <vector>

#include <boost/log/trivial.hpp>

#include "Hermit_context.hpp"
#include "Hermit_lru.hpp"
#include "Hermit_app_types.hpp"
#include "Hermit_llm_config.hpp"
#include "Hermit_tg_kc_config.hpp"
#include "Hermit_sd_config.hpp"
#include "Hermit_media_config.hpp"
#include "Hermit_aux_config.hpp"

namespace Hermit
{
    struct config;

    llm_mode string_to_llm_mode(std::string_view name);

    std::string llm_mode_to_target(llm_mode mode, const config& cfg);

    struct config
    {
        using cache_type = lru_cache<1000>;

        command_mode command_mode;
        std::string base_path;
        boost::log::trivial::severity_level log_level{ boost::log::trivial::info };
        std::string log_file;
        std::string config_file;
        bool verbose{};
        int number_iterations{};
        std::vector<std::string> user_defined_variables;
        std::vector<std::string> phases;

        int seed{};

        unsigned int timeout_connect{};
        unsigned int timeout_request{};

        llm_parameters llm;
        tg_parameters tg;
        kc_parameters kc;
        sd_parameters sd;
        sb_parameters sb;
        cu_parameters cu;
        png_parameters png;
        process_parameters proc;

        mutable cache_type lru_cache{ make_lru_cache_callback() };
        context ctx;

        cache_type::callback_type make_lru_cache_callback();
    };

}

#endif
