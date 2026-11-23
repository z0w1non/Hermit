#include "Hermit_public_http_adapter.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace Hermit::public_api
{
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
            return executor(host, target, request).body();
        };
    }

    http_executor make_http_executor(tcp::request_executor executor)
    {
        return [executor = std::move(executor)](const http_request_data& data)
        {
            tcp::request_type request{ data.method == http_method::get
                ? tcp::make_get_json_request(data.host, data.target)
                : tcp::make_post_json_request(data.host, data.target, data.body) };
            if (!data.content_type.empty())
            {
                request.set(boost::beast::http::field::content_type, data.content_type);
            }
            if (data.options.authorization)
            {
                request.set(boost::beast::http::field::authorization, *data.options.authorization);
            }
            const tcp::response_type response{ executor(data.host, data.port, request) };
            http_response_data result;
            result.status_code = response.result_int();
            result.body = response.body();
            if (const auto content_type{ response[boost::beast::http::field::content_type] }; !content_type.empty())
            {
                result.content_type = std::string{ content_type };
            }
            result.is_binary = result.content_type.rfind("image/", 0) == 0 || result.content_type == "application/octet-stream";
            return result;
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
            data.method = request.method() == boost::beast::http::verb::get ? http_method::get : http_method::post;
            data.content_type = std::string{ request[boost::beast::http::field::content_type] };
            const http_response_data result{ executor(data) };
            const unsigned body_size{ static_cast<unsigned>(std::min<std::size_t>(result.body.size(), static_cast<std::size_t>(std::numeric_limits<unsigned>::max()))) };
            tcp::response_type response{ static_cast<boost::beast::http::status>(result.status_code), body_size };
            response.body() = result.body;
            response.set(boost::beast::http::field::content_type, result.content_type);
            return response;
        };
    }
}
