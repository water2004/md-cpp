#include <array>
#include <cstdint>
#include <vector>

#include "support/folia_test.hpp"
import elmd.core.image_metadata;

using namespace boost::ut;
using namespace elmd;

suite image_metadata_tests = [] {

"common image headers expose stable dimensions"_test = [] {
    std::array<std::uint8_t, 24> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    png[18] = 0x02;
    png[19] = 0x80;
    png[22] = 0x01;
    png[23] = 0xe0;
    auto png_metadata = probe_encoded_image_metadata(png);
    expect(png_metadata.has_value());
    expect(png_metadata->format == EncodedImageFormat::Png);
    expect(png_metadata->width == 640_u);
    expect(png_metadata->height == 480_u);

    std::array<std::uint8_t, 10> gif{'G', 'I', 'F', '8', '9', 'a', 0x20, 0x03, 0x58, 0x02};
    auto gif_metadata = probe_encoded_image_metadata(gif);
    expect(gif_metadata.has_value());
    expect(gif_metadata->format == EncodedImageFormat::Gif);
    expect(gif_metadata->width == 800_u);
    expect(gif_metadata->height == 600_u);

    std::array<std::uint8_t, 26> bmp{'B', 'M'};
    bmp[18] = 0x40;
    bmp[19] = 0x01;
    bmp[22] = 0x10;
    bmp[23] = 0xff;
    bmp[24] = 0xff;
    bmp[25] = 0xff;
    auto bmp_metadata = probe_encoded_image_metadata(bmp);
    expect(bmp_metadata.has_value());
    expect(bmp_metadata->format == EncodedImageFormat::Bmp);
    expect(bmp_metadata->width == 320_u);
    expect(bmp_metadata->height == 240_u);

    std::array<std::uint8_t, 8> ico{0, 0, 1, 0, 1, 0, 0, 0};
    auto ico_metadata = probe_encoded_image_metadata(ico);
    expect(ico_metadata.has_value());
    expect(ico_metadata->format == EncodedImageFormat::Ico);
    expect(ico_metadata->width == 256_u);
    expect(ico_metadata->height == 256_u);

    std::array<std::uint8_t, 30> webp{};
    std::ranges::copy(std::string_view{"RIFF"}, webp.begin());
    std::ranges::copy(std::string_view{"WEBP"}, webp.begin() + 8);
    std::ranges::copy(std::string_view{"VP8X"}, webp.begin() + 12);
    webp[24] = 0xff;
    webp[25] = 0x03;
    webp[27] = 0xff;
    webp[28] = 0x01;
    auto webp_metadata = probe_encoded_image_metadata(webp);
    expect(webp_metadata.has_value());
    expect(webp_metadata->format == EncodedImageFormat::WebP);
    expect(webp_metadata->width == 1024_u);
    expect(webp_metadata->height == 512_u);
};

"jpeg scanning tolerates metadata segments before the frame"_test = [] {
    std::vector<std::uint8_t> jpeg{
        0xff, 0xd8,
        0xff, 0xe0, 0x00, 0x04, 0xaa, 0xbb,
        0xff, 0xc0, 0x00, 0x08, 0x08, 0x02, 0x58, 0x03, 0x20, 0x03,
    };
    auto metadata = probe_encoded_image_metadata(jpeg);
    expect(metadata.has_value());
    expect(metadata->format == EncodedImageFormat::Jpeg);
    expect(metadata->width == 800_u);
    expect(metadata->height == 600_u);
};

"invalid and excessive dimensions are rejected"_test = [] {
    std::array<std::uint8_t, 24> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    expect(!probe_encoded_image_metadata(png).has_value());
    png[16] = 0x01;
    png[20] = 0x01;
    expect(!probe_encoded_image_metadata(png).has_value());
    std::array<std::uint8_t, 5> truncated_jpeg{0xff, 0xd8, 0xff, 0xe0, 0x00};
    expect(!probe_encoded_image_metadata(truncated_jpeg).has_value());
};

};
