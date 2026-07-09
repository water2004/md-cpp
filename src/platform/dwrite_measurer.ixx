// elmd.platform.dwrite_measurer — DirectWrite-backed TextMeasurer.
//
// Platform layer: Windows/DirectWrite headers live in the global module
// fragment so they (and their macros) cannot leak into importers.
module;
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <cstdint>

export module elmd.platform.dwrite_measurer;
import std;
import elmd.core.render_model;
import elmd.core.text_measurer;

using Microsoft::WRL::ComPtr;

export namespace elmd::platform {

// Caches IDWriteTextFormat per (family, weight, italic, size) bucket so the
// measure and draw paths use the SAME format object (acceptance invariant:
// otherwise widths diverge and adjacent glyph runs overlap).
class TextFormatCache {
public:
    TextFormatCache() = default;
    explicit TextFormatCache(ComPtr<IDWriteFactory> factory) : factory_(std::move(factory)) {}

    ComPtr<IDWriteTextFormat> get(const elmd::InlineStyle& s, float font_size, std::wstring_view family = L"Segoe UI") {
        DWRITE_FONT_WEIGHT weight = s.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
        DWRITE_FONT_STYLE style = s.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
        for (const auto& e : entries_) {
            if (e.weight == weight && e.style == style &&
                std::abs(e.size - font_size) < 0.001f && e.family == family) {
                return e.format;
            }
        }
        Entry e;
        e.family = std::wstring(family);
        e.weight = weight; e.style = style; e.size = font_size;
        ComPtr<IDWriteTextFormat> fmt;
        if (factory_) {
            factory_->CreateTextFormat(
                e.family.c_str(), nullptr, weight, style, DWRITE_FONT_STRETCH_NORMAL,
                font_size, L"en-us", fmt.GetAddressOf());
        }
        e.format = fmt;
        entries_.push_back(std::move(e));
        return fmt;
    }

    void set_family(std::wstring f) { default_family_ = std::move(f); }
    const std::wstring& family() const { return default_family_; }

private:
    struct Entry {
        std::wstring family;
        DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
        DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
        float size = 0.0f;
        ComPtr<IDWriteTextFormat> format;
    };
    ComPtr<IDWriteFactory> factory_;
    std::vector<Entry> entries_;
    std::wstring default_family_ = L"Segoe UI";
};

class DirectWriteMeasurer : public elmd::TextMeasurer {
public:
    DirectWriteMeasurer() = default;
    explicit DirectWriteMeasurer(ComPtr<IDWriteFactory> factory)
        : factory_(std::move(factory)), cache_(factory_) {}
    explicit DirectWriteMeasurer(ComPtr<IDWriteFactory> factory, std::wstring family)
        : factory_(std::move(factory)), cache_(factory_) { cache_.set_family(std::move(family)); }

    const ComPtr<IDWriteFactory>& factory() const { return factory_; }
    TextFormatCache& cache() { return cache_; }

    elmd::ShapeResult measure(std::u32string_view text_sv, float font_size,
                              const elmd::InlineStyle& style,
                              std::optional<float> max_width = std::nullopt) override {
        elmd::ShapeResult r;
        if (text_sv.empty()) return r;
        if (!factory_) return fallback(text_sv, font_size);

        auto fmt = cache_.get(style, font_size);
        if (!fmt) return fallback(text_sv, font_size);

        std::u32string text(text_sv);
        std::wstring utf16;
        utf16.reserve(text.size());
        for (char32_t c : text) {
            if (c <= 0xFFFF) utf16.push_back(static_cast<wchar_t>(c));
            else {
                c -= 0x10000;
                utf16.push_back(static_cast<wchar_t>(0xD800 + (c >> 10)));
                utf16.push_back(static_cast<wchar_t>(0xDC00 + (c & 0x3FF)));
            }
        }
        ComPtr<IDWriteTextLayout> layout;
        factory_->CreateTextLayout(
            utf16.data(), static_cast<UINT32>(utf16.size()), fmt.Get(),
            max_width.value_or(1'000'000.0f), 1'000'000.0f,
            layout.GetAddressOf());
        if (!layout) return fallback(text_sv, font_size);

        layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

        DWRITE_TEXT_METRICS m{};
        if (FAILED(layout->GetMetrics(&m))) return fallback(text_sv, font_size);

        std::vector<float> leading_x(text.size() + 1, 0.0f);
        std::vector<UINT32> char_to_utf16;
        char_to_utf16.reserve(text.size());
        UINT32 acc = 0;
        for (char32_t c : text) {
            char_to_utf16.push_back(acc);
            acc += (c <= 0xFFFF) ? 1 : 2;
        }
        for (std::size_t i = 0; i < text.size(); ++i) {
            float x = 0, y = 0;
            DWRITE_HIT_TEST_METRICS ht{};
            layout->HitTestTextPosition(char_to_utf16[i], false, &x, &y, &ht);
            leading_x[i] = x;
        }
        {
            float x = 0, y = 0;
            DWRITE_HIT_TEST_METRICS ht{};
            layout->HitTestTextPosition(acc, false, &x, &y, &ht);
            leading_x[text.size()] = x;
        }

        r.glyphs.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i) {
            elmd::GlyphInfo g;
            g.advance = (leading_x[i + 1] - leading_x[i]);
            if (g.advance < 0.0f) g.advance = 0.0f;
            g.is_whitespace = (text[i] == U' ' || text[i] == U'\t' || text[i] == U'\n');
            g.char_index = i;
            r.glyphs.push_back(g);
        }
        r.width = leading_x[text.size()];
        r.height = m.height;
        return r;
    }

private:
    elmd::ShapeResult fallback(std::u32string_view text, float font_size) const {
        float adv = font_size * 0.6f;
        elmd::ShapeResult r;
        r.width = adv * static_cast<float>(text.size());
        r.height = font_size * 1.4f;
        for (std::size_t i = 0; i < text.size(); ++i) {
            elmd::GlyphInfo g;
            g.advance = adv; g.char_index = i;
            g.is_whitespace = (text[i] == U' ' || text[i] == U'\t' || text[i] == U'\n');
            r.glyphs.push_back(g);
        }
        return r;
    }

    ComPtr<IDWriteFactory> factory_;
    TextFormatCache cache_;
};

} // namespace elmd::platform