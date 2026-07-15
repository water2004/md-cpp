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

inline ColorRgba palette_color(elmd::Theme t, std::string_view token) {
    bool dark = (t == elmd::Theme::Dark);
    if (token == "text")        return dark ? ColorRgba{0.86f,0.86f,0.86f,1.f} : ColorRgba{0.13f,0.13f,0.13f,1.f};
    if (token == "heading")     return dark ? ColorRgba{0.34f,0.61f,0.84f,1.f} : ColorRgba{0.18f,0.31f,0.55f,1.f};
    if (token == "code")        return dark ? ColorRgba{0.88f,0.55f,0.45f,1.f} : ColorRgba{0.72f,0.40f,0.30f,1.f};
    if (token == "marker")      return dark ? ColorRgba{0.40f,0.40f,0.43f,1.f} : ColorRgba{0.65f,0.65f,0.68f,1.f};
    if (token == "caret")       return dark ? ColorRgba{0.92f,0.92f,0.92f,1.f} : ColorRgba{0.11f,0.11f,0.11f,1.f};
    if (token == "selection")   return dark ? ColorRgba{0.30f,0.45f,0.70f,0.45f} : ColorRgba{0.55f,0.70f,0.90f,0.45f};
    if (token == "background")  return dark ? ColorRgba{0.118f,0.118f,0.129f,1.f} : ColorRgba{1.f,1.f,1.f,1.f};
    if (token == "code_bg")     return dark ? ColorRgba{0.16f,0.16f,0.18f,1.f} : ColorRgba{0.93f,0.93f,0.96f,1.f};
    if (token == "quote_bar")   return dark ? ColorRgba{0.35f,0.40f,0.50f,1.f} : ColorRgba{0.55f,0.60f,0.70f,1.f};
    if (token == "math")        return dark ? ColorRgba{0.78f,0.62f,0.40f,1.f} : ColorRgba{0.62f,0.42f,0.20f,1.f};
    return dark ? ColorRgba{0.86f,0.86f,0.86f,1.f} : ColorRgba{0.13f,0.13f,0.13f,1.f};
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