#ifndef HERMIT_CORE_HPP
#define HERMIT_CORE_HPP

#include <string>
#include <string_view>

#include "config.hpp"
#include "app_types.hpp"
#include "context.hpp"
#include "sd_config.hpp"
#include "llm_types.hpp"

namespace hermit
{
    std::string value_to_string(const value_type& value);
    std::string value_reference_to_string(const value_reference_type& value_reference);
    CommandMode StringToCommandMode(std::string_view name);
    sd_mode string_to_sd_mode(std::string_view name);
    std::string sd_mode_to_target(sd_mode mode, const config& cfg);
    llm_mode string_to_llm_mode(std::string_view name);
    std::string llm_mode_to_target(llm_mode mode, const config& cfg);
}

#endif
