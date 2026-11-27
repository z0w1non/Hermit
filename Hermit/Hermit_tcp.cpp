#include "Hermit_tcp.hpp"
#include "Hermit_errors.hpp"
#include "Hermit_log.hpp"
#include <boost/beast/version.hpp>

namespace Hermit
{
    tcp::tcp()
    {
    }

    tcp::~tcp()
    {
        close();
    }

    void tcp::connect(std::string_view host, std::string_view port)
    {
        boost::asio::ip::tcp::resolver resolver{ ioc };
        const boost::asio::ip::tcp::resolver::results_type endpoints{ resolver.resolve(host, port) };
        tcp_stream.connect(endpoints, error_code);
        if (error_code)
        {
            Hermit::throw_exception
            (
                connect_exception{}
                << error_info::beast::error_code{ error_code }
                << error_info::host{ std::string{ host } }
                << error_info::port{ std::string{ port } }
            );
        }
        connected = true;
        HERMIT_LOG(info) << "Connect " << host << ":" << port;
    }

    void tcp::close() noexcept
    {
        if (connected)
        {
            tcp_stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, error_code);
            tcp_stream.socket().close(error_code);
            connected = false;
        }
    }

    void tcp::ensure_success(const response_type& response)
    {
        if (response.result() != boost::beast::http::status::ok)
        {
            Hermit::throw_exception
            (
                http_status_exception{}
                << error_info::http::response::status{ static_cast<int>(response.result_int()) }
                << error_info::http::response::reason{ std::to_string(response.result_int()) }
            );
        }
    }

    tcp::response_type tcp::request(const tcp::request_type& request)
    {
        boost::beast::http::write(tcp_stream, request, error_code);
        if (error_code)
        {
            Hermit::throw_exception
            (
                http_send_exception{} << error_info::beast::error_code{ error_code }
            );
        }

        boost::beast::flat_buffer buffer;
        boost::beast::http::response_parser<body_type> parser;
        parser.body_limit(max_response_body_size);
        boost::beast::http::read(tcp_stream, buffer, parser, error_code);
        if (error_code)
        {
            Hermit::throw_exception
            (
                http_receive_exception{} << error_info::beast::error_code{ error_code }
            );
        }
        const response_type response{ parser.release() };

        ensure_success(response);

        return response;
    }

    tcp::request_type tcp::make_request
    (
        boost::beast::http::verb method,
        std::string_view host,
        std::string_view target,
        std::optional<std::string_view> content_type,
        std::optional<std::string_view> body
    )
    {
        request_type request{ method, target, 11 };
        request.set(boost::beast::http::field::host, host);
        request.set(boost::beast::http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        if (content_type)
        {
            request.set(boost::beast::http::field::content_type, *content_type);
        }
        if (body)
        {
            request.body() = *body;
        }
        return request;
    }

    tcp::request_type tcp::make_post_json_request
    (
        std::string_view host,
        std::string_view target,
        std::string_view body
    )
    {
        return make_request(boost::beast::http::verb::post, host, target, "application/json; charset=UTF-8", body);
    }

    tcp::request_type tcp::make_get_json_request
    (
        std::string_view host,
        std::string_view target
    )
    {
        return make_request(boost::beast::http::verb::get, host, target, "application/json; charset=UTF-8", std::nullopt);
    }

} // namespace Hermit
