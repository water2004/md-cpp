export module elmd.core.image_metadata;
import std;

export namespace elmd {

enum class EncodedImageFormat {
    Gif,
    Png,
    Jpeg,
    Bmp,
    Ico,
    WebP,
};

struct EncodedImageMetadata {
    EncodedImageFormat format = EncodedImageFormat::Png;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

namespace image_metadata_detail {

inline std::uint16_t read_big16(std::span<std::uint8_t const> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8) | bytes[offset + 1]);
}

inline std::uint32_t read_little24(std::span<std::uint8_t const> bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16);
}

inline std::uint32_t read_big32(std::span<std::uint8_t const> bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24)
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 8)
        | bytes[offset + 3];
}

inline std::uint32_t read_little32(std::span<std::uint8_t const> bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

inline bool starts_with(
    std::span<std::uint8_t const> bytes,
    std::size_t offset,
    std::string_view signature)
{
    if (offset > bytes.size() || signature.size() > bytes.size() - offset) return false;
    for (std::size_t index = 0; index < signature.size(); ++index) {
        if (bytes[offset + index] != static_cast<std::uint8_t>(signature[index])) return false;
    }
    return true;
}

inline std::optional<EncodedImageMetadata> valid(
    EncodedImageFormat format,
    std::uint32_t width,
    std::uint32_t height)
{
    constexpr std::uint64_t maximum_pixels = 16ull * 1024ull * 1024ull;
    if (width == 0 || height == 0
        || static_cast<std::uint64_t>(width) * height > maximum_pixels) return std::nullopt;
    return EncodedImageMetadata{format, width, height};
}

} // namespace image_metadata_detail

inline std::optional<EncodedImageMetadata> probe_encoded_image_metadata(
    std::span<std::uint8_t const> bytes)
{
    using namespace image_metadata_detail;
    if (bytes.size() >= 10
        && (starts_with(bytes, 0, "GIF87a") || starts_with(bytes, 0, "GIF89a"))) {
        return valid(
            EncodedImageFormat::Gif,
            static_cast<std::uint32_t>(bytes[6]) | (static_cast<std::uint32_t>(bytes[7]) << 8),
            static_cast<std::uint32_t>(bytes[8]) | (static_cast<std::uint32_t>(bytes[9]) << 8));
    }
    constexpr std::array<std::uint8_t, 8> png_signature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (bytes.size() >= 24 && std::ranges::equal(bytes.first<8>(), png_signature)) {
        return valid(
            EncodedImageFormat::Png,
            read_big32(bytes, 16),
            read_big32(bytes, 20));
    }
    if (bytes.size() >= 26 && bytes[0] == 'B' && bytes[1] == 'M') {
        auto signed_width = static_cast<std::int32_t>(read_little32(bytes, 18));
        auto signed_height = static_cast<std::int32_t>(read_little32(bytes, 22));
        if (signed_width > 0 && signed_height != 0
            && signed_height != (std::numeric_limits<std::int32_t>::min)()) {
            return valid(
                EncodedImageFormat::Bmp,
                static_cast<std::uint32_t>(signed_width),
                static_cast<std::uint32_t>(signed_height < 0 ? -signed_height : signed_height));
        }
    }
    if (bytes.size() >= 8 && bytes[0] == 0 && bytes[1] == 0
        && (bytes[2] == 1 || bytes[2] == 2) && bytes[3] == 0) {
        return valid(
            EncodedImageFormat::Ico,
            bytes[6] == 0 ? 256u : bytes[6],
            bytes[7] == 0 ? 256u : bytes[7]);
    }
    if (bytes.size() >= 30 && starts_with(bytes, 0, "RIFF") && starts_with(bytes, 8, "WEBP")) {
        if (starts_with(bytes, 12, "VP8X")) {
            return valid(
                EncodedImageFormat::WebP,
                read_little24(bytes, 24) + 1,
                read_little24(bytes, 27) + 1);
        }
        if (starts_with(bytes, 12, "VP8L") && bytes[20] == 0x2f) {
            auto width = 1u + bytes[21]
                + ((static_cast<std::uint32_t>(bytes[22]) & 0x3fu) << 8);
            auto height = 1u + (bytes[22] >> 6)
                + (static_cast<std::uint32_t>(bytes[23]) << 2)
                + ((static_cast<std::uint32_t>(bytes[24]) & 0x0fu) << 10);
            return valid(EncodedImageFormat::WebP, width, height);
        }
        if (starts_with(bytes, 12, "VP8 ")
            && bytes[23] == 0x9d && bytes[24] == 0x01 && bytes[25] == 0x2a) {
            return valid(
                EncodedImageFormat::WebP,
                (static_cast<std::uint32_t>(bytes[26])
                    | (static_cast<std::uint32_t>(bytes[27]) << 8)) & 0x3fffu,
                (static_cast<std::uint32_t>(bytes[28])
                    | (static_cast<std::uint32_t>(bytes[29]) << 8)) & 0x3fffu);
        }
    }
    if (bytes.size() >= 4 && bytes[0] == 0xff && bytes[1] == 0xd8) {
        auto position = std::size_t{2};
        while (position + 8 < bytes.size()) {
            while (position < bytes.size() && bytes[position] != 0xff) ++position;
            while (position < bytes.size() && bytes[position] == 0xff) ++position;
            if (position >= bytes.size()) break;
            auto marker = bytes[position++];
            if (marker == 0xd8 || marker == 0xd9 || (marker >= 0xd0 && marker <= 0xd7)) continue;
            if (position + 2 > bytes.size()) break;
            auto segment_length = read_big16(bytes, position);
            if (segment_length < 2 || position + segment_length > bytes.size()) break;
            auto start_of_frame = (marker >= 0xc0 && marker <= 0xc3)
                || (marker >= 0xc5 && marker <= 0xc7)
                || (marker >= 0xc9 && marker <= 0xcb)
                || (marker >= 0xcd && marker <= 0xcf);
            if (start_of_frame && segment_length >= 7) {
                return valid(
                    EncodedImageFormat::Jpeg,
                    read_big16(bytes, position + 5),
                    read_big16(bytes, position + 3));
            }
            position += segment_length;
        }
    }
    return std::nullopt;
}

} // namespace elmd
