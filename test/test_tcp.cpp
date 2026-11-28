#include <chrono>
#include <array>
#include <exception>
#include <string>
#include <thread>
#include <vector>
#include <utility>

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <boost/asio/write.hpp>

#include "Hermit_tcp.hpp"
#include "Hermit_errors.hpp"
#include "Hermit_public_http_adapter.hpp"
#include "test_support.hpp"

TEST(tcp, injects_transport_for_http_get)
{
    testing::StrictMock<MockHttpTransport> transport;
    Hermit::tcp::response_type response{ boost::beast::http::status::ok, 11 };
    response.body() = "mock-response";
    response.prepare_payload();
    EXPECT_CALL(transport, expires_after(std::chrono::seconds{ 1 })).WillOnce(testing::ReturnRef(transport));
    EXPECT_CALL(transport, connect("localhost", "8080"));
    EXPECT_CALL(transport, expires_after(std::chrono::seconds{ 2 })).WillOnce(testing::ReturnRef(transport));
    EXPECT_CALL(transport, request(testing::_)).WillOnce(testing::Return(response));
    const Hermit::tcp::response_type result{ Hermit::tcp::send_http_get(transport, "localhost", "8080", "/health", std::chrono::seconds{ 1 }, std::chrono::seconds{ 2 }) };
    EXPECT_EQ(result.body(), "mock-response");
}

TEST(tcp, builds_json_request_method_headers_and_body)
{
    const Hermit::tcp::request_type request{ Hermit::tcp::make_post_json_request("example.test", "/generate", "{\"prompt\":\"test\"}") };

    EXPECT_EQ(request.method(), boost::beast::http::verb::post);
    EXPECT_EQ(request.target(), "/generate");
    EXPECT_EQ(request.version(), 11);
    EXPECT_EQ(request[boost::beast::http::field::host], "example.test");
    EXPECT_EQ(request[boost::beast::http::field::content_type], "application/json; charset=UTF-8");
    EXPECT_EQ(request.body(), "{\"prompt\":\"test\"}");

    const Hermit::tcp::request_type get_request{ Hermit::tcp::make_get_json_request("example.test", "/status") };
    EXPECT_EQ(get_request.method(), boost::beast::http::verb::get);
    EXPECT_EQ(get_request[boost::beast::http::field::host], "example.test");
    EXPECT_EQ(get_request[boost::beast::http::field::content_type], "application/json; charset=UTF-8");
    EXPECT_TRUE(get_request.body().empty());
}

TEST(public_http_adapter, preserves_http_methods_for_injected_tcp_executor)
{
    for (const auto& [method, expected] : std::vector<std::pair<Hermit::public_api::http_method, boost::beast::http::verb>>
        {{Hermit::public_api::http_method::get, boost::beast::http::verb::get},
        {Hermit::public_api::http_method::post, boost::beast::http::verb::post},
        {Hermit::public_api::http_method::put, boost::beast::http::verb::put},
        {Hermit::public_api::http_method::patch, boost::beast::http::verb::patch},
        {Hermit::public_api::http_method::delete_, boost::beast::http::verb::delete_},
        {Hermit::public_api::http_method::head, boost::beast::http::verb::head},
        {Hermit::public_api::http_method::options, boost::beast::http::verb::options}})
    {
        const Hermit::tcp::request_executor tcp_executor{ [expected](std::string_view, std::string_view, const Hermit::tcp::request_type& request)
        {
            EXPECT_EQ(request.target(), "/extension/custom");
            EXPECT_EQ(request.method(), expected);
            EXPECT_EQ(request.body(), "{}");
            if (expected == boost::beast::http::verb::post || expected == boost::beast::http::verb::put
                || expected == boost::beast::http::verb::patch)
            {
                EXPECT_EQ(request[boost::beast::http::field::content_type], "application/json");
            }
            else
            {
                EXPECT_TRUE(request[boost::beast::http::field::content_type].empty());
            }
            Hermit::tcp::response_type response{ boost::beast::http::status::ok, 0 };
            response.prepare_payload();
            return response;
        } };
        const Hermit::public_api::http_executor executor{ Hermit::public_api::make_http_executor(tcp_executor) };
        Hermit::public_api::http_request_data request;
        request.host = "localhost";
        request.port = "7860";
        request.target = "/extension/custom";
        request.body = "{}";
        request.method = method;
        const Hermit::public_api::http_response_data response{ executor(request) };
        EXPECT_EQ(response.status_code, 200);
    }
}

TEST(public_http_adapter, omits_automatic_json_content_type_for_bodyless_methods)
{
    for (const Hermit::public_api::http_method method : { Hermit::public_api::http_method::get,
        Hermit::public_api::http_method::delete_, Hermit::public_api::http_method::head,
        Hermit::public_api::http_method::options })
    {
        const Hermit::tcp::request_executor tcp_executor{ [method](std::string_view, std::string_view,
            const Hermit::tcp::request_type& request)
        {
            EXPECT_TRUE(request.body().empty());
            EXPECT_TRUE(request[boost::beast::http::field::content_type].empty());
            if (method == Hermit::public_api::http_method::get) EXPECT_EQ(request.method(), boost::beast::http::verb::get);
            else if (method == Hermit::public_api::http_method::delete_) EXPECT_EQ(request.method(), boost::beast::http::verb::delete_);
            else if (method == Hermit::public_api::http_method::head) EXPECT_EQ(request.method(), boost::beast::http::verb::head);
            else EXPECT_EQ(request.method(), boost::beast::http::verb::options);
            Hermit::tcp::response_type response{ boost::beast::http::status::ok, 0 };
            response.prepare_payload();
            return response;
        } };
        const Hermit::public_api::http_executor executor{ Hermit::public_api::make_http_executor(tcp_executor) };
        Hermit::public_api::http_request_data request;
        request.host = "localhost";
        request.port = "7860";
        request.target = "/extension/custom";
        request.method = method;
        EXPECT_EQ(executor(request).status_code, 200);
    }
}

TEST(public_http_adapter, classifies_binary_and_json_response_content_types)
{
    const Hermit::tcp::request_executor tcp_executor{ [](std::string_view, std::string_view, const Hermit::tcp::request_type& request)
    {
        Hermit::tcp::response_type response{ boost::beast::http::status::ok, 3 };
        response.body() = "raw";
        response.set(boost::beast::http::field::content_type,
            request.target() == "/binary" ? "application/pdf" : "application/vnd.example+json; charset=utf-8");
        response.prepare_payload();
        return response;
    } };
    const Hermit::public_api::http_executor executor{ Hermit::public_api::make_http_executor(tcp_executor) };
    Hermit::public_api::http_request_data request;
    request.host = "localhost";
    request.port = "7860";
    request.method = Hermit::public_api::http_method::get;
    request.target = "/binary";
    EXPECT_TRUE(executor(request).is_binary);
    request.target = "/json";
    EXPECT_FALSE(executor(request).is_binary);
}

TEST(tcp, injected_transport_rejects_non_success_http_status)
{
    testing::StrictMock<MockHttpTransport> transport;
    Hermit::tcp::response_type response{ boost::beast::http::status::internal_server_error, 0 };
    response.prepare_payload();
    EXPECT_CALL(transport, expires_after(std::chrono::seconds{ 1 })).WillOnce(testing::ReturnRef(transport));
    EXPECT_CALL(transport, connect("localhost", "8080"));
    EXPECT_CALL(transport, expires_after(std::chrono::seconds{ 2 })).WillOnce(testing::ReturnRef(transport));
    EXPECT_CALL(transport, request(testing::_)).WillOnce(testing::Return(response));

    EXPECT_THROW(Hermit::tcp::send_http_get(transport, "localhost", "8080", "/health",
        std::chrono::seconds{ 1 }, std::chrono::seconds{ 2 }), Hermit::http_status_exception);
}

TEST(tcp, rejects_response_body_over_limit_from_content_length)
{
    boost::asio::io_context server_context;
    boost::asio::ip::tcp::acceptor acceptor{
        server_context,
        boost::asio::ip::tcp::endpoint{ boost::asio::ip::address_v4::loopback(), 0 }
    };
    const std::string port{ std::to_string(acceptor.local_endpoint().port()) };
    std::thread server{ [&acceptor]
    {
        boost::asio::ip::tcp::socket socket{ acceptor.get_executor() };
        acceptor.accept(socket);
        const std::string response{
            "HTTP/1.1 200 OK\r\nContent-Length: "
            + std::to_string(Hermit::tcp::max_response_body_size + 1)
            + "\r\nConnection: close\r\n\r\n"
        };
        boost::asio::write(socket, boost::asio::buffer(response));
    } };

    Hermit::tcp transport;
    transport.connect("127.0.0.1", port);
    const Hermit::tcp::request_type request{ Hermit::tcp::make_get_json_request("127.0.0.1", "/oversized") };
    EXPECT_THROW(transport.request(request), Hermit::http_receive_exception);
    server.join();
}

TEST(tcp, delivers_http_response_body_incrementally_to_stream_handler)
{
    boost::asio::io_context server_context;
    boost::asio::ip::tcp::acceptor acceptor{
        server_context,
        boost::asio::ip::tcp::endpoint{ boost::asio::ip::address_v4::loopback(), 0 }
    };
    const std::string port{ std::to_string(acceptor.local_endpoint().port()) };
    const std::string body{ "event: response.created\ndata: {}\n\nevent: response.completed\ndata: {}\n\n" };
    std::exception_ptr server_error;
    std::thread server{ [&acceptor, &body, &server_error]
    {
        try
        {
            boost::asio::ip::tcp::socket socket{ acceptor.get_executor() };
            acceptor.accept(socket);
            std::array<char, 4096> request{};
            static_cast<void>(socket.read_some(boost::asio::buffer(request)));
            const std::string header{ "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: "
                + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" };
            boost::asio::write(socket, boost::asio::buffer(header));
            boost::asio::write(socket, boost::asio::buffer(body));
        }
        catch (...)
        {
            server_error = std::current_exception();
        }
    } };

    Hermit::tcp transport;
    transport.expires_after(std::chrono::seconds{ 2 }).connect("127.0.0.1", port);
    Hermit::tcp::request_type request{ Hermit::tcp::make_post_json_request("localhost", "/v1/responses", "{}") };
    request.prepare_payload();
    std::string received;
    std::size_t callback_count{};
    Hermit::tcp::stream_response_type response;
    std::exception_ptr request_error;
    try
    {
        response = transport.expires_after(std::chrono::seconds{ 2 }).stream_request(
            request, [&received, &callback_count](std::string_view chunk)
        {
            received.append(chunk);
            ++callback_count;
            return true;
        });
    }
    catch (...)
    {
        request_error = std::current_exception();
    }
    server.join();
    if (server_error) std::rethrow_exception(server_error);
    if (request_error) std::rethrow_exception(request_error);
    EXPECT_EQ(response.result_int(), 200);
    EXPECT_EQ(response[boost::beast::http::field::content_type], "text/event-stream");
    EXPECT_EQ(received, body);
    EXPECT_GT(callback_count, 0U);
}
