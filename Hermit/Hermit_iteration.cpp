#include <chrono>
#include <iostream>
#include <random>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/nowide/args.hpp>
#include <boost/nowide/iostream.hpp>

#include "Hermit_builtin.hpp"
#include "Hermit_command_line.hpp"
#include "Hermit_config.hpp"
#include "Hermit_filesystem.hpp"
#include "Hermit_generation.hpp"
#include "Hermit_llm.hpp"
#include "Hermit_log.hpp"
#include "Hermit_lru.hpp"
#include "Hermit_png.hpp"
#include "Hermit_public_http_adapter.hpp"
#include "Hermit_runtime.hpp"

namespace Hermit
{
    static bool is_llm_backend(command_mode mode)
    {
        return mode == command_mode::tg || mode == command_mode::kc || mode == command_mode::lmstudio
            || mode == command_mode::llama_cpp || mode == command_mode::vllm || mode == command_mode::ollama;
    }

    static void generate_and_output_impl(const config& cfg, const filesystem::file_writer* writer,
        const tcp::request_executor* tcp_executor, const public_api::http_executor* http_executor,
        const filesystem::file_reader* reader)
    {
        if (is_llm_backend(cfg.command_mode))
        {
            const std::string prompt{ filesystem::prompt_from_string_or_file_path(cfg.llm.prompt, cfg.llm.prompt_file, cfg) };
            if (cfg.llm.mode == llm_mode::completions && writer)
            {
                if (http_executor)
                {
                    llm::completions_and_write_file(cfg, prompt, cfg.ctx, *writer, *http_executor);
                }
                else if (tcp_executor)
                {
                    llm::completions_and_write_file(cfg, prompt, cfg.ctx, *writer, *tcp_executor);
                }
                else
                {
                    llm::completions_and_write_file(cfg, prompt, cfg.ctx, *writer);
                }
            }
            else if (cfg.llm.mode == llm_mode::completions)
            {
                llm::completions_and_write_file(cfg, prompt, cfg.ctx);
            }
            else if (cfg.llm.mode == llm_mode::chat_completions && writer)
            {
                if (http_executor)
                {
                    llm::chat_completions_and_write_file(cfg, prompt, cfg.ctx, *writer, *http_executor, *reader);
                }
                else if (tcp_executor)
                {
                    llm::chat_completions_and_write_file(cfg, prompt, cfg.ctx, *writer, *tcp_executor, *reader);
                }
                else
                {
                    llm::chat_completions_and_write_file(cfg, prompt, cfg.ctx, *writer);
                }
            }
            else if (cfg.llm.mode == llm_mode::chat_completions)
            {
                llm::chat_completions_and_write_file(cfg, prompt, cfg.ctx);
            }
        }
        else if (cfg.command_mode == command_mode::sd)
        {
            const std::string prompt_string{ expand_macro(filesystem::prompt_from_string_or_file_path(cfg.sd.common.prompt, cfg.sd.prompt_file, cfg), cfg, cfg.ctx) };
            const std::string negative_prompt_string{ expand_macro(filesystem::prompt_from_string_or_file_path(cfg.sd.common.negative_prompt, cfg.sd.negative_prompt_file, cfg), cfg, cfg.ctx) };
            const std::string image{ http_executor
                ? sd::send_request(cfg, prompt_string, negative_prompt_string, *http_executor, *reader)
                : tcp_executor ? sd::send_request(cfg, prompt_string, negative_prompt_string, *tcp_executor, *reader)
                : sd::send_request(cfg, prompt_string, negative_prompt_string) };
            if (writer)
            {
                filesystem::write_file(cfg, image, cfg.sd.output_file, std::ios::binary, *writer);
            }
            else
            {
                filesystem::write_file(cfg, image, cfg.sd.output_file, std::ios::binary);
            }
        }
        else if (cfg.command_mode == command_mode::sb)
        {
            const std::string text{ expand_macro(filesystem::prompt_from_string_or_file_path(cfg.sb.text, cfg.sb.text_file, cfg), cfg, cfg.ctx) };
            const std::string voice{ http_executor
                ? sb::send_request(cfg, text, *http_executor)
                : tcp_executor ? sb::send_request(cfg, text, *tcp_executor)
                : sb::send_request(cfg, text) };
            if (writer)
            {
                filesystem::write_file(cfg, voice, cfg.sb.output_file, std::ios::binary, *writer);
            }
            else
            {
                filesystem::write_file(cfg, voice, cfg.sb.output_file, std::ios::binary);
            }
        }
        else if (cfg.command_mode == command_mode::cu)
        {
            const std::string prompt{ expand_macro(filesystem::prompt_from_string_or_file_path(cfg.cu.prompt, cfg.cu.prompt_file, cfg), cfg, cfg.ctx) };
            if (writer)
            {
                const public_api::binary_storage storage{ [&cfg, writer](std::string_view path, std::string_view data)
                {
                    filesystem::write_file(cfg, data, path, std::ios::binary, *writer);
                } };
                if (http_executor)
                {
                    cu::send_request(cfg, prompt, *http_executor, storage);
                }
                else if (tcp_executor)
                {
                    cu::send_request(cfg, prompt, *tcp_executor, storage);
                }
                else
                {
                    cu::send_request(cfg, prompt, storage);
                }
            }
            else
            {
                cu::send_request(cfg, prompt);
            }
        }
    }

    void generate_and_output(const config& cfg)
    {
        generate_and_output_impl(cfg, nullptr, nullptr, nullptr, nullptr);
    }

    void generate_and_output(const config& cfg, const filesystem::file_writer& writer)
    {
        generate_and_output_impl(cfg, &writer, nullptr, nullptr, nullptr);
    }

    void generate_and_output(const config& cfg, const filesystem::file_writer& writer,
        const public_api::http_executor& executor, const filesystem::file_reader& reader)
    {
        generate_and_output_impl(cfg, &writer, nullptr, &executor, &reader);
    }

    void generate_and_output(const config& cfg, const filesystem::file_writer& writer,
        const tcp::request_executor& executor, const filesystem::file_reader& reader)
    {
        generate_and_output_impl(cfg, &writer, &executor, nullptr, &reader);
    }

    void set_seed(config& cfg)
    {
        if (cfg.seed == -1)
        {
            static thread_local std::mt19937 generator{ std::random_device{}() };
            cfg.tg.common.seed
                = cfg.kc.common.sampler_seed
                = cfg.sd.common.seed
                = std::uniform_int_distribution<std::uint32_t>{}(generator);
        }
        else
        {
            cfg.tg.common.seed
                = cfg.kc.common.sampler_seed
                = cfg.sd.common.seed
                = cfg.seed;
        }
    }

    void create_process(const config& cfg)
    {
        create_process(cfg, [](std::string_view executable_file, const std::vector<std::string>& arguments)
        {
            create_process_async(executable_file, arguments);
        });
    }

    void create_process(const config& cfg, const process_launcher& launcher)
    {
        if (!cfg.proc.server_executable_file.empty())
        {
            const std::vector<std::string> arguments{ command_line::split_command_line_args(cfg.proc.server_arguments) };
            create_process_async(cfg.proc.server_executable_file, arguments, launcher);
            if (!wait_for_port(cfg.proc.server_host, cfg.proc.server_port, cfg.proc.server_max_retries, cfg.proc.server_wait_ms))
            {
                HERMIT_LOG(warning) << "Connection timed out waiting for server response.";
            }
        }
    }

    void terminate_process(const config& cfg)
    {
        if (!cfg.proc.server_executable_file.empty())
        {
            if (terminate_process(cfg, [](const std::filesystem::path& executable_file_path)
                {
                    return terminate_process_by_path(executable_file_path);
                }) == 0)
            {
                HERMIT_LOG(warning) << "Failed to terminate process by executable file path (" << cfg.proc.server_executable_file << ").";
            }
        }
    }

    std::size_t terminate_process(const config& cfg, const process_terminator& terminator)
    {
        if (cfg.proc.server_executable_file.empty()) return 0;
        return terminator(cfg.proc.server_executable_file);
    }

    void iterate(config& cfg)
    {
        if (is_llm_backend(cfg.command_mode))
        {
            cfg.lru_cache.from_file(cfg);
        }

        for (int iteration_count{}; cfg.number_iterations == -1 || iteration_count < cfg.number_iterations; iteration_count += 1)
        {
            set_seed(cfg);

            set_dynamic_builtin_variables(cfg);
            cfg.ctx.set("N", std::to_string(iteration_count + 1));

            for (std::size_t phase_index{}; phase_index < cfg.phases.size(); ++phase_index)
            {
                set_phase_variables(cfg.phases, phase_index, cfg.ctx);
                generate_and_output(cfg);
            }

            if (is_llm_backend(cfg.command_mode))
            {
                cfg.lru_cache.to_file(cfg);
            }
        }
    }

    int exception_safe_main(int argc, char** argv) noexcept
    {
        try
        {
            config cfg;

            const command_line::parse_result result{ command_line::parse(argc, argv, cfg) };
            if (result == command_line::parse_result::help)
            {
                return 0;
            }
            else if (result == command_line::parse_result::program_options_error)
            {
                return -1;
            }
            command_line::after_parse(cfg);

            if (cfg.command_mode == command_mode::list_models || cfg.command_mode == command_mode::list_running_models
                || cfg.command_mode == command_mode::get_ollama_version || cfg.command_mode == command_mode::manage_comfyui_api
                || cfg.command_mode == command_mode::show_model || cfg.command_mode == command_mode::manage_ollama_model
                || cfg.command_mode == command_mode::create_embeddings
                || cfg.command_mode == command_mode::tokenize || cfg.command_mode == command_mode::detokenize)
            {
                if (cfg.command_mode == command_mode::manage_comfyui_api && cfg.cu.api_action == "events")
                {
                    cu::receive_events(cfg);
                    return 0;
                }
                tcp transport;
                const public_api::http_executor executor{ public_api::make_http_executor(
                    [&transport, &cfg](std::string_view host, std::string_view port, const tcp::request_type& request)
                    {
                        transport.expires_after(std::chrono::seconds{ cfg.timeout_connect }).connect(host, port);
                        return transport.expires_after(std::chrono::seconds{ cfg.timeout_request }).request(request);
                    }) };
                const std::string response{ cfg.command_mode == command_mode::list_models ? llm::list_models(cfg, executor)
                    : cfg.command_mode == command_mode::list_running_models ? llm::list_running_models(cfg, executor)
                    : cfg.command_mode == command_mode::get_ollama_version ? llm::get_ollama_version(cfg, executor)
                    : cfg.command_mode == command_mode::show_model ? llm::show_model(cfg, executor)
                    : cfg.command_mode == command_mode::manage_ollama_model ? llm::manage_ollama_model(cfg, executor)
                    : cfg.command_mode == command_mode::manage_comfyui_api ? cu::manage_api(cfg, executor)
                    : cfg.command_mode == command_mode::create_embeddings ? llm::create_embeddings(cfg, executor)
                    : cfg.command_mode == command_mode::tokenize ? llm::tokenize(cfg, executor) : llm::detokenize(cfg, executor) };
                if (cfg.command_mode == command_mode::manage_comfyui_api && cu::api_returns_binary(cfg.cu))
                {
                    boost::nowide::cout.write(response.data(), static_cast<std::streamsize>(response.size()));
                    boost::nowide::cout.flush();
                }
                else
                {
                    boost::nowide::cout << response << '\n' << std::flush;
                }
                return 0;
            }

            if (cfg.command_mode == command_mode::extract_png_parameters)
            {
                const std::string parameters{ tEXt::extract_parameters(filesystem::read_binary_file_to_string(cfg.png.png_file, cfg)) };
                boost::nowide::cout << parameters << std::flush;
                return 0;
            }
            else if (cfg.command_mode == command_mode::create_process)
            {
                create_process(cfg);
                return 0;
            }
            else if (cfg.command_mode == command_mode::terminate_process)
            {
                terminate_process(cfg);
                return 0;
            }

            if (is_llm_backend(cfg.command_mode))
            {
                llm::read_paragraphs_file(cfg);
            }

            set_static_builtin_variables(cfg);

            if (cfg.command_mode == command_mode::cu)
            {
                cu::upload_images(cfg, cfg.ctx);
            }

            iterate(cfg);
        }
        catch (const boost::exception& exception)
        {
            HERMIT_LOG(error) << boost::diagnostic_information(exception);
            return -1;
        }
        catch (const std::exception& exception)
        {
            HERMIT_LOG(error) << exception.what();
            return -1;
        }
        catch (...)
        {
            HERMIT_LOG(error) << "Unknown exception caught.";
            return -1;
        }

        return 0;
    }

    int nowide_main(int argc, char** argv)
    {
        try
        {
            boost::nowide::args _(argc, argv);
            return exception_safe_main(argc, argv);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Initialization error: " << e.what() << '\n';
        }

        return -1;
    }
} // namespace Hermit


