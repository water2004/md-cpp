#pragma once

#include "EditorEmbeddedDraw.h"
#include "EditorInlineImageRenderer.h"
#include "EditorInteractionMap.h"
#include "EditorRenderResources.h"
#include "EditorStyleSheet.h"
#include "EditorTextLayoutEngine.h"
#include "MathJaxRenderer.h"
#include "SvgNormalizer.h"

namespace elmd
{
    struct RenderBlock;
}

namespace winrt::ElMd
{
    struct EditorTableBlockRenderer
    {
        static std::optional<float> Render(
            elmd::RenderBlock const& block,
            elmd::TextPosition caret,
            elmd::TextSelection selection,
            float documentLeft,
            float documentRight,
            float top,
            float scrollOffset,
            bool svgSupported,
            bool requestEmbedded,
            EditorRenderResources& resources,
            EditorStyleSheet const& styleSheet,
            EditorInteractionMap& interactionMap,
            EditorTextLayoutEngine& textLayoutEngine,
            EditorInlineImageRenderer& inlineImageRenderer,
            MathJaxRenderer& mathJax,
            SvgNormalizer& svgNormalizer,
            EditorDrawMath const& drawMath,
            EditorDrawMathFallback const& drawMathFallback);
    };
}
