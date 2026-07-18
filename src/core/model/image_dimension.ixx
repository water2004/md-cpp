// folia.core.image_dimension — unit-aware dimensions projected from image syntax.
export module folia.core.image_dimension;
import std;

export namespace folia {

enum class ImageDimensionUnit {
    Pixels,
    Percent,
};

struct ImageDimension {
    float value = 0.0f;
    ImageDimensionUnit unit = ImageDimensionUnit::Pixels;

    static ImageDimension pixels(float value) {
        return {value, ImageDimensionUnit::Pixels};
    }

    static ImageDimension percent(float value) {
        return {value, ImageDimensionUnit::Percent};
    }

    auto operator<=>(const ImageDimension&) const = default;
};

// HTML exposes image dimensions as either CSS pixels or percentages. Keep
// the unit in the semantic projection and reject partially parsed values;
// the original attribute spelling remains authoritative in the lossless CST.
inline std::optional<ImageDimension> parse_html_image_dimension(std::string_view source) {
    while (!source.empty() && std::isspace(static_cast<unsigned char>(source.front()))) source.remove_prefix(1);
    while (!source.empty() && std::isspace(static_cast<unsigned char>(source.back()))) source.remove_suffix(1);
    auto unit = ImageDimensionUnit::Pixels;
    if (source.ends_with('%')) {
        unit = ImageDimensionUnit::Percent;
        source.remove_suffix(1);
    } else if (source.size() >= 2
        && std::tolower(static_cast<unsigned char>(source[source.size() - 2])) == 'p'
        && std::tolower(static_cast<unsigned char>(source.back())) == 'x') {
        source.remove_suffix(2);
    }
    while (!source.empty() && std::isspace(static_cast<unsigned char>(source.back()))) source.remove_suffix(1);
    if (source.empty()) return std::nullopt;
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stof(std::string(source), &consumed);
        if (consumed != source.size() || !std::isfinite(parsed) || parsed <= 0.0f) return std::nullopt;
        const auto maximum = unit == ImageDimensionUnit::Percent ? 1000.0f : 16384.0f;
        if (parsed > maximum) return std::nullopt;
        return ImageDimension{parsed, unit};
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace folia
