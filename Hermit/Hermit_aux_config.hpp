#ifndef HERMIT_AUX_CONFIG_HPP
#define HERMIT_AUX_CONFIG_HPP

#include <string>

namespace Hermit
{
    struct png_parameters
    {
        std::string png_file;
    };

    struct process_parameters
    {
        std::string server_executable_file;
        std::string server_arguments;
        std::string server_host;
        std::string server_port;
        int server_max_retries{};
        int server_wait_ms{};
    };
}

#endif
