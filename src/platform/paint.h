// elmd.platform.paint — render a core LayoutTree to a Direct2D target.
//
// Windows headers + ComPtr don't behave well as a C++23 module interface on
// the current MSVC build (IFC ICE on nested loops over imported types), so
// this is a regular non-module header + cpp. Platform-only: never included
// from core. The app layer includes it.
#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <memory>
#include <cstddef>

namespace elmd { enum class Theme : int; struct CaretState; struct Selection; struct LayoutTree; }
namespace elmd::platform { struct PaintStats; }

namespace elmd::platform {

struct PaintStats {
    std::size_t blocks = 0;
    std::size_t lines = 0;
    std::size_t runs = 0;
};

class D2DPainter {
public:
    D2DPainter();
    D2DPainter(Microsoft::WRL::ComPtr<ID2D1RenderTarget> rt,
               Microsoft::WRL::ComPtr<IDWriteFactory> dw);
    ~D2DPainter();
    D2DPainter(const D2DPainter&) = delete;
    D2DPainter& operator=(const D2DPainter&) = delete;

    void set_target(Microsoft::WRL::ComPtr<ID2D1RenderTarget> rt);
    void set_theme(elmd::Theme t);

    PaintStats paint(const elmd::LayoutTree& tree,
                     const elmd::CaretState* caret = nullptr,
                     const elmd::Selection* sel = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace elmd::platform