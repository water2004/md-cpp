#pragma once

#include "MathJaxRenderer.h"

namespace winrt::ElMd
{
    using EditorDrawMath = std::function<bool(MathJaxSvgFragment const&, D2D1_POINT_2F, D2D1_COLOR_F)>;
    using EditorDrawMathFallback = std::function<void(std::size_t, std::size_t, D2D1_POINT_2F)>;
}
