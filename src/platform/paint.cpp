// elmd.platform.paint — implementation stub.
//
// NOTE: the real D2D painter body (iterating elmd.core.layout_tree's
// LayoutTree) hits an MSVC internal compiler error (C1001, msc1.cpp:1635,
// "IFC import") when a TU mixes `#include <d2d1.h>/<dwrite.h>` with
// `import elmd.core.layout_tree;` and then walks the imported tree with
// nested loops. This is a known class of MSVC C++23 module bugs. The full
// painter will be reinstated once the toolchain is patched; the interface
// in paint.h is stable and the WinUI app wires to it.
//
// To restore: see paint.cpp.real (kept in source control history) and
// re-enable the `import elmd.core.layout_tree;` block once the ICE is gone.
#include "paint.h"

namespace elmd::platform {

struct D2DPainter::Impl {};

D2DPainter::D2DPainter() : impl_(std::make_unique<Impl>()) {}
D2DPainter::D2DPainter(Microsoft::WRL::ComPtr<ID2D1RenderTarget>,
                       Microsoft::WRL::ComPtr<IDWriteFactory>)
    : impl_(std::make_unique<Impl>()) {}
D2DPainter::~D2DPainter() = default;
void D2DPainter::set_target(Microsoft::WRL::ComPtr<ID2D1RenderTarget>) {}
void D2DPainter::set_theme(elmd::Theme) {}

PaintStats D2DPainter::paint(const elmd::LayoutTree&, const elmd::CaretState*, const elmd::Selection*) {
    return PaintStats{};
}

} // namespace elmd::platform