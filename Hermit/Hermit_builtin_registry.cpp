#include "Hermit_builtin.hpp"
#include "Hermit_errors.hpp"

namespace Hermit
{
    std::optional<builtin::macro_type> builtin::get_macro(std::string_view name)
    {
        using map_type = string_view_unordered_map<macro_type>;
        static const map_type macros
        {
            { "int", int_ }, { "double", double_ }, { "char", char_ }, { "string", string_ },
            { "file", macro_type{ static_cast<value_type(*)(macro_argument_type)>(file) } }, { "head", head }, { "tail", tail }, { "head_tail", head_tail },
            { "json_literal", json_literal },
            { "getenv", macro_type{ static_cast<value_type(*)(macro_argument_type)>(getenv) } },
            { "setenv", macro_type{ static_cast<value_type(*)(macro_argument_type)>(setenv) } },
            { "generated", macro_type{ static_cast<value_type(*)(macro_argument_type)>(generated) } }, { "random", macro_type{ static_cast<value_type(*)(macro_argument_type)>(random) } },
            { "choice", macro_type{ static_cast<value_type(*)(macro_argument_type)>(choice) } },
            { "exec", macro_type{ static_cast<value_type(*)(macro_argument_type)>(exec) } },
            { "code_block", code_block }, { "summary", macro_type{ static_cast<value_type(*)(macro_argument_type)>(summary) } }, { "root", root }, { "parent", parent },
            { "stem", stem }, { "extension", extension }
        };
        if (const map_type::const_iterator iter{ macros.find(name) }; iter != macros.end()) return iter->second;
        return std::nullopt;
    }

    void builtin::validate_arguments_size(const std::vector<value_type>& arguments, std::optional<std::size_t> min, std::optional<std::size_t> max)
    {
        if ((min && arguments.size() < *min) || (max && arguments.size() > *max))
        {
            Hermit::throw_exception<macro_exception>(macro_exception{});
        }
    }
}
