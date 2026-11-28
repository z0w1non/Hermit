#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <limits>
#include <locale>
#include <sstream>
#include <string>

#include <boost/lexical_cast.hpp>

#include "builtin.hpp"
#include "config.hpp"
#include "errors.hpp"
#include "filesystem.hpp"
#include "string_utils.hpp"
#include "templates.hpp"

namespace hermit
{
    namespace builtin
    {
        namespace detail
        {
            template<typename T>
            value_type cast_to(const value_type argument);
            template<typename T>
            value_type cast_to(const std::vector<value_type>& arguments);
            value_type head_tail_impl(macro_argument_type args, bool reverse);
        }

        value_type int_(macro_argument_type args)
        {
            return detail::cast_to<int>(args.arguments);
        }

        value_type double_(macro_argument_type args)
        {
            return detail::cast_to<double>(args.arguments);
        }

        value_type char_(macro_argument_type args)
        {
            return detail::cast_to<char>(args.arguments);
        }

        value_type string_(macro_argument_type args)
        {
            return detail::cast_to<std::string>(args.arguments);
        }

        value_type file(macro_argument_type args)
        {
            return file(args, [](std::string_view filename, const config& cfg)
            {
                return filesystem::read_text_file_to_string(filename, cfg);
            });
        }

        value_type file(macro_argument_type args, const text_file_reader& reader)
        {
            validate_arguments_size(args.arguments, 1, 1);
            const std::string_view filename{ get_or_throw<std::string>(args.arguments[0]) };
            return reader(filename, args.cfg);
        }

        value_type head(macro_argument_type args)
        {
            return detail::head_tail_impl(args, false);
        }

        value_type tail(macro_argument_type args)
        {
            return detail::head_tail_impl(args, true);
        }

        value_type head_tail(macro_argument_type args)
        {
            validate_arguments_size(args.arguments, 3, 3);
            const std::string_view str{ get_or_throw<std::string>(args.arguments[0]) };
            const int head_max_tokens{ get_or_throw<int>(args.arguments[1]) };
            const int tail_max_tokens{ get_or_throw<int>(args.arguments[2]) };

            const std::string_view ellipsis{ "..." };
            const int ellipsis_tokens{ args.cfg.lru_cache.get_tokens(ellipsis) };

            const int total_tokens{ args.cfg.lru_cache.get_tokens(str) };

            if (head_max_tokens + ellipsis_tokens + tail_max_tokens >= total_tokens)
            {
                return std::string{ str };
            }

            std::string result;
            const token_count_string truncate_head{ string_utils::truncate_by_tokens(str, head_max_tokens, args.cfg, false) };
            result += truncate_head.str;
            result += ellipsis;
            const token_count_string truncate_tail{ string_utils::truncate_by_tokens(str, tail_max_tokens, args.cfg, true) };
            result += truncate_tail.str;
            return result;
        }

        value_type json_literal(macro_argument_type args)
        {
            validate_arguments_size(args.arguments, 1, 1);
            return string_utils::json_escape_string(get_or_throw<std::string>(args.arguments[0]));
        }

        value_type random(macro_argument_type args)
        {
            return random(args,
                [](std::int64_t min, std::int64_t max)
                {
                    return hermit::random<std::int64_t>(min, max);
                });
        }

        value_type random(macro_argument_type args, const random_source& source)
        {
            validate_arguments_size(args.arguments, 0, 2);
            const std::optional<int> optional_min{ args.arguments.size() > 0 ? get_optional<int>(args.arguments[0]) : std::nullopt };
            const std::optional<int> optional_max{ args.arguments.size() > 1 ? get_optional<int>(args.arguments[1]) : std::nullopt };

            const std::int64_t min{ optional_min ? *optional_min : 0 };
            const std::int64_t max{ optional_max ? *optional_max : static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()) };
            if (min > max)
            {
                hermit::throw_exception(macro_exception{} << error_info::description{ "Random range minimum exceeds its maximum" });
            }

            const std::int64_t result{ source(min, max) };
            if (result < min || result > max)
            {
                hermit::throw_exception(macro_exception{} << error_info::description{ "Random source returned a value outside the requested range" });
            }
            return std::to_string(result);
        }

        value_type choice(macro_argument_type args)
        {
            return choice(args,
                [](std::int64_t min, std::int64_t max)
                {
                    return hermit::random<std::int64_t>(min, max);
                });
        }

        value_type choice(macro_argument_type args, const random_source& source)
        {
            validate_arguments_size(args.arguments, 1);
            const std::int64_t max{ static_cast<std::int64_t>(args.arguments.size() - 1) };
            const std::int64_t index{ source(0, max) };
            if (index < 0 || index > max)
            {
                hermit::throw_exception(macro_exception{} << error_info::description{ "Random source returned a choice index outside the requested range" });
            }
            return args.arguments[static_cast<std::size_t>(index)];
        }

        namespace detail
        {
            template<typename T>
            value_type cast_to(const value_type argument)
            {
                try
                {
                    return boost::apply_visitor([&](const auto& value) { return boost::lexical_cast<T>(value); }, argument);
                }
                catch (const boost::exception&)
                {
                    throw_nested_exception(macro_exception{});
                }
            }

            template<typename T>
            value_type cast_to(const std::vector<value_type>& arguments)
            {
                validate_arguments_size(arguments, 1, 1);
                return cast_to<T>(arguments[0]);
            }

            value_type head_tail_impl(macro_argument_type args, bool reverse)
            {
                validate_arguments_size(args.arguments, 2, 2);
                const std::string_view str{ get_or_throw<std::string>(args.arguments[0]) };
                const int max_tokens{ get_or_throw<int>(args.arguments[1]) };

                const token_count_string truncated{ string_utils::truncate_by_tokens(str, max_tokens, args.cfg, reverse) };
                return truncated.str;
            }
        } // namespace detail
    } // namespace builtin

    // unused
} // namespace Hermit

