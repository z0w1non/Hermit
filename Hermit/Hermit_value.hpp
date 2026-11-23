#ifndef HERMIT_VALUE_HPP
#define HERMIT_VALUE_HPP

#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Hermit
{
    template<typename Key, typename T, typename Compare = std::less<>, typename Allocator = std::allocator<std::pair<const Key, T>>>
    using transparent_map = std::map<Key, T, Compare>;
    template<typename T> using string_map = std::map<std::string, T, std::less<>>;
    template<typename T> using string_view_map = std::map<std::string_view, T, std::less<>>;
    template<typename Key, typename T, typename Allocator = std::allocator<std::pair<const Key, T>>>
    using transparent_unordered_map = std::unordered_map<Key, T, std::hash<Key>, std::equal_to<>, Allocator>;
    struct string_hash
    {
        using is_transparent = void;
        std::size_t operator()(std::string_view sv) const noexcept;
        std::size_t operator()(const std::string& s) const noexcept;
        std::size_t operator()(const char* s) const noexcept;
    };
    template<typename T> using string_unordered_map = std::unordered_map<std::string, T, string_hash, std::equal_to<>>;
    template<typename T> using string_view_unordered_map = std::unordered_map<std::string_view, T, string_hash, std::equal_to<>>;
}

#endif
