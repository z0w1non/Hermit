#ifndef HERMIT_STRING_UTILS_HPP
#define HERMIT_STRING_UTILS_HPP

#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include "nlohmann/json.hpp"
#include "context.hpp"

namespace hermit
{
    struct config;
    namespace string_utils
    {
    std::string unescape_string(std::string_view str);
    template<typename T>
    T unescape_strings(const T& strings)
    {
        T temp{ strings };
        for (auto& str : temp)
        {
            str = unescape_string(str);
        }
        return temp;
    }
    std::string json_escape_string(std::string_view str);
    std::string truncate_prompt_by_config(std::string_view prompt, const config& cfg);
    std::string truncate_prompt_by_config(std::string_view prompt, const config& cfg,
        const std::function<int(std::string_view)>& token_counter);
    std::string base64_encode(std::string_view encoded_string);
    std::string base64_decode(std::string_view encoded_string);
    bool is_base64(std::string_view str, std::size_t threshold);
    bool has_base64(const nlohmann::json& json, std::size_t threshold);
    std::string console_string_to_u8string(std::string_view input);
    token_count_string truncate_by_tokens(std::string_view string, int max_tokens, const config& cfg, bool reverse);
    token_count_string truncate_by_tokens(std::string_view string, int max_tokens, const config& cfg, bool reverse,
        const std::function<int(std::string_view)>& token_counter);
    void truncate_prompt(std::string_view string, const config& cfg, bool reverse, std::string& result, int& remaining_tokens);
    }
}

#endif
