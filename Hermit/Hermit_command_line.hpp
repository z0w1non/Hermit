#ifndef HERMIT_COMMAND_LINE_HPP
#define HERMIT_COMMAND_LINE_HPP

#include <istream>
#include <string>
#include <string_view>
#include <vector>

namespace boost::program_options
{
    class options_description;
    class variables_map;
}

namespace Hermit
{
    struct config;
    struct context;
    namespace command_line
    {
        void parse_user_defined_variables(const std::vector<std::string>& predefined_macros, context& ctx);
        std::vector<std::string> split_command_line_args(std::string_view args);
        enum class parse_result
        {
            success,
            help,
            program_options_error
        };
        boost::program_options::options_description make_options_description(config& cfg);
        void parse_command_line(const boost::program_options::options_description&, int, char**, boost::program_options::variables_map&);
        void parse_config_stream(const boost::program_options::options_description&, std::istream&, boost::program_options::variables_map&);
        parse_result parse(int argc, char** argv, config& cfg);
        void after_parse(config& cfg);
    }
}

#endif
