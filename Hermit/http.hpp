#ifndef HERMIT_HTTP_HPP
#define HERMIT_HTTP_HPP

#include <array>
#include <charconv>
#include <type_traits>
#include <boost/url.hpp>

namespace hermit
{
    struct url_params_setter
    {
        url_params_setter(boost::urls::url& url) : url{ url } {}
        template<typename T> url_params_setter& operator()(std::string_view key, T value);
        template<typename T> url_params_setter& set_if(bool condition, std::string_view key, const T& value);
        template<typename T1, typename T2> url_params_setter& set_if_else(bool condition, std::string_view key_true, const T1& value_true, std::string_view key_false, const T2& value_false);

        boost::urls::url& url;
    };

    template<typename T>
    url_params_setter& url_params_setter::operator()(std::string_view key, T value)
    {
        if constexpr (std::is_convertible_v<T, std::string_view>)
            url.params().set(key, value);
        else if constexpr (std::is_same_v<std::decay_t<T>, bool>)
            url.params().set(key, value ? "true" : "false");
        else if constexpr (std::is_arithmetic_v<T>)
        {
            std::array<char, 64> buffer;
            const auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
            if (ec == std::errc{})
                url.params().set(key, std::string_view{ buffer.data(), static_cast<std::size_t>(ptr - buffer.data()) });
        }
        return *this;
    }

    template<typename T>
    url_params_setter& url_params_setter::set_if(bool condition, std::string_view key, const T& value)
    {
        if (condition) (*this)(key, value);
        return *this;
    }

    template<typename T1, typename T2>
    url_params_setter& url_params_setter::set_if_else(bool condition, std::string_view key_true, const T1& value_true, std::string_view key_false, const T2& value_false)
    {
        if (condition) (*this)(key_true, value_true);
        else (*this)(key_false, value_false);
        return *this;
    }

}

#endif
