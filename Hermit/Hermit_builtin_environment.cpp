#include "Hermit_builtin.hpp"
#include "Hermit_templates.hpp"

#include <boost/nowide/cstdlib.hpp>

namespace Hermit::builtin
{
    value_type getenv(macro_argument_type args)
    {
        return getenv(args,
            [](std::string_view key) -> std::optional<std::string>
            {
                if (const char* env{ boost::nowide::getenv(std::string{ key }.c_str()) }; env)
                {
                    return std::string{ env };
                }
                return std::nullopt;
            });
    }

    value_type getenv(macro_argument_type args, const environment_getter& getter)
    {
        builtin::validate_arguments_size(args.arguments, 1, 1);
        const std::string& key{ get_or_throw<std::string>(args.arguments[0]) };
        const std::optional<std::string> value{ getter(key) };
        return value.value_or(std::string{});
    }

    value_type setenv(macro_argument_type args)
    {
        return setenv(args,
            [](std::string_view key, std::string_view value)
            {
                return boost::nowide::setenv(std::string{ key }.c_str(), std::string{ value }.c_str(), true) == 0;
            });
    }

    value_type setenv(macro_argument_type args, const environment_setter& setter)
    {
        builtin::validate_arguments_size(args.arguments, 2);
        const std::string& key{ get_or_throw<std::string>(args.arguments[0]) };
        const std::string& value{ get_or_throw<std::string>(args.arguments[1]) };
        setter(key, value);
        return std::string{};
    }
}
