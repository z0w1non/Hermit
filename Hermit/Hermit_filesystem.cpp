#include "Hermit_filesystem.hpp"
#include "Hermit_config.hpp"
#include "Hermit_builtin.hpp"
#include "Hermit_string_utils.hpp"
#include "Hermit_errors.hpp"
#include "Hermit_log.hpp"
#include "Hermit_runtime.hpp"
#include <boost/nowide/fstream.hpp>
#include <boost/range/algorithm.hpp>
#include <iterator>

namespace Hermit
{
    namespace filesystem
    {
        void create_parent_directories(const std::filesystem::path& path)
        {
            if (path.empty() || !path.has_parent_path())
            {
                return;
            }

            std::filesystem::create_directories(path.parent_path());
        }

        std::string read_file_to_string(const std::filesystem::path& file, std::ios::openmode openmode)
        {
            std::string result;
            if (!std::filesystem::exists(file) || !std::filesystem::is_regular_file(file))
            {
                Hermit::throw_exception(file_open_exception{} << error_info::path{ file });
            }
            boost::nowide::ifstream ifs{ file, openmode };
            if (!ifs.is_open())
            {
                Hermit::throw_exception(file_open_exception{} << error_info::path{ file });
            }
            const std::string file_content{ (std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>() };
            result = file_content;
            return result;
        }

        std::string read_binary_file_to_string(std::string_view file, const config& cfg)
        {
            return read_file_to_string(string_to_path_by_config(file, cfg), std::ios::binary);
        }

        std::string read_text_file_to_string(std::string_view path, const config& cfg, std::string_view extension)
        {
            return read_file_to_string(string_to_path_by_config(complement_extension(path, extension), cfg));
        }

        std::string image_path_to_base64_encoded_string(std::string_view image_path, const config& cfg)
        {
            return string_utils::base64_encode(read_binary_file_to_string(image_path, cfg));
        }

        std::vector<std::string> image_paths_to_base64_encoded_strings(const std::vector<std::string>& paths, const config& cfg)
        {
            std::vector<std::string> encoded_images;
            encoded_images.reserve(paths.size());
            const auto unary_operator{ [&cfg](std::string_view image_path) { return image_path_to_base64_encoded_string(image_path, cfg); } };
            boost::transform(paths, std::back_inserter(encoded_images), unary_operator);
            return encoded_images;
        }

        std::string complement_extension(std::string_view filepath, std::string_view extension)
        {
            std::filesystem::path temp{ filepath };
            if (!temp.has_extension())
            {
                temp.replace_extension(extension);
            }
            return temp.string();
        }

        std::string complement_codeblock_extension(std::string_view language_identifier)
        {
            const std::string_view extension{ language_identifier_to_extension(language_identifier) };
            if (!extension.empty() && extension.front() != '.')
            {
                return std::string{ extension };
            }

            std::string result;
            result.reserve(language_identifier.size() + extension.size());
            result += language_identifier;
            result += extension;
            return result;
        }

        std::filesystem::path string_to_path_by_base_path(std::string_view path, std::string_view base)
        {
            const std::filesystem::path file_path{ path };
            if (file_path.is_relative())
            {
                const std::filesystem::path base_path{ base };
                return base_path / file_path;
            }
            return file_path;
        }

        std::filesystem::path string_to_path_by_config(std::string_view path, const config& cfg)
        {
            const std::filesystem::path file_path{ expand_macro(path, cfg, cfg.ctx) };
            if (file_path.is_relative())
            {
                const std::filesystem::path base_path{ expand_macro(cfg.base_path, cfg, cfg.ctx) };
                return base_path / file_path;
            }
            return file_path;
        }

        std::string base64_image_to_url(std::string_view base64_image, std::string_view mime_type)
        {
            std::string url{ "data:image/" };
            url.reserve(19 + mime_type.size() + base64_image.size());
            url += mime_type;
            url += ";base64,";
            url += base64_image;
            return url;
        }

        std::string_view extension_to_mime_type(std::string_view extension)
        {
            using map_type = string_view_unordered_map<std::string_view>;
            static const map_type map
            {
                { ".jpg", "jpg" },
                { ".jpeg", "jpg" },
                { ".png", "png" },
                { ".webp", "webp" },
                { ".gif", "gif" },
                { ".bmp", "bmp" },
                { ".svg", "svg+xml" },
                { ".avif", "avif" },
                { ".tif", "tiff" },
                { ".tiff", "tiff" },
                { ".ico", "x-icon" }
            };
            if (const map_type::const_iterator iter{ map.find(extension) }; iter != map.end())
            {
                return iter->second;
            }
            Hermit::throw_exception(logic_error{});
        }

        void write_file(const config& cfg, const char* data, std::size_t size, std::string_view filepath, std::ios_base::openmode mode)
        {
            const bool is_binary{ (mode & std::ios::binary) != 0 };
            const std::string expanded{ expand_macro(filepath, cfg, cfg.ctx) };
            const std::string complemented{ filesystem::complement_extension(expanded, ".txt") };
            const std::filesystem::path file_path{ filesystem::string_to_path_by_config(is_binary ? filepath : complemented, cfg) };
            filesystem::create_parent_directories(file_path);
            boost::nowide::ofstream ofs{ file_path, mode };
            if (!ofs.is_open())
            {
                Hermit::throw_exception(file_open_exception{} << error_info::path{ file_path });
            }
            ofs.write(data, size);
            const std::string_view file_type{ is_binary ? "binary" : "text" };
            HERMIT_LOG(info) << "Write " << file_type << " to " << file_path;
        }

        void write_file(const config& cfg, std::string_view data, std::string_view filepath, std::ios_base::openmode mode)
        {
            return write_file(cfg, data.data(), data.size(), filepath, mode);
        }

        std::string prompt_from_string_or_file_path(std::string_view string, std::string_view file_path, const config& cfg)
        {
            return string.empty() ? filesystem::read_text_file_to_string(file_path, cfg) : std::string{ string };
        }
    } // namespace filesystem
} // namespace Hermit
