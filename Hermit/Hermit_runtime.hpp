#ifndef HERMIT_RUNTIME_HPP
#define HERMIT_RUNTIME_HPP

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Hermit_config.hpp"

namespace Hermit
{
    void set_phase_variables(const std::vector<std::string>& phases, std::size_t phase_index, context& ctx);
    void set_static_builtin_variables(config& cfg);
    void set_dynamic_builtin_variables(config& cfg);
    void set_paragraphs_to_phases(const std::vector<item>& paragraphs, std::vector<std::string>& phases);
    std::string sanitize_as_filename(std::string_view name);
    std::string_view language_identifier_to_extension(std::string_view language_identifier);
    bool wait_for_port(const std::string& host, const std::string& port, unsigned int max_retries, unsigned int wait_ms);
    void create_process_async(std::string_view excutable_file, const std::vector<std::string>& arguments);
    std::size_t terminate_process_by_path(const std::filesystem::path& executable_file_path);
    void generate_and_output(const config& cfg);
    void set_seed(config& cfg);
    void create_process(const config& cfg);
    void terminate_process(const config& cfg);
    void iterate(config& cfg);
    int exception_safe_main(int argc, char** argv) noexcept;
    int nowide_main(int argc, char** argv);
}

#endif
