#include "Hermit_runtime.hpp"
#include "Hermit_config.hpp"
#include "Hermit_filesystem.hpp"
#include "Hermit_builtin.hpp"
#include "Hermit_llm.hpp"
#include "Hermit_generation.hpp"
#include "Hermit_command_line.hpp"
#include "Hermit_png.hpp"
#include "Hermit_log.hpp"
#include "Hermit_lru.hpp"

#include <boost/exception/diagnostic_information.hpp>
#include <boost/nowide/args.hpp>
#include <boost/nowide/iostream.hpp>

#include <random>
#include <iostream>

namespace Hermit
{
    void generate_and_output(const config& cfg)
    {
        if (cfg.command_mode == command_mode::tg || cfg.command_mode == command_mode::kc)
        {
            const std::string prompt{ filesystem::prompt_from_string_or_file_path(cfg.llm.prompt, cfg.llm.prompt_file, cfg) };
            if (cfg.llm.mode == llm_mode::completions)
            {
                llm::completions_and_write_file(cfg, prompt, cfg.ctx);
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
            const std::string image{ sd::send_request(cfg, prompt_string, negative_prompt_string) };
            filesystem::write_file(cfg, image, cfg.sd.output_file, std::ios::binary);
        }
        else if (cfg.command_mode == command_mode::sb)
        {
            const std::string text{ expand_macro(filesystem::prompt_from_string_or_file_path(cfg.sb.text, cfg.sb.text_file, cfg), cfg, cfg.ctx) };
            const std::string voice{ sb::send_request(cfg, text) };
            filesystem::write_file(cfg, voice, cfg.sb.output_file, std::ios::binary);
        }
        else if (cfg.command_mode == command_mode::cu)
        {
            const std::string prompt{ expand_macro(filesystem::prompt_from_string_or_file_path(cfg.cu.prompt, cfg.cu.prompt_file, cfg), cfg, cfg.ctx) };
            cu::send_request(cfg, prompt);
        }
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
        if (!cfg.proc.server_executable_file.empty())
        {
            const std::vector<std::string> arguments{ command_line::split_command_line_args(cfg.proc.server_arguments) };
            create_process_async(cfg.proc.server_executable_file, arguments);
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
            if (terminate_process_by_path(cfg.proc.server_executable_file) == 0)
            {
                HERMIT_LOG(warning) << "Failed to terminate process by executable file path (" << cfg.proc.server_executable_file << ").";
            }
        }
    }

    void iterate(config& cfg)
    {
        if (cfg.command_mode == command_mode::tg || cfg.command_mode == command_mode::kc)
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

            if (cfg.command_mode == command_mode::tg || cfg.command_mode == command_mode::kc)
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

            if (cfg.command_mode == command_mode::tg || cfg.command_mode == command_mode::kc)
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


