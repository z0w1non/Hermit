#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "filesystem.hpp"
#include "config.hpp"

TEST(filesystem, pure_path_helpers)
{
    EXPECT_EQ(hermit::filesystem::complement_extension("result", ".txt"), "result.txt");
    EXPECT_EQ(hermit::filesystem::complement_extension("result.json", ".txt"), "result.json");
    EXPECT_EQ(hermit::filesystem::complement_codeblock_extension("cpp"), "cpp.cpp");
    EXPECT_EQ(hermit::filesystem::base64_image_to_url("abc", "png"), "data:image/png;base64,abc");
    EXPECT_EQ(hermit::filesystem::extension_to_mime_type(".jpg"), "jpg");
}

TEST(filesystem, resolves_relative_and_absolute_paths)
{
    EXPECT_EQ(hermit::filesystem::string_to_path_by_base_path("file.txt", "C:/base"), std::filesystem::path{"C:/base/file.txt"});
    EXPECT_EQ(hermit::filesystem::string_to_path_by_base_path("C:/absolute.txt", "C:/base"), std::filesystem::path{"C:/absolute.txt"});
}

TEST(filesystem, rejects_unknown_image_extension)
{
    EXPECT_THROW(hermit::filesystem::extension_to_mime_type(".unknown"), std::exception);
}

TEST(filesystem, reads_and_writes_text_and_binary_files_with_parent_creation)
{
    hermit::config cfg;
    const std::filesystem::path base_path{ std::filesystem::temp_directory_path() / "hermit_filesystem_io_test" };
    std::filesystem::remove_all(base_path);
    cfg.base_path = base_path.string();

    hermit::filesystem::write_file(cfg, "text content", "nested/text");
    EXPECT_EQ(hermit::filesystem::read_text_file_to_string("nested/text", cfg), "text content");

    const std::string binary_content{ "a\0b", 3 };
    hermit::filesystem::write_file(cfg, binary_content, "nested/data.bin", std::ios::binary);
    EXPECT_EQ(hermit::filesystem::read_binary_file_to_string("nested/data.bin", cfg), binary_content);
    std::filesystem::remove_all(base_path);
}

TEST(filesystem, injects_reader_after_path_resolution_and_extension_completion)
{
    hermit::config cfg;
    cfg.base_path = "base";
    std::filesystem::path captured_path;
    std::ios_base::openmode captured_mode{};
    const hermit::filesystem::file_reader reader{ [&captured_path, &captured_mode](const std::filesystem::path& path,
        std::ios_base::openmode mode)
    {
        captured_path = path;
        captured_mode = mode;
        return std::string{ "mock contents" };
    } };

    EXPECT_EQ(hermit::filesystem::read_text_file_to_string("nested/prompt", cfg, reader), "mock contents");
    EXPECT_EQ(captured_path, std::filesystem::path{ "base/nested/prompt.txt" });
    EXPECT_EQ(captured_mode, std::ios::in);
    EXPECT_EQ(hermit::filesystem::read_binary_file_to_string("image.png", cfg, reader), "mock contents");
    EXPECT_EQ(captured_path, std::filesystem::path{ "base/image.png" });
    EXPECT_EQ(captured_mode, std::ios::binary);
}

TEST(filesystem, propagates_injected_reader_failure)
{
    hermit::config cfg;
    const hermit::filesystem::file_reader reader{ [](const std::filesystem::path&, std::ios_base::openmode) -> std::string
    {
        throw std::runtime_error{ "injected reader failure" };
    } };

    EXPECT_THROW(hermit::filesystem::read_text_file_to_string("prompt", cfg, reader), std::runtime_error);
}

TEST(filesystem, rejects_missing_files_and_prefers_prompt_text_over_file)
{
    hermit::config cfg;
    const std::filesystem::path base_path{ std::filesystem::temp_directory_path() / "hermit_filesystem_prompt_test" };
    std::filesystem::remove_all(base_path);
    std::filesystem::create_directories(base_path);
    cfg.base_path = base_path.string();
    hermit::filesystem::write_file(cfg, "file prompt", "prompt.txt");

    EXPECT_EQ(hermit::filesystem::prompt_from_string_or_file_path("inline prompt", "missing.txt", cfg), "inline prompt");
    EXPECT_EQ(hermit::filesystem::prompt_from_string_or_file_path("", "prompt", cfg), "file prompt");
    EXPECT_THROW(hermit::filesystem::read_text_file_to_string("missing", cfg), std::exception);
    std::filesystem::remove_all(base_path);
}

TEST(filesystem, propagates_parent_directory_creation_failure)
{
    hermit::config cfg;
    const std::filesystem::path base_path{ std::filesystem::temp_directory_path() / "hermit_filesystem_write_failure_test" };
    std::filesystem::remove_all(base_path);
    std::filesystem::create_directories(base_path.parent_path());
    {
        std::ofstream file{ base_path };
        ASSERT_TRUE(file.is_open());
        file << "not a directory";
    }
    cfg.base_path = base_path.string();

    EXPECT_THROW(hermit::filesystem::write_file(cfg, "content", "child/file.txt"), std::filesystem::filesystem_error);
    std::filesystem::remove(base_path);
}

TEST(filesystem, injects_writer_after_path_resolution_and_extension_completion)
{
    hermit::config cfg;
    cfg.base_path = "base";
    std::filesystem::path captured_path;
    std::string captured_data;
    std::ios_base::openmode captured_mode{};
    const hermit::filesystem::file_writer writer{ [&captured_path, &captured_data, &captured_mode](
        const std::filesystem::path& path, const char* data, std::size_t size, std::ios_base::openmode mode)
    {
        captured_path = path;
        captured_data.assign(data, size);
        captured_mode = mode;
    } };

    hermit::filesystem::write_file(cfg, "payload", "nested/result", std::ios::out, writer);

    EXPECT_EQ(captured_path, std::filesystem::path{ "base/nested/result.txt" });
    EXPECT_EQ(captured_data, "payload");
    EXPECT_EQ(captured_mode, std::ios::out);
}

TEST(filesystem, propagates_injected_writer_failure)
{
    hermit::config cfg;
    const hermit::filesystem::file_writer writer{ [](const std::filesystem::path&, const char*, std::size_t, std::ios_base::openmode)
    {
        throw std::runtime_error{ "injected writer failure" };
    } };

    EXPECT_THROW(hermit::filesystem::write_file(cfg, "payload", "result", std::ios::binary, writer), std::runtime_error);
}

TEST(filesystem, reads_image_bytes_and_encodes_base64)
{
    hermit::config cfg;
    const std::filesystem::path base_path{ std::filesystem::temp_directory_path() / "hermit_filesystem_image_test" };
    std::filesystem::remove_all(base_path);
    std::filesystem::create_directories(base_path);
    cfg.base_path = base_path.string();
    const std::string image_content{ "png\0data", 8 };
    hermit::filesystem::write_file(cfg, image_content, "image.png", std::ios::binary);

    EXPECT_EQ(hermit::filesystem::image_path_to_base64_encoded_string("image.png", cfg), "cG5nAGRhdGE=");
    std::filesystem::remove_all(base_path);
}
