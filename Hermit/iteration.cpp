#include <chrono>
#include <iostream>
#include <random>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/nowide/args.hpp>
#include <boost/nowide/iostream.hpp>

#include "builtin.hpp"
#include "command_line.hpp"
#include "config.hpp"
#include "filesystem.hpp"
#include "generation.hpp"
#include "llm.hpp"
#include "log.hpp"
#include "lru.hpp"
#include "png.hpp"
#include "public_http_adapter.hpp"
#include "runtime.hpp"

namespace hermit
{
    static bool is_llm_backend(CommandMode mode)
    {
        return mode == CommandMode::kTg || mode == CommandMode::kKc || mode == CommandMode::kLmStudio
            || mode == CommandMode::kLlamaCpp || mode == CommandMode::kVllm || mode == CommandMode::kOllama;
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
        else if (cfg.command_mode == CommandMode::kSd)
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
        else if (cfg.command_mode == CommandMode::kSb)
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
        else if (cfg.command_mode == CommandMode::kCu)
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

            if (cfg.command_mode == CommandMode::kListModels || cfg.command_mode == CommandMode::kListRunningModels
                || cfg.command_mode == CommandMode::kGetOllamaVersion || cfg.command_mode == CommandMode::kManageComfyuiApi
                || cfg.command_mode == CommandMode::kManageSdApi || cfg.command_mode == CommandMode::kRequestSdApi
                || cfg.command_mode == CommandMode::kShowModel || cfg.command_mode == CommandMode::kManageOllamaModel
                || cfg.command_mode == CommandMode::kManageLmStudioModel || cfg.command_mode == CommandMode::kCreateLmStudioChat
                || cfg.command_mode == CommandMode::kCreateLmStudioResponse
                || cfg.command_mode == CommandMode::kCreateLmStudioDecision
                || cfg.command_mode == CommandMode::kCreateLmStudioMessages
                || cfg.command_mode == CommandMode::kCreateLmStudioSystemone
                || cfg.command_mode == CommandMode::kCreateVllmBatchChat
                || cfg.command_mode == CommandMode::kCreateTgModerations
                || cfg.command_mode == CommandMode::kCheckTgHealth
                || cfg.command_mode == CommandMode::kGetTgModelInfo
                || cfg.command_mode == CommandMode::kGetKcInfo
                || cfg.command_mode == CommandMode::kManageTgModel
                || cfg.command_mode == CommandMode::kStopTgGeneration
                || cfg.command_mode == CommandMode::kGetTgLogits
                || cfg.command_mode == CommandMode::kCreateTgChatPrompt
                || cfg.command_mode == CommandMode::kListTgLoras
                || cfg.command_mode == CommandMode::kManageTgLoras
                || cfg.command_mode == CommandMode::kRequestLlmApi || cfg.command_mode == CommandMode::kStreamLlmApi
                || cfg.command_mode == CommandMode::kCreateEmbeddings
                || cfg.command_mode == CommandMode::kTokenize || cfg.command_mode == CommandMode::kDetokenize
                || cfg.command_mode == CommandMode::kConvertKcJsonToGrammar
                || cfg.command_mode == CommandMode::kStreamKcGeneration
                || cfg.command_mode == CommandMode::kCheckKcGeneration || cfg.command_mode == CommandMode::kAbortKcGeneration
                || cfg.command_mode == CommandMode::kGetKcLogprobs)
            {
                if (cfg.command_mode == CommandMode::kManageComfyuiApi && cfg.cu.api_action == "events")
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
                const public_api::http_stream_executor stream_executor{ public_api::make_http_stream_executor(
                    [&transport, &cfg](std::string_view host, std::string_view port, const tcp::request_type& request,
                        const tcp::stream_handler& handler)
                    {
                        transport.expires_after(std::chrono::seconds{ cfg.timeout_connect }).connect(host, port);
                        return transport.expires_after(std::chrono::seconds{ cfg.timeout_request }).stream_request(request, handler);
                    }) };
                if (cfg.command_mode == CommandMode::kStreamKcGeneration)
                {
                    const public_api::http_stream_handler writer{ [](std::string_view chunk)
                    {
                        boost::nowide::cout.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
                        boost::nowide::cout.flush();
                        return true;
                    } };
                    llm::stream_kc_generation(cfg, stream_executor, writer);
                    return 0;
                }
                if (cfg.command_mode == CommandMode::kStreamLlmApi)
                {
                    const public_api::http_stream_handler writer{ [](std::string_view chunk)
                    {
                        boost::nowide::cout.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
                        boost::nowide::cout.flush();
                        return true;
                    } };
                    llm::StreamApiRequest(cfg, stream_executor, writer);
                    return 0;
                }
                if (cfg.command_mode == CommandMode::kRequestSdApi)
                {
                    const public_api::http_response_data response{ sd::make_api_request(cfg, executor) };
                    if (response.is_binary)
                    {
                        boost::nowide::cout.write(response.body.data(), static_cast<std::streamsize>(response.body.size()));
                        boost::nowide::cout.flush();
                    }
                    else
                    {
                        boost::nowide::cout << response.body << '\n' << std::flush;
                    }
                    return 0;
                }
                if (cfg.command_mode == CommandMode::kRequestLlmApi)
                {
                    const public_api::http_response_data response{ llm::make_api_request(cfg, executor) };
                    if (response.is_binary)
                    {
                        boost::nowide::cout.write(response.body.data(), static_cast<std::streamsize>(response.body.size()));
                        boost::nowide::cout.flush();
                    }
                    else
                    {
                        boost::nowide::cout << response.body << '\n' << std::flush;
                    }
                    return 0;
                }
                if (cfg.command_mode == CommandMode::kCreateLmStudioResponse || cfg.command_mode == CommandMode::kCreateLmStudioChat
                    || cfg.command_mode == CommandMode::kCreateLmStudioMessages)
                {
                    const public_api::http_stream_handler writer{ [](std::string_view chunk)
                    {
                        boost::nowide::cout.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
                        boost::nowide::cout.flush();
                        return true;
                    } };
                    const std::string response{ cfg.command_mode == CommandMode::kCreateLmStudioChat
                        ? llm::create_lmstudio_chat(cfg, executor, stream_executor, writer)
                        : cfg.command_mode == CommandMode::kCreateLmStudioMessages
                            ? llm::create_lmstudio_messages(cfg, executor, stream_executor, writer)
                            : llm::create_lmstudio_response(cfg, executor, stream_executor, writer) };
                    if (!response.empty()) boost::nowide::cout << response << '\n' << std::flush;
                    return 0;
                }
                const std::string response{ cfg.command_mode == CommandMode::kListTgOpenAiModels ? llm::list_tg_openai_models(cfg, executor)
                    : cfg.command_mode == CommandMode::kListModels ? llm::list_models(cfg, executor)
                    : cfg.command_mode == CommandMode::kListRunningModels ? llm::list_running_models(cfg, executor)
                    : cfg.command_mode == CommandMode::kGetOllamaVersion ? llm::get_ollama_version(cfg, executor)
                    : cfg.command_mode == CommandMode::kShowModel ? llm::show_model(cfg, executor)
                    : cfg.command_mode == CommandMode::kManageOllamaModel ? llm::manage_ollama_model(cfg, executor)
                    : cfg.command_mode == CommandMode::kManageLmStudioModel ? llm::manage_lmstudio_model(cfg, executor)
                    : cfg.command_mode == CommandMode::kCreateLmStudioChat ? llm::create_lmstudio_chat(cfg, executor)
                    : cfg.command_mode == CommandMode::kCreateLmStudioResponse ? llm::create_lmstudio_response(cfg, executor)
                    : cfg.command_mode == CommandMode::kCreateLmStudioDecision ? llm::create_lmstudio_decision(cfg, executor)
                    : cfg.command_mode == CommandMode::kCreateLmStudioMessages ? llm::create_lmstudio_messages(cfg, executor)
                    : cfg.command_mode == CommandMode::kCreateLmStudioSystemone ? llm::create_lmstudio_systemone(cfg, executor)
                    : cfg.command_mode == CommandMode::kCreateVllmBatchChat ? llm::create_vllm_batch_chat(cfg, executor)
                    : cfg.command_mode == CommandMode::kCreateTgModerations ? llm::create_tg_moderations(cfg, executor)
                    : cfg.command_mode == CommandMode::kCheckTgHealth ? llm::check_tg_health(cfg, executor)
                    : cfg.command_mode == CommandMode::kGetTgModelInfo ? llm::get_tg_model_info(cfg, executor)
                    : cfg.command_mode == CommandMode::kGetKcInfo ? llm::get_kc_info(cfg, executor)
                    : cfg.command_mode == CommandMode::kManageTgModel ? llm::manage_tg_model(cfg, executor)
                    : cfg.command_mode == CommandMode::kStopTgGeneration ? llm::stop_tg_generation(cfg, executor)
                    : cfg.command_mode == CommandMode::kGetTgLogits ? llm::get_tg_logits(cfg, executor)
                    : cfg.command_mode == CommandMode::kCreateTgChatPrompt ? llm::create_tg_chat_prompt(cfg, executor)
                    : cfg.command_mode == CommandMode::kListTgLoras ? llm::list_tg_loras(cfg, executor)
                    : cfg.command_mode == CommandMode::kManageTgLoras ? llm::manage_tg_loras(cfg, executor)
                    : cfg.command_mode == CommandMode::kManageComfyuiApi ? cu::manage_api(cfg, executor)
                    : cfg.command_mode == CommandMode::kManageSdApi ? sd::manage_api(cfg, executor)
                    : cfg.command_mode == CommandMode::kCreateEmbeddings ? llm::create_embeddings(cfg, executor)
                    : cfg.command_mode == CommandMode::kTokenize ? (cfg.llm.backend_name == "tg"
                        ? llm::encode_tg_tokens(cfg, executor) : llm::tokenize(cfg, executor))
                    : cfg.command_mode == CommandMode::kDetokenize ? (cfg.llm.backend_name == "tg"
                        ? llm::decode_tg_tokens(cfg, executor) : llm::detokenize(cfg, executor))
                    : cfg.command_mode == CommandMode::kConvertKcJsonToGrammar ? llm::convert_kc_json_to_grammar(cfg, executor)
                    : llm::manage_kc_generation(cfg, executor) };
                if ((cfg.command_mode == CommandMode::kManageComfyuiApi && cu::api_returns_binary(cfg.cu))
                    || (cfg.command_mode == CommandMode::kManageSdApi && cfg.sd.api_action == "sysinfo-download"))
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

            if (cfg.command_mode == CommandMode::kExtractPngParameters)
            {
                const std::string parameters{ tEXt::extract_parameters(filesystem::read_binary_file_to_string(cfg.png.png_file, cfg)) };
                boost::nowide::cout << parameters << std::flush;
                return 0;
            }
            else if (cfg.command_mode == CommandMode::kCreateProcess)
            {
                create_process(cfg);
                return 0;
            }
            else if (cfg.command_mode == CommandMode::kTerminateProcess)
            {
                terminate_process(cfg);
                return 0;
            }

            if (is_llm_backend(cfg.command_mode))
            {
                llm::read_paragraphs_file(cfg);
            }

            set_static_builtin_variables(cfg);

            if (cfg.command_mode == CommandMode::kCu)
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


