#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "Hermit_config.hpp"
#include "Hermit_generation.hpp"
#include "Hermit_tcp.hpp"

TEST(sd, png_parameter_serialization)
{
    Hermit::sd_parameters parameters;
    parameters.common.steps = 20;
    parameters.common.cfg_scale = 7.5;
    const std::string result{ Hermit::sd::make_png_parameters(parameters, "prompt", "negative") };
    EXPECT_THAT(result, testing::HasSubstr("prompt"));
    EXPECT_THAT(result, testing::HasSubstr("negative"));
    EXPECT_THAT(result, testing::HasSubstr("Steps: 20"));
}

TEST(sd, builds_txt2img_request_with_optional_scripts)
{
    Hermit::config cfg;
    cfg.sd.mode = Hermit::sd_mode::txt2img;
    cfg.sd.common.sampler_name = "Euler a";
    cfg.sd.common.alwayson_scripts.adetailer_parameters.ad_enable = true;
    cfg.sd.common.alwayson_scripts.adetailer_parameters.args1.ad_model = "face_yolov8n.pt";
    cfg.sd.abg_remover_enable = true;
    const nlohmann::json request{ Hermit::sd::make_request(cfg, "prompt", "negative") };
    EXPECT_EQ(request.at("prompt"), "prompt");
    EXPECT_EQ(request.at("negative_prompt"), "negative");
    EXPECT_EQ(request.at("sampler_name"), "Euler a");
    EXPECT_EQ(request.at("alwayson_scripts").at("ADetailer").at("args").at(2).at("ad_model"), "face_yolov8n.pt");
    EXPECT_EQ(request.at("script_name"), "abg remover");
}

TEST(sd, injects_request_executor)
{
    Hermit::config cfg;
    cfg.sd.mode = Hermit::sd_mode::txt2img;
    cfg.sd.host = "localhost";
    cfg.sd.port = "7860";
    cfg.sd.txt2img.target = "/sdapi/v1/txt2img";
    const Hermit::tcp::request_executor executor = [](std::string_view host, std::string_view port, const Hermit::tcp::request_type& request)
    {
        EXPECT_EQ(host, "localhost");
        EXPECT_EQ(port, "7860");
        EXPECT_EQ(request.target(), "/sdapi/v1/txt2img");
        EXPECT_THAT(request.body(), testing::HasSubstr("\"prompt\":\"prompt\""));
        Hermit::tcp::response_type response{ boost::beast::http::status::ok, 11 };
        response.body() = R"({"images":["bW9ja19pbWFnZQ=="]})";
        response.prepare_payload();
        return response;
    };
    EXPECT_EQ(Hermit::sd::send_request(cfg, "prompt", "negative", executor), "mock_image");
}
