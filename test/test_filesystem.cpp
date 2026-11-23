#include <filesystem>

#include <gtest/gtest.h>

#include "Hermit_filesystem.hpp"

TEST(filesystem, pure_path_helpers)
{
    EXPECT_EQ(Hermit::filesystem::complement_extension("result", ".txt"), "result.txt");
    EXPECT_EQ(Hermit::filesystem::complement_extension("result.json", ".txt"), "result.json");
    EXPECT_EQ(Hermit::filesystem::complement_codeblock_extension("cpp"), "cpp.cpp");
    EXPECT_EQ(Hermit::filesystem::base64_image_to_url("abc", "png"), "data:image/png;base64,abc");
    EXPECT_EQ(Hermit::filesystem::extension_to_mime_type(".jpg"), "jpg");
}

TEST(filesystem, resolves_relative_and_absolute_paths)
{
    EXPECT_EQ(Hermit::filesystem::string_to_path_by_base_path("file.txt", "C:/base"), std::filesystem::path{"C:/base/file.txt"});
    EXPECT_EQ(Hermit::filesystem::string_to_path_by_base_path("C:/absolute.txt", "C:/base"), std::filesystem::path{"C:/absolute.txt"});
}

TEST(filesystem, rejects_unknown_image_extension)
{
    EXPECT_THROW(Hermit::filesystem::extension_to_mime_type(".unknown"), std::exception);
}
