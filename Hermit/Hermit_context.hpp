#ifndef HERMIT_CONTEXT_HPP
#define HERMIT_CONTEXT_HPP

#include <functional>
#include <optional>
#include <string>

#include <boost/variant.hpp>

#include "Hermit_errors.hpp"
#include "Hermit_value.hpp"

namespace Hermit
{
    using value_type = boost::variant<int, bool, char, double, std::string>;
    struct undefined_variable_type { std::string name; };
    template<typename ... Args>
    using basic_value_reference_type = boost::variant<Args ..., std::reference_wrapper<Args> ..., undefined_variable_type>;
    using value_reference_type = basic_value_reference_type<int, bool, char, double, std::string>;
    template<typename T> decltype(auto) unwrap(T&& arg);
    template<typename T> struct unwrap_reference;
    template<typename T> using unwrap_reference_t = typename unwrap_reference<T>::type;
    std::string value_to_string(const value_type& value);
    std::string value_reference_to_string(const value_reference_type& value_reference);
    template<typename Result, typename Exception = macro_exception>
    const Result& get_or_throw(const value_type& value);
    template<typename Result> std::optional<Result> get_optional(const value_type& value);
    struct context
    {
        using variable_map_type = string_unordered_map<value_type>;
        context();
        context& operator=(const context&) = delete;
        context(context&&) = delete;
        context& operator=(context&&) = delete;
        context make_pushed() const;
        void set(std::string_view key, const value_type& value);
        void erase(std::string_view key);
        const value_type* get(std::string_view key) const;
        value_type* get(std::string_view key);
    private:
        context(const context& ctx);
        variable_map_type variable_map;
        const context* base{};
    };
    struct token_count_string { std::string str; int tokens{}; };
}

#endif
