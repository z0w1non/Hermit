#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "Hermit_generation.hpp"

TEST(cu, multipart_helpers)
{
    const std::string boundary{ "boundary" };
    EXPECT_EQ(Hermit::cu::make_image_content_type(boundary), "multipart/form-data; boundary=boundary");
    const std::string body{ Hermit::cu::make_image_body("data", "image.png", boundary, true) };
    EXPECT_THAT(body, testing::HasSubstr("image.png"));
    EXPECT_THAT(body, testing::HasSubstr("data"));
}

TEST(cu, multipart_body_can_omit_overwrite_field)
{
    const std::string body{ Hermit::cu::make_image_body("data", "image.png", "boundary", false) };
    EXPECT_THAT(body, testing::HasSubstr("--boundary--"));
    EXPECT_THAT(body, testing::Not(testing::HasSubstr("name=\"overwrite\"")));
}
