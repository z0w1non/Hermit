#include "Hermit_runtime.hpp"
#include "Hermit_app_types.hpp"
#include "Hermit_value.hpp"
#include "Hermit_log.hpp"
#include "Hermit_command_line.hpp"
#include "Hermit_builtin.hpp"
#include <algorithm>
#include <chrono>
#include <regex>
#include <thread>
#include <boost/algorithm/string.hpp>
#include <boost/predef.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/scope/scope_exit.hpp>
#include <boost/scope/defer.hpp>
#include <boost/process/v2/execute.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>
#include "Hermit_platform.hpp"
#if BOOST_OS_WINDOWS
#include <boost/process/v2/windows/creation_flags.hpp>
#endif

#if BOOST_OS_WINDOWS
#include <io.h>
#else
#include <unistd.h>
#endif

namespace Hermit
{
    void set_phase_variables(const std::vector<std::string>& phases, std::size_t phase_index, context& ctx)
    {
        if (phase_index >= phases.size())
        {
            Hermit::throw_exception(logic_error{});
        }

        if (phase_index > 0)
        {
            ctx.set("prev_phase", phases[phase_index - 1]);
        }

        ctx.set("phase", phases[phase_index]);

        if (phase_index + 1 < phases.size())
        {
            ctx.set("next_phase", phases[phase_index + 1]);
        }
    }

    void set_static_builtin_variables(config& cfg)
    {
        cfg.ctx.set("stdin", builtin::stdin_(cfg));
    }

    void set_dynamic_builtin_variables(config& cfg)
    {
        cfg.ctx.set("date", builtin::date());
        cfg.ctx.set("time", builtin::time());
        cfg.ctx.set("datetime", builtin::datetime());
    }

    void set_paragraphs_to_phases(const std::vector<item>& paragraphs, std::vector<std::string>& phases)
    {
        for (const item& paragraph : paragraphs)
        {
            std::string temp{ paragraph.head };
            for (const std::string& description : paragraph.descriptions)
            {
                temp += "\n";
                temp += description;
            }
            phases.push_back(temp);
        }
    }

    std::string sanitize_as_filename(std::string_view name)
    {
        const std::regex illegal_chars{ R"([:*?"<>|#])" };
        std::string sanitized{ name.begin(), name.end() };
        std::replace(sanitized.begin(), sanitized.end(), ' ', '_');
        sanitized = std::regex_replace(sanitized, illegal_chars, "");
        boost::algorithm::trim(sanitized);
        return sanitized;
    }

    std::string_view language_identifier_to_extension(std::string_view language_identifier)
    {
        using map_type = string_view_unordered_map<std::string_view>;
        static const map_type map
        {
            { "assembly", ".asm" },
            { "bash", ".sh" },
            { "bat", ".bat" },
            { "batch", ".bat" },
            { "c", ".c" },
            { "clojure", ".clj" },
            { "cmake", "CMakeLists.txt" },
            { "cmd", ".cmd" },
            { "cpp", ".cpp" },
            { "csharp", ".cs" },
            { "css", ".css" },
            { "csv", ".csv" },
            { "dart", ".dart" },
            { "diff", ".diff" },
            { "dockerfile", "Dockerfile" },
            { "elixir", ".ex" },
            { "erlang", ".erl" },
            { "fish", ".fish" },
            { "fortran", ".f90" },
            { "fsharp", ".fs" },
            { "go", ".go" },
            { "graphql", ".graphql" },
            { "haskell", ".hs" },
            { "html", ".html" },
            { "ini", ".ini" },
            { "java", ".java" },
            { "javascript", ".js" },
            { "json", ".json" },
            { "jsonc", ".jsonc" },
            { "jsx", ".jsx" },
            { "kotlin", ".kt" },
            { "latex", ".tex" },
            { "less", ".less" },
            { "lua", ".lua" },
            { "makefile", "Makefile" },
            { "markdown", ".md" },
            { "perl", ".pl" },
            { "php", ".php" },
            { "powershell", ".ps1" },
            { "python", ".py" },
            { "r", ".r" },
            { "ruby", ".rb" },
            { "rust", ".rs" },
            { "sass", ".sass" },
            { "scala", ".scala" },
            { "scss", ".scss" },
            { "sh", ".sh" },
            { "sql", ".sql" },
            { "swift", ".swift" },
            { "tex", ".tex" },
            { "toml", ".toml" },
            { "tsv", ".tsv" },
            { "tsx", ".tsx" },
            { "typescript", ".ts" },
            { "vbs", ".vbs" },
            { "xml", ".xml" },
            { "yaml", ".yaml" },
            { "zsh", ".zsh" },
            { "plaintext", ".txt" }
        };
        if (const map_type::const_iterator iter{ map.find(language_identifier) }; iter != map.end())
        {
            return iter->second;
        }
        return std::string_view{};
    }

    bool wait_for_port(const std::string& host, const std::string& port, unsigned int max_retries, unsigned int wait_ms)
    {
        boost::system::error_code error_code;

        boost::asio::io_context ctx;
        boost::asio::ip::tcp::resolver resolver{ ctx };
        const boost::asio::ip::tcp::resolver::results_type results{ resolver.resolve(host, port, error_code) };
        if (error_code || results.empty())
        {
            Hermit::throw_exception(dns_resolve_exception{} << error_info::asio::error_code{ error_code });
        }

        boost::asio::ip::tcp::endpoint endpoint{ *results.begin() };
        for (unsigned int retries{}; retries < max_retries; ++retries)
        {
            boost::asio::ip::tcp::socket socket{ ctx };
            socket.connect(endpoint, error_code);
            if (!error_code)
            {
                socket.close();
                return true;
            }

            HERMIT_LOG(trace)
                << "[Waiting " << (retries + 1) << "/" << max_retries << "] "
                << host << ":" << port << " (" << error_code.message() << ")";

            std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
        }

        return false;
    }

    void create_process_async(std::string_view excutable_file, const std::vector<std::string>& arguments)
    {
        namespace process = boost::process::v2;
        boost::asio::io_context ctx;
        //auto exe = process::environment::find_executable(boost::filesystem::path{ excutable });
        //if (exe.empty())
        //{
        //    HERMIT_LOG(warning) << "exe not found";
        //    return;
        //}
        process::process proc{ ctx, excutable_file, arguments, process::windows::create_new_console };
        proc.detach();
    }

    std::size_t terminate_process_by_path(const std::filesystem::path& executable_file_path)
    {
        std::size_t terminated_count{};

#if BOOST_OS_WINDOWS
        const HANDLE snapshot{ CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            return 0;
        }
        BOOST_SCOPE_DEFER[snapshot]{ CloseHandle(snapshot); };

        const DWORD current_pid{ GetCurrentProcessId() };

        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(PROCESSENTRY32W);

        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                if (entry.th32ProcessID == current_pid)
                {
                    continue;
                }

                const HANDLE process{ OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, entry.th32ProcessID) };
                if (process != nullptr)
                {
                    BOOST_SCOPE_DEFER[process]{ CloseHandle(process); };
                    wchar_t current_path_buffer[MAX_PATH]{};
                    DWORD size{ MAX_PATH };

                    if (QueryFullProcessImageNameW(process, 0, current_path_buffer, &size))
                    {
                        const std::filesystem::path current_path{ current_path_buffer };
                        const std::wstring current_target
                        {
                            executable_file_path.has_parent_path()
                            ? current_path.wstring()
                            : current_path.filename().wstring()
                        };

                        if (boost::algorithm::iequals(current_target, executable_file_path.wstring()))
                        {
                            if (TerminateProcess(process, 1))
                            {
                                terminated_count += 1;
                            }
                        }
                    }
                }

            } while (Process32NextW(snapshot, &entry));
        }
#endif

        return terminated_count;
    }

} // namespace Hermit
