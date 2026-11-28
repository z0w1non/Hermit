#include <atomic>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/program_options.hpp>
#include <boost/nowide/iostream.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include "Hermit_app_types.hpp"
#include "Hermit_builtin.hpp"
#include "Hermit_command_line.hpp"
#include "Hermit_config.hpp"
#include "Hermit_context.hpp"
#include "Hermit_core.hpp"
#include "Hermit_filesystem.hpp"
#include "Hermit_errors.hpp"
#include "Hermit_generation.hpp"
#include "Hermit_llm.hpp"
#include "Hermit_llm_types.hpp"
#include "Hermit_lru.hpp"
#include "Hermit_lru_impl.hpp"
#include "Hermit_log.hpp"
#include "Hermit_media_config.hpp"
#include "Hermit_parser.hpp"
#include "Hermit_png.hpp"
#include "Hermit_runtime.hpp"
#include "Hermit_sd_config.hpp"
#include "Hermit_string_utils.hpp"
#include "Hermit_tcp.hpp"
#include "Hermit_templates.hpp"
#include "Hermit_tg_kc_config.hpp"
#include "Hermit_value.hpp"
#include "test_support.hpp"

namespace
{
    class LoopbackHttpServer
    {
    public:
        using request_type = boost::beast::http::request<boost::beast::http::string_body>;
        using response_type = boost::beast::http::response<boost::beast::http::string_body>;
        using handler_type = std::function<response_type(const request_type&)>;

        explicit LoopbackHttpServer(handler_type handler)
            : acceptor_{ context_, boost::asio::ip::tcp::endpoint{ boost::asio::ip::address_v4::loopback(), 0 } },
              port_{ acceptor_.local_endpoint().port() }, handler_{ std::move(handler) }
        {
            acceptor_.non_blocking(true);
            thread_ = std::thread{ [this] { serve(); } };
        }

        ~LoopbackHttpServer()
        {
            stop_requested_ = true;
            if (thread_.joinable()) thread_.join();
        }

        LoopbackHttpServer(const LoopbackHttpServer&) = delete;
        LoopbackHttpServer& operator=(const LoopbackHttpServer&) = delete;

        std::string port() const { return std::to_string(port_); }

        void finish()
        {
            stop_requested_ = true;
            if (thread_.joinable()) thread_.join();
            if (error_) std::rethrow_exception(error_);
        }

    private:
        void serve() noexcept
        {
            try
            {
                boost::system::error_code error;
                while (!stop_requested_)
                {
                    boost::asio::ip::tcp::socket socket{ context_ };
                    const auto deadline{ std::chrono::steady_clock::now() + std::chrono::seconds{ 5 } };
                    while (!stop_requested_)
                    {
                        acceptor_.accept(socket, error);
                        if (!error) break;
                        if (error != boost::asio::error::would_block && error != boost::asio::error::try_again)
                        {
                            throw boost::system::system_error{ error };
                        }
                        if (std::chrono::steady_clock::now() >= deadline)
                        {
                            throw std::runtime_error{ "loopback HTTP server timed out waiting for a request" };
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
                        error.clear();
                    }
                    if (stop_requested_) break;

                    boost::beast::flat_buffer buffer;
                    request_type request;
                    boost::beast::http::read(socket, buffer, request, error);
                    if (error) throw boost::system::system_error{ error };

                    response_type response{ handler_(request) };
                    response.version(request.version());
                    response.set(boost::beast::http::field::server, "Hermit loopback test");
                    response.keep_alive(false);
                    response.prepare_payload();
                    boost::beast::http::write(socket, response);
                    socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send, error);
                    socket.non_blocking(true, error);
                    const auto close_deadline{ std::chrono::steady_clock::now() + std::chrono::seconds{ 5 } };
                    std::array<char, 256> trailing_data{};
                    while (true)
                    {
                        const std::size_t received{ socket.read_some(boost::asio::buffer(trailing_data), error) };
                        if (error == boost::asio::error::eof || error == boost::asio::error::connection_reset) break;
                        if (error && error != boost::asio::error::would_block && error != boost::asio::error::try_again)
                        {
                            throw boost::system::system_error{ error };
                        }
                        if (!error && received != 0) continue;
                        if (std::chrono::steady_clock::now() >= close_deadline) break;
                        error.clear();
                        std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
                    }
                }
            }
            catch (...)
            {
                error_ = std::current_exception();
            }
        }

        boost::asio::io_context context_;
        boost::asio::ip::tcp::acceptor acceptor_;
        unsigned short port_{};
        handler_type handler_;
        std::thread thread_;
        std::atomic_bool stop_requested_{};
        std::exception_ptr error_;
    };
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

TEST(parse_command_line, set_verbose)
{
    commandline_args args{ "Hermit.exe", "--logging.console" };
    Hermit::config cfg;
    boost::program_options::options_description od{ Hermit::command_line::make_options_description(cfg) };
    boost::program_options::variables_map vm;
    Hermit::command_line::parse_command_line(od, args.argc(), args.argv(), vm);
    boost::program_options::notify(vm);
    EXPECT_EQ(cfg.verbose, true);
}

TEST(parse_command_line, applies_common_defaults_and_values)
{
    commandline_args args{ "Hermit.exe", "generate", "--backend", "kc", "--iteration.count", "3", "--network.connect-timeout", "5", "--path.base", "data" };
    Hermit::config cfg;
    const Hermit::command_line::parse_result result{ Hermit::command_line::parse(args.argc(), args.argv(), cfg) };

    EXPECT_EQ(result, Hermit::command_line::parse_result::success);
    EXPECT_EQ(cfg.command_mode, Hermit::command_mode::kc);
    EXPECT_EQ(cfg.number_iterations, 3);
    EXPECT_EQ(cfg.timeout_connect, 5);
    EXPECT_EQ(cfg.base_path, "data");
    EXPECT_EQ(cfg.log_level, Hermit::log::severity_level::info);
    EXPECT_FALSE(cfg.allow_process_exec);
}

TEST(parse_command_line, configures_new_local_llm_backends)
{
    const std::vector<std::pair<std::string, std::string>> backends
    {
        { "lmstudio", "1234" },
        { "llama-cpp", "8080" },
        { "vllm", "8000" }
    };

    for (const auto& [name, port] : backends)
    {
        commandline_args args{ "Hermit.exe", "generate", "--backend", name, "--llm.model", "served-model" };
        Hermit::config cfg;
        EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::success) << name;
        EXPECT_TRUE(cfg.tg.openai_compatible) << name;
        EXPECT_EQ(cfg.llm.backend, &cfg.tg) << name;
        EXPECT_EQ(cfg.llm.port, port) << name;
        EXPECT_EQ(cfg.tg.common.model, "served-model") << name;
        EXPECT_EQ(cfg.llm.completions_target, "/v1/completions") << name;
        EXPECT_EQ(cfg.llm.chat_completions_target, "/v1/chat/completions") << name;
        EXPECT_EQ(cfg.llm.token_count_target, "/v1/completions") << name;
        EXPECT_TRUE(cfg.tg.common.stop.empty()) << name;
    }

    commandline_args ollama_args{ "Hermit.exe", "generate", "--backend", "ollama", "--llm.model", "llama3.2" };
    Hermit::config ollama;
    EXPECT_EQ(Hermit::command_line::parse(ollama_args.argc(), ollama_args.argv(), ollama), Hermit::command_line::parse_result::success);
    EXPECT_TRUE(ollama.kc.ollama_compatible);
    EXPECT_EQ(ollama.llm.backend, &ollama.kc);
    EXPECT_EQ(ollama.llm.port, "11434");
    EXPECT_EQ(ollama.kc.model, "llama3.2");
    EXPECT_EQ(ollama.llm.completions_target, "/api/generate");
    EXPECT_EQ(ollama.llm.chat_completions_target, "/api/chat");
    EXPECT_EQ(ollama.llm.token_count_target, "/api/generate");
}

TEST(parse_command_line, configures_model_list_routes_for_local_llm_backends)
{
    const std::vector<std::tuple<std::string, std::string, std::string>> backends
    {
        { "lmstudio", "1234", "/api/v1/models" },
        { "llama-cpp", "8080", "/v1/models" },
        { "vllm", "8000", "/v1/models" },
        { "ollama", "11434", "/api/tags" }
    };

    for (const auto& [name, port, target] : backends)
    {
        commandline_args args{ "Hermit.exe", "models", "list", "--backend", name };
        Hermit::config cfg;
        EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::success) << name;
        EXPECT_EQ(cfg.command_mode, Hermit::command_mode::list_models) << name;
        EXPECT_EQ(cfg.llm.backend_name, name) << name;
        EXPECT_EQ(cfg.llm.port, port) << name;
        EXPECT_EQ(cfg.llm.models_target, target) << name;
    }

    commandline_args override_args{ "Hermit.exe", "models", "list", "--backend", "ollama", "--llm.port", "12000", "--llm.models-target", "/custom/tags" };
    Hermit::config override_cfg;
    EXPECT_EQ(Hermit::command_line::parse(override_args.argc(), override_args.argv(), override_cfg), Hermit::command_line::parse_result::success);
    EXPECT_EQ(override_cfg.llm.port, "12000");
    EXPECT_EQ(override_cfg.llm.models_target, "/custom/tags");
}

TEST(parse_command_line, configures_ollama_model_details_and_running_models)
{
    commandline_args show_args{ "Hermit.exe", "models", "show", "--backend", "ollama", "--llm.model", "gemma4",
        "--llm.model-info-options", R"({"verbose":true})" };
    Hermit::config show_cfg;
    EXPECT_EQ(Hermit::command_line::parse(show_args.argc(), show_args.argv(), show_cfg), Hermit::command_line::parse_result::success);
    EXPECT_EQ(show_cfg.command_mode, Hermit::command_mode::show_model);
    EXPECT_EQ(show_cfg.llm.port, "11434");
    EXPECT_EQ(show_cfg.llm.model_info_target, "/api/show");

    commandline_args running_args{ "Hermit.exe", "models", "running", "--backend", "ollama" };
    Hermit::config running_cfg;
    EXPECT_EQ(Hermit::command_line::parse(running_args.argc(), running_args.argv(), running_cfg), Hermit::command_line::parse_result::success);
    EXPECT_EQ(running_cfg.command_mode, Hermit::command_mode::list_running_models);
    EXPECT_EQ(running_cfg.llm.running_models_target, "/api/ps");

    commandline_args version_args{ "Hermit.exe", "models", "version", "--backend", "ollama" };
    Hermit::config version_cfg;
    EXPECT_EQ(Hermit::command_line::parse(version_args.argc(), version_args.argv(), version_cfg), Hermit::command_line::parse_result::success);
    EXPECT_EQ(version_cfg.command_mode, Hermit::command_mode::get_ollama_version);
    EXPECT_EQ(version_cfg.llm.port, "11434");
}

TEST(parse_command_line, configures_ollama_model_lifecycle_commands)
{
    for (const std::string& action : std::vector<std::string>{ "create", "copy", "delete", "pull", "push" })
    {
        commandline_args args{ "Hermit.exe", "models", action, "--backend", "ollama", "--llm.model-action-options", "{}" };
        Hermit::config cfg;
        EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::success) << action;
        EXPECT_EQ(cfg.command_mode, Hermit::command_mode::manage_ollama_model) << action;
        EXPECT_EQ(cfg.llm.model_action, action) << action;
        EXPECT_EQ(cfg.llm.port, "11434") << action;
    }
}

TEST(parse_command_line, configures_lmstudio_model_management_commands)
{
    for (const std::string& action : std::vector<std::string>{ "load", "unload", "download", "download-status" })
    {
        commandline_args args{ "Hermit.exe", "models", action, "--backend", "lmstudio", "--llm.model-action-options", "{}" };
        Hermit::config cfg;
        EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::success) << action;
        EXPECT_EQ(cfg.command_mode, Hermit::command_mode::manage_lmstudio_model) << action;
        EXPECT_EQ(cfg.llm.model_action, action) << action;
        EXPECT_EQ(cfg.llm.port, "1234") << action;
    }
    commandline_args override_args{ "Hermit.exe", "models", "load", "--backend", "lmstudio", "--llm.port", "4321",
        "--llm.model-action-options", R"({"model":"catalog/model"})" };
    Hermit::config override_cfg;
    EXPECT_EQ(Hermit::command_line::parse(override_args.argc(), override_args.argv(), override_cfg), Hermit::command_line::parse_result::success);
    EXPECT_EQ(override_cfg.llm.port, "4321");
}

TEST(parse_command_line, configures_lmstudio_responses_create)
{
    commandline_args args{ "Hermit.exe", "responses", "create", "--backend", "lmstudio",
        "--llm.api-options", R"({"model":"test-model","input":"hello"})" };
    Hermit::config cfg;
    EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::success);
    EXPECT_EQ(cfg.command_mode, Hermit::command_mode::create_lmstudio_response);
    EXPECT_EQ(cfg.llm.backend_name, "lmstudio");
    EXPECT_EQ(cfg.llm.port, "1234");
    EXPECT_EQ(cfg.llm.api_options, R"({"model":"test-model","input":"hello"})");

    commandline_args missing_options{ "Hermit.exe", "responses", "create", "--backend", "lmstudio" };
    Hermit::config missing_cfg;
    EXPECT_EQ(Hermit::command_line::parse(missing_options.argc(), missing_options.argv(), missing_cfg),
        Hermit::command_line::parse_result::program_options_error);

    commandline_args wrong_backend{ "Hermit.exe", "responses", "create", "--backend", "vllm", "--llm.api-options", "{}" };
    Hermit::config wrong_backend_cfg;
    EXPECT_EQ(Hermit::command_line::parse(wrong_backend.argc(), wrong_backend.argv(), wrong_backend_cfg),
        Hermit::command_line::parse_result::program_options_error);
}

TEST(parse_command_line, configures_lmstudio_native_chat_create)
{
    commandline_args args{ "Hermit.exe", "lmstudio-chat", "create", "--backend", "lmstudio",
        "--llm.api-options", R"({"model":"test-model","input":"hello","previous_response_id":"resp-old"})" };
    Hermit::config cfg;
    EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::success);
    EXPECT_EQ(cfg.command_mode, Hermit::command_mode::create_lmstudio_chat);
    EXPECT_EQ(cfg.llm.backend_name, "lmstudio");
    EXPECT_EQ(cfg.llm.port, "1234");
    commandline_args wrong_backend{ "Hermit.exe", "lmstudio-chat", "create", "--backend", "ollama", "--llm.api-options", "{}" };
    Hermit::config wrong_backend_cfg;
    EXPECT_EQ(Hermit::command_line::parse(wrong_backend.argc(), wrong_backend.argv(), wrong_backend_cfg),
        Hermit::command_line::parse_result::program_options_error);
}

TEST(parse_command_line, configures_lmstudio_decisions_create)
{
    commandline_args args{ "Hermit.exe", "decisions", "create", "--backend", "lmstudio",
        "--llm.api-options", R"({"model":"decision-model","input":"hello","questions":[{"type":"predicate","name":"greeting","instructions":"Is this a greeting?"}]})" };
    Hermit::config cfg;
    EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::success);
    EXPECT_EQ(cfg.command_mode, Hermit::command_mode::create_lmstudio_decision);
    EXPECT_EQ(cfg.llm.port, "1234");
    commandline_args unsupported{ "Hermit.exe", "decisions", "create", "--backend", "ollama", "--llm.api-options", "{}" };
    Hermit::config unsupported_cfg;
    EXPECT_EQ(Hermit::command_line::parse(unsupported.argc(), unsupported.argv(), unsupported_cfg),
        Hermit::command_line::parse_result::program_options_error);
}

TEST(parse_command_line, configures_lmstudio_anthropic_messages_create)
{
    commandline_args args{ "Hermit.exe", "messages", "create", "--backend", "lmstudio",
        "--llm.api-options", R"({"model":"test-model","max_tokens":32,"messages":[{"role":"user","content":"hello"}]})" };
    Hermit::config cfg;
    EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::success);
    EXPECT_EQ(cfg.command_mode, Hermit::command_mode::create_lmstudio_messages);
    EXPECT_EQ(cfg.llm.backend_name, "lmstudio");
    EXPECT_EQ(cfg.llm.port, "1234");
    commandline_args unsupported{ "Hermit.exe", "messages", "create", "--backend", "ollama", "--llm.api-options", "{}" };
    Hermit::config unsupported_cfg;
    EXPECT_EQ(Hermit::command_line::parse(unsupported.argc(), unsupported.argv(), unsupported_cfg),
        Hermit::command_line::parse_result::program_options_error);
}

TEST(parse_command_line, configures_lmstudio_systemone_create)
{
    commandline_args args{ "Hermit.exe", "systemone", "create", "--backend", "lmstudio",
        "--llm.api-options", R"({"model":"decision-model","state":"hello","questions":{"greeting":{"type":"noul","instructions":"Is this a greeting?"}}})" };
    Hermit::config cfg;
    EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::success);
    EXPECT_EQ(cfg.command_mode, Hermit::command_mode::create_lmstudio_systemone);
    EXPECT_EQ(cfg.llm.port, "1234");
    commandline_args unsupported{ "Hermit.exe", "systemone", "create", "--backend", "ollama", "--llm.api-options", "{}" };
    Hermit::config unsupported_cfg;
    EXPECT_EQ(Hermit::command_line::parse(unsupported.argc(), unsupported.argv(), unsupported_cfg),
        Hermit::command_line::parse_result::program_options_error);
}

TEST(parse_command_line, configures_vllm_batch_chat_create)
{
    commandline_args args{ "Hermit.exe", "batch-chat", "create", "--backend", "vllm",
        "--llm.api-options", R"({"model":"m","messages":[[{"role":"user","content":"a"}]]})" };
    Hermit::config cfg;
    EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::success);
    EXPECT_EQ(cfg.command_mode, Hermit::command_mode::create_vllm_batch_chat);
    EXPECT_EQ(cfg.llm.port, "8000");
    commandline_args unsupported{ "Hermit.exe", "batch-chat", "create", "--backend", "lmstudio", "--llm.api-options", "{}" };
    Hermit::config unsupported_cfg;
    EXPECT_EQ(Hermit::command_line::parse(unsupported.argc(), unsupported.argv(), unsupported_cfg),
        Hermit::command_line::parse_result::program_options_error);
}

TEST(parse_command_line, rejects_model_list_for_unsupported_backends)
{
    commandline_args args{ "Hermit.exe", "models", "list", "--backend", "tg" };
    Hermit::config cfg;
    EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::program_options_error);
}

TEST(parse_command_line, configures_stable_diffusion_api_actions)
{
    for (const std::string& action : std::vector<std::string>{ "progress", "interrupt", "skip", "interrogate", "extra-single-image", "extra-batch-images", "png-info", "options", "options-set", "create-embedding", "create-hypernetwork", "train-embedding", "train-hypernetwork", "memory", "scripts", "script-info", "extensions", "cmd-flags", "samplers", "schedulers", "upscalers", "latent-upscale-modes", "sd-models", "sd-vae", "hypernetworks", "face-restorers", "realesrgan-models", "prompt-styles", "embeddings", "refresh-embeddings", "refresh-checkpoints", "refresh-vae", "unload-checkpoint", "reload-checkpoint", "server-kill", "server-restart", "server-stop" })
    {
        std::vector<std::string> arguments{ "Hermit.exe", "sd-api", action };
        if (action == "interrogate")
        {
            arguments.emplace_back("--sd.api-options");
            arguments.emplace_back(R"({"image":"aW1hZ2U=","model":"clip"})");
        }
        else if (action == "extra-single-image" || action == "png-info")
        {
            arguments.emplace_back("--sd.api-options");
            arguments.emplace_back(R"({"image":"aW1hZ2U="})");
        }
        else if (action == "extra-batch-images")
        {
            arguments.emplace_back("--sd.api-options");
            arguments.emplace_back(R"({"imageList":[{"name":"input.png","data":"aW1hZ2U="}]})");
        }
        else if (action == "options-set")
        {
            arguments.emplace_back("--sd.api-options");
            arguments.emplace_back(R"({"samples_save":false})");
        }
        else if (action == "create-embedding" || action == "create-hypernetwork" || action == "train-embedding" || action == "train-hypernetwork")
        {
            arguments.emplace_back("--sd.api-options");
            arguments.emplace_back(R"({"opaque":"pass-through"})");
        }
        commandline_args args{ arguments };
        Hermit::config cfg;
        EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::success) << action;
        EXPECT_EQ(cfg.command_mode, Hermit::command_mode::manage_sd_api) << action;
        EXPECT_EQ(cfg.sd.api_action, action) << action;
    }
    commandline_args missing_options{ "Hermit.exe", "sd-api", "interrogate" };
    Hermit::config cfg;
    EXPECT_EQ(Hermit::command_line::parse(missing_options.argc(), missing_options.argv(), cfg), Hermit::command_line::parse_result::program_options_error);
}

TEST(parse_command_line, configures_generic_stable_diffusion_api_request)
{
    for (const std::string method : {"GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"})
    {
        commandline_args args{ "Hermit.exe", "sd-api", "request", "--sd.api-method", method, "--sd.api-path", "/extension/custom", "--sd.api-options", R"({"value":1})" };
        Hermit::config cfg;
        EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::success);
        EXPECT_EQ(cfg.command_mode, Hermit::command_mode::request_sd_api);
        EXPECT_EQ(cfg.sd.api_method, method);
        EXPECT_EQ(cfg.sd.api_path, "/extension/custom");
    }
    commandline_args invalid_method{ "Hermit.exe", "sd-api", "request", "--sd.api-method", "TRACE" };
    Hermit::config invalid_cfg;
    EXPECT_EQ(Hermit::command_line::parse(invalid_method.argc(), invalid_method.argv(), invalid_cfg), Hermit::command_line::parse_result::program_options_error);
    commandline_args routes{ "Hermit.exe", "sd-api", "routes" };
    Hermit::config routes_cfg;
    EXPECT_EQ(Hermit::command_line::parse(routes.argc(), routes.argv(), routes_cfg), Hermit::command_line::parse_result::success);
    EXPECT_EQ(routes_cfg.sd.api_action, "routes");
    commandline_args invalid_server_action{ "Hermit.exe", "server", "request" };
    Hermit::config server_cfg;
    EXPECT_EQ(Hermit::command_line::parse(invalid_server_action.argc(), invalid_server_action.argv(), server_cfg), Hermit::command_line::parse_result::program_options_error);
    commandline_args multipart{ "Hermit.exe", "sd-api", "request", "--sd.api-method", "POST", "--sd.api-path", "/upload", "--sd.api-body-file", "payload.bin", "--sd.api-content-type", "multipart/form-data; boundary=test", "--sd.api-headers", R"({"X-Trace":"trace-1"})" };
    Hermit::config multipart_cfg;
    EXPECT_EQ(Hermit::command_line::parse(multipart.argc(), multipart.argv(), multipart_cfg), Hermit::command_line::parse_result::success);
    EXPECT_EQ(multipart_cfg.sd.api_body_file, "payload.bin");
    EXPECT_EQ(multipart_cfg.sd.api_content_type, "multipart/form-data; boundary=test");
    EXPECT_EQ(multipart_cfg.sd.api_headers, R"({"X-Trace":"trace-1"})");
    commandline_args conflicting_body{ "Hermit.exe", "sd-api", "request", "--sd.api-body-file", "payload.bin", "--sd.api-options", "{}" };
    Hermit::config conflicting_cfg;
    EXPECT_EQ(Hermit::command_line::parse(conflicting_body.argc(), conflicting_body.argv(), conflicting_cfg), Hermit::command_line::parse_result::program_options_error);
}

TEST(parse_command_line, configures_generic_llm_backend_api_request)
{
    commandline_args lmstudio{ "Hermit.exe", "backend-api", "request", "--backend", "lmstudio",
        "--llm.api-method", "POST", "--llm.api-path", "/v1/responses", "--llm.api-options", R"({"input":"hello"})" };
    Hermit::config cfg;
    EXPECT_EQ(Hermit::command_line::parse(lmstudio.argc(), lmstudio.argv(), cfg), Hermit::command_line::parse_result::success);
    EXPECT_EQ(cfg.command_mode, Hermit::command_mode::request_llm_api);
    EXPECT_EQ(cfg.llm.port, "1234");
    EXPECT_EQ(cfg.llm.api_path, "/v1/responses");
    commandline_args llama{ "Hermit.exe", "backend-api", "request", "--backend", "llama-cpp" };
    Hermit::config llama_cfg;
    EXPECT_EQ(Hermit::command_line::parse(llama.argc(), llama.argv(), llama_cfg), Hermit::command_line::parse_result::success);
    EXPECT_EQ(llama_cfg.llm.port, "8080");
    commandline_args unsupported{ "Hermit.exe", "backend-api", "request", "--backend", "sd" };
    Hermit::config unsupported_cfg;
    EXPECT_EQ(Hermit::command_line::parse(unsupported.argc(), unsupported.argv(), unsupported_cfg), Hermit::command_line::parse_result::program_options_error);
}

TEST(parse_command_line, configures_comfyui_queue_and_control_operations)
{
        for (const std::string& action : std::vector<std::string>{ "queue", "queue-update", "history", "history-item", "history-update", "interrupt", "free", "events", "object-info", "models", "models-folder", "embeddings", "extensions", "features", "upload-image", "upload-mask", "view", "view-metadata", "system-stats", "default-workflow", "prompt-status", "workflow-templates", "i18n", "web-ui", "template-file", "users", "user-create", "userdata-list", "userdata-list-v2", "userdata-get", "userdata-put", "userdata-delete", "userdata-move", "jobs", "job", "job-cancel", "jobs-cancel" })
    {
        std::vector<std::string> arguments{ "Hermit.exe", "comfy", action };
        if (action == "queue-update")
        {
            arguments.emplace_back("--cu.api-options");
            arguments.emplace_back(R"({"clear":true})");
        }
        else if (action == "history")
        {
            arguments.emplace_back("--cu.api-options");
            arguments.emplace_back(R"({"max_items":5})");
        }
        else if (action == "history-item" || action == "job" || action == "job-cancel")
        {
            arguments.emplace_back("--cu.prompt-id");
            arguments.emplace_back("prompt-1");
        }
        else if (action == "history-update")
        {
            arguments.emplace_back("--cu.api-options");
            arguments.emplace_back(R"({"clear":true})");
        }
        else if (action == "jobs")
        {
            arguments.emplace_back("--cu.api-options");
            arguments.emplace_back(R"({"limit":5,"status":"pending"})");
        }
        else if (action == "jobs-cancel")
        {
            arguments.emplace_back("--cu.api-options");
            arguments.emplace_back(R"({"job_ids":["job-1"]})");
        }
        else if (action == "user-create")
        {
            arguments.emplace_back("--cu.api-options");
            arguments.emplace_back(R"({"username":"Test User"})");
        }
        else if (action == "userdata-list")
        {
            arguments.emplace_back("--cu.api-options");
            arguments.emplace_back(R"({"dir":"settings"})");
        }
        else if (action == "userdata-list-v2")
        {
            arguments.emplace_back("--cu.api-options");
            arguments.emplace_back(R"({"path":"settings"})");
        }
        else if (action == "userdata-get" || action == "userdata-delete" || action == "userdata-put" || action == "userdata-move")
        {
            arguments.emplace_back("--cu.userdata-path");
            arguments.emplace_back("settings/config.json");
            if (action == "userdata-put")
            {
                arguments.emplace_back("--cu.upload-file");
                arguments.emplace_back("config.json");
            }
            if (action == "userdata-move")
            {
                arguments.emplace_back("--cu.userdata-destination");
                arguments.emplace_back("settings/archive.json");
            }
        }
        else if (action == "events")
        {
            arguments.emplace_back("--cu.client-id");
            arguments.emplace_back("client-1");
            arguments.emplace_back("--cu.event-count");
            arguments.emplace_back("2");
        }
        else if (action == "object-info")
        {
            arguments.emplace_back("--cu.node-class");
            arguments.emplace_back("LoadImage");
        }
        else if (action == "models-folder")
        {
            arguments.emplace_back("--cu.model-folder");
            arguments.emplace_back("checkpoints");
        }
        else if (action == "template-file")
        {
            arguments.emplace_back("--cu.template-path");
            arguments.emplace_back("example-node/example_workflows/example.json");
        }
        else if (action == "view")
        {
            arguments.emplace_back("--cu.api-options");
            arguments.emplace_back(R"({"filename":"image one.png","type":"output"})");
        }
        else if (action == "view-metadata")
        {
            arguments.emplace_back("--cu.metadata-folder");
            arguments.emplace_back("checkpoints");
            arguments.emplace_back("--cu.api-options");
            arguments.emplace_back(R"({"filename":"model.safetensors"})");
        }
        else if (action == "upload-image" || action == "upload-mask")
        {
            arguments.emplace_back("--cu.upload-file");
            arguments.emplace_back("images/input.png");
            if (action == "upload-mask")
            {
                arguments.emplace_back("--cu.api-options");
                arguments.emplace_back(R"({"original_ref":{"filename":"input.png"}})");
            }
        }
        commandline_args args{ arguments };
        Hermit::config cfg;
        EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::success) << action;
        EXPECT_EQ(cfg.command_mode, Hermit::command_mode::manage_comfyui_api) << action;
        EXPECT_EQ(cfg.cu.api_action, action) << action;
        EXPECT_EQ(cfg.cu.host, "localhost");
        EXPECT_EQ(cfg.cu.port, "8188");
        if (action == "events")
        {
            EXPECT_EQ(cfg.cu.client_id, "client-1");
            EXPECT_EQ(cfg.cu.event_count, 2);
        }
        if (action == "object-info") EXPECT_EQ(cfg.cu.api_node_class, "LoadImage");
        if (action == "models-folder") EXPECT_EQ(cfg.cu.api_model_folder, "checkpoints");
        if (action == "view-metadata") EXPECT_EQ(cfg.cu.api_metadata_folder, "checkpoints");
        if (action == "upload-image" || action == "upload-mask") EXPECT_EQ(cfg.cu.upload_file, "images/input.png");
    }
}

TEST(parse_command_line, configures_embedding_routes_for_local_llm_backends)
{
    const std::vector<std::tuple<std::string, std::string, std::string>> backends
    {
        { "lmstudio", "1234", "/v1/embeddings" },
        { "llama-cpp", "8080", "/v1/embeddings" },
        { "vllm", "8000", "/v1/embeddings" },
        { "ollama", "11434", "/api/embed" }
    };

    for (const auto& [name, port, target] : backends)
    {
        commandline_args args{ "Hermit.exe", "embeddings", "create", "--backend", name, "--llm.model", "embed-model", "--llm.prompt", "text" };
        Hermit::config cfg;
        EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::success) << name;
        EXPECT_EQ(cfg.command_mode, Hermit::command_mode::create_embeddings) << name;
        EXPECT_EQ(cfg.llm.backend_name, name) << name;
        EXPECT_EQ(cfg.llm.port, port) << name;
        EXPECT_EQ(cfg.llm.embedding_target, target) << name;
    }

    commandline_args override_args{ "Hermit.exe", "embeddings", "create", "--backend", "ollama", "--llm.model", "embed-model", "--llm.embedding-target", "/custom/embed" };
    Hermit::config override_cfg;
    EXPECT_EQ(Hermit::command_line::parse(override_args.argc(), override_args.argv(), override_cfg), Hermit::command_line::parse_result::success);
    EXPECT_EQ(override_cfg.llm.embedding_target, "/custom/embed");
}

TEST(parse_command_line, requires_model_and_rejects_unsupported_embedding_backends)
{
    commandline_args missing_model{ "Hermit.exe", "embeddings", "create", "--backend", "ollama" };
    Hermit::config missing_model_cfg;
    EXPECT_EQ(Hermit::command_line::parse(missing_model.argc(), missing_model.argv(), missing_model_cfg), Hermit::command_line::parse_result::program_options_error);

    commandline_args unsupported_backend{ "Hermit.exe", "embeddings", "create", "--backend", "tg", "--llm.model", "embed-model" };
    Hermit::config unsupported_backend_cfg;
    EXPECT_EQ(Hermit::command_line::parse(unsupported_backend.argc(), unsupported_backend.argv(), unsupported_backend_cfg), Hermit::command_line::parse_result::program_options_error);
}

TEST(parse_command_line, accepts_multiple_embedding_inputs_and_json_options)
{
    commandline_args args{ "Hermit.exe", "embeddings", "create", "--backend", "ollama", "--llm.model", "embeddinggemma",
        "--llm.embedding-input", "first", "second", "--llm.embedding-options", R"({"truncate":false,"dimensions":256})" };
    Hermit::config cfg;
    EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::success);
    EXPECT_EQ(cfg.llm.embedding_inputs, (std::vector<std::string>{ "first", "second" }));
    EXPECT_EQ(cfg.llm.embedding_options, R"({"truncate":false,"dimensions":256})");
}

TEST(parse_command_line, configures_llama_cpp_tokenize_and_detokenize)
{
    commandline_args encode_args{ "Hermit.exe", "tokenize", "encode", "--backend", "llama-cpp", "--llm.prompt", "hello",
        "--llm.tokenize-options", R"({"with_pieces":true})" };
    Hermit::config encode_cfg;
    EXPECT_EQ(Hermit::command_line::parse(encode_args.argc(), encode_args.argv(), encode_cfg), Hermit::command_line::parse_result::success);
    EXPECT_EQ(encode_cfg.command_mode, Hermit::command_mode::tokenize);
    EXPECT_EQ(encode_cfg.llm.port, "8080");
    EXPECT_EQ(encode_cfg.llm.tokenize_target, "/tokenize");

    commandline_args decode_args{ "Hermit.exe", "tokenize", "decode", "--backend", "llama-cpp", "--llm.token-ids", "5", "6" };
    Hermit::config decode_cfg;
    EXPECT_EQ(Hermit::command_line::parse(decode_args.argc(), decode_args.argv(), decode_cfg), Hermit::command_line::parse_result::success);
    EXPECT_EQ(decode_cfg.command_mode, Hermit::command_mode::detokenize);
    EXPECT_EQ(decode_cfg.llm.detokenize_target, "/detokenize");
    EXPECT_EQ(decode_cfg.llm.token_ids, (std::vector<int>{ 5, 6 }));

    commandline_args unsupported_args{ "Hermit.exe", "tokenize", "encode", "--backend", "ollama" };
    Hermit::config unsupported_cfg;
    EXPECT_EQ(Hermit::command_line::parse(unsupported_args.argc(), unsupported_args.argv(), unsupported_cfg), Hermit::command_line::parse_result::program_options_error);
}

TEST(parse_command_line, explicitly_enables_exec_macro_process_launch)
{
    commandline_args args{ "Hermit.exe", "generate", "--backend", "tg", "--security.allow-process-exec" };
    Hermit::config cfg;
    EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::success);
    EXPECT_TRUE(cfg.allow_process_exec);
}

TEST(parse_command_line, applies_backend_options)
{
    commandline_args args{ "Hermit.exe", "generate", "--backend", "sd", "--sd.mode", "img2img", "--sd.steps", "40", "--sb.language", "JP", "--cu.port", "9000", "--kc.bypass-eos" };
    Hermit::config cfg;
    const Hermit::command_line::parse_result result{ Hermit::command_line::parse(args.argc(), args.argv(), cfg) };

    EXPECT_EQ(result, Hermit::command_line::parse_result::success);
    EXPECT_EQ(cfg.command_mode, Hermit::command_mode::sd);
    EXPECT_EQ(cfg.sd.mode, Hermit::sd_mode::img2img);
    EXPECT_EQ(cfg.sd.common.steps, 40);
    EXPECT_EQ(cfg.sb.language, "JP");
    EXPECT_EQ(cfg.cu.port, "9000");
    EXPECT_TRUE(cfg.kc.common.bypass_eos);
    EXPECT_TRUE(cfg.kc.common.trim_stop);
}

TEST(parse_command_line, separates_sd_common_and_img2img_seed_resize_options)
{
    commandline_args args
    {
        "Hermit.exe", "generate", "--backend", "sd",
        "--sd.seed-resize-from-h", "512",
        "--sd.seed-resize-from-w", "768",
        "--sd.img2img-seed-resize-from-h", "640",
        "--sd.img2img-seed-resize-from-w", "896"
    };
    Hermit::config cfg;
    const Hermit::command_line::parse_result result{ Hermit::command_line::parse(args.argc(), args.argv(), cfg) };

    EXPECT_EQ(result, Hermit::command_line::parse_result::success);
    EXPECT_EQ(cfg.sd.common.seed_resize_from_h, 512);
    EXPECT_EQ(cfg.sd.common.seed_resize_from_w, 768);
    EXPECT_EQ(cfg.sd.img2img.seed_resize_from_h, 640);
    EXPECT_EQ(cfg.sd.img2img.seed_resize_from_w, 896);
}

TEST(parse_command_line, accepts_sd_generation_options)
{
    commandline_args args
    {
        "Hermit.exe", "generate", "--backend", "sd",
        "--sd.generation-options", R"({"custom_extension_field":true})"
    };
    Hermit::config cfg;
    EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::success);
    EXPECT_EQ(cfg.sd.generation_options, R"({"custom_extension_field":true})");
}

struct help_option_test : testing::TestWithParam<const char*> {};

TEST_P(help_option_test, help)
{
    const char* option{ GetParam() };
    commandline_args args{ "Hermit.exe", option };
    const scoped_ostream_redirect cout{ boost::nowide::cout };

    Hermit::command_line::parse_result result{};
    Hermit::config cfg;
    EXPECT_NO_THROW({
        result = Hermit::command_line::parse(args.argc(), args.argv(), cfg);
        });
    EXPECT_EQ(result, Hermit::command_line::parse_result::help);

    const std::string cout_output{ cout.str() };
    EXPECT_THAT(cout_output, testing::HasSubstr("使い方:"));
}

INSTANTIATE_TEST_SUITE_P(
    parse,
    help_option_test,
    testing::Values("--help", "-h")
);

TEST(parse, unrecognised_options)
{
    commandline_args args{ "Hermit.exe", "--unrecognised-option" };
    const scoped_ostream_redirect cerr{ boost::nowide::cerr };

    Hermit::command_line::parse_result result{};
    Hermit::config cfg;
    result = Hermit::command_line::parse(args.argc(), args.argv(), cfg);
    EXPECT_EQ(result, Hermit::command_line::parse_result::program_options_error);

    const std::string cerr_output{ cerr.str() };
    EXPECT_THAT(cerr_output, testing::HasSubstr("unrecognised option"));
    EXPECT_THAT(cerr_output, testing::HasSubstr("--unrecognised-option"));
}

TEST(parse, rejects_invalid_backend_modes)
{
    commandline_args args{ "Hermit.exe", "generate", "--backend", "create-process" };
    const scoped_ostream_redirect cerr{ boost::nowide::cerr };
    Hermit::config cfg;

    EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::program_options_error);
}

TEST(parse, routes_server_and_png_commands)
{
    {
        commandline_args args{ "Hermit.exe", "server", "start" };
        Hermit::config cfg;
        EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::success);
        EXPECT_EQ(cfg.command_mode, Hermit::command_mode::create_process);
    }
    {
        commandline_args args{ "Hermit.exe", "server", "stop" };
        Hermit::config cfg;
        EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::success);
        EXPECT_EQ(cfg.command_mode, Hermit::command_mode::terminate_process);
    }
    {
        commandline_args args{ "Hermit.exe", "png", "extract" };
        Hermit::config cfg;
        EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::success);
        EXPECT_EQ(cfg.command_mode, Hermit::command_mode::extract_png_parameters);
    }
}

TEST(parse, rejects_legacy_flat_cli_syntax)
{
    for (const std::vector<std::string>& values : {
        std::vector<std::string>{ "Hermit.exe", "--mode", "tg" },
        std::vector<std::string>{ "Hermit.exe", "generate", "--backend", "tg", "--llm-host", "localhost" }
    })
    {
        commandline_args args{ values };
        const scoped_ostream_redirect cerr{ boost::nowide::cerr };
        Hermit::config cfg;
        EXPECT_EQ(Hermit::command_line::parse(args.argc(), args.argv(), cfg), Hermit::command_line::parse_result::program_options_error);
    }
}

TEST(test_exception_safe_main, unrecognised_option)
{
    commandline_args args{ "Hermit.exe", "--unrecognised-option" };
    const scoped_ostream_redirect cerr{ boost::nowide::cerr };

    int result{};
    EXPECT_NO_THROW({
        result = Hermit::exception_safe_main(args.argc(), args.argv());
        });
    EXPECT_NE(result, 0);
}

TEST(exception_safe_main, help)
{
    commandline_args args{ "Hermit.exe", "--help" };
    const scoped_ostream_redirect cout{ boost::nowide::cout };

    int result{};
    EXPECT_NO_THROW({
        result = Hermit::exception_safe_main(args.argc(), args.argv());
        });
    EXPECT_EQ(result, 0);
}

TEST(exception_safe_main, help_short)
{
    commandline_args args{ "Hermit.exe", "-h" };
    const scoped_ostream_redirect cout{ boost::nowide::cout };

    int result{};
    EXPECT_NO_THROW({
        result = Hermit::exception_safe_main(args.argc(), args.argv());
        });
    EXPECT_EQ(result, 0);
}

TEST(lru_cache, get_tokens)
{
    using cache_type = Hermit::lru_cache<1>;
    constexpr int factor{ 2 };
    const auto callback{ [](std::string_view str) -> int { return static_cast<int>(str.size()) * factor; } };

    cache_type cache{ callback };
    const std::string str{ "test" };
    EXPECT_EQ(cache.get_tokens(str), callback(str));
    EXPECT_EQ(cache.get_tokens(str), callback(str));
}

TEST(lru_cache, lru)
{
    using cache_type = Hermit::lru_cache<3>;
    int count{};
    const auto callback{ [&count](std::string_view str) -> int { return count++; } };

    cache_type cache{ callback };
    EXPECT_EQ(cache.get_tokens("a"), 0);
    EXPECT_EQ(cache.get_tokens("a"), 0);
    EXPECT_EQ(cache.get_tokens("b"), 1);
    EXPECT_EQ(cache.get_tokens("b"), 1);
    EXPECT_EQ(cache.get_tokens("a"), 0);
    EXPECT_EQ(cache.get_tokens("b"), 1);
    EXPECT_EQ(cache.get_tokens("c"), 2);
    EXPECT_EQ(cache.get_tokens("c"), 2);
    EXPECT_EQ(cache.get_tokens("d"), 3);
    EXPECT_EQ(cache.get_tokens("a"), 4);
}

TEST(lru_cache, persists_entries_and_capacity_through_cbor_file)
{
    using cache_type = Hermit::lru_cache<2>;
    const std::filesystem::path base_path{ std::filesystem::temp_directory_path() / "hermit_lru_persistence_test" };
    std::filesystem::remove_all(base_path);
    Hermit::config cfg;
    cfg.base_path = base_path.string();
    int callback_count{};
    const auto callback{ [&callback_count](std::string_view value) { ++callback_count; return static_cast<int>(value.size()); } };

    cache_type cache{ callback };
    EXPECT_EQ(cache.get_tokens("one"), 3);
    EXPECT_EQ(cache.get_tokens("two"), 3);
    cache.to_file(cfg);

    cache_type restored{ callback };
    restored.from_file(cfg);
    EXPECT_EQ(restored.get_tokens("one"), 3);
    EXPECT_EQ(restored.get_tokens("two"), 3);
    EXPECT_EQ(callback_count, 2);
    restored.get_tokens("three");
    restored.get_tokens("one");
    EXPECT_EQ(callback_count, 4);
    std::filesystem::remove_all(base_path);
}

TEST(lru_cache, persists_and_restores_through_injected_storage)
{
    using cache_type = Hermit::lru_cache<2>;
    Hermit::config cfg;
    cfg.base_path = "memory-store";
    cache_type cache{ [](std::string_view value) { return static_cast<int>(value.size()); } };
    EXPECT_EQ(cache.get_tokens("stored"), 6);
    std::filesystem::path stored_path;
    std::string stored_bytes;
    const Hermit::filesystem::file_writer writer{ [&stored_path, &stored_bytes](const std::filesystem::path& path,
        const char* data, std::size_t size, std::ios_base::openmode mode)
    {
        stored_path = path;
        EXPECT_EQ(mode, std::ios::binary);
        stored_bytes.assign(data, size);
    } };

    cache.to_file(cfg, writer);

    EXPECT_EQ(stored_path, std::filesystem::path{ "memory-store/.token_cache.bin" });
    ASSERT_FALSE(stored_bytes.empty());
    cache_type restored{ [](std::string_view) { return -1; } };
    const Hermit::filesystem::file_reader reader{ [&stored_path, &stored_bytes](const std::filesystem::path& path,
        std::ios_base::openmode mode)
    {
        EXPECT_EQ(path, stored_path);
        EXPECT_EQ(mode, std::ios::binary);
        return stored_bytes;
    } };
    restored.from_file(cfg, reader);
    EXPECT_EQ(restored.get_tokens("stored"), 6);

    const Hermit::filesystem::file_reader corrupt_reader{ [](const std::filesystem::path&, std::ios_base::openmode)
    {
        return std::string{ "corrupt cbor" };
    } };
    EXPECT_NO_THROW(restored.from_file(cfg, corrupt_reader));
    EXPECT_EQ(restored.get_tokens("stored"), 6);
}

TEST(lru_cache, ignores_empty_or_corrupt_persistence_without_discarding_entries)
{
    using cache_type = Hermit::lru_cache<2>;
    const std::filesystem::path base_path{ std::filesystem::temp_directory_path() / "hermit_lru_corrupt_test" };
    std::filesystem::remove_all(base_path);
    std::filesystem::create_directories(base_path);
    Hermit::config cfg;
    cfg.base_path = base_path.string();
    cache_type cache{ [](std::string_view value) { return static_cast<int>(value.size()); } };
    cache.get_tokens("preserved");
    {
        std::ofstream empty_file{ base_path / ".token_cache.bin", std::ios::binary };
    }
    EXPECT_NO_THROW(cache.from_file(cfg));
    EXPECT_EQ(cache.get_tokens("preserved"), 9);
    {
        std::ofstream file{ base_path / ".token_cache.bin", std::ios::binary };
        file << "invalid cbor";
    }

    EXPECT_NO_THROW(cache.from_file(cfg));
    EXPECT_EQ(cache.get_tokens("preserved"), 9);
    std::filesystem::remove_all(base_path);
}

TEST(context, inherited_values_and_override)
{
    Hermit::context base;
    base.set("name", std::string{"base"});
    Hermit::context child{ base.make_pushed() };

    ASSERT_NE(child.get("name"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*child.get("name")), "base");
    child.set("name", std::string{"child"});
    EXPECT_EQ(Hermit::value_to_string(*child.get("name")), "child");
    EXPECT_NE(child.get("name"), base.get("name"));
    EXPECT_EQ(child.get("missing"), nullptr);
}

TEST(context, typed_access_and_parent_value_lifetime)
{
    Hermit::context base;
    base.set("count", 42);
    Hermit::context child{ base.make_pushed() };

    const Hermit::value_type* inherited{ child.get("count") };
    ASSERT_NE(inherited, nullptr);
    EXPECT_EQ(&Hermit::get_or_throw<int>(*inherited), boost::get<int>(base.get("count")));
    EXPECT_EQ(Hermit::get_optional<int>(*inherited), std::optional<int>{ 42 });
    EXPECT_FALSE(Hermit::get_optional<std::string>(*inherited).has_value());
    EXPECT_THROW(Hermit::get_or_throw<std::string>(*inherited), Hermit::macro_exception);
    EXPECT_EQ(child.get("unknown"), nullptr);
}

TEST(process, injects_port_connection_retries_and_waits)
{
    unsigned int attempts{};
    std::vector<std::pair<unsigned int, unsigned int>> waits;
    EXPECT_TRUE(Hermit::wait_for_port(4, 17,
        [&attempts]() { return ++attempts == 3; },
        [&waits](unsigned int retries, unsigned int delay_ms) { waits.emplace_back(retries, delay_ms); }));
    EXPECT_EQ(attempts, 3U);
    EXPECT_EQ(waits, (std::vector<std::pair<unsigned int, unsigned int>>{ { 0, 17 }, { 1, 17 } }));

    attempts = 0;
    waits.clear();
    EXPECT_FALSE(Hermit::wait_for_port(2, 5,
        [&attempts]() { ++attempts; return false; },
        [&waits](unsigned int retries, unsigned int delay_ms) { waits.emplace_back(retries, delay_ms); }));
    EXPECT_EQ(attempts, 2U);
    EXPECT_EQ(waits, (std::vector<std::pair<unsigned int, unsigned int>>{ { 0, 5 }, { 1, 5 } }));

    bool resolver_invoked{};
    EXPECT_TRUE(Hermit::wait_for_port("mock-host", "1234", 2, 8,
        [&resolver_invoked](std::string_view host, std::string_view port)
        {
            resolver_invoked = true;
            EXPECT_EQ(host, "mock-host");
            EXPECT_EQ(port, "1234");
            return [] { return true; };
        },
        [](unsigned int, unsigned int) {}));
    EXPECT_TRUE(resolver_invoked);
    EXPECT_THROW(Hermit::wait_for_port("invalid-host", "1234", 2, 8,
        [](std::string_view, std::string_view) -> Hermit::port_connector { throw std::runtime_error{ "resolve failed" }; },
        [](unsigned int, unsigned int) {}), std::runtime_error);
}

TEST(process, injects_async_process_launcher_and_propagates_failure)
{
    const std::vector<std::string> arguments{ "--flag", "value" };
    bool invoked{};
    Hermit::create_process_async("mock-tool", arguments,
        [&invoked](std::string_view executable, const std::vector<std::string>& actual_arguments)
        {
            invoked = true;
            EXPECT_EQ(executable, "mock-tool");
            EXPECT_EQ(actual_arguments, (std::vector<std::string>{ "--flag", "value" }));
        });
    EXPECT_TRUE(invoked);
    EXPECT_THROW(Hermit::create_process_async("mock-tool", arguments,
        [](std::string_view, const std::vector<std::string>&) { throw std::runtime_error{ "launch failed" }; }), std::runtime_error);
}

TEST(process, create_process_injects_launcher_at_runtime_boundary)
{
    Hermit::config cfg;
    cfg.proc.server_executable_file = "mock-server";
    cfg.proc.server_arguments = "--flag value";
    cfg.proc.server_host = "127.0.0.1";
    cfg.proc.server_port = "0";
    cfg.proc.server_max_retries = 0;

    bool invoked{};
    Hermit::create_process(cfg,
        [&invoked](std::string_view executable_file, const std::vector<std::string>& arguments)
        {
            invoked = true;
            EXPECT_EQ(executable_file, "mock-server");
            EXPECT_EQ(arguments, (std::vector<std::string>{ "--flag", "value" }));
        });

    EXPECT_TRUE(invoked);
    EXPECT_THROW(Hermit::create_process(cfg,
        [](std::string_view, const std::vector<std::string>&) { throw std::runtime_error{ "runtime launch failed" }; }), std::runtime_error);
}

TEST(process, terminate_process_injects_runtime_terminator)
{
    Hermit::config cfg;
    cfg.proc.server_executable_file = "mock-server";
    bool invoked{};
    const std::size_t terminated_count{ Hermit::terminate_process(cfg,
        [&invoked](const std::filesystem::path& executable_file_path)
        {
            invoked = true;
            EXPECT_EQ(executable_file_path, std::filesystem::path{ "mock-server" });
            return 2U;
        }) };

    EXPECT_TRUE(invoked);
    EXPECT_EQ(terminated_count, 2U);
    EXPECT_EQ(Hermit::terminate_process(cfg,
        [](const std::filesystem::path&) { return 0U; }), 0U);
}

TEST(context, erases_only_values_owned_by_current_scope)
{
    Hermit::context base;
    base.set("name", std::string{ "base" });
    Hermit::context child{ base.make_pushed() };
    child.set("temporary", std::string{ "value" });

    child.erase("temporary");
    child.erase("name");

    EXPECT_EQ(child.get("temporary"), nullptr);
    ASSERT_NE(child.get("name"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*child.get("name")), "base");
}

TEST(runtime, clears_stale_previous_and_next_phase_variables)
{
    Hermit::context ctx;
    ctx.set("prev_phase", std::string{ "stale previous" });
    ctx.set("next_phase", std::string{ "stale next" });
    const std::vector<std::string> phases{ "first", "last" };

    Hermit::set_phase_variables(phases, 0, ctx);
    EXPECT_EQ(ctx.get("prev_phase"), nullptr);
    ASSERT_NE(ctx.get("next_phase"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("next_phase")), "last");

    Hermit::set_phase_variables(phases, 1, ctx);
    ASSERT_NE(ctx.get("prev_phase"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("prev_phase")), "first");
    EXPECT_EQ(ctx.get("next_phase"), nullptr);

    const std::vector<std::string> single_phase{ "only" };
    Hermit::set_phase_variables(single_phase, 0, ctx);
    EXPECT_EQ(ctx.get("prev_phase"), nullptr);
    EXPECT_EQ(ctx.get("next_phase"), nullptr);
}

TEST(command_line, split_command_line_args)
{
    const auto args{ Hermit::command_line::split_command_line_args("server.exe --model \"model name.gguf\" --flag") };
    ASSERT_EQ(args.size(), 4U);
    EXPECT_EQ(args[0], "server.exe");
    EXPECT_EQ(args[1], "--model");
    EXPECT_EQ(args[2], "model name.gguf");
    EXPECT_EQ(args[3], "--flag");
}

TEST(command_line, parses_user_defined_variables_without_truncating_values)
{
    Hermit::context ctx;
    const std::vector<std::string> definitions{ "name=first", "name=last=value", "=ignored", "missing_separator" };
    Hermit::command_line::parse_user_defined_variables(definitions, ctx);

    ASSERT_NE(ctx.get("name"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*ctx.get("name")), "last=value");
    EXPECT_EQ(ctx.get("ignored"), nullptr);
    EXPECT_EQ(ctx.get("missing_separator"), nullptr);
}

TEST(command_line, reads_config_stream_and_notifies_dotted_llm_options)
{
    Hermit::config cfg;
    const boost::program_options::options_description options{ Hermit::command_line::make_options_description(cfg) };
    boost::program_options::variables_map variables;
    std::istringstream config_stream{ "backend=kc\nllm.mode=chat_completions\nllm.host=config.example\nllm.max-completion-iterations=7\n" };

    Hermit::command_line::parse_config_stream(options, config_stream, variables);
    boost::program_options::notify(variables);

    EXPECT_EQ(variables.at("backend").as<std::string>(), "kc");
    EXPECT_EQ(cfg.llm.mode, Hermit::llm_mode::chat_completions);
    EXPECT_EQ(cfg.llm.host, "config.example");
    EXPECT_EQ(cfg.llm.max_completion_iterations, 7);
    EXPECT_EQ(variables.at("backend").as<std::string>(), "kc");
}

TEST(command_line, after_parse_applies_user_defined_variables)
{
    Hermit::config cfg;
    cfg.user_defined_variables = { "name=value=with=equals", "=ignored" };

    Hermit::command_line::after_parse(cfg);

    ASSERT_NE(cfg.ctx.get("name"), nullptr);
    EXPECT_EQ(Hermit::value_to_string(*cfg.ctx.get("name")), "value=with=equals");
    EXPECT_EQ(cfg.ctx.get("ignored"), nullptr);
}

TEST(command_line, command_line_values_take_precedence_over_config_stream)
{
    Hermit::config cfg;
    const boost::program_options::options_description options{ Hermit::command_line::make_options_description(cfg) };
    boost::program_options::variables_map variables;
    commandline_args arguments{ "Hermit.exe", "generate", "--backend=tg", "--llm.host=cli.example" };
    Hermit::command_line::parse_command_line(options, arguments.argc(), arguments.argv(), variables);
    std::istringstream config_stream{ "backend=kc\nllm.host=config.example\n" };

    Hermit::command_line::parse_config_stream(options, config_stream, variables);
    boost::program_options::notify(variables);

    EXPECT_EQ(variables.at("backend").as<std::string>(), "tg");
    EXPECT_EQ(cfg.llm.host, "cli.example");
}

TEST(command_line, rejects_invalid_llm_mode_from_config_stream)
{
    Hermit::config cfg;
    const boost::program_options::options_description options{ Hermit::command_line::make_options_description(cfg) };
    boost::program_options::variables_map variables;
    std::istringstream config_stream{ "llm.mode=unknown\n" };
    Hermit::command_line::parse_config_stream(options, config_stream, variables);

    EXPECT_THROW(boost::program_options::notify(variables), Hermit::command_line_exception);
}

TEST(command_mode, string_conversion)
{
    EXPECT_EQ(Hermit::string_to_command_mode("tg"), Hermit::command_mode::tg);
    EXPECT_EQ(Hermit::string_to_command_mode("kc"), Hermit::command_mode::kc);
    EXPECT_EQ(Hermit::string_to_command_mode("cu"), Hermit::command_mode::cu);
    EXPECT_EQ(Hermit::string_to_command_mode("create-process"), Hermit::command_mode::create_process);
    EXPECT_EQ(Hermit::string_to_command_mode("terminate-process"), Hermit::command_mode::terminate_process);
    EXPECT_EQ(Hermit::string_to_command_mode("extract-png-parameters"), Hermit::command_mode::extract_png_parameters);
    EXPECT_THROW(Hermit::string_to_command_mode("unknown"), std::exception);
}

TEST(string_utils, filename_and_language_helpers)
{
    EXPECT_EQ(Hermit::sanitize_as_filename("a:b/c*?"), "abc");
    EXPECT_EQ(Hermit::sanitize_as_filename("..\\..\\secret"), "....secret");
    EXPECT_TRUE(Hermit::sanitize_as_filename("..").empty());
    EXPECT_EQ(Hermit::language_identifier_to_extension("cpp"), ".cpp");
    EXPECT_EQ(Hermit::language_identifier_to_extension("unknown-language"), "");
}

TEST(media_config, loads_image_data_and_maps_log_severity)
{
    const std::filesystem::path base_path{ std::filesystem::temp_directory_path() / "hermit_media_config_test" };
    std::filesystem::remove_all(base_path);
    std::filesystem::create_directories(base_path);
    const std::string image_data{ "image-bytes" };
    {
        std::ofstream image_file{ base_path / "sample.png", std::ios::binary };
        image_file.write(image_data.data(), static_cast<std::streamsize>(image_data.size()));
    }
    Hermit::config cfg;
    cfg.base_path = base_path.string();

    const Hermit::image_info_type image{ Hermit::image_info_type::from_file("sample.png", cfg) };
    EXPECT_EQ(image.base64_image, "aW1hZ2UtYnl0ZXM=");
    EXPECT_EQ(image.mime_type, "png");
    EXPECT_THROW(Hermit::image_info_type::from_file("missing.png", cfg), std::exception);
    EXPECT_EQ(Hermit::log::string_to_severity_level("trace"), Hermit::log::severity_level::trace);
    EXPECT_EQ(Hermit::log::string_to_severity_level("debug"), Hermit::log::severity_level::debug);
    EXPECT_EQ(Hermit::log::string_to_severity_level("info"), Hermit::log::severity_level::info);
    EXPECT_EQ(Hermit::log::string_to_severity_level("warning"), Hermit::log::severity_level::warning);
    EXPECT_EQ(Hermit::log::string_to_severity_level("error"), Hermit::log::severity_level::error);
    EXPECT_EQ(Hermit::log::string_to_severity_level("fatal"), Hermit::log::severity_level::fatal);
    EXPECT_THROW(Hermit::log::string_to_severity_level("unknown"), Hermit::logic_error);
    std::filesystem::remove_all(base_path);
}

TEST(media_config, loads_images_through_injected_reader)
{
    Hermit::config cfg;
    cfg.base_path = "memory-images";
    std::filesystem::path captured_path;
    std::ios_base::openmode captured_mode{};
    const Hermit::filesystem::file_reader reader{ [&captured_path, &captured_mode](const std::filesystem::path& path,
        std::ios_base::openmode mode)
    {
        captured_path = path;
        captured_mode = mode;
        return std::string{ "png\0data", 8 };
    } };

    const Hermit::image_info_type image{ Hermit::image_info_type::from_file("images/sample.png", cfg, reader) };

    EXPECT_EQ(captured_path, std::filesystem::path{ "memory-images/images/sample.png" });
    EXPECT_EQ(captured_mode, std::ios::binary);
    EXPECT_EQ(image.base64_image, "cG5nAGRhdGE=");
    EXPECT_EQ(image.mime_type, "png");
    EXPECT_THROW(Hermit::image_info_type::from_file("images/sample.unknown", cfg, reader), std::exception);
    EXPECT_THROW(Hermit::image_info_type::from_file("images/sample.png", cfg,
        [](const std::filesystem::path&, std::ios_base::openmode) -> std::string
        {
            throw std::runtime_error{ "injected image reader failure" };
        }), std::runtime_error);
}

TEST(runtime, generate_and_output_uses_injected_http_and_file_boundaries)
{
    Hermit::config cfg;
    cfg.command_mode = Hermit::command_mode::sd;
    cfg.sd.mode = Hermit::sd_mode::img2img;
    cfg.sd.host = "localhost";
    cfg.sd.port = "7860";
    cfg.sd.img2img.target = "/sdapi/v1/img2img";
    cfg.base_path = "runtime-images";
    cfg.sd.common.prompt = "runtime prompt";
    cfg.sd.common.negative_prompt = "runtime negative";
    cfg.sd.img2img.init_images = { "init.png" };
    cfg.sd.output_file = "result.png";

    std::vector<std::filesystem::path> reads;
    const Hermit::filesystem::file_reader reader{ [&reads](const std::filesystem::path& path, std::ios_base::openmode mode)
    {
        EXPECT_EQ(mode, std::ios::binary);
        reads.push_back(path);
        return std::string{ "init-bytes" };
    } };
    std::filesystem::path written_path;
    std::string written_data;
    std::ios_base::openmode written_mode{};
    const Hermit::filesystem::file_writer writer{ [&written_path, &written_data, &written_mode](const std::filesystem::path& path,
        const char* data, std::size_t size, std::ios_base::openmode mode)
    {
        written_path = path;
        written_data.assign(data, size);
        written_mode = mode;
    } };
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        const nlohmann::json body{ nlohmann::json::parse(request.body) };
        EXPECT_EQ(body.at("prompt"), "runtime prompt");
        EXPECT_EQ(body.at("init_images").at(0), "aW5pdC1ieXRlcw==");
        return Hermit::public_api::http_response_data{ 200, R"({"images":["Z2VuZXJhdGVkLWltYWdl"]})" };
    } };

    Hermit::generate_and_output(cfg, writer, executor, reader);

    ASSERT_EQ(reads.size(), 1U);
    EXPECT_EQ(reads.front(), std::filesystem::path{ "runtime-images/init.png" });
    EXPECT_EQ(written_path, std::filesystem::path{ "runtime-images/result.png" });
    EXPECT_EQ(written_data, "generated-image");
    EXPECT_EQ(written_mode, std::ios::binary);
}

TEST(runtime, propagates_generated_file_writer_failure)
{
    Hermit::config cfg;
    cfg.command_mode = Hermit::command_mode::sd;
    cfg.sd.mode = Hermit::sd_mode::txt2img;
    cfg.sd.host = "localhost";
    cfg.sd.port = "7860";
    cfg.sd.txt2img.target = "/txt2img";
    cfg.sd.common.prompt = "runtime prompt";
    cfg.sd.common.negative_prompt = "runtime negative";
    cfg.sd.output_file = "result.png";
    const Hermit::filesystem::file_writer writer{ [](const std::filesystem::path&, const char*, std::size_t, std::ios_base::openmode)
    {
        throw std::runtime_error{ "injected runtime writer failure" };
    } };
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.target, "/txt2img");
        return Hermit::public_api::http_response_data{ 200, R"({"images":["aW1hZ2U="]})" };
    } };

    EXPECT_THROW(Hermit::generate_and_output(cfg, writer, executor, Hermit::filesystem::file_reader{}), std::runtime_error);
}

TEST(runtime, preserves_legacy_generate_and_output_overloads)
{
    using config_overload = void(*)(const Hermit::config&);
    using writer_overload = void(*)(const Hermit::config&, const Hermit::filesystem::file_writer&);
    const config_overload generate{ static_cast<config_overload>(&Hermit::generate_and_output) };
    const writer_overload generate_with_writer{ static_cast<writer_overload>(&Hermit::generate_and_output) };

    Hermit::config cfg;
    cfg.command_mode = Hermit::command_mode::extract_png_parameters;
    int write_count{};
    const Hermit::filesystem::file_writer writer{ [&write_count](const std::filesystem::path&, const char*, std::size_t, std::ios_base::openmode)
    {
        ++write_count;
    } };

    generate(cfg);
    generate_with_writer(cfg, writer);

    EXPECT_EQ(write_count, 0);
}

TEST(runtime, legacy_writer_overload_uses_default_tcp_for_llm)
{
    std::string received_target;
    std::string received_body;
    LoopbackHttpServer server{ [&received_target, &received_body](const LoopbackHttpServer::request_type& request)
    {
        received_target = request.target();
        received_body = request.body();
        LoopbackHttpServer::response_type response{ boost::beast::http::status::ok, 11 };
        response.body() = R"({"choices":[{"message":{"content":"loopback answer"}}]})";
        return response;
    } };

    Hermit::config cfg;
    cfg.command_mode = Hermit::command_mode::tg;
    cfg.llm.mode = Hermit::llm_mode::chat_completions;
    cfg.llm.backend = &cfg.tg;
    cfg.llm.host = "127.0.0.1";
    cfg.llm.port = server.port();
    cfg.llm.chat_completions_target = "/chat";
    cfg.llm.prompt = "loopback prompt";
    cfg.llm.chat_file = "chat.json";
    cfg.llm.output_file = "answer.txt";
    cfg.base_path = "runtime-legacy-llm";
    cfg.timeout_connect = 2;
    cfg.timeout_request = 2;

    std::vector<std::filesystem::path> written_paths;
    std::vector<std::string> written_data;
    const Hermit::filesystem::file_writer writer{ [&written_paths, &written_data](const std::filesystem::path& path,
        const char* data, std::size_t size, std::ios_base::openmode)
    {
        written_paths.push_back(path);
        written_data.emplace_back(data, size);
    } };

    std::string client_error;
    try { Hermit::generate_and_output(cfg, writer); }
    catch (const boost::exception& error) { client_error = boost::diagnostic_information(error); }
    catch (const std::exception& error) { client_error = error.what(); }
    std::string server_error;
    try { server.finish(); }
    catch (const std::exception& error) { server_error = error.what(); }

    EXPECT_TRUE(client_error.empty()) << client_error;
    EXPECT_TRUE(server_error.empty()) << server_error;
    EXPECT_EQ(received_target, "/chat");
    const nlohmann::json body{ nlohmann::json::parse(received_body) };
    EXPECT_EQ(body.at("messages").at(0).at("content").at(0).at("text"), "loopback prompt");
    ASSERT_EQ(written_paths.size(), 2U);
    EXPECT_EQ(written_paths[0], std::filesystem::path{ "runtime-legacy-llm/chat.json" });
    EXPECT_EQ(written_paths[1], std::filesystem::path{ "runtime-legacy-llm/answer.txt" });
    EXPECT_EQ(written_data[1], "loopback answer");
}

TEST(runtime, legacy_no_writer_overload_uses_default_tcp_for_sd)
{
    std::string received_target;
    LoopbackHttpServer server{ [&received_target](const LoopbackHttpServer::request_type& request)
    {
        received_target = request.target();
        LoopbackHttpServer::response_type response{ boost::beast::http::status::ok, 11 };
        response.body() = R"({"images":["b2xkLW91dHB1dA=="]})";
        return response;
    } };

    const std::filesystem::path base_path{ std::filesystem::temp_directory_path()
        / ("hermit_legacy_runtime_sd_" + Hermit::llm::generate_uuid_v4()) };
    Hermit::config cfg;
    cfg.command_mode = Hermit::command_mode::sd;
    cfg.base_path = base_path.string();
    cfg.sd.mode = Hermit::sd_mode::txt2img;
    cfg.sd.host = "127.0.0.1";
    cfg.sd.port = server.port();
    cfg.sd.txt2img.target = "/txt2img";
    cfg.sd.common.prompt = "loopback prompt";
    cfg.sd.common.negative_prompt = "loopback negative";
    cfg.sd.output_file = "result.png";
    cfg.timeout_connect = 2;
    cfg.timeout_request = 2;

    std::string client_error;
    try
    {
        Hermit::generate_and_output(cfg);
    }
    catch (const boost::exception& error)
    {
        client_error = boost::diagnostic_information(error);
    }
    catch (const std::exception& error)
    {
        client_error = error.what();
    }
    std::string server_error;
    try { server.finish(); }
    catch (const std::exception& error) { server_error = error.what(); }

    EXPECT_TRUE(client_error.empty()) << client_error;
    EXPECT_TRUE(server_error.empty()) << server_error;
    EXPECT_EQ(received_target, "/txt2img");
    EXPECT_EQ(Hermit::filesystem::read_file_to_string(base_path / "result.png", std::ios::binary), "old-output");
    std::filesystem::remove_all(base_path);
}

TEST(runtime, legacy_no_writer_overload_uses_default_tcp_for_sb)
{
    std::string received_target;
    LoopbackHttpServer server{ [&received_target](const LoopbackHttpServer::request_type& request)
    {
        received_target = request.target();
        LoopbackHttpServer::response_type response{ boost::beast::http::status::ok, 11 };
        response.body() = "speech-bytes";
        return response;
    } };

    const std::filesystem::path base_path{ std::filesystem::temp_directory_path()
        / ("hermit_legacy_runtime_sb_" + Hermit::llm::generate_uuid_v4()) };
    Hermit::config cfg;
    cfg.command_mode = Hermit::command_mode::sb;
    cfg.base_path = base_path.string();
    cfg.sb.host = "127.0.0.1";
    cfg.sb.port = server.port();
    cfg.sb.target = "/voice";
    cfg.sb.text = "loopback speech";
    cfg.sb.output_file = "result.wav";
    cfg.timeout_connect = 2;
    cfg.timeout_request = 2;

    std::string client_error;
    try { Hermit::generate_and_output(cfg); }
    catch (const boost::exception& error) { client_error = boost::diagnostic_information(error); }
    catch (const std::exception& error) { client_error = error.what(); }
    std::string server_error;
    try { server.finish(); }
    catch (const std::exception& error) { server_error = error.what(); }

    EXPECT_TRUE(client_error.empty()) << client_error;
    EXPECT_TRUE(server_error.empty()) << server_error;
    EXPECT_THAT(received_target, testing::HasSubstr("/voice?text=loopback+speech"));
    EXPECT_EQ(Hermit::filesystem::read_file_to_string(base_path / "result.wav", std::ios::binary), "speech-bytes");
    std::filesystem::remove_all(base_path);
}

TEST(runtime, legacy_no_writer_overload_uses_default_tcp_for_comfyui)
{
    std::vector<std::string> received_targets;
    LoopbackHttpServer server{ [&received_targets](const LoopbackHttpServer::request_type& request)
    {
        received_targets.emplace_back(request.target());
        LoopbackHttpServer::response_type response{ boost::beast::http::status::ok, 11 };
        if (request.target() == "/prompt")
        {
            response.body() = R"({"prompt_id":"prompt-1"})";
        }
        else if (request.target() == "/history/prompt-1")
        {
            response.body() = R"({"prompt-1":{"outputs":{"node":{"images":[{"filename":"generated.png","type":"output"}]}}}})";
        }
        else
        {
            response.body() = "image-bytes";
        }
        return response;
    } };

    const std::filesystem::path base_path{ std::filesystem::temp_directory_path()
        / ("hermit_legacy_runtime_cu_" + Hermit::llm::generate_uuid_v4()) };
    Hermit::config cfg;
    cfg.command_mode = Hermit::command_mode::cu;
    cfg.base_path = base_path.string();
    cfg.cu.host = "127.0.0.1";
    cfg.cu.port = server.port();
    cfg.cu.prompt_target = "/prompt";
    cfg.cu.prompt = R"({"1":{"class_type":"test"}})";
    cfg.cu.output_directory = "images";
    cfg.timeout_connect = 2;
    cfg.timeout_request = 2;

    std::string client_error;
    try { Hermit::generate_and_output(cfg); }
    catch (const boost::exception& error) { client_error = boost::diagnostic_information(error); }
    catch (const std::exception& error) { client_error = error.what(); }
    std::string server_error;
    try { server.finish(); }
    catch (const std::exception& error) { server_error = error.what(); }

    EXPECT_TRUE(client_error.empty()) << client_error;
    EXPECT_TRUE(server_error.empty()) << server_error;
    ASSERT_EQ(received_targets.size(), 3U);
    EXPECT_EQ(received_targets[0], "/prompt");
    EXPECT_EQ(received_targets[1], "/history/prompt-1");
    EXPECT_THAT(received_targets[2], testing::StartsWith("/?filename=generated.png"));
    EXPECT_EQ(Hermit::filesystem::read_file_to_string(base_path / "images/generated.png", std::ios::binary), "image-bytes");
    std::filesystem::remove_all(base_path);
}

TEST(runtime, generate_and_output_injects_sb_http_and_binary_writer)
{
    Hermit::config cfg;
    cfg.command_mode = Hermit::command_mode::sb;
    cfg.base_path = "runtime-audio";
    cfg.sb.host = "localhost";
    cfg.sb.port = "5000";
    cfg.sb.target = "/voice";
    cfg.sb.text = "hello";
    cfg.sb.output_file = "speech.wav";
    std::filesystem::path written_path;
    std::string written_data;
    std::ios_base::openmode written_mode{};
    const Hermit::filesystem::file_writer writer{ [&written_path, &written_data, &written_mode](const std::filesystem::path& path,
        const char* data, std::size_t size, std::ios_base::openmode mode)
    {
        written_path = path;
        written_data.assign(data, size);
        written_mode = mode;
    } };
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.host, "localhost");
        EXPECT_EQ(request.port, "5000");
        EXPECT_THAT(request.target, testing::HasSubstr("text=hello"));
        return Hermit::public_api::http_response_data{ 200, "audio-bytes" };
    } };

    Hermit::generate_and_output(cfg, writer, executor, Hermit::filesystem::file_reader{});

    EXPECT_EQ(written_path, std::filesystem::path{ "runtime-audio/speech.wav" });
    EXPECT_EQ(written_data, "audio-bytes");
    EXPECT_EQ(written_mode, std::ios::binary);
}

TEST(runtime, propagates_sb_binary_writer_failure)
{
    Hermit::config cfg;
    cfg.command_mode = Hermit::command_mode::sb;
    cfg.base_path = "runtime-sb-writer-failure";
    cfg.sb.host = "localhost";
    cfg.sb.port = "5000";
    cfg.sb.target = "/voice";
    cfg.sb.text = "hello";
    cfg.sb.output_file = "speech.wav";
    const Hermit::filesystem::file_writer writer{ [](const std::filesystem::path&, const char*, std::size_t, std::ios_base::openmode)
    {
        throw std::runtime_error{ "injected SB binary writer failure" };
    } };
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data&)
    {
        return Hermit::public_api::http_response_data{ 200, "audio-bytes" };
    } };

    EXPECT_THROW(Hermit::generate_and_output(cfg, writer, executor, Hermit::filesystem::file_reader{}), std::runtime_error);
}

TEST(runtime, generate_and_output_injects_llm_http_and_file_boundaries)
{
    Hermit::config cfg;
    cfg.command_mode = Hermit::command_mode::tg;
    cfg.llm.mode = Hermit::llm_mode::chat_completions;
    cfg.llm.backend = &cfg.tg;
    cfg.llm.host = "localhost";
    cfg.llm.port = "8080";
    cfg.llm.chat_completions_target = "/chat";
    cfg.llm.prompt = "runtime prompt";
    cfg.llm.image_file = "input.png";
    cfg.llm.chat_file = "chat.json";
    cfg.llm.output_file = "answer.txt";
    cfg.llm.code_block_extract = false;
    cfg.base_path = "runtime-llm";

    std::vector<std::filesystem::path> written_paths;
    std::vector<std::string> written_data;
    const Hermit::filesystem::file_writer writer{ [&written_paths, &written_data](const std::filesystem::path& path,
        const char* data, std::size_t size, std::ios_base::openmode)
    {
        written_paths.push_back(path);
        written_data.emplace_back(data, size);
    } };
    std::filesystem::path read_path;
    const Hermit::filesystem::file_reader reader{ [&read_path](const std::filesystem::path& path, std::ios_base::openmode mode)
    {
        EXPECT_EQ(mode, std::ios::binary);
        read_path = path;
        return std::string{ "png-bytes" };
    } };
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.host, "localhost");
        EXPECT_EQ(request.target, "/chat");
        const nlohmann::json body{ nlohmann::json::parse(request.body) };
        EXPECT_EQ(body.at("messages").at(0).at("content").at(0).at("text"), "runtime prompt");
        EXPECT_EQ(body.at("messages").at(0).at("content").at(1).at("image_url").at("url"), "data:image/png;base64,cG5nLWJ5dGVz");
        return Hermit::public_api::http_response_data{ 200, R"({"choices":[{"message":{"content":"runtime answer"}}]})" };
    } };

    Hermit::generate_and_output(cfg, writer, executor, reader);

    EXPECT_EQ(read_path, std::filesystem::path{ "runtime-llm/input.png" });
    ASSERT_EQ(written_paths.size(), 2U);
    EXPECT_EQ(written_paths[0], std::filesystem::path{ "runtime-llm/chat.json" });
    EXPECT_EQ(written_paths[1], std::filesystem::path{ "runtime-llm/answer.txt" });
    EXPECT_EQ(written_data[1], "runtime answer");
}

TEST(runtime, propagates_llm_text_writer_failure)
{
    Hermit::config cfg;
    cfg.command_mode = Hermit::command_mode::tg;
    cfg.llm.mode = Hermit::llm_mode::chat_completions;
    cfg.llm.backend = &cfg.tg;
    cfg.llm.host = "localhost";
    cfg.llm.port = "8080";
    cfg.llm.chat_completions_target = "/chat";
    cfg.llm.prompt = "runtime prompt";
    cfg.llm.chat_file = "chat.json";
    cfg.llm.output_file = "answer.txt";
    const Hermit::filesystem::file_writer writer{ [](const std::filesystem::path&, const char*, std::size_t, std::ios_base::openmode)
    {
        throw std::runtime_error{ "injected LLM writer failure" };
    } };
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data& request)
    {
        EXPECT_EQ(request.target, "/chat");
        return Hermit::public_api::http_response_data{ 200, R"({"choices":[{"message":{"content":"runtime answer"}}]})" };
    } };

    EXPECT_THROW(Hermit::generate_and_output(cfg, writer, executor, Hermit::filesystem::file_reader{}), std::runtime_error);
}

TEST(runtime, generate_and_output_injects_completion_and_token_count_http_requests)
{
    Hermit::config cfg;
    cfg.command_mode = Hermit::command_mode::tg;
    cfg.llm.mode = Hermit::llm_mode::completions;
    cfg.llm.backend = &cfg.tg;
    cfg.llm.host = "localhost";
    cfg.llm.port = "8080";
    cfg.llm.completions_target = "/completion";
    cfg.llm.token_count_target = "/token-count";
    cfg.llm.prompt = "runtime prompt";
    cfg.llm.output_file = "answer.txt";
    cfg.llm.max_completion_iterations = 1;
    cfg.llm.min_completion_tokens = 5;
    cfg.tg.common.max_tokens = 12;
    cfg.tg.common.truncation_length = 100;
    cfg.base_path = "runtime-completion";

    std::vector<std::string> request_targets;
    std::filesystem::path written_path;
    std::string written_data;
    const Hermit::filesystem::file_writer writer{ [&written_path, &written_data](const std::filesystem::path& path,
        const char* data, std::size_t size, std::ios_base::openmode)
    {
        written_path = path;
        written_data.assign(data, size);
    } };
    const Hermit::public_api::http_executor executor{ [&request_targets](const Hermit::public_api::http_request_data& request)
    {
        request_targets.push_back(request.target);
        if (request.target == "/token-count")
        {
            const nlohmann::json body{ nlohmann::json::parse(request.body) };
            EXPECT_TRUE(body.contains("text"));
            return Hermit::public_api::http_response_data{ 200, R"({"length":0})" };
        }
        EXPECT_EQ(request.target, "/completion");
        return Hermit::public_api::http_response_data{ 200, R"({"choices":[{"text":"runtime completion"}]})" };
    } };

    Hermit::generate_and_output(cfg, writer, executor, Hermit::filesystem::file_reader{});

    ASSERT_EQ(request_targets.size(), 4U);
    EXPECT_EQ(request_targets[0], "/token-count");
    EXPECT_EQ(request_targets[1], "/token-count");
    EXPECT_EQ(request_targets[2], "/completion");
    EXPECT_EQ(request_targets[3], "/token-count");
    EXPECT_EQ(written_path, std::filesystem::path{ "runtime-completion/answer.txt" });
    EXPECT_EQ(written_data, "runtime completion");
}

TEST(runtime, completion_token_count_http_failure_propagates_without_writing)
{
    Hermit::config cfg;
    cfg.command_mode = Hermit::command_mode::tg;
    cfg.llm.mode = Hermit::llm_mode::completions;
    cfg.llm.backend = &cfg.tg;
    cfg.llm.host = "localhost";
    cfg.llm.port = "8080";
    cfg.llm.token_count_target = "/token-count";
    cfg.llm.prompt = "runtime prompt";

    int write_count{};
    const Hermit::filesystem::file_writer writer{ [&write_count](const std::filesystem::path&, const char*, std::size_t, std::ios_base::openmode)
    {
        ++write_count;
    } };
    const Hermit::public_api::http_executor executor{ [](const Hermit::public_api::http_request_data&)
    {
        return Hermit::public_api::http_response_data{ 503, "unavailable" };
    } };

    EXPECT_THROW(Hermit::generate_and_output(cfg, writer, executor, Hermit::filesystem::file_reader{}), Hermit::http_status_exception);
    EXPECT_EQ(write_count, 0);
}

TEST(runtime, generate_and_output_injects_completion_and_token_count_tcp_requests)
{
    Hermit::config cfg;
    cfg.command_mode = Hermit::command_mode::tg;
    cfg.llm.mode = Hermit::llm_mode::completions;
    cfg.llm.backend = &cfg.tg;
    cfg.llm.host = "localhost";
    cfg.llm.port = "8080";
    cfg.llm.completions_target = "/completion";
    cfg.llm.token_count_target = "/token-count";
    cfg.llm.prompt = "runtime prompt";
    cfg.llm.output_file = "answer.txt";
    cfg.llm.max_completion_iterations = 1;
    cfg.llm.min_completion_tokens = 5;
    cfg.tg.common.max_tokens = 12;
    cfg.tg.common.truncation_length = 100;

    std::vector<std::string> request_targets;
    const Hermit::filesystem::file_writer writer{ [](const std::filesystem::path&, const char*, std::size_t, std::ios_base::openmode) {} };
    const Hermit::tcp::request_executor executor{ [&request_targets](std::string_view host, std::string_view port,
        const Hermit::tcp::request_type& request)
    {
        EXPECT_EQ(host, "localhost");
        EXPECT_EQ(port, "8080");
        request_targets.emplace_back(request.target());
        Hermit::tcp::response_type response{ boost::beast::http::status::ok, 11 };
        if (request.target() == "/token-count")
        {
            EXPECT_TRUE(nlohmann::json::parse(request.body()).contains("text"));
            response.body() = R"({"length":0})";
        }
        else
        {
            EXPECT_EQ(request.target(), "/completion");
            response.body() = R"({"choices":[{"text":"tcp completion"}]})";
        }
        response.prepare_payload();
        return response;
    } };

    Hermit::generate_and_output(cfg, writer, executor, Hermit::filesystem::file_reader{});

    ASSERT_EQ(request_targets.size(), 4U);
    EXPECT_EQ(request_targets[0], "/token-count");
    EXPECT_EQ(request_targets[1], "/token-count");
    EXPECT_EQ(request_targets[2], "/completion");
    EXPECT_EQ(request_targets[3], "/token-count");
}

TEST(runtime, generate_and_output_injects_comfyui_http_and_binary_writer)
{
    Hermit::config cfg;
    cfg.command_mode = Hermit::command_mode::cu;
    cfg.base_path = "runtime-comfyui";
    cfg.cu.host = "localhost";
    cfg.cu.port = "8188";
    cfg.cu.prompt_target = "/prompt";
    cfg.cu.prompt = R"({"1":{"class_type":"test"}})";
    cfg.cu.output_directory = "images";

    std::vector<std::filesystem::path> written_paths;
    std::vector<std::string> written_data;
    std::vector<std::string> request_targets;
    const Hermit::filesystem::file_writer writer{ [&written_paths, &written_data](const std::filesystem::path& path,
        const char* data, std::size_t size, std::ios_base::openmode mode)
    {
        EXPECT_EQ(mode, std::ios::binary);
        written_paths.push_back(path);
        written_data.emplace_back(data, size);
    } };
    const Hermit::public_api::http_executor executor{ [&request_targets](const Hermit::public_api::http_request_data& request)
    {
        request_targets.push_back(request.target);
        if (request.target == "/prompt")
        {
            return Hermit::public_api::http_response_data{ 200, R"({"prompt_id":"prompt-1"})" };
        }
        if (request.target == "/history/prompt-1")
        {
            return Hermit::public_api::http_response_data{ 200,
                R"({"prompt-1":{"outputs":{"node":{"images":[{"filename":"generated.png","type":"output"}]}}}})" };
        }
        EXPECT_THAT(request.target, testing::HasSubstr("filename=generated.png"));
        return Hermit::public_api::http_response_data{ 200, "image-bytes" };
    } };

    Hermit::generate_and_output(cfg, writer, executor, Hermit::filesystem::file_reader{});

    ASSERT_EQ(request_targets.size(), 3U);
    EXPECT_EQ(request_targets[0], "/prompt");
    EXPECT_EQ(request_targets[1], "/history/prompt-1");
    ASSERT_EQ(written_paths.size(), 1U);
    EXPECT_EQ(written_paths.front(), std::filesystem::path{ "runtime-comfyui/images/generated.png" });
    EXPECT_EQ(written_data.front(), "image-bytes");
}

TEST(runtime, propagates_comfyui_binary_storage_failure)
{
    Hermit::config cfg;
    cfg.command_mode = Hermit::command_mode::cu;
    cfg.cu.host = "localhost";
    cfg.cu.port = "8188";
    cfg.cu.prompt_target = "/prompt";
    cfg.cu.prompt = R"({"1":{"class_type":"test"}})";
    cfg.cu.output_directory = "images";
    int request_count{};
    const Hermit::filesystem::file_writer writer{ [](const std::filesystem::path&, const char*, std::size_t, std::ios_base::openmode)
    {
        throw std::runtime_error{ "injected ComfyUI storage failure" };
    } };
    const Hermit::public_api::http_executor executor{ [&request_count](const Hermit::public_api::http_request_data& request)
    {
        ++request_count;
        if (request.target == "/prompt") return Hermit::public_api::http_response_data{ 200, R"({"prompt_id":"prompt-1"})" };
        if (request.target == "/history/prompt-1")
        {
            return Hermit::public_api::http_response_data{ 200,
                R"({"prompt-1":{"outputs":{"node":{"images":[{"filename":"generated.png","type":"output"}]}}}})" };
        }
        return Hermit::public_api::http_response_data{ 200, "image-bytes" };
    } };

    EXPECT_THROW(Hermit::generate_and_output(cfg, writer, executor, Hermit::filesystem::file_reader{}), std::runtime_error);
    EXPECT_EQ(request_count, 3);
}

TEST(log, writes_to_injected_stream_factory)
{
    const std::filesystem::path requested_path{ "memory-log/test.log" };
    std::filesystem::path captured_path;
    const std::shared_ptr<std::ostringstream> stream{ std::make_shared<std::ostringstream>() };
    const std::shared_ptr<std::ostream> created_stream{ Hermit::log::create_stream(requested_path,
        [&captured_path, stream](const std::filesystem::path& path)
    {
        captured_path = path;
        return stream;
    }) };
    *created_stream << "injected log output";

    EXPECT_EQ(captured_path, requested_path);
    EXPECT_EQ(created_stream, stream);
    EXPECT_EQ(stream->str(), "injected log output");
    EXPECT_THROW(Hermit::log::create_stream(requested_path,
        [](const std::filesystem::path&) -> std::shared_ptr<std::ostream>
        {
            throw std::runtime_error{ "injected stream factory failure" };
        }), std::runtime_error);
}

TEST(value, conversion)
{
    EXPECT_EQ(Hermit::value_to_string(Hermit::value_type{ 42 }), "42");
    EXPECT_EQ(Hermit::value_to_string(Hermit::value_type{ true }), "1");
    EXPECT_EQ(Hermit::value_to_string(Hermit::value_type{ std::string{"hello"} }), "hello");
    EXPECT_EQ(Hermit::get_optional<int>(Hermit::value_type{ 7 }), std::optional<int>{ 7 });
    EXPECT_FALSE(Hermit::get_optional<int>(Hermit::value_type{ std::string{"x"} }).has_value());
}

TEST(string_utils, encoding_and_escaping)
{
    const std::string encoded{ Hermit::string_utils::base64_encode("hello") };
    EXPECT_EQ(encoded, "aGVsbG8=");
    EXPECT_EQ(Hermit::string_utils::base64_decode(encoded), "hello");
    EXPECT_TRUE(Hermit::string_utils::is_base64(encoded, 4));
    EXPECT_TRUE(Hermit::string_utils::is_base64(encoded, encoded.size()));
    EXPECT_FALSE(Hermit::string_utils::is_base64(encoded, encoded.size() + 1));
    EXPECT_FALSE(Hermit::string_utils::is_base64("aGVsbG8!", 4));
    EXPECT_FALSE(Hermit::string_utils::is_base64("AA=A", 4));
    EXPECT_THROW(Hermit::string_utils::base64_decode("!!!!"), std::exception);
    EXPECT_EQ(Hermit::string_utils::json_escape_string("a\"b\n"), "a\\\"b\\n");
    EXPECT_EQ(Hermit::string_utils::unescape_string("a\\n"), "a\n");
}

TEST(png, validates_signature_and_bounds_chunk_lengths)
{
    std::string image{ "\x89PNG\r\n\x1A\n", 8 };
    image.append("\0\0\0\x0D", 4);
    image += "IHDR";
    image.append(13, '\0');
    image.append(4, '\0');

    const std::string metadata{ "parameters\0prompt", 17 };
    image.append("\0\0\0\x11", 4);
    image += "tEXt";
    image += metadata;
    image.append(4, '\0');

    const std::string& with_metadata{ image };
    EXPECT_EQ(Hermit::tEXt::extract_parameters(with_metadata, "parameters"), "prompt");
    EXPECT_THROW(Hermit::tEXt::extract_parameters(with_metadata, "missing"), Hermit::png_exception);

    std::string invalid_signature{ image };
    invalid_signature[7] = '\0';
    EXPECT_THROW(Hermit::tEXt::extract_parameters(invalid_signature, "parameters"), std::exception);

    std::string oversized_chunk{ "\x89PNG\r\n\x1A\n", 8 };
    oversized_chunk.append("\xFF\xFF\xFF\xFFtEXt", 8);
    oversized_chunk.append(4, '\0');
    EXPECT_THROW(Hermit::tEXt::extract_parameters(oversized_chunk, "parameters"), std::exception);
}

TEST(core, converts_all_public_backend_modes)
{
    EXPECT_EQ(Hermit::string_to_llm_mode("completions"), Hermit::llm_mode::completions);
    EXPECT_EQ(Hermit::string_to_llm_mode("chat_completions"), Hermit::llm_mode::chat_completions);
    EXPECT_EQ(Hermit::string_to_sd_mode("txt2img"), Hermit::sd_mode::txt2img);
    EXPECT_EQ(Hermit::string_to_sd_mode("img2img"), Hermit::sd_mode::img2img);
    EXPECT_THROW(Hermit::string_to_llm_mode("invalid"), std::exception);
    EXPECT_THROW(Hermit::string_to_sd_mode("invalid"), std::exception);
}

TEST(core, resolves_backend_request_targets)
{
    Hermit::config cfg;
    cfg.llm.completions_target = "/v1/completions";
    cfg.llm.chat_completions_target = "/v1/chat/completions";
    cfg.sd.txt2img.target = "/sdapi/v1/txt2img";
    cfg.sd.img2img.target = "/sdapi/v1/img2img";

    EXPECT_EQ(Hermit::llm_mode_to_target(Hermit::llm_mode::completions, cfg), "/v1/completions");
    EXPECT_EQ(Hermit::llm_mode_to_target(Hermit::llm_mode::chat_completions, cfg), "/v1/chat/completions");
    EXPECT_EQ(Hermit::sd_mode_to_target(Hermit::sd_mode::txt2img, cfg), "/sdapi/v1/txt2img");
    EXPECT_EQ(Hermit::sd_mode_to_target(Hermit::sd_mode::img2img, cfg), "/sdapi/v1/img2img");
}


