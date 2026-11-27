#ifndef HERMIT_PUBLIC_TYPES_HPP
#define HERMIT_PUBLIC_TYPES_HPP

#include <functional>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace Hermit::public_api
{
    enum class http_method { get, post, delete_ };

    using wait_callback = std::function<void(std::chrono::milliseconds)>;
    using binary_storage = std::function<void(std::string_view path, std::string_view data)>;

    struct http_transport_options
    {
        std::chrono::milliseconds connect_timeout{ 0 };
        std::chrono::milliseconds request_timeout{ 0 };
        std::optional<std::string> authorization;
    };

    struct http_request_data
    {
        std::string host;
        std::string port;
        std::string target;
        std::string body;
        std::string content_type;
        http_method method{ http_method::post };
        http_transport_options options;
        std::map<std::string, std::string> headers;
    };

    struct http_response_data
    {
        int status_code{};
        std::string body;
        std::string content_type;
        bool is_binary{};
    };

    using http_executor = std::function<http_response_data(const http_request_data&)>;

    // Legacy string-only boundary retained for source compatibility.
    using http_request = std::function<std::string(std::string_view host, std::string_view target, std::string_view body)>;

    struct llm_backend
    {
        virtual ~llm_backend() = default;
        virtual std::string complete(std::string_view prompt, int max_tokens) = 0;
        virtual std::string chat(std::string_view messages, int max_tokens) = 0;
    };
}

#endif
