#include "utils/program_images.hpp"
#include "test_harness.hpp"

namespace
{
    const std::string png = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aRZkAAAAASUVORK5CYII=";
}

TEST(program_images_validate_mime_and_bytes)
{
    std::vector<uint8_t> data;
    ASSERT_TRUE(decode_program_image(png, "image/png", data));
    ASSERT_TRUE(!decode_program_image(png, "image/jpeg", data));
    ASSERT_TRUE(!decode_program_image(png, "image/svg+xml", data));
    ASSERT_TRUE(!decode_program_image("PHN2Zz48L3N2Zz4=", "image/png", data));
    ASSERT_TRUE(valid_program_image("image/jpeg", {0xff, 0xd8, 0xff, 0xd9}));
    ASSERT_TRUE(valid_program_image("image/gif", {'G','I','F','8','9','a',1,0,1,0,0,0,0}));
    ASSERT_TRUE(valid_program_image("image/webp", {'R','I','F','F',8,0,0,0,'W','E','B','P','V','P','8',' '}));
}

TEST(program_images_reject_invalid_base64)
{
    std::vector<uint8_t> data;
    for (const auto &invalid : {"", "!!!!", "====", "A===", "AAAA=AAA", "AB==", "AAB=", "AAAA\n"})
        ASSERT_TRUE(!decode_program_image(invalid, "image/png", data));
}

TEST(program_images_enforce_exact_size_limit)
{
    std::vector<uint8_t> data;
    ASSERT_TRUE(decode_program_image(png, "image/png", data));
    data.resize(kMaxProgramImageBytes);
    ASSERT_TRUE(valid_program_image("image/png", data));
    data.push_back(0);
    ASSERT_TRUE(!valid_program_image("image/png", data));
}

TEST(program_images_preserve_order_and_repeated_references)
{
    const std::string id = "12345678-1234-1234-1234-123456789abc";
    const std::string copy = "aaaaaaaa-1234-1234-1234-123456789abc";
    const std::string html = "before<img src=\"/api/program-images/" + id
        + "\">middle<img src='/api/program-images/" + id + "'>after";
    ASSERT_EQ(program_image_ids(html).size(), 1u);
    ASSERT_STR_EQ(replace_program_image_ids(html, {{id, copy}}),
        "before<img src=\"/api/program-images/" + copy + "\">middle<img src='/api/program-images/" + copy + "'>after");
    ASSERT_STR_EQ(replace_program_image_ids("Legacy plain text", {}), "Legacy plain text");
    ASSERT_TRUE(program_image_ids("/api/program-images/not-an-id").empty());
}
