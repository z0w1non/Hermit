#ifndef HERMIT_RUNTIME_HPP
#define HERMIT_RUNTIME_HPP

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "filesystem.hpp"
#include "public_http_adapter.hpp"
#include "tcp.hpp"

namespace hermit
{
    void set_phase_variables(const std::vector<std::string>& phases, std::size_t phase_index, context& ctx);
    void set_static_builtin_variables(config& cfg);
    void set_dynamic_builtin_variables(config& cfg);
    void set_paragraphs_to_phases(const std::vector<Item>& paragraphs, std::vector<std::string>& phases);
    std::string sanitize_as_filename(std::string_view name);
    std::string_view language_identifier_to_extension(std::string_view language_identifier);
    bool wait_for_port(const std::string& host, const std::string& port, unsigned int max_retries, unsigned int wait_ms);
    using port_connector = std::function<bool()>;
    using port_connector_factory = std::function<port_connector(std::string_view, std::string_view)>;
    using wait_callback = std::function<void(unsigned int, unsigned int)>;
    bool wait_for_port(unsigned int max_retries, unsigned int wait_ms, const port_connector& connector, const wait_callback& wait);
    bool wait_for_port(std::string_view host, std::string_view port, unsigned int max_retries, unsigned int wait_ms, const port_connector_factory& connector_factory, const wait_callback& wait);
    using process_launcher = std::function<void(std::string_view, const std::vector<std::string>&)>;
    using process_terminator = std::function<std::size_t(const std::filesystem::path&)>;
    void create_process_async(std::string_view excutable_file, const std::vector<std::string>& arguments);
    void create_process_async(std::string_view executable_file, const std::vector<std::string>& arguments, const process_launcher& launcher);
    std::size_t terminate_process_by_path(const std::filesystem::path& executable_file_path);
    void generate_and_output(const config& cfg);
    void generate_and_output(const config& cfg, const filesystem::file_writer& writer);
    void generate_and_output(const config& cfg, const filesystem::file_writer& writer, const public_api::http_executor& executor, const filesystem::file_reader& reader);
    void generate_and_output(const config& cfg, const filesystem::file_writer& writer, const tcp::request_executor& executor, const filesystem::file_reader& reader);
    void set_seed(config& cfg);
    void create_process(const config& cfg);
    void create_process(const config& cfg, const process_launcher& launcher);
    void terminate_process(const config& cfg);
    std::size_t terminate_process(const config& cfg, const process_terminator& terminator);
    void iterate(config& cfg);
    int exception_safe_main(int argc, char** argv) noexcept;
    int nowide_main(int argc, char** argv);
}

#endif
