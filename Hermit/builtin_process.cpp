#include "builtin.hpp"
#include "config.hpp"
#include "errors.hpp"
#include "string_utils.hpp"
#include "templates.hpp"

#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/process/v2/environment.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

namespace hermit::builtin
{
    value_type exec(macro_argument_type args)
    {
        return exec(args,
            [](std::string_view executable, const std::vector<std::string>& arguments)
            {
                namespace process = boost::process::v2;
                namespace asio = boost::asio;
                const auto executable_path{ process::environment::find_executable(executable) };
                if (executable_path.empty())
                {
                    hermit::throw_exception(macro_exception{});
                }
                asio::io_context io_context;
                asio::readable_pipe pipe{ io_context };
                int exit_code{};
                {
                    process::process_stdio stdio{ nullptr, pipe, {} };
                    process::process child{ io_context, executable_path, arguments, stdio };
                    exit_code = child.wait();
                }
                std::string output;
                boost::system::error_code error_code;
                asio::read(pipe, asio::dynamic_buffer(output), error_code);
                if (error_code && error_code != asio::error::eof && error_code != asio::error::broken_pipe)
                {
                    hermit::throw_exception(macro_exception{} << error_info::system::error_code{ error_code });
                }
                return std::pair<int, std::string>{ exit_code, output };
            });
    }

    value_type exec(macro_argument_type args, const process_runner& runner)
    {
        builtin::validate_arguments_size(args.arguments, 1);
        if (!args.cfg.allow_process_exec)
        {
            hermit::throw_exception(macro_exception{} << error_info::description{
                "Process execution from the exec macro is disabled; enable --security-allow-process-exec to allow it"
            });
        }
        const std::string_view exe_name{ get_or_throw<std::string>(args.arguments[0]) };
        std::vector<std::string> args_;
        args_.reserve(args.arguments.size());

        for (std::size_t i{ 1 }; i < args.arguments.size(); ++i)
        {
            args_.push_back(get_or_throw<std::string>(args.arguments[i]));
        }

        const std::pair<int, std::string> result{ runner(exe_name, args_) };
        args.ctx.set("exit_code", result.first);
        return string_utils::console_string_to_u8string(result.second);
    }
}
