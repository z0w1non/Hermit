#ifndef HERMIT_BUILTIN_HPP
#define HERMIT_BUILTIN_HPP

#include <functional>
#include <cstdint>
#include <utility>
#include <optional>
#include <string>
#include <vector>

#include "Hermit_context.hpp"
#include "Hermit_public_types.hpp"

namespace Hermit
{
    struct config;
    std::string expand_macro(std::string_view input, const config& cfg, const context& ctx);
    namespace builtin
    {
        struct macro_argument_type { const std::vector<value_type>& arguments; const config& cfg; context& ctx; };
        using macro_type = std::function<value_type(macro_argument_type)>;
        using environment_getter = std::function<std::optional<std::string>(std::string_view)>;
        using environment_setter = std::function<bool(std::string_view, std::string_view)>;
        using random_source = std::function<std::int64_t(std::int64_t, std::int64_t)>;
        using process_runner = std::function<std::pair<int, std::string>(std::string_view, const std::vector<std::string>&)>;
        using text_file_reader = std::function<std::string(std::string_view, const config&)>;
        std::optional<macro_type> get_macro(std::string_view name);
        void validate_arguments_size(const std::vector<value_type>& arguments, std::optional<std::size_t> min = std::nullopt, std::optional<std::size_t> max = std::nullopt);
        value_type int_(macro_argument_type); value_type double_(macro_argument_type); value_type char_(macro_argument_type); value_type string_(macro_argument_type);
        value_type file(macro_argument_type); value_type head(macro_argument_type); value_type tail(macro_argument_type); value_type head_tail(macro_argument_type);
        value_type file(macro_argument_type, const text_file_reader&);
        value_type json_literal(macro_argument_type); value_type getenv(macro_argument_type); value_type setenv(macro_argument_type); value_type generated(macro_argument_type);
        value_type generated(macro_argument_type, const public_api::http_executor& executor);
        value_type getenv(macro_argument_type, const environment_getter&);
        value_type setenv(macro_argument_type, const environment_setter&);
        value_type random(macro_argument_type); value_type choice(macro_argument_type); value_type exec(macro_argument_type); value_type code_block(macro_argument_type);
        value_type random(macro_argument_type, const random_source&);
        value_type choice(macro_argument_type, const random_source&);
        value_type exec(macro_argument_type, const process_runner&);
        value_type summary(macro_argument_type); value_type summary(macro_argument_type, const public_api::http_executor& executor);
        value_type root(macro_argument_type); value_type parent(macro_argument_type); value_type stem(macro_argument_type); value_type extension(macro_argument_type);
        std::string date(); std::string time(); std::string datetime(); std::string stdin_(const config& cfg);
    }
}

#endif
