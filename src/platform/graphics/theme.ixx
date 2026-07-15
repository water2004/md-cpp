// elmd.platform.theme — platform palette / brush resolution.
module;
#include <d2d1.h>
#include <wrl/client.h>
#include <vector>
#include <unordered_map>

export module elmd.platform.theme;
import std;
import elmd.core.theme;
import elmd.core.ids;

using Microsoft::WRL::ComPtr;

export namespace elmd::platform {

struct ColorRgba { float r, g, b, a; };

inline ColorRgba color_rgba(elmd::Color color) {
    constexpr auto scale = 1.0f / 255.0f;
    return {color.r * scale, color.g * scale, color.b * scale, color.a * scale};
}

inline ColorRgba palette_color(elmd::ThemeProfile const& theme, std::string_view token) {
    auto const& colors = theme.colors;
    if (token == "text") return color_rgba(colors.fg);
    if (token == "heading") return color_rgba(colors.heading_fg);
    if (token == "code") return color_rgba(colors.code_block_fg);
    if (token == "marker") return color_rgba(colors.marker_fg);
    if (token == "caret") return color_rgba(colors.caret_fg);
    if (token == "selection") return color_rgba(colors.selection_bg);
    if (token == "background") return color_rgba(colors.bg);
    if (token == "code_bg") return color_rgba(colors.code_block_bg);
    if (token == "quote_bar") return color_rgba(colors.blockquote_border);
    if (token == "math") return color_rgba(colors.math_fg);
    return color_rgba(colors.fg);
}

// Lazily create & cache ID2D1SolidColorBrush instances keyed by BrushId.
class BrushCache {
public:
    BrushCache() = default;
    explicit BrushCache(ComPtr<ID2D1RenderTarget> rt) : rt_(std::move(rt)) {}
    void set_target(ComPtr<ID2D1RenderTarget> rt) { rt_ = std::move(rt); brushes_.clear(); }
    ComPtr<ID2D1SolidColorBrush> get(elmd::BrushId id, const ColorRgba& c) {
        if (!rt_) return nullptr;
        auto it = brushes_.find(id.v);
        if (it != brushes_.end()) return it->second;
        ComPtr<ID2D1SolidColorBrush> b;
        D2D1_COLOR_F col{c.r, c.g, c.b, c.a};
        rt_->CreateSolidColorBrush(col, b.GetAddressOf());
        brushes_[id.v] = b;
        return b;
    }
    void clear() { brushes_.clear(); }
private:
    ComPtr<ID2D1RenderTarget> rt_;
    std::unordered_map<std::uint64_t, ComPtr<ID2D1SolidColorBrush>> brushes_;
};

} // namespace elmd::platform
