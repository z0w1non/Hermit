#ifndef HERMIT_FILESYSTEM_HPP
#define HERMIT_FILESYSTEM_HPP

#include <filesystem>
#include <ios>
#include <string>
#include <string_view>
#include <vector>

namespace Hermit
{
    struct config;
    namespace filesystem
    {
        void create_parent_directories(const std::filesystem::path& path);
        std::string read_file_to_string(const std::filesystem::path& file, std::ios::openmode openmode = {});
        std::string read_binary_file_to_string(std::string_view file, const config& cfg);
        std::string read_text_file_to_string(std::string_view path, const config& cfg, std::string_view extension = ".txt");
        std::string image_path_to_base64_encoded_string(std::string_view image_path, const config& cfg);
        std::vector<std::string> image_paths_to_base64_encoded_strings(const std::vector<std::string>& paths, const config& cfg);
        std::string complement_extension(std::string_view filepath, std::string_view extension);
        std::string complement_codeblock_extension(std::string_view language_identifier);
        std::filesystem::path string_to_path_by_base_path(std::string_view path, std::string_view base);
        std::filesystem::path string_to_path_by_config(std::string_view path, const config& cfg);
        std::string base64_image_to_url(std::string_view base64_image, std::string_view mime_type);
        std::string_view extension_to_mime_type(std::string_view extension);
        void write_file(const config& cfg, const char* data, std::size_t size, std::string_view filepath, std::ios_base::openmode mode = 0);
        void write_file(const config& cfg, std::string_view data, std::string_view filepath, std::ios_base::openmode mode = 0);
        std::string prompt_from_string_or_file_path(std::string_view string, std::string_view file_path, const config& cfg);
    }
}

#endif
