#include "Hermit_string_utils.hpp"
#include "Hermit_config.hpp"
#include "Hermit_platform.hpp"

#include <boost/algorithm/string.hpp>

namespace Hermit
{
    namespace string_utils
    {
        std::string unescape_string(std::string_view str)
        {
            std::string result;
            result.reserve(str.size());

            bool in_escape{};

            for (const char c : str)
            {
                if (in_escape)
                {
                    switch (c)
                    {
                    case '\"': result += '\"'; break;
                    case '\'': result += '\''; break;
                    case '\\': result += '\\'; break;
                    case 'a':  result += '\a'; break;
                    case 'b':  result += '\b'; break;
                    case 'f':  result += '\f'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    default:
                        result += '\\';
                        result += c;
                        break;
                    }
                    in_escape = false;
                }
                else
                {
                    if (c == '\\')
                    {
                        in_escape = true;
                    }
                    else
                    {
                        result += c;
                    }
                }
            }

            if (in_escape)
            {
                result += '\\';
            }

            return result;
        }

        std::string json_escape_string(std::string_view str)
        {
            constexpr std::size_t escape_overhead_denominator{ 8 };
            std::string result;
            result.reserve(str.size() + str.size() / escape_overhead_denominator);

            for (const char c : str)
            {
                switch (c)
                {
                case '"':   result += "\\\""; break;
                case '\\':  result += "\\\\"; break;
                case '\b':  result += "\\b"; break;
                case '\f':  result += "\\f"; break;
                case '\n':  result += "\\n"; break;
                case '\r':  result += "\\r"; break;
                case '\t':  result += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20)
                    {
                        char buf[7];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                        result.append(buf, std::size(buf) - 1);
                    }
                    else
                    {
                        result.push_back(c);
                    }
                    break;
                }
            }

            return result;
        }

        std::string truncate_prompt_by_config(std::string_view prompt, const config& cfg)
        {
            std::string result;
            int remaining_tokens{ cfg.llm.backend->get_truncation_length() - cfg.llm.backend->get_max_tokens() };
            truncate_prompt(prompt, cfg, false, result, remaining_tokens);
            return result;
        }

        std::string base64_encode(std::string_view input)
        {
            using namespace boost::archive::iterators;
            using iterator = base64_from_binary<transform_width<std::string::const_iterator, 6, 8>>;

            const std::size_t missing_size{ (3 - input.size() % 3) % 3 };
            std::string padded_input{ input };
            padded_input.append(missing_size, '\0');

            std::string encoded{ iterator{ padded_input.begin() }, iterator{ padded_input.end() } };
            if (missing_size > 0)
            {
                encoded.replace(encoded.size() - missing_size, missing_size, missing_size, '=');
            }
            return encoded;
        }

        std::string base64_decode(std::string_view input)
        {
            using namespace boost::archive::iterators;
            using iterator = transform_width<binary_from_base64<std::string_view::const_iterator>, 8, 6>;

            std::size_t padding_count{};
            while (padding_count < input.size() && input[input.size() - 1 - padding_count] == '=')
            {
                ++padding_count;
            }

            const std::string_view trimed_input{ input.substr(0, input.size() - padding_count) };
            std::string decoded{ iterator{ input.begin() }, iterator{ input.end() } };

            std::size_t expected_size{ input.size() / 4 * 3 - padding_count };
            if (decoded.size() > expected_size)
            {
                decoded.resize(expected_size);
            }

            return decoded;
        }

        bool is_base64(std::string_view str, std::size_t threshold)
        {
            if (str.size() >= threshold && str.size() % 4 == 0)
            {
                std::size_t padding_count{};
                for (std::size_t i{}; i < str.size(); ++i)
                {
                    const char c{ str[i] };
                    if (std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '/')
                    {
                        if (padding_count > 0)
                        {
                            return false;
                        }
                    }
                    else if (c == '=')
                    {
                        ++padding_count;
                        if (padding_count > 2 || i + padding_count < str.size())
                        {
                            return false;
                        }
                    }
                    else
                    {
                        return false;
                    }
                }
                return true;
            }
            return false;
        }

        bool has_base64(const nlohmann::json& json, std::size_t threshold)
        {
            if (json.is_array())
            {
                for (const auto& value : json)
                {
                    if (has_base64(value, threshold))
                    {
                        return true;
                    }
                }
            }
            else if (json.is_object())
            {
                for (const auto& [key, value] : json.items())
                {
                    if (has_base64(value, threshold))
                    {
                        return true;
                    }
                }
            }
            else if (json.is_string())
            {
                const std::string_view str{ json.get_ref<const std::string&>() };
                if (is_base64(str, threshold))
                {
                    return true;
                }
            }
            return false;
        }

        std::string console_string_to_u8string(std::string_view input)
        {
#if BOOST_OS_WINDOWS
            if (input.empty())
            {
                return std::string{};
            }

            const UINT cp{ GetConsoleOutputCP() };
            if (cp == CP_UTF8)
            {
                return std::string{ input };
            }

            const int wstring_length{ MultiByteToWideChar(cp, 0, input.data(), static_cast<int>(input.size()), nullptr, 0) };
            if (wstring_length <= 0)
            {
                return std::string{ input };
            }

            std::wstring wstring(wstring_length, L'\0');
            MultiByteToWideChar(cp, 0, input.data(), static_cast<int>(input.size()), wstring.data(), wstring_length);

            const int u8string_length{ WideCharToMultiByte(CP_UTF8, 0, wstring.data(), wstring_length, nullptr, 0, nullptr, nullptr) };
            if (u8string_length <= 0)
            {
                return std::string{ input };
            }

            std::string u8_string(u8string_length, '\0');
            WideCharToMultiByte(CP_UTF8, 0, wstring.data(), wstring_length, u8_string.data(), u8string_length, nullptr, nullptr);

            return u8_string;
#else
            return std::string{ input };
#endif
        }

        token_count_string truncate_by_tokens(std::string_view string, int max_tokens, const config& cfg, bool reverse)
        {
            token_count_string result;

            std::vector<std::string> lines;
            boost::split(lines, string, boost::is_any_of("\n"));
            std::vector<std::string> temp;

            const auto truncate = [&](auto first, auto last)
                {
                    for (; first != last; ++first)
                    {
                        const int next_tokens{ cfg.lru_cache.get_tokens(*first) };
                        if (result.tokens + next_tokens > max_tokens)
                        {
                            break;
                        }
                        temp.push_back(*first);
                        result.tokens += next_tokens;
                    }
                };

            if (reverse)
            {
                truncate(lines.rbegin(), lines.rend());
                std::reverse(temp.begin(), temp.end());
            }
            else
            {
                truncate(lines.begin(), lines.end());
            }

            for (const std::string& line : temp)
            {
                result.str += line;
            }

            return result;
        }

        void truncate_prompt(std::string_view string, const config& cfg, bool reverse, std::string& result, int& remaining_tokens)
        {
            const token_count_string cruncated{ truncate_by_tokens(string, remaining_tokens, cfg, reverse) };
            result += cruncated.str;
            remaining_tokens -= cruncated.tokens;
        }
    } // namespace string_utils
} // namespace Hermit
