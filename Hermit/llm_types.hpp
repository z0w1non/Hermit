#ifndef HERMIT_LLM_TYPES_HPP
#define HERMIT_LLM_TYPES_HPP

#include <string>
#include <string_view>

#include <boost/describe.hpp>

#include "nlohmann/json.hpp"

namespace hermit
{
    struct LlmBackendParameters
    {
        virtual ~LlmBackendParameters() {}
        virtual nlohmann::json get_request_for_completions(std::string_view prompt, int max_tokens) const = 0;
        virtual std::string parse_response_for_completions(const std::string& response) const = 0;
        virtual nlohmann::json get_request_for_token_count(std::string_view prompt) const = 0;
        virtual int parse_response_for_token_count(const std::string& response) const = 0;
        virtual nlohmann::json get_request_for_chat_completions(const nlohmann::json& messages) const = 0;
        virtual std::string parse_response_for_chat_completions(const std::string& response) const = 0;
        virtual int get_max_tokens() const = 0;
        virtual int get_truncation_length() const = 0;
    };
    BOOST_DEFINE_ENUM_CLASS(llm_mode, completions, chat_completions);
}

#endif
