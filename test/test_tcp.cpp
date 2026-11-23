#include <chrono>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "Hermit_tcp.hpp"
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
