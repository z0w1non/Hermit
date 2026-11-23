#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "Hermit_config.hpp"
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
