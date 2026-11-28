#ifndef HERMIT_TCP_HPP
#define HERMIT_TCP_HPP

#include <chrono>
#include <cstdint>
#include <functional>
#include <string_view>
#include <optional>
#include <string_view>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

namespace hermit
{
    struct tcp
    {
        static constexpr std::uint64_t max_response_body_size{ 128ULL * 1024ULL * 1024ULL };
        using body_type = boost::beast::http::string_body;
        using request_type = boost::beast::http::request<body_type>;
        using response_type = boost::beast::http::response<body_type>;
        using request_executor = std::function<response_type(std::string_view, std::string_view, const request_type&)>;
        using stream_handler = std::function<bool(std::string_view)>;
        using stream_response_type = boost::beast::http::response<boost::beast::http::empty_body>;
        using stream_request_executor = std::function<stream_response_type(std::string_view, std::string_view,
            const request_type&, const stream_handler&)>;
        static void ensure_success(const response_type& response);
        tcp(); ~tcp();
        tcp(const tcp&) = delete; tcp& operator=(const tcp&) = delete;
        tcp(tcp&&) = default; tcp& operator=(tcp&&) = default;
        void connect(std::string_view host, std::string_view port);
        void close() noexcept;
        response_type request(const request_type& request);
        stream_response_type stream_request(const request_type& request, const stream_handler& handler);
        template<typename Rep, typename Period> tcp& expires_after(std::chrono::duration<Rep, Period> timeout)
        {
            if (timeout <= std::chrono::duration<Rep, Period>::zero()) tcp_stream.expires_never();
            else tcp_stream.expires_after(timeout);
            return *this;
        }
        static request_type make_request(boost::beast::http::verb method, std::string_view host, std::string_view target,
            std::optional<std::string_view> content_type = std::nullopt, std::optional<std::string_view> body = std::nullopt);
        static request_type make_post_json_request(std::string_view host, std::string_view target, std::string_view body);
        static request_type make_get_json_request(std::string_view host, std::string_view target);
        template<typename Transport = tcp, typename Duration1 = std::chrono::seconds, typename Duration2 = std::chrono::seconds> static response_type send_http_get(std::string_view host, std::string_view port,
            std::string_view target, Duration1 connect_timeout, Duration2 request_timeout)
        {
            Transport tcp; tcp.expires_after(connect_timeout).connect(host, port);
            request_type request{ make_request(boost::beast::http::verb::get, host, target) };
            return tcp.expires_after(request_timeout).request(request);
        }
        template<typename Transport, typename Duration1, typename Duration2> static response_type send_http_get(Transport& transport,
            std::string_view host, std::string_view port, std::string_view target, Duration1 connect_timeout, Duration2 request_timeout)
        {
            transport.expires_after(connect_timeout).connect(host, port);
            request_type request{ make_request(boost::beast::http::verb::get, host, target) };
            response_type response{ transport.expires_after(request_timeout).request(request) };
            ensure_success(response);
            return response;
        }
    private:
        boost::beast::error_code error_code;
        boost::asio::io_context ioc;
        boost::beast::tcp_stream tcp_stream{ ioc };
        bool connected{};
    };
}

#endif
