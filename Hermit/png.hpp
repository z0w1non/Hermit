#ifndef HERMIT_PNG_HPP
#define HERMIT_PNG_HPP

#include <string>
#include <string_view>

namespace hermit::tEXt
{
    std::string extract_parameters(std::string_view image, std::string_view target_key = "parameters");
}

#endif
