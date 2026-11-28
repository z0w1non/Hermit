#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <utility>

#include <boost/beast/version.hpp>

#include "public_http_adapter.hpp"
#include "errors.hpp"

namespace hermit::public_api
{
    namespace
    {
        bool is_binary_content_type(std::string_view content_type)
        {
            if (content_type.empty()) return false;
            std::string media_type{ content_type.substr(0, content_type.find(';')) };
            std::transform(media_type.begin(), media_type.end(), media_type.begin(), [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
            if (media_type.starts_with("text/")) return false;
            if (media_type == "application/json" || media_type.ends_with("+json")
                || media_type == "application/xml" || media_type.ends_with("+xml")
                || media_type == "application/javascript" || media_type == "application/x-www-form-urlencoded"
                || media_type == "application/graphql-response+json") return false;
            return true;
        }
    }

    void ensure_success(const http_response_data& response)
    {
        if (response.status_code < 200 || response.status_code >= 300)
        {
            hermit::throw_exception
            (
                http_status_exception{}
                << error_info::http::response::status{ response.status_code }
                << error_info::http::response::reason{ std::to_string(response.status_code) }
            );
        }
    }

    http_request_data make_http_request_data(std::string_view host, std::string_view port,
        std::string_view target, std::string body, http_transport_options options)
    {
        http_request_data request{ std::string{ host }, std::string{ port }, std::string{ target }, std::move(body) };
        request.options = std::move(options);
        return request;
    }

    http_request make_http_request(tcp::request_executor executor)
    {
        return [executor = std::move(executor)](std::string_view host, std::string_view target, std::string_view body)
        {
            const tcp::request_type request{ tcp::make_post_json_request(host, target, body) };
            const tcp::response_type response{ executor(host, target, request) };
            tcp::ensure_success(response);
            return response.body();
        };
    }

    http_executor make_http_executor(tcp::request_executor executor)
    {
        return [executor = std::move(executor)](const http_request_data& data)
        {
            tcp::request_type request{ boost::beast::http::verb::get, data.target, 11 };
            request.set(boost::beast::http::field::host, data.host);
            request.set(boost::beast::http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            switch (data.method)
            {
            case http_method::get: request.method(boost::beast::http::verb::get); break;
            case http_method::post: request.method(boost::beast::http::verb::post); break;
            case http_method::put: request.method(boost::beast::http::verb::put); break;
            case http_method::patch: request.method(boost::beast::http::verb::patch); break;
            case http_method::delete_: request.method(boost::beast::http::verb::delete_); break;
            case http_method::head: request.method(boost::beast::http::verb::head); break;
            case http_method::options: request.method(boost::beast::http::verb::options); break;
            }
            request.body() = data.body;
            request.prepare_payload();
            if (!data.content_type.empty())
            {
                request.set(boost::beast::http::field::content_type, data.content_type);
            }
            else if (data.method == http_method::post || data.method == http_method::put || data.method == http_method::patch)
            {
                request.set(boost::beast::http::field::content_type, "application/json");
            }
            if (data.options.authorization)
            {
                request.set(boost::beast::http::field::authorization, *data.options.authorization);
            }
            for (const auto& [name, value] : data.headers)
            {
                request.set(name, value);
            }
            const tcp::response_type response{ executor(data.host, data.port, request) };
            tcp::ensure_success(response);
            http_response_data result;
            result.status_code = response.result_int();
            result.body = response.body();
            if (const auto content_type{ response[boost::beast::http::field::content_type] }; !content_type.empty())
            {
                result.content_type = std::string{ content_type };
            }
            result.is_binary = is_binary_content_type(result.content_type);
            return result;
        };
    }

    http_stream_executor make_http_stream_executor(tcp::stream_request_executor executor)
    {
        return [executor = std::move(executor)](const http_request_data& data, const http_stream_handler& handler)
        {
            const auto method{ data.method == http_method::get ? boost::beast::http::verb::get
                : data.method == http_method::put ? boost::beast::http::verb::put
                : data.method == http_method::patch ? boost::beast::http::verb::patch
                : data.method == http_method::delete_ ? boost::beast::http::verb::delete_
                : data.method == http_method::head ? boost::beast::http::verb::head
                : data.method == http_method::options ? boost::beast::http::verb::options
                : boost::beast::http::verb::post };
            tcp::request_type request{ method, data.target, 11 };
            request.set(boost::beast::http::field::host, data.host);
            request.set(boost::beast::http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            request.body() = data.body;
            request.prepare_payload();
            if (!data.content_type.empty())
            {
                request.set(boost::beast::http::field::content_type, data.content_type);
            }
            else if (data.method == http_method::post || data.method == http_method::put || data.method == http_method::patch)
            {
                request.set(boost::beast::http::field::content_type, "application/json");
            }
            if (data.options.authorization) request.set(boost::beast::http::field::authorization, *data.options.authorization);
            for (const auto& [name, value] : data.headers) request.set(name, value);
            const tcp::stream_response_type response{ executor(data.host, data.port, request, handler) };
            return http_stream_response_data{ static_cast<int>(response.result_int()),
                std::string{ response[boost::beast::http::field::content_type] } };
        };
    }

    tcp::request_executor make_tcp_executor(http_executor executor)
    {
        return [executor = std::move(executor)](std::string_view host, std::string_view port, const tcp::request_type& request)
        {
            http_request_data data;
            data.host = host;
            data.port = port;
            data.target = request.target();
            data.body = request.body();
            const auto method{ request.method() };
            data.method = method == boost::beast::http::verb::get ? http_method::get
                : method == boost::beast::http::verb::put ? http_method::put
                : method == boost::beast::http::verb::patch ? http_method::patch
                : method == boost::beast::http::verb::delete_ ? http_method::delete_
                : method == boost::beast::http::verb::head ? http_method::head
                : method == boost::beast::http::verb::options ? http_method::options : http_method::post;
            data.content_type = std::string{ request[boost::beast::http::field::content_type] };
            for (const auto& field : request)
            {
                data.headers.emplace(std::string{ field.name_string() }, std::string{ field.value() });
            }
            const http_response_data result{ executor(data) };
            ensure_success(result);
            const unsigned body_size{ static_cast<unsigned>(std::min<std::size_t>(result.body.size(), static_cast<std::size_t>(std::numeric_limits<unsigned>::max()))) };
            tcp::response_type response{ static_cast<boost::beast::http::status>(result.status_code), body_size };
            response.body() = result.body;
            response.set(boost::beast::http::field::content_type, result.content_type);
            return response;
        };
    }
}
