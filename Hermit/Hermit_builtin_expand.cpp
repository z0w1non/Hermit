#include "Hermit_builtin.hpp"
#include "Hermit_parser.hpp"

namespace Hermit
{
    std::string expand_macro(std::string_view input, const config& cfg, const context& ctx)
    {
        constexpr unsigned int max_depth{ 32 };
        context pushed{ ctx.make_pushed() };
        return parser::evaluate_recursive(std::string{ input }, cfg, max_depth, pushed);
    }
}
