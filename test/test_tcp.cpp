#include <chrono>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <boost/asio/write.hpp>

#include "Hermit_tcp.hpp"
#include "Hermit_errors.hpp"
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
