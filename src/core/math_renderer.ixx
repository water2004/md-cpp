// elmd.core.math_renderer — FallbackMathRenderer + SimpleMathParser + MathCache.
export module elmd.core.math_renderer;
import std;
import elmd.core.types;
import elmd.core.theme;
import elmd.core.utf;
import elmd.core.render_model; // MathDisplayMode

export namespace elmd {

enum class MathDisplayMode2 { Inline, Block }; // (kept separate to avoid cycle; layout uses MathDisplayMode)

class MathRenderer {
public:
    virtual ~MathRenderer() = default;
    virtual RenderedMath render_inline(const std::string& tex, const MathStyle& style) = 0;
    virtual RenderedMath render_block(const std::string& tex, const MathStyle& style) = 0;
};

// Fallback implementation (v1): always returns PlainTextFallback.
class FallbackMathRenderer : public MathRenderer {
public:
    RenderedMath render_inline(const std::string& tex, const MathStyle& style) override {
        RenderedMath r;
        r.size = LogicalSize(tex.size() * style.font_size * 0.6f, style.font_size * 1.2f);
        r.baseline = style.font_size * 0.8f;
        r.kind = RenderedMathKind::PlainTextFallback;
        r.fallback_text = tex;
        return r;
    }
    RenderedMath render_block(const std::string& tex, const MathStyle& style) override {
        std::vector<std::string> lines;
        std::string acc;
        for (char c : tex + "\n") {
            if (c == '\n') { lines.push_back(acc); acc.clear(); }
            else acc.push_back(c);
        }
        std::size_t max_len = 0; for (const auto& l : lines) max_len = std::max(max_len, l.size());
        RenderedMath r;
        r.size = LogicalSize(max_len * style.font_size * 0.6f, lines.size() * style.font_size * 1.5f + 20.0f);
        r.baseline = style.font_size * 0.8f;
        r.kind = RenderedMathKind::PlainTextFallback;
        r.fallback_text = tex;
        return r;
    }
};

enum class MathTokenKind { Text, Superscript, Subscript, GroupStart, GroupEnd, Command, Fraction, Sqrt };

struct MathToken {
    MathTokenKind kind = MathTokenKind::Text;
    std::string text;
};

struct MathParseResult {
    std::vector<MathToken> tokens;
    std::vector<std::string> errors;
};

// Simple tokenizer (matches the Rust SimpleMathParser).
inline MathParseResult parse_math(const std::string& tex) {
    MathParseResult res;
    std::size_t i = 0;
    auto flush_text = [&](std::string& acc) { if (!acc.empty()) { res.tokens.push_back({MathTokenKind::Text, acc}); acc.clear(); } };
    std::string acc;
    while (i < tex.size()) {
        char c = tex[i];
        if (c == '^') { flush_text(acc); res.tokens.push_back({MathTokenKind::Superscript, ""}); ++i; continue; }
        if (c == '_') { flush_text(acc); res.tokens.push_back({MathTokenKind::Subscript, ""}); ++i; continue; }
        if (c == '{') { flush_text(acc); res.tokens.push_back({MathTokenKind::GroupStart, ""}); ++i; continue; }
        if (c == '}') { flush_text(acc); res.tokens.push_back({MathTokenKind::GroupEnd, ""}); ++i; continue; }
        if (c == '\\') {
            flush_text(acc); ++i;
            std::string cmd;
            while (i < tex.size() && std::isalpha(static_cast<unsigned char>(tex[i]))) { cmd.push_back(tex[i]); ++i; }
            if (cmd.empty()) res.errors.push_back("Empty command");
            else res.tokens.push_back({MathTokenKind::Command, cmd});
            continue;
        }
        if (c == ' ' || c == '\t') { ++i; continue; }
        acc.push_back(c); ++i;
    }
    flush_text(acc);
    return res;
}

// Math cache.
struct MathCacheKey {
    std::uint64_t tex_hash{};
    std::uint32_t font_size_bits{};
    std::uint32_t dpi_bits{};
    std::uint64_t theme_hash{};
    MathDisplayMode display_mode = MathDisplayMode::Inline;
    bool operator==(const MathCacheKey&) const = default;
};

struct MathCacheEntry { MathCacheKey key; RenderedMath result; };

struct MathCache {
    std::vector<MathCacheEntry> entries;
    std::size_t max_entries = 256;

    static MathCacheKey make_key(const std::string& tex, float font_size, float dpi,
                                 std::uint64_t theme_hash, MathDisplayMode display) {
        MathCacheKey k;
        std::hash<std::string> h;
        k.tex_hash = h(tex);
        std::uint32_t fs; std::memcpy(&fs, &font_size, sizeof(fs));
        std::uint32_t dp; std::memcpy(&dp, &dpi, sizeof(dp));
        k.font_size_bits = fs; k.dpi_bits = dp; k.theme_hash = theme_hash; k.display_mode = display;
        return k;
    }
    const RenderedMath* get(const MathCacheKey& k) const {
        for (const auto& e : entries) if (e.key == k) return &e.result;
        return nullptr;
    }
    void put(const MathCacheKey& k, RenderedMath r) {
        for (auto& e : entries) if (e.key == k) { e.result = std::move(r); return; }
        if (entries.size() >= max_entries) entries.erase(entries.begin());
        entries.push_back({k, std::move(r)});
    }
    void clear() { entries.clear(); }
};

} // namespace elmd
