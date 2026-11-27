#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "Hermit_config.hpp"
#include "Hermit_errors.hpp"
#include "Hermit_generation.hpp"
#include "Hermit_tcp.hpp"

TEST(sb, request_url)
{
    Hermit::sb_parameters parameters;
    parameters.host = "127.0.0.1";
    parameters.port = "5000";
    const std::string url{ Hermit::sb::make_request_url("/voice", parameters, "hello world") };
    EXPECT_FALSE(url.empty());
    EXPECT_THAT(url, testing::HasSubstr("hello"));
}

TEST(sb, request_url_includes_optional_parameters)
{
    Hermit::sb_parameters parameters;
    parameters.host = "localhost";
    parameters.port = "5001";
    parameters.model_name = "model";
    parameters.speaker_name = "speaker";
    parameters.assist_text = "assist";
    parameters.style = "calm";
    const std::string url{ Hermit::sb::make_request_url("/voice", parameters, "hello world") };
    EXPECT_THAT(url, testing::HasSubstr("model_name=model"));
    EXPECT_THAT(url, testing::HasSubstr("speaker_name=speaker"));
    EXPECT_THAT(url, testing::HasSubstr("assist_text=assist"));
    EXPECT_THAT(url, testing::HasSubstr("style=calm"));
}

TEST(sb, request_url_uses_ids_when_names_are_empty)
{
    Hermit::sb_parameters parameters;
    parameters.model_id = 2;
    parameters.speaker_id = 7;
    const std::string url{ Hermit::sb::make_request_url("/voice", parameters, "hello") };
    EXPECT_THAT(url, testing::HasSubstr("model_id=2"));
    EXPECT_THAT(url, testing::HasSubstr("speaker_id=7"));
    EXPECT_THAT(url, testing::Not(testing::HasSubstr("model_name=")));
    EXPECT_THAT(url, testing::Not(testing::HasSubstr("speaker_name=")));
}

TEST(sb, injects_request_executor)
{
    Hermit::config cfg;
    cfg.sb.host = "localhost";
    cfg.sb.port = "5000";
    cfg.sb.target = "/voice";
    const Hermit::tcp::request_executor executor = [](std::string_view host, std::string_view port, const Hermit::tcp::request_type& request)
    {
        EXPECT_EQ(host, "localhost");
        EXPECT_EQ(port, "5000");
        EXPECT_THAT(std::string{ request.target() }, testing::HasSubstr("text=hello"));
        Hermit::tcp::response_type response{ boost::beast::http::status::ok, 11 };
        response.body() = "audio-data";
        response.prepare_payload();
        return response;
    };
    EXPECT_EQ(Hermit::sb::send_request(cfg, "hello", executor), "audio-data");
}

TEST(sb, rejects_non_success_status_from_injected_executors)
{
    Hermit::config cfg;
    const Hermit::tcp::request_executor tcp_executor{ [](std::string_view, std::string_view, const Hermit::tcp::request_type&)
    {
        return Hermit::tcp::response_type{ boost::beast::http::status::internal_server_error, 0 };
    } };
    const Hermit::public_api::http_executor http_executor{ [](const Hermit::public_api::http_request_data&)
    {
        return Hermit::public_api::http_response_data{ 500, "server error" };
    } };

    EXPECT_THROW(Hermit::sb::send_request(cfg, "text", tcp_executor), Hermit::http_status_exception);
    EXPECT_THROW(Hermit::sb::send_request(cfg, "text", http_executor), Hermit::http_status_exception);
}

TEST(sb, injects_read_only_model_status_and_phoneme_requests)
{
    Hermit::config cfg;
    cfg.sb.host = "sb.example";
    cfg.sb.port = "5001";
    std::vector<Hermit::public_api::http_request_data> requests;
    const Hermit::public_api::http_executor executor{ [&requests](const Hermit::public_api::http_request_data& request)
    {
        requests.push_back(request);
        return Hermit::public_api::http_response_data{ 200, R"({"ok":true})" };
    } };

    EXPECT_EQ(Hermit::sb::get_models_info(cfg, executor).at("ok"), true);
    EXPECT_EQ(Hermit::sb::get_status(cfg, executor).at("ok"), true);
    EXPECT_EQ(Hermit::sb::get_phonemes(cfg, "こんにちは 世界", executor).at("ok"), true);

    ASSERT_EQ(requests.size(), 3);
    EXPECT_EQ(requests[0].method, Hermit::public_api::http_method::get);
    EXPECT_EQ(requests[0].target, "/models/info");
    EXPECT_EQ(requests[1].method, Hermit::public_api::http_method::get);
    EXPECT_EQ(requests[1].target, "/status");
    EXPECT_EQ(requests[2].method, Hermit::public_api::http_method::post);
    EXPECT_THAT(requests[2].target, testing::HasSubstr("/g2p?text="));
    EXPECT_THAT(requests[2].target, testing::Not(testing::HasSubstr("こんにちは 世界")));
    EXPECT_EQ(requests[2].body, "");
}

TEST(sb, refreshes_models_with_post_json_and_returns_model_listing)
{
    Hermit::config cfg;
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, Hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/models/refresh");
        EXPECT_EQ(request.body, "{}");
        EXPECT_EQ(request.content_type, "application/json");
        return Hermit::public_api::http_response_data{ 200, R"({"0":{"model_path":"model.pth"}})" };
    } };

    EXPECT_EQ(Hermit::sb::refresh_models(cfg, executor).at("0").at("model_path"), "model.pth");
}

TEST(sb, exposes_post_voice_and_binary_audio_download)
{
    Hermit::config cfg;
    cfg.sb.target = "/voice";
    std::vector<Hermit::public_api::http_request_data> requests;
    const Hermit::public_api::http_executor executor{ [&requests](const Hermit::public_api::http_request_data& request)
    {
        requests.push_back(request);
        return Hermit::public_api::http_response_data{ 200, std::string{ "\0\1\2", 3 }, "audio/wav", true };
    } };

    EXPECT_EQ(Hermit::sb::send_post_request(cfg, "hello", executor), std::string("\0\1\2", 3));
    EXPECT_EQ(Hermit::sb::get_audio(cfg, "C:\\models\\speech.wav", executor), std::string("\0\1\2", 3));

    ASSERT_EQ(requests.size(), 2);
    EXPECT_EQ(requests[0].method, Hermit::public_api::http_method::post);
    EXPECT_THAT(requests[0].target, testing::HasSubstr("/voice?text=hello"));
    EXPECT_EQ(requests[1].method, Hermit::public_api::http_method::get);
    EXPECT_THAT(requests[1].target, testing::HasSubstr("/tools/get_audio?path="));
    EXPECT_THAT(requests[1].target, testing::Not(testing::HasSubstr("C:\\models\\speech.wav")));
}

TEST(sb, metadata_and_status_operations_propagate_http_and_json_errors)
{
    Hermit::config cfg;
    const Hermit::public_api::http_executor failed_executor{ [](const Hermit::public_api::http_request_data&)
    {
        return Hermit::public_api::http_response_data{ 503, "unavailable" };
    } };
    const Hermit::public_api::http_executor invalid_json_executor{ [](const Hermit::public_api::http_request_data&)
    {
        return Hermit::public_api::http_response_data{ 200, "not-json" };
    } };

    EXPECT_THROW(Hermit::sb::get_models_info(cfg, failed_executor), Hermit::http_status_exception);
    EXPECT_THROW(Hermit::sb::refresh_models(cfg, failed_executor), Hermit::http_status_exception);
    EXPECT_THROW(Hermit::sb::get_status(cfg, failed_executor), Hermit::http_status_exception);
    EXPECT_THROW(Hermit::sb::get_phonemes(cfg, "text", failed_executor), Hermit::http_status_exception);
    EXPECT_THROW(Hermit::sb::send_post_request(cfg, "text", failed_executor), Hermit::http_status_exception);
    EXPECT_THROW(Hermit::sb::get_audio(cfg, "audio.wav", failed_executor), Hermit::http_status_exception);
    EXPECT_THROW(Hermit::sb::get_status(cfg, invalid_json_executor), nlohmann::json::exception);
}
