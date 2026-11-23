#ifndef HERMIT_TEMPLATES_HPP
#define HERMIT_TEMPLATES_HPP

#include <concepts>
#include <limits>
#include <random>
#include <type_traits>
#include <utility>
#include <functional>
#include <optional>

#include <boost/lexical_cast.hpp>
#include <boost/variant/get.hpp>

#include "Hermit_context.hpp"
#include "Hermit_templates_core.hpp"
#include "Hermit_errors.hpp"
#include "Hermit_log.hpp"

namespace Hermit
{
    template<std::integral Integer>
    Integer random(Integer min = (std::numeric_limits<Integer>::min)(), Integer max = (std::numeric_limits<Integer>::max)())
    {
        thread_local std::mt19937 random_engine{ std::random_device{}() };
        return std::uniform_int_distribution<Integer>{ min, max }(random_engine);
    }

    struct value_reference_to_string_visitor
    {
        template<typename T>
        std::string operator()(const T& value_reference) const
        {
            if constexpr (std::is_same_v<unwrap_reference_t<T>, std::string>)
            {
                return unwrap(value_reference);
            }
            return boost::lexical_cast<std::string>(unwrap(value_reference));
        }

        [[noreturn]] std::string operator()(const undefined_variable_type& undefined_variable) const
        {
            HERMIT_LOG(warning) << "Failed to convert undefined variable to string (" << undefined_variable.name << ")";
            Hermit::throw_exception(macro_exception{});
        }
    };

    template<typename Result, typename Exception>
    const Result& get_or_throw(const value_type& value)
    {
        if (const Result* ptr{ boost::get<Result>(&value) }; ptr)
        {
            return *ptr;
        }
        Hermit::throw_exception(Exception{});
    }

    template<typename Result>
    std::optional<Result> get_optional(const value_type& value)
    {
        if (const Result* ptr{ boost::get<Result>(&value) }; ptr)
        {
            return *ptr;
        }
        return std::nullopt;
    }
}

#endif
