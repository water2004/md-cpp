// folia.core.text_measurer — TextMeasurer interface + StubMeasurer for tests.
export module folia.core.text_measurer;
import std;
import folia.core.types;
import folia.core.render_model;

export namespace folia {

struct GlyphInfo {
    std::uint16_t glyph_index{};
    float advance{};
    float offset_x{};
    float offset_y{};
    bool is_whitespace{};
    std::size_t char_index{};
};

struct ShapeResult {
    float width{};
    float height{};
    std::vector<GlyphInfo> glyphs;
    ShapeResult() = default;
    ShapeResult(float w, float h, std::vector<GlyphInfo> gs) : width(w), height(h), glyphs(std::move(gs)) {}
    float advance_total() const {
        float s = 0; for (const auto& g : glyphs) s += g.advance; return s;
    }
};

// Abstract measurer (platform = DirectWrite in production; StubMeasurer in tests).
class TextMeasurer {
public:
    virtual ~TextMeasurer() = default;
    virtual ShapeResult measure(std::u32string_view text, float font_size,
                                const InlineStyle& style,
                                std::optional<float> max_width = std::nullopt) = 0;
};

// Stub: uniform advance per char (whitespace flag set). Used by tests.
class StubMeasurer : public TextMeasurer {
public:
    float advance_per_char = 10.0f;
    StubMeasurer() = default;
    explicit StubMeasurer(float a) : advance_per_char(a) {}
    ShapeResult measure(std::u32string_view text, float font_size,
                        const InlineStyle& /*style*/,
                        std::optional<float> /*max_width*/ = std::nullopt) override {
        std::vector<GlyphInfo> gs;
        gs.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i) {
            GlyphInfo g;
            g.advance = advance_per_char; g.char_index = i;
            g.is_whitespace = (text[i] == ' ' || text[i] == '\t' || text[i] == '\n');
            gs.push_back(g);
        }
        return {advance_per_char * static_cast<float>(text.size()), font_size * 1.4f, std::move(gs)};
    }
};

} // namespace folia