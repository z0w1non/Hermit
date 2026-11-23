#ifndef HERMIT_PUBLIC_HPP
#define HERMIT_PUBLIC_HPP

#include <string>
#include <string_view>

namespace Hermit
{
    struct config;
    struct context;

    // Lightweight, dependency-free entry point for macro expansion.
    std::string expand_macro(std::string_view input, const config& cfg, const context& ctx);
}

#endif
