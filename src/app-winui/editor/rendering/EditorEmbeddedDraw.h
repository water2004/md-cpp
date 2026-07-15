#pragma once

#include "media/MathJaxRenderer.h"

import elmd.core.text_edit;

namespace winrt::ElMd
{
    using EditorDrawMath = std::function<bool(MathJaxSvgFragment const&, D2D1_POINT_2F, D2D1_COLOR_F)>;
    using EditorDrawMathFallback = std::function<void(elmd::TextSpan, D2D1_POINT_2F)>;
}
