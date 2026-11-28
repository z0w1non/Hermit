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

TEST(public_api, adapts_http_stream_executor_with_metadata_and_incremental_callback)
{
    const Hermit::public_api::http_stream_executor executor{ Hermit::public_api::make_http_stream_executor(
        [](std::string_view host, std::string_view port, const Hermit::tcp::request_type& request,
            const Hermit::tcp::stream_handler& handler)
        {
            EXPECT_EQ(host, "stream-host");
            EXPECT_EQ(port, "1234");
            EXPECT_EQ(request.method(), boost::beast::http::verb::post);
            EXPECT_EQ(request[boost::beast::http::field::content_type], "application/json");
            EXPECT_TRUE(handler("data: first\n\n"));
            return Hermit::tcp::stream_response_type{ boost::beast::http::status::ok, 11 };
        }) };
    Hermit::public_api::http_request_data request;
    request.host = "stream-host";
    request.port = "1234";
    request.target = "/v1/responses";
    request.body = R"({"stream":true})";
    std::string chunk;
    const Hermit::public_api::http_stream_response_data response{ executor(request, [&chunk](std::string_view data)
    {
        chunk.append(data);
        return true;
    }) };
    EXPECT_EQ(response.status_code, 200);
    EXPECT_EQ(chunk, "data: first\n\n");
}

TEST(public_api, adapts_http_stream_executor_with_get_method_without_json_content_type)
{
    const Hermit::public_api::http_stream_executor executor{ Hermit::public_api::make_http_stream_executor(
        [](std::string_view, std::string_view, const Hermit::tcp::request_type& request,
            const Hermit::tcp::stream_handler&)
        {
            EXPECT_EQ(request.method(), boost::beast::http::verb::get);
            EXPECT_EQ(request.find(boost::beast::http::field::content_type), request.end());
            return Hermit::tcp::stream_response_type{ boost::beast::http::status::ok, 11 };
        }) };
    Hermit::public_api::http_request_data request;
    request.host = "stream-host";
    request.port = "1234";
    request.target = "/call/api_name/event_id";
    request.method = Hermit::public_api::http_method::get;
    const Hermit::public_api::http_stream_response_data response{ executor(request, [](std::string_view) { return true; }) };
    EXPECT_EQ(response.status_code, 200);
}

TEST(public_api, adapts_delete_http_method_with_json_body)
{
    const Hermit::public_api::http_executor executor{ Hermit::public_api::make_http_executor(
        [](std::string_view, std::string_view, const Hermit::tcp::request_type& request)
        {
            EXPECT_EQ(request.method(), boost::beast::http::verb::delete_);
            EXPECT_EQ(request.body(), R"({"model":"old-model"})");
            Hermit::tcp::response_type response;
            response.result(boost::beast::http::status::ok);
            response.body() = "deleted";
            return response;
        }) };
    Hermit::public_api::http_request_data request;
    request.method = Hermit::public_api::http_method::delete_;
    request.body = R"({"model":"old-model"})";
    EXPECT_EQ(executor(request).body, "deleted");
}

TEST(public_api, adapts_custom_headers_and_accepts_no_content_success)
{
    const Hermit::public_api::http_executor executor{ Hermit::public_api::make_http_executor(
        [](std::string_view, std::string_view, const Hermit::tcp::request_type& request)
        {
            EXPECT_EQ(request["comfy-user"], "user_123");
            Hermit::tcp::response_type response;
            response.result(boost::beast::http::status::no_content);
            return response;
        }) };
    Hermit::public_api::http_request_data request;
    request.method = Hermit::public_api::http_method::delete_;
    request.headers.emplace("comfy-user", "user_123");
    EXPECT_EQ(executor(request).status_code, 204);
}

TEST(public_api, adapts_custom_headers_from_tcp_request)
{
    bool header_forwarded{};
    const Hermit::tcp::request_executor executor{ Hermit::public_api::make_tcp_executor(
        [&header_forwarded](const Hermit::public_api::http_request_data& request)
    {
        header_forwarded = request.headers.at("comfy-user") == "user_123";
        return Hermit::public_api::http_response_data{ 200, "{}", "application/json" };
    }) };
    Hermit::tcp::request_type request{ Hermit::tcp::make_get_json_request("host", "/users") };
    request.set("comfy-user", "user_123");
    static_cast<void>(executor("host", "8188", request));
    EXPECT_TRUE(header_forwarded);
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
