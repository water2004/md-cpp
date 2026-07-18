#pragma once

#include "media/MathJaxRenderer.h"

import folia.core.text_edit;

namespace winrt::Folia
{
    using EditorDrawMath = std::function<bool(MathJaxSvgFragment const&, D2D1_POINT_2F, D2D1_COLOR_F)>;
    using EditorDrawMathFallback = std::function<void(folia::TextSpan, D2D1_POINT_2F)>;
}
