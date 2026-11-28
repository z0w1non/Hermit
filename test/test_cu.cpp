#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "generation.hpp"
#include "errors.hpp"

TEST(cu, multipart_helpers)
{
    const std::string boundary{ "boundary" };
    EXPECT_EQ(hermit::cu::make_image_content_type(boundary), "multipart/form-data; boundary=boundary");
    const std::string body{ hermit::cu::make_image_body("data", "image.png", boundary, true) };
    EXPECT_THAT(body, testing::HasSubstr("image.png"));
    EXPECT_THAT(body, testing::HasSubstr("data"));
}

TEST(cu, multipart_body_can_omit_overwrite_field)
{
    const std::string body{ hermit::cu::make_image_body("data", "image.png", "boundary", false) };
    EXPECT_THAT(body, testing::HasSubstr("--boundary--"));
    EXPECT_THAT(body, testing::Not(testing::HasSubstr("name=\"overwrite\"")));
}

TEST(cu, uploads_images_and_masks_through_multipart_api)
{
    const std::string image_data{ '\0', '\x01', static_cast<char>(0xFF) };
    struct upload_case
    {
        std::string action;
        std::string target;
        std::string options;
        std::vector<std::string> expected_fields;
    };
    const std::vector<upload_case> cases
    {
        { "upload-image", "/upload/image", R"({"overwrite":false,"type":"input","subfolder":"refs"})",
            { "name=\"overwrite\"\r\n\r\nfalse", "name=\"type\"\r\n\r\ninput", "name=\"subfolder\"\r\n\r\nrefs" } },
        { "upload-mask", "/upload/mask", R"({"original_ref":{"filename":"base.png","type":"output","subfolder":"nested"},"type":"input","subfolder":"masks"})",
            { "name=\"original_ref\"\r\n\r\n{\"filename\":\"base.png\",\"subfolder\":\"nested\",\"type\":\"output\"}",
                "name=\"type\"\r\n\r\ninput", "name=\"subfolder\"\r\n\r\nmasks" } }
    };

    for (const upload_case& test_case : cases)
    {
        hermit::config cfg;
        cfg.cu.host = "comfy-host";
        cfg.cu.port = "8188";
        cfg.cu.upload_image_target = "/upload/image";
        cfg.cu.upload_file = "images/input.png";
        cfg.cu.api_action = test_case.action;
        cfg.cu.api_options = test_case.options;
        cfg.timeout_connect = 3;
        cfg.timeout_request = 9;
        bool file_read{};
        const hermit::filesystem::file_reader reader{ [&file_read, &image_data](const std::filesystem::path& path,
            std::ios_base::openmode mode)
        {
            EXPECT_EQ(path.filename(), "input.png");
            EXPECT_EQ(mode, std::ios::binary);
            file_read = true;
            return image_data;
        } };
        bool request_sent{};
        const hermit::public_api::http_executor executor{ [&test_case, &image_data, &request_sent](const hermit::public_api::http_request_data& request)
        {
            request_sent = true;
            EXPECT_EQ(request.method, hermit::public_api::http_method::post);
            EXPECT_EQ(request.host, "comfy-host");
            EXPECT_EQ(request.port, "8188");
            EXPECT_EQ(request.target, test_case.target);
            EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 3 });
            EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 9 });
            EXPECT_THAT(request.content_type, testing::StartsWith("multipart/form-data; boundary="));
            EXPECT_THAT(request.body, testing::HasSubstr("name=\"image\"; filename=\"input.png\""));
            EXPECT_THAT(request.body, testing::HasSubstr(image_data));
            for (const std::string& field : test_case.expected_fields)
            {
                EXPECT_THAT(request.body, testing::HasSubstr(field));
            }
            return hermit::public_api::http_response_data{ 200, R"({"name":"uploaded.png","subfolder":"","type":"input"})", "application/json" };
        } };

        const std::string response{ hermit::cu::manage_api(cfg, executor, reader) };

        EXPECT_EQ(response, R"({"name":"uploaded.png","subfolder":"","type":"input"})");
        EXPECT_TRUE(file_read);
        EXPECT_TRUE(request_sent);
    }

    EXPECT_THROW(hermit::cu::make_image_body("data", "bad\r\nname.png", "boundary", true), std::invalid_argument);

    hermit::config cfg;
    cfg.cu.api_action = "upload-mask";
    cfg.cu.upload_file = "mask.png";
    cfg.cu.api_options = R"({"original_ref":{"filename":"base.png"}})";
    bool request_sent{};
    const hermit::public_api::http_executor executor{ [&request_sent](const hermit::public_api::http_request_data&)
    {
        request_sent = true;
        return hermit::public_api::http_response_data{ 200, "{}" };
    } };
    const hermit::filesystem::file_reader failing_reader{ [](const std::filesystem::path&, std::ios_base::openmode) -> std::string
    {
        throw std::runtime_error{ "file read failed" };
    } };
    EXPECT_THROW(hermit::cu::manage_api(cfg, executor, failing_reader), std::runtime_error);
    EXPECT_FALSE(request_sent);
}

TEST(cu, generated_file_info_is_received_from_history)
{
    hermit::config cfg;
    cfg.cu.host = "localhost";
    cfg.cu.port = "8188";
    std::vector<std::string> targets;
    const hermit::tcp::request_executor executor{ [&targets](std::string_view host, std::string_view port,
        const hermit::tcp::request_type& request)
    {
        EXPECT_EQ(host, "localhost");
        EXPECT_EQ(port, "8188");
        targets.emplace_back(request.target());
        hermit::tcp::response_type response{ boost::beast::http::status::ok, 11 };
        response.body() = R"({"prompt-1":{"outputs":{"node":{"images":[{"filename":"image.png","subfolder":"nested","type":"output"}]}}}})";
        response.prepare_payload();
        return response;
    } };
    const hermit::cu::wait_function wait{ [](std::chrono::milliseconds duration) { EXPECT_EQ(duration.count(), 1000); } };

    const std::vector<hermit::cu::generated_file_info> files{
        hermit::cu::receive_generated_file_info(cfg, "prompt-1", executor, wait) };

    ASSERT_EQ(files.size(), 1);
    EXPECT_EQ(files.front().filename, "image.png");
    EXPECT_EQ(files.front().subfolder, "nested");
    EXPECT_EQ(files.front().type, "output");
    ASSERT_EQ(targets.size(), 1);
    EXPECT_THAT(targets.front(), testing::HasSubstr("/history/prompt-1"));
}

TEST(cu, generated_files_use_injected_storage_and_preserve_subdirectories)
{
    hermit::config cfg;
    cfg.cu.host = "localhost";
    cfg.cu.port = "8188";
    cfg.cu.output_directory = "generated";
    cfg.cu.preserve_subdirectories = true;
    const std::vector<hermit::cu::generated_file_info> files{ { "image.png", "nested/folder", "output" } };
    hermit::public_api::binary_storage stored;
    std::string stored_path;
    std::string stored_data;
    stored = [&stored_path, &stored_data](std::string_view path, std::string_view data)
    {
        stored_path = path;
        stored_data = data;
    };
    const hermit::tcp::request_executor executor{ [](std::string_view, std::string_view, const hermit::tcp::request_type& request)
    {
        EXPECT_EQ(request.method(), boost::beast::http::verb::get);
        EXPECT_THAT(request.target(), testing::HasSubstr("filename=image.png"));
        EXPECT_THAT(request.target(), testing::HasSubstr("subfolder=nested/folder"));
        hermit::tcp::response_type response{ boost::beast::http::status::ok, 6 };
        response.body() = std::string{ "\0\x01\xFF", 3 };
        response.prepare_payload();
        return response;
    } };

    hermit::cu::write_generated_files(cfg, files, executor, stored);

    EXPECT_EQ(std::filesystem::path{ stored_path }, std::filesystem::path{ "generated" } / "nested" / "folder" / "image.png");
    EXPECT_EQ(stored_data, std::string({ '\0', '\x01', static_cast<char>(0xFF) }));
}

TEST(cu, generated_file_names_are_sanitized)
{
    hermit::config cfg;
    cfg.cu.output_directory = "generated";
    const std::vector<hermit::cu::generated_file_info> files{ { "../bad:name?.png", "", "output" } };
    std::string stored_path;
    const hermit::public_api::binary_storage stored{ [&stored_path](std::string_view path, std::string_view)
    {
        stored_path = path;
    } };
    const hermit::tcp::request_executor executor{ [](std::string_view, std::string_view, const hermit::tcp::request_type&)
    {
        hermit::tcp::response_type response{ boost::beast::http::status::ok, 0 };
        response.prepare_payload();
        return response;
    } };

    hermit::cu::write_generated_files(cfg, files, executor, stored);

    EXPECT_EQ(std::filesystem::path{ stored_path }, std::filesystem::path{ "generated" } / "badname.png");
}

TEST(cu, generated_file_subfolder_traversal_is_rejected_before_request)
{
    hermit::config cfg;
    cfg.cu.output_directory = "generated";
    cfg.cu.preserve_subdirectories = true;
    const std::vector<hermit::cu::generated_file_info> files{ { "image.png", "../../outside", "output" } };
    bool request_called{};
    const hermit::tcp::request_executor executor{ [&request_called](std::string_view, std::string_view, const hermit::tcp::request_type&)
    {
        request_called = true;
        return hermit::tcp::response_type{};
    } };
    const hermit::public_api::binary_storage stored{ [](std::string_view, std::string_view) {} };

    EXPECT_THROW(hermit::cu::write_generated_files(cfg, files, executor, stored), hermit::comfy_ui_generation_exception);
    EXPECT_FALSE(request_called);
}

TEST(cu, generated_file_storage_failure_is_propagated)
{
    hermit::config cfg;
    cfg.cu.output_directory = "generated";
    const std::vector<hermit::cu::generated_file_info> files{ { "image.png", "", "output" } };
    const hermit::tcp::request_executor executor{ [](std::string_view, std::string_view, const hermit::tcp::request_type&)
    {
        hermit::tcp::response_type response{ boost::beast::http::status::ok, 0 };
        response.prepare_payload();
        return response;
    } };
    const hermit::public_api::binary_storage storage{ [](std::string_view, std::string_view)
    {
        throw std::runtime_error{ "storage failure" };
    } };

    EXPECT_THROW(hermit::cu::write_generated_files(cfg, files, executor, storage), std::runtime_error);
}

TEST(cu, rejects_non_success_status_from_injected_executors)
{
    hermit::config cfg;
    cfg.cu.host = "localhost";
    cfg.cu.port = "8188";
    const hermit::tcp::request_executor tcp_executor{ [](std::string_view, std::string_view, const hermit::tcp::request_type&)
    {
        return hermit::tcp::response_type{ boost::beast::http::status::internal_server_error, 0 };
    } };
    const hermit::public_api::http_executor http_executor{ [](const hermit::public_api::http_request_data&)
    {
        return hermit::public_api::http_response_data{ 500, "server error" };
    } };
    bool storage_called{};
    const hermit::public_api::binary_storage storage{ [&storage_called](std::string_view, std::string_view)
    {
        storage_called = true;
    } };

    EXPECT_THROW(hermit::cu::send_request(cfg, "{}", tcp_executor), hermit::http_status_exception);
    EXPECT_THROW(hermit::cu::write_generated_files(cfg, { { "image.png", "", "output" } }, tcp_executor, storage), hermit::http_status_exception);
    EXPECT_THROW(hermit::cu::write_generated_files(cfg, { { "image.png", "", "output" } }, http_executor, storage), hermit::http_status_exception);
    EXPECT_FALSE(storage_called);
}

TEST(cu, routes_queue_and_control_operations_with_json_contracts)
{
    struct api_case
    {
        std::string action;
        hermit::public_api::http_method method;
        std::string target;
        std::string node_class;
        std::string model_folder;
        std::string options;
        std::string expected_body;
        std::string prompt_id;
    };
    const std::vector<api_case> cases
    {
        { "queue", hermit::public_api::http_method::get, "/queue", "", "", "", "", "" },
        { "queue-update", hermit::public_api::http_method::post, "/queue", "", "", R"({"clear":true})", R"({"clear":true})", "" },
        { "history", hermit::public_api::http_method::get, "/history?max_items=5&offset=2", "", "", R"({"max_items":5,"offset":2})", "", "" },
        { "history-item", hermit::public_api::http_method::get, "/history/prompt%2Fid", "", "", "", "", "prompt/id" },
        { "history-update", hermit::public_api::http_method::post, "/history", "", "", R"({"clear":true})", R"({"clear":true})", "" },
        { "history-update", hermit::public_api::http_method::post, "/history", "", "", R"({"delete":["prompt-1","prompt-2"]})", R"({"delete":["prompt-1","prompt-2"]})", "" },
        { "interrupt", hermit::public_api::http_method::post, "/interrupt", "", "", R"({"prompt_id":"job-1"})", R"({"prompt_id":"job-1"})", "" },
        { "free", hermit::public_api::http_method::post, "/free", "", "", R"({"unload_models":true,"free_memory":true})", R"({"unload_models":true,"free_memory":true})", "" },
        { "object-info", hermit::public_api::http_method::get, "/object_info", "", "", "", "", "" },
        { "object-info", hermit::public_api::http_method::get, "/object_info/LoadImage%2Fnode", "LoadImage/node", "", "", "", "" },
        { "models", hermit::public_api::http_method::get, "/models", "", "", "", "", "" },
        { "models-folder", hermit::public_api::http_method::get, "/models/checkpoints%2Fsecondary", "", "checkpoints/secondary", "", "", "" },
        { "embeddings", hermit::public_api::http_method::get, "/embeddings", "", "", "", "", "" },
        { "extensions", hermit::public_api::http_method::get, "/extensions", "", "", "", "", "" },
        { "features", hermit::public_api::http_method::get, "/features", "", "", "", "", "" },
        { "system-stats", hermit::public_api::http_method::get, "/system_stats", "", "", "", "", "" },
        { "default-workflow", hermit::public_api::http_method::get, "/default_workflow", "", "", "", "", "" },
        { "prompt-status", hermit::public_api::http_method::get, "/prompt", "", "", "", "", "" },
        { "workflow-templates", hermit::public_api::http_method::get, "/workflow_templates", "", "", "", "", "" },
        { "i18n", hermit::public_api::http_method::get, "/i18n", "", "", "", "", "" },
        { "web-ui", hermit::public_api::http_method::get, "/", "", "", "", "", "" },
        { "users", hermit::public_api::http_method::get, "/users", "", "", "", "", "" },
        { "user-create", hermit::public_api::http_method::post, "/users", "", "", R"({"username":"Test User"})", R"({"username":"Test User"})", "" },
        { "jobs", hermit::public_api::http_method::get, "/api/jobs?limit=20&offset=4&sort_by=created_at&sort_order=desc&status=pending,failed&workflow_id=workflow+one", "", "", R"({"status":"pending,failed","workflow_id":"workflow one","sort_by":"created_at","sort_order":"desc","limit":20,"offset":4})", "", "" },
        { "job", hermit::public_api::http_method::get, "/api/jobs/job%2Fone", "", "", "", "", "job/one" },
        { "job-cancel", hermit::public_api::http_method::post, "/api/jobs/job%2Fone/cancel", "", "", "", "{}", "job/one" },
        { "jobs-cancel", hermit::public_api::http_method::post, "/api/jobs/cancel", "", "", R"({"job_ids":["job-1","job-2"]})", R"({"job_ids":["job-1","job-2"]})", "" }
    };
    for (const api_case& test_case : cases)
    {
        hermit::config cfg;
        cfg.cu.host = "comfy-host";
        cfg.cu.port = "8188";
        cfg.cu.api_action = test_case.action;
        cfg.cu.api_node_class = test_case.node_class;
        cfg.cu.api_model_folder = test_case.model_folder;
        cfg.cu.api_options = test_case.options;
        cfg.cu.api_prompt_id = test_case.prompt_id;
        cfg.timeout_connect = 3;
        cfg.timeout_request = 9;
        const hermit::public_api::http_executor executor{ [&test_case](const hermit::public_api::http_request_data& request)
        {
            EXPECT_EQ(request.method, test_case.method);
            EXPECT_EQ(request.host, "comfy-host");
            EXPECT_EQ(request.port, "8188");
            EXPECT_EQ(request.target, test_case.target);
            if (test_case.expected_body.empty())
            {
                EXPECT_TRUE(request.body.empty());
            }
            else
            {
                EXPECT_EQ(nlohmann::json::parse(request.body), nlohmann::json::parse(test_case.expected_body));
            }
            EXPECT_EQ(request.options.connect_timeout, std::chrono::seconds{ 3 });
            EXPECT_EQ(request.options.request_timeout, std::chrono::seconds{ 9 });
            return hermit::public_api::http_response_data{ 200, "{}", "application/json" };
        } };
        EXPECT_EQ(hermit::cu::manage_api(cfg, executor), "{}") << test_case.action;
    }
}

TEST(cu, routes_view_and_model_metadata_queries)
{
    struct view_case
    {
        std::string action;
        std::string options;
        std::string folder;
        std::string expected_target;
    };
    const std::vector<view_case> cases
    {
        { "view", R"({"filename":"image one.png","type":"output","subfolder":"nested","preview":"webp;90","channel":"rgb"})",
            "", "/view?channel=rgb&filename=image+one.png&preview=webp;90&subfolder=nested&type=output" },
        { "view-metadata", R"({"filename":"model one.safetensors"})", "check points",
            "/view_metadata/check%20points?filename=model+one.safetensors" }
    };

    for (const view_case& test_case : cases)
    {
        hermit::config cfg;
        cfg.cu.host = "comfy-host";
        cfg.cu.port = "8188";
        cfg.cu.api_action = test_case.action;
        cfg.cu.api_options = test_case.options;
        cfg.cu.api_metadata_folder = test_case.folder;
        const std::string binary{ '\0', '\x01', static_cast<char>(0xFF) };
        bool executor_called{};
        const hermit::public_api::http_executor executor{ [&test_case, &binary, &executor_called](const hermit::public_api::http_request_data& request)
        {
            executor_called = true;
            EXPECT_EQ(request.method, hermit::public_api::http_method::get);
            EXPECT_EQ(request.target, test_case.expected_target);
            EXPECT_TRUE(request.body.empty());
            return hermit::public_api::http_response_data{ 200, binary, test_case.action == "view" ? "image/png" : "application/json" };
        } };

        EXPECT_EQ(hermit::cu::manage_api(cfg, executor), binary);
        EXPECT_TRUE(executor_called);
        EXPECT_EQ(hermit::cu::api_returns_binary(cfg.cu), test_case.action == "view");
    }

    hermit::config unsafe_cfg;
    unsafe_cfg.cu.api_action = "view-metadata";
    unsafe_cfg.cu.api_metadata_folder = "../outside";
    unsafe_cfg.cu.api_options = R"({"filename":"model.safetensors"})";
    bool request_sent{};
    const hermit::public_api::http_executor executor{ [&request_sent](const hermit::public_api::http_request_data&)
    {
        request_sent = true;
        return hermit::public_api::http_response_data{ 200, "{}" };
    } };
    EXPECT_THROW(hermit::cu::manage_api(unsafe_cfg, executor), hermit::command_line_exception);
    EXPECT_FALSE(request_sent);
}

TEST(cu, downloads_custom_node_template_files_with_segment_encoding_and_path_validation)
{
    hermit::config cfg;
    cfg.cu.api_action = "template-file";
    cfg.cu.api_template_path = "Example Node/example_workflows/video demo.json";
    const hermit::public_api::http_executor executor{ [](const hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.method, hermit::public_api::http_method::get);
        EXPECT_EQ(request.target, "/api/workflow_templates/Example%20Node/example_workflows/video%20demo.json");
        return hermit::public_api::http_response_data{ 200, R"({"workflow":{}})", "application/json" };
    } };
    EXPECT_EQ(hermit::cu::manage_api(cfg, executor), R"({"workflow":{}})");

    cfg.cu.api_template_path = "Example Node/../secrets.json";
    EXPECT_THROW(hermit::cu::manage_api(cfg, executor), boost::exception);
}

TEST(cu, routes_userdata_api_with_binary_bodies_user_header_and_safe_paths)
{
    struct userdata_case
    {
        std::string action;
        hermit::public_api::http_method method;
        std::string path;
        std::string destination;
        std::string options;
        std::string expected_target;
        std::string expected_body;
        std::string response_body;
        int status_code;
    };
    const std::string binary{ '\0', '\x01', static_cast<char>(0xFF) };
    const std::vector<userdata_case> cases
    {
        { "userdata-list", hermit::public_api::http_method::get, "", "", R"({"dir":"settings","recurse":"true","full_info":"true","split":"false"})",
            "/userdata?dir=settings&full_info=true&recurse=true&split=false", "", "[]", 200 },
        { "userdata-list-v2", hermit::public_api::http_method::get, "", "", R"({"path":"folder one"})",
            "/v2/userdata?path=folder+one", "", "[]", 200 },
        { "userdata-get", hermit::public_api::http_method::get, "nested/config.json", "", "",
            "/userdata/nested%2Fconfig.json", "", binary, 200 },
        { "userdata-put", hermit::public_api::http_method::post, "nested/blob.bin", "", R"({"overwrite":false,"full_info":true})",
            "/userdata/nested%2Fblob.bin?full_info=true&overwrite=false", binary, R"("nested/blob.bin")", 200 },
        { "userdata-delete", hermit::public_api::http_method::delete_, "nested/blob.bin", "", "",
            "/userdata/nested%2Fblob.bin", "", "", 204 },
        { "userdata-move", hermit::public_api::http_method::post, "source/name.bin", "archive/name.bin", R"({"overwrite":false})",
            "/userdata/source%2Fname.bin/move/archive%2Fname.bin?overwrite=false", "", R"("archive/name.bin")", 200 }
    };
    for (const userdata_case& test_case : cases)
    {
        hermit::config cfg;
        cfg.cu.host = "comfy-host";
        cfg.cu.port = "8188";
        cfg.cu.api_action = test_case.action;
        cfg.cu.api_options = test_case.options;
        cfg.cu.userdata_path = test_case.path;
        cfg.cu.userdata_destination = test_case.destination;
        cfg.cu.api_user_id = "example_user_123";
        cfg.cu.upload_file = "local.bin";
        const hermit::filesystem::file_reader reader{ [&binary, &test_case](const std::filesystem::path&, std::ios_base::openmode mode)
        {
            EXPECT_TRUE((mode & std::ios_base::binary) != 0);
            return binary;
        } };
        const hermit::public_api::http_executor executor{ [&test_case, &binary](const hermit::public_api::http_request_data& request)
        {
            EXPECT_EQ(request.method, test_case.method);
            EXPECT_EQ(request.host, "comfy-host");
            EXPECT_EQ(request.port, "8188");
            EXPECT_EQ(request.target, test_case.expected_target);
            EXPECT_EQ(request.body, test_case.expected_body);
            EXPECT_EQ(request.headers.at("comfy-user"), "example_user_123");
            if (test_case.action == "userdata-put") EXPECT_EQ(request.content_type, "application/octet-stream");
            return hermit::public_api::http_response_data{ test_case.status_code, test_case.response_body,
                test_case.action == "userdata-get" ? "application/octet-stream" : "application/json" };
        } };
        EXPECT_EQ(hermit::cu::manage_api(cfg, executor, reader), test_case.response_body) << test_case.action;
        EXPECT_EQ(hermit::cu::api_returns_binary(cfg.cu), test_case.action == "userdata-get") << test_case.action;
    }

    hermit::config unsafe_cfg;
    unsafe_cfg.cu.api_action = "userdata-get";
    unsafe_cfg.cu.userdata_path = "nested/../secrets.json";
    bool request_sent{};
    const hermit::public_api::http_executor executor{ [&request_sent](const hermit::public_api::http_request_data&)
    {
        request_sent = true;
        return hermit::public_api::http_response_data{ 200, "{}" };
    } };
    EXPECT_THROW(hermit::cu::manage_api(unsafe_cfg, executor), hermit::command_line_exception);
    EXPECT_FALSE(request_sent);
}

TEST(cu, forwards_text_and_binary_websocket_events)
{
    hermit::config cfg;
    cfg.cu.host = "comfy-host";
    cfg.cu.port = "8188";
    cfg.cu.client_id = "client one";
    cfg.cu.event_count = 2;
    std::vector<std::string> output;
    const hermit::cu::websocket_executor executor{ [](std::string_view host, std::string_view port, std::string_view target,
        const hermit::cu::websocket_event_handler& handler)
    {
        EXPECT_EQ(host, "comfy-host");
        EXPECT_EQ(port, "8188");
        EXPECT_EQ(target, "/ws?clientId=client+one");
        EXPECT_TRUE(handler(R"({"type":"progress","value":1})", false));
        EXPECT_FALSE(handler("abc", true));
    } };
    const hermit::cu::websocket_event_writer writer{ [&output](std::string_view message)
    {
        output.emplace_back(message);
    } };

    hermit::cu::receive_events(cfg, executor, writer);

    ASSERT_EQ(output.size(), 2);
    EXPECT_EQ(output[0], R"({"type":"progress","value":1})");
    const nlohmann::json binary_event{ nlohmann::json::parse(output[1]) };
    EXPECT_EQ(binary_event.at("type"), "binary");
    EXPECT_EQ(binary_event.at("data"), "YWJj");
}
