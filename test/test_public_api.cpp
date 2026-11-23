#include <chrono>
#include <type_traits>

#include <gtest/gtest.h>

#include "Hermit_public.hpp"
#include "Hermit_public_http_adapter.hpp"
#include "Hermit_public_types.hpp"
#include "Hermit_llm.hpp"

TEST(public_api, lightweight_header_keeps_macro_entrypoint_compatible)
{
    using signature = std::string(*)(std::string_view, const Hermit::config&, const Hermit::context&);
    EXPECT_TRUE((std::is_same_v<decltype(&Hermit::expand_macro), signature>));
}

TEST(public_api, type_erased_backend_contract_is_polymorphic)
{
    EXPECT_TRUE(std::is_polymorphic_v<Hermit::public_api::llm_backend>);
    EXPECT_TRUE(std::is_move_constructible_v<Hermit::public_api::http_request>);
}

TEST(public_api, http_executor_contract_carries_transport_metadata)
{
    Hermit::public_api::http_request_data request;
    request.host = "localhost";
    request.port = "8080";
    request.target = "/v1/completions";
    request.body = "{}";
    request.options.connect_timeout = std::chrono::milliseconds{ 1000 };
    request.options.request_timeout = std::chrono::milliseconds{ 2000 };
    request.options.authorization = "Bearer token";

    const Hermit::public_api::http_executor executor{ [](const auto& value)
    {
        Hermit::public_api::http_response_data response;
        response.status_code = value.options.authorization ? 200 : 401;
        response.body = "ok";
        response.content_type = "application/json";
        return response;
    } };

    const auto response{ executor(request) };
    EXPECT_EQ(response.status_code, 200);
    EXPECT_EQ(response.content_type, "application/json");
}

TEST(public_api, public_http_contract_supports_comfyui_binary_and_wait_boundaries)
{
    Hermit::public_api::http_request_data request;
    request.method = Hermit::public_api::http_method::get;
    EXPECT_EQ(request.method, Hermit::public_api::http_method::get);

    Hermit::public_api::http_response_data response;
    response.body.assign("\0\1", 2);
    response.is_binary = true;
    EXPECT_TRUE(response.is_binary);
    EXPECT_EQ(response.body.size(), 2U);

    bool waited{};
    const Hermit::public_api::wait_callback wait{ [&waited](std::chrono::milliseconds duration) { waited = duration.count() == 100; } };
    wait(std::chrono::milliseconds{ 100 });
    EXPECT_TRUE(waited);
}

TEST(public_api, adapts_executor_with_status_and_headers)
{
    const Hermit::public_api::http_executor executor{ Hermit::public_api::make_http_executor(
        [](std::string_view, std::string_view, const Hermit::tcp::request_type&)
        {
            Hermit::tcp::response_type response;
            response.result(boost::beast::http::status::ok);
            response.set(boost::beast::http::field::content_type, "application/json");
            response.body() = "{}";
            return response;
        }) };
    Hermit::public_api::http_request_data request;
    request.host = "host";
    request.port = "80";
    request.target = "/target";
    const auto response{ executor(request) };
    EXPECT_EQ(response.status_code, 200);
    EXPECT_EQ(response.content_type, "application/json");
}

TEST(public_api, marks_binary_download_responses)
{
    const Hermit::public_api::http_executor executor{ Hermit::public_api::make_http_executor(
        [](std::string_view, std::string_view, const Hermit::tcp::request_type&)
        {
            Hermit::tcp::response_type response;
            response.set(boost::beast::http::field::content_type, "image/png");
            response.body() = "binary";
            return response;
        }) };
    Hermit::public_api::http_request_data request;
    request.method = Hermit::public_api::http_method::get;
    const auto response{ executor(request) };
    EXPECT_TRUE(response.is_binary);
}

TEST(public_api, separates_json_body_from_transport_data)
{
    Hermit::public_api::http_transport_options options;
    options.authorization = "Bearer token";
    const auto request{ Hermit::public_api::make_http_request_data("host", "8080", "/target", "{\"prompt\":\"hello\"}", options) };
    EXPECT_EQ(request.host, "host");
    EXPECT_EQ(request.port, "8080");
    EXPECT_EQ(request.target, "/target");
    EXPECT_EQ(request.body, "{\"prompt\":\"hello\"}");
    EXPECT_EQ(request.options.authorization, "Bearer token");
}

TEST(public_api, adapts_tcp_executor_to_string_http_boundary)
{
    std::string captured_host;
    std::string captured_target;
    std::string captured_body;
    const Hermit::public_api::http_request request{ Hermit::public_api::make_http_request(
        [&](std::string_view host, std::string_view target, const Hermit::tcp::request_type& value)
        {
            captured_host = std::string{ host };
            captured_target = std::string{ target };
            captured_body = value.body();
            Hermit::tcp::response_type response;
            response.body() = "ok";
            return response;
        }) };

    EXPECT_EQ(request("host", "/target", "body"), "ok");
    EXPECT_EQ(captured_host, "host");
    EXPECT_EQ(captured_target, "/target");
    EXPECT_EQ(captured_body, "body");
}
