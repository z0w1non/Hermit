#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <filesystem>

#include "Hermit_config.hpp"
#include "Hermit_errors.hpp"
#include "Hermit_generation.hpp"
#include "Hermit_tcp.hpp"
#include "Hermit_filesystem.hpp"
#include "Hermit_public_http_adapter.hpp"

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

TEST(sd, injects_image_reader_into_request_executor)
{
    Hermit::config cfg;
    cfg.sd.mode = Hermit::sd_mode::img2img;
    cfg.base_path = "images-root";
    cfg.sd.img2img.init_images = { "init.png" };
    cfg.sd.img2img.mask = "mask.png";
    const Hermit::filesystem::file_reader reader{ [](const std::filesystem::path& path, std::ios_base::openmode mode)
    {
        EXPECT_EQ(mode, std::ios::binary);
        EXPECT_TRUE(path.string().starts_with("images-root"));
        return std::string{ "image-data" };
    } };
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        const nlohmann::json body{ nlohmann::json::parse(request.body) };
        EXPECT_EQ(body.at("init_images").at(0), "aW1hZ2UtZGF0YQ==");
        EXPECT_EQ(body.at("mask"), "aW1hZ2UtZGF0YQ==");
        return Hermit::public_api::http_response_data{ 200, R"({"images":["bW9ja19pbWFnZQ=="]})" };
    } };

    EXPECT_EQ(Hermit::sd::send_request(cfg, "prompt", "negative", executor, reader), "mock_image");
}

TEST(sd, rejects_non_success_status_from_injected_executors)
{
    Hermit::config cfg;
    cfg.sd.mode = Hermit::sd_mode::txt2img;
    const Hermit::tcp::request_executor tcp_executor{ [](std::string_view, std::string_view, const Hermit::tcp::request_type&)
    {
        return Hermit::tcp::response_type{ boost::beast::http::status::internal_server_error, 0 };
    } };
    const Hermit::public_api::http_executor http_executor{ [](const Hermit::public_api::http_request_data&)
    {
        return Hermit::public_api::http_response_data{ 500, "server error" };
    } };

    EXPECT_THROW(Hermit::sd::send_request(cfg, "prompt", "negative", tcp_executor), Hermit::http_status_exception);
    EXPECT_THROW(Hermit::sd::send_request(cfg, "prompt", "negative", http_executor), Hermit::http_status_exception);
}

TEST(sd, builds_img2img_request_with_init_images_and_masks)
{
    Hermit::config cfg;
    const std::filesystem::path base_path{ std::filesystem::temp_directory_path() / "hermit_sd_img2img_test" };
    std::filesystem::remove_all(base_path);
    std::filesystem::create_directories(base_path);
    cfg.base_path = base_path.string();
    const std::string image{ "image" };
    const std::string second_image{ "second" };
    const std::string mask{ "mask" };
    const std::string latent_mask{ "latent" };
    Hermit::filesystem::write_file(cfg, image, "init.png", std::ios::binary);
    Hermit::filesystem::write_file(cfg, second_image, "second.png", std::ios::binary);
    Hermit::filesystem::write_file(cfg, mask, "mask.png", std::ios::binary);
    Hermit::filesystem::write_file(cfg, latent_mask, "latent.png", std::ios::binary);
    cfg.sd.img2img.init_images = { "init.png", "second.png" };
    cfg.sd.img2img.mask = "mask.png";
    cfg.sd.img2img.latent_mask = "latent.png";

    const nlohmann::json request{ Hermit::sd::make_img2img_request(cfg, "prompt", "negative") };
    EXPECT_EQ(request.at("init_images"), nlohmann::json::array({ "aW1hZ2U=", "c2Vjb25k" }));
    EXPECT_EQ(request.at("mask"), "bWFzaw==");
    EXPECT_EQ(request.at("latent_mask"), "bGF0ZW50");
    std::filesystem::remove_all(base_path);
}

TEST(sd, img2img_seed_resize_uses_common_values_unless_overridden)
{
    Hermit::config cfg;
    cfg.sd.mode = Hermit::sd_mode::img2img;
    cfg.sd.common.seed_resize_from_h = 512;
    cfg.sd.common.seed_resize_from_w = 768;
    const Hermit::filesystem::file_reader reader{ [](const std::filesystem::path&, std::ios_base::openmode)
    {
        ADD_FAILURE() << "No image file should be read";
        return std::string{};
    } };

    const nlohmann::json common_values{ Hermit::sd::make_request(cfg, "prompt", "negative", reader) };
    EXPECT_EQ(common_values.at("seed_resize_from_h"), 512);
    EXPECT_EQ(common_values.at("seed_resize_from_w"), 768);

    cfg.sd.img2img.seed_resize_from_h = 640;
    cfg.sd.img2img.seed_resize_from_w = 896;
    const nlohmann::json img2img_values{ Hermit::sd::make_request(cfg, "prompt", "negative", reader) };
    EXPECT_EQ(img2img_values.at("seed_resize_from_h"), 640);
    EXPECT_EQ(img2img_values.at("seed_resize_from_w"), 896);
}

TEST(sd, rejects_missing_img2img_input_files)
{
    Hermit::config cfg;
    const std::filesystem::path base_path{ std::filesystem::temp_directory_path() / "hermit_sd_img2img_missing_test" };
    std::filesystem::remove_all(base_path);
    std::filesystem::create_directories(base_path);
    cfg.base_path = base_path.string();
    cfg.sd.img2img.init_images = { "missing.png" };
    cfg.sd.img2img.mask = "missing-mask.png";
    cfg.sd.img2img.latent_mask = "missing-latent.png";

    EXPECT_THROW(Hermit::sd::make_img2img_request(cfg, "prompt", "negative"), std::exception);
    Hermit::filesystem::write_file(cfg, "mask", "mask.png", std::ios::binary);
    cfg.sd.img2img.init_images.clear();
    cfg.sd.img2img.mask = "mask.png";
    EXPECT_THROW(Hermit::sd::make_img2img_request(cfg, "prompt", "negative"), std::exception);
    Hermit::filesystem::write_file(cfg, "latent", "latent.png", std::ios::binary);
    cfg.sd.img2img.latent_mask = "latent.png";
    cfg.sd.img2img.mask = "missing-mask.png";
    EXPECT_THROW(Hermit::sd::make_img2img_request(cfg, "prompt", "negative"), std::exception);
    std::filesystem::remove_all(base_path);
}
