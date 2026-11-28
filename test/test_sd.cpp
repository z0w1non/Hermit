#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <filesystem>
#include <tuple>
#include <vector>

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

TEST(sd, forwards_additional_dynamic_generation_fields)
{
    Hermit::config cfg;
    cfg.sd.mode = Hermit::sd_mode::txt2img;
    cfg.sd.common.steps = 30;
    cfg.sd.generation_options = R"({"steps":47,"hr_prompt":"hires prompt","custom_extension_field":{"enabled":true}})";
    nlohmann::json txt2img_request;
    try { txt2img_request = Hermit::sd::make_request(cfg, "prompt", "negative"); }
    catch (const std::exception& error) { FAIL() << error.what(); }
    EXPECT_EQ(txt2img_request.at("steps"), 47);
    EXPECT_EQ(txt2img_request.at("hr_prompt"), "hires prompt");
    EXPECT_EQ(txt2img_request.at("custom_extension_field").at("enabled"), true);

    cfg.sd.mode = Hermit::sd_mode::img2img;
    cfg.sd.img2img.init_images = { "init.png" };
    cfg.sd.generation_options = R"({"image_cfg_scale":1.75,"custom_img2img_field":"value"})";
    const Hermit::filesystem::file_reader reader{ [](const std::filesystem::path&, std::ios_base::openmode)
    {
        return std::string{ "image" };
    } };
    const nlohmann::json img2img_request{ Hermit::sd::make_request(cfg, "prompt", "negative", reader) };
    EXPECT_EQ(img2img_request.at("image_cfg_scale"), 1.75);
    EXPECT_EQ(img2img_request.at("custom_img2img_field"), "value");
}

TEST(sd, rejects_non_object_generation_options)
{
    Hermit::config cfg;
    cfg.sd.generation_options = "[]";
    EXPECT_THROW(Hermit::sd::make_request(cfg, "prompt", "negative"), std::invalid_argument);
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

TEST(sd, manages_progress_interrogate_and_control_api_contracts)
{
    Hermit::config cfg;
    cfg.sd.host = "sd-host";
    cfg.sd.port = "7860";
    const std::vector<std::tuple<std::string, std::string, Hermit::public_api::http_method, std::string, std::string>> cases
    {
        { "options", "", Hermit::public_api::http_method::get, "/sdapi/v1/options", "" },
        { "options-set", R"({"samples_save":false})", Hermit::public_api::http_method::post,
            "/sdapi/v1/options", R"({"samples_save":false})" },
        { "create-embedding", R"({"opaque":"pass-through"})", Hermit::public_api::http_method::post,
            "/sdapi/v1/create/embedding", R"({"opaque":"pass-through"})" },
        { "create-hypernetwork", R"({"opaque":"pass-through"})", Hermit::public_api::http_method::post,
            "/sdapi/v1/create/hypernetwork", R"({"opaque":"pass-through"})" },
        { "train-embedding", R"({"opaque":"pass-through"})", Hermit::public_api::http_method::post,
            "/sdapi/v1/train/embedding", R"({"opaque":"pass-through"})" },
        { "train-hypernetwork", R"({"opaque":"pass-through"})", Hermit::public_api::http_method::post,
            "/sdapi/v1/train/hypernetwork", R"({"opaque":"pass-through"})" },
        { "memory", "", Hermit::public_api::http_method::get, "/sdapi/v1/memory", "" },
        { "scripts", "", Hermit::public_api::http_method::get, "/sdapi/v1/scripts", "" },
        { "script-info", "", Hermit::public_api::http_method::get, "/sdapi/v1/script-info", "" },
        { "extensions", "", Hermit::public_api::http_method::get, "/sdapi/v1/extensions", "" },
        { "unload-checkpoint", "", Hermit::public_api::http_method::post, "/sdapi/v1/unload-checkpoint", "" },
        { "reload-checkpoint", "", Hermit::public_api::http_method::post, "/sdapi/v1/reload-checkpoint", "" },
        { "server-kill", "", Hermit::public_api::http_method::post, "/sdapi/v1/server-kill", "" },
        { "server-restart", "", Hermit::public_api::http_method::post, "/sdapi/v1/server-restart", "" },
        { "server-stop", "", Hermit::public_api::http_method::post, "/sdapi/v1/server-stop", "" },
        { "cmd-flags", "", Hermit::public_api::http_method::get, "/sdapi/v1/cmd-flags", "" },
        { "samplers", "", Hermit::public_api::http_method::get, "/sdapi/v1/samplers", "" },
        { "schedulers", "", Hermit::public_api::http_method::get, "/sdapi/v1/schedulers", "" },
        { "upscalers", "", Hermit::public_api::http_method::get, "/sdapi/v1/upscalers", "" },
        { "latent-upscale-modes", "", Hermit::public_api::http_method::get, "/sdapi/v1/latent-upscale-modes", "" },
        { "sd-models", "", Hermit::public_api::http_method::get, "/sdapi/v1/sd-models", "" },
        { "sd-vae", "", Hermit::public_api::http_method::get, "/sdapi/v1/sd-vae", "" },
        { "hypernetworks", "", Hermit::public_api::http_method::get, "/sdapi/v1/hypernetworks", "" },
        { "face-restorers", "", Hermit::public_api::http_method::get, "/sdapi/v1/face-restorers", "" },
        { "realesrgan-models", "", Hermit::public_api::http_method::get, "/sdapi/v1/realesrgan-models", "" },
        { "prompt-styles", "", Hermit::public_api::http_method::get, "/sdapi/v1/prompt-styles", "" },
        { "embeddings", "", Hermit::public_api::http_method::get, "/sdapi/v1/embeddings", "" },
        { "refresh-embeddings", "", Hermit::public_api::http_method::post, "/sdapi/v1/refresh-embeddings", "" },
        { "refresh-checkpoints", "", Hermit::public_api::http_method::post, "/sdapi/v1/refresh-checkpoints", "" },
        { "refresh-vae", "", Hermit::public_api::http_method::post, "/sdapi/v1/refresh-vae", "" },
        { "progress", R"({"skip_current_image":true})", Hermit::public_api::http_method::get,
            "/sdapi/v1/progress?skip_current_image=true", "" },
        { "interrupt", "", Hermit::public_api::http_method::post, "/sdapi/v1/interrupt", "" },
        { "skip", "", Hermit::public_api::http_method::post, "/sdapi/v1/skip", "" },
        { "interrogate", R"({"image":"aW1hZ2U=","model":"clip"})", Hermit::public_api::http_method::post,
            "/sdapi/v1/interrogate", R"({"image":"aW1hZ2U=","model":"clip"})" },
        { "extra-single-image", R"({"image":"aW1hZ2U=","upscaling_resize":2})", Hermit::public_api::http_method::post,
            "/sdapi/v1/extra-single-image", R"({"image":"aW1hZ2U=","upscaling_resize":2})" },
        { "extra-batch-images", R"({"imageList":[{"data":"aW1hZ2U=","name":"input.png"}]})", Hermit::public_api::http_method::post,
            "/sdapi/v1/extra-batch-images", R"({"imageList":[{"data":"aW1hZ2U=","name":"input.png"}]})" },
        { "png-info", R"({"image":"aW1hZ2U="})", Hermit::public_api::http_method::post,
            "/sdapi/v1/png-info", R"({"image":"aW1hZ2U="})" }
    };
    for (const auto& [action, options, method, target, body] : cases)
    {
        cfg.sd.api_action = action;
        cfg.sd.api_options = options;
        const Hermit::public_api::http_executor executor{ [&](const Hermit::public_api::http_request_data& request)
        {
            EXPECT_EQ(request.host, "sd-host");
            EXPECT_EQ(request.port, "7860");
            EXPECT_EQ(request.method, method);
            EXPECT_EQ(request.target, target);
            EXPECT_EQ(request.body, body);
            return Hermit::public_api::http_response_data{ 200, R"({"ok":true})" };
        } };
        EXPECT_EQ(Hermit::sd::manage_api(cfg, executor), R"({"ok":true})") << action;
    }
}

TEST(sd, rejects_invalid_api_action_options_and_http_status)
{
    Hermit::config cfg;
    cfg.sd.api_action = "interrogate";
    cfg.sd.api_options = R"({"image":"abc","model":"unknown"})";
    const Hermit::public_api::http_executor success_executor{ [](const Hermit::public_api::http_request_data&)
    {
        return Hermit::public_api::http_response_data{ 200, "{}" };
    } };
    EXPECT_THROW(Hermit::sd::manage_api(cfg, success_executor), std::invalid_argument);
    cfg.sd.api_action = "progress";
    cfg.sd.api_options = R"({"skip_current_image":"yes"})";
    EXPECT_THROW(Hermit::sd::manage_api(cfg, success_executor), std::invalid_argument);
    cfg.sd.api_action = "interrupt";
    cfg.sd.api_options.clear();
    const Hermit::public_api::http_executor failing_executor{ [](const Hermit::public_api::http_request_data&)
    {
        return Hermit::public_api::http_response_data{ 500, "error" };
    } };
    EXPECT_THROW(Hermit::sd::manage_api(cfg, failing_executor), Hermit::http_status_exception);
    cfg.sd.api_action = "extra-batch-images";
    cfg.sd.api_options = R"({"image":"not-an-array"})";
    EXPECT_THROW(Hermit::sd::manage_api(cfg, success_executor), std::invalid_argument);
    cfg.sd.api_action = "options-set";
    cfg.sd.api_options = "[]";
    EXPECT_THROW(Hermit::sd::manage_api(cfg, success_executor), std::invalid_argument);
    cfg.sd.api_action = "refresh-checkpoints";
    cfg.sd.api_options = R"({"force":true})";
    EXPECT_THROW(Hermit::sd::manage_api(cfg, success_executor), std::invalid_argument);
    cfg.sd.api_action = "unload-checkpoint";
    EXPECT_THROW(Hermit::sd::manage_api(cfg, success_executor), std::invalid_argument);
    cfg.sd.api_action = "server-stop";
    EXPECT_THROW(Hermit::sd::manage_api(cfg, success_executor), std::invalid_argument);
    cfg.sd.api_action = "train-embedding";
    cfg.sd.api_options = "{}";
    EXPECT_THROW(Hermit::sd::manage_api(cfg, success_executor), std::invalid_argument);
    cfg.sd.api_options = "[]";
    EXPECT_THROW(Hermit::sd::manage_api(cfg, success_executor), std::invalid_argument);
}

TEST(sd, invokes_runtime_discovered_routes_and_generic_methods)
{
    Hermit::config cfg;
    cfg.sd.host = "sd-host";
    cfg.sd.port = "7860";
    const std::vector<std::tuple<std::string, Hermit::public_api::http_method, std::string, std::string>> routes
    {
        { "routes", Hermit::public_api::http_method::get, "/openapi.json", "" },
        { "pending-tasks", Hermit::public_api::http_method::get, "/internal/pending-tasks", "" },
        { "quicksettings-hint", Hermit::public_api::http_method::get, "/internal/quicksettings-hint", "" },
        { "ping", Hermit::public_api::http_method::get, "/internal/ping", "" },
        { "profile-startup", Hermit::public_api::http_method::get, "/internal/profile-startup", "" },
        { "sysinfo", Hermit::public_api::http_method::get, "/internal/sysinfo", "" },
        { "sysinfo-download", Hermit::public_api::http_method::get, "/internal/sysinfo-download", "" },
        { "internal-progress", Hermit::public_api::http_method::post, "/internal/progress", R"({"id_task":"task-1"})" }
    };
    for (const auto& [action, method, target, body] : routes)
    {
        cfg.sd.api_action = action;
        cfg.sd.api_options = action == "internal-progress" ? R"({"id_task":"task-1"})" : "";
        const Hermit::public_api::http_executor executor{ [&](const Hermit::public_api::http_request_data& request)
        {
            EXPECT_EQ(request.method, method);
            EXPECT_EQ(request.target, target);
            EXPECT_EQ(request.body, body);
            return Hermit::public_api::http_response_data{ 200, "{}" };
        } };
        EXPECT_EQ(Hermit::sd::manage_api(cfg, executor), "{}") << action;
    }

    for (const auto& [method_name, method] : std::vector<std::pair<std::string, Hermit::public_api::http_method>>
        {{"GET", Hermit::public_api::http_method::get}, {"POST", Hermit::public_api::http_method::post},
        {"PUT", Hermit::public_api::http_method::put}, {"PATCH", Hermit::public_api::http_method::patch},
        {"DELETE", Hermit::public_api::http_method::delete_}, {"HEAD", Hermit::public_api::http_method::head},
        {"OPTIONS", Hermit::public_api::http_method::options}})
    {
        cfg.sd.api_method = method_name;
        cfg.sd.api_path = "/extension/custom";
        cfg.sd.api_options = R"({"value":1})";
        const Hermit::public_api::http_executor executor{ [&](const Hermit::public_api::http_request_data& request)
        {
            EXPECT_EQ(request.method, method);
            EXPECT_EQ(request.target, "/extension/custom");
            EXPECT_EQ(request.body, R"({"value":1})");
            return Hermit::public_api::http_response_data{ 200, "ok" };
        } };
        EXPECT_EQ(Hermit::sd::make_api_request(cfg, executor).body, "ok");
    }

    for (const std::string bad_path : {"", "relative", "/../private", "/api/./private", "/api\\evil",
        "/api#fragment", "/api\nheader", "/%2e%2e/private", "/api/%2E%2E/private", "/%2f%2fevil"})
    {
        cfg.sd.api_path = bad_path;
        EXPECT_THROW(Hermit::sd::make_api_request(cfg, [](const Hermit::public_api::http_request_data&)
        {
            return Hermit::public_api::http_response_data{ 200, "" };
        }), std::invalid_argument);
    }
}

TEST(sd, supports_binary_api_request_body_headers_and_response)
{
    Hermit::config cfg;
    cfg.sd.host = "sd-host";
    cfg.sd.port = "7860";
    cfg.sd.api_method = "POST";
    cfg.sd.api_path = "/upload/image";
    cfg.sd.api_body_file = "request.bin";
    cfg.sd.api_content_type = "multipart/form-data; boundary=hermit-test";
    cfg.sd.api_headers = R"({"Authorization":"Bearer test-token","X-Trace":"trace-1"})";
    const std::string binary_body{ "\0\x01payload\0", 10 };
    std::filesystem::path read_path;
    const Hermit::filesystem::file_reader reader{ [&](const std::filesystem::path& path, std::ios_base::openmode mode)
    {
        read_path = path;
        EXPECT_EQ(mode, std::ios::binary);
        return binary_body;
    } };
    const Hermit::public_api::http_executor executor{ [&](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, Hermit::public_api::http_method::post);
        EXPECT_EQ(request.target, "/upload/image");
        EXPECT_EQ(request.body, binary_body);
        EXPECT_EQ(request.content_type, "multipart/form-data; boundary=hermit-test");
        EXPECT_EQ(request.headers.at("Authorization"), "Bearer test-token");
        EXPECT_EQ(request.headers.at("X-Trace"), "trace-1");
        return Hermit::public_api::http_response_data{ 200, std::string{ "\0\x01\x02", 3 }, "image/png", true };
    } };
    const Hermit::public_api::http_response_data response{ Hermit::sd::make_api_request(cfg, executor, reader) };
    EXPECT_EQ(read_path.filename(), "request.bin");
    EXPECT_EQ(response.body, std::string("\0\x01\x02", 3));
    EXPECT_TRUE(response.is_binary);
}

TEST(sd, rejects_invalid_generic_api_request_metadata_without_sending)
{
    Hermit::config cfg;
    cfg.sd.api_method = "POST";
    cfg.sd.api_path = "/extension/custom";
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data&)
    {
        ADD_FAILURE() << "invalid request must be rejected before transport";
        return Hermit::public_api::http_response_data{ 200, "" };
    } };
    cfg.sd.api_body_file = "body.bin";
    cfg.sd.api_options = "{}";
    EXPECT_THROW(Hermit::sd::make_api_request(cfg, executor, {}), std::invalid_argument);
    cfg.sd.api_body_file.clear();
    cfg.sd.api_headers = R"({"Content-Length":"100"})";
    EXPECT_THROW(Hermit::sd::make_api_request(cfg, executor), std::invalid_argument);
    cfg.sd.api_headers = R"({"X-Test":"bad\r\nvalue"})";
    EXPECT_THROW(Hermit::sd::make_api_request(cfg, executor), std::invalid_argument);
    cfg.sd.api_headers.clear();
    cfg.sd.api_content_type = "multipart/form-data\r\nX-Evil: true";
    EXPECT_THROW(Hermit::sd::make_api_request(cfg, executor), std::invalid_argument);
    cfg.sd.api_content_type = std::string{ "application/json\0X", 18 };
    EXPECT_THROW(Hermit::sd::make_api_request(cfg, executor), std::invalid_argument);
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
