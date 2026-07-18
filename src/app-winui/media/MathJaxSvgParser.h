#pragma once

#include <string_view>

#include "media/MathJaxRenderer.h"

namespace winrt::Folia
{
    MathJaxSvg ParseMathJaxSvgOutput(
        std::string_view output,
        bool display,
        float em);
}
