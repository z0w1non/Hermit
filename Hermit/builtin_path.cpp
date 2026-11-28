#include "builtin.hpp"
#include "templates.hpp"

#include <filesystem>
#include <functional>
#include <string>

namespace hermit::builtin
{
    namespace
    {
        template<typename Function>
        concept path_to_path_operation
            = std::invocable<Function, const std::filesystem::path>
            && std::same_as<std::invoke_result_t<Function, const std::filesystem::path>, std::filesystem::path>;

        template<path_to_path_operation Function>
        std::string apply_path_operation(macro_argument_type args, Function function)
        {
            builtin::validate_arguments_size(args.arguments, 1, 1);
            return std::invoke(function, std::filesystem::path{ get_or_throw<std::string>(args.arguments[0]) }).string();
        }
    }

    value_type root(macro_argument_type args)
    {
        return apply_path_operation(args, &std::filesystem::path::root_path);
    }

    value_type parent(macro_argument_type args)
    {
        return apply_path_operation(args, &std::filesystem::path::parent_path);
    }

    value_type stem(macro_argument_type args)
    {
        return apply_path_operation(args, &std::filesystem::path::stem);
    }

    value_type extension(macro_argument_type args)
    {
        return apply_path_operation(args, &std::filesystem::path::extension);
    }
}
