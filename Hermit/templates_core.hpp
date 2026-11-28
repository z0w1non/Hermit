#ifndef HERMIT_TEMPLATES_CORE_HPP
#define HERMIT_TEMPLATES_CORE_HPP

#include <functional>
#include <type_traits>
#include <utility>

namespace hermit
{
    namespace detail
    {
        struct unwrap
        {
            template<typename T> decltype(auto) operator()(T&& value) { return std::forward<T>(value); }
            template<typename T> T& operator()(std::reference_wrapper<T> ref) { return ref.get(); }
            template<typename T> const T& operator()(std::reference_wrapper<const T> ref) { return ref.get(); }
        };
    }

    template<typename T> struct unwrap_reference { using type = T; };
    template<typename T> struct unwrap_reference<std::reference_wrapper<T>> { using type = T; };
    template<typename T> using unwrap_reference_t = typename unwrap_reference<T>::type;
    template<typename T> decltype(auto) unwrap(T&& arg) { return detail::unwrap{}(std::forward<T>(arg)); }
    template<typename T> using decay_t = typename unwrap_reference<std::decay_t<T>>::type;
}

#endif
