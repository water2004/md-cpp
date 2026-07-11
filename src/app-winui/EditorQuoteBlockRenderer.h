#pragma once

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
    struct EditorQuoteBlockRenderer
    {
        using DrawMath = std::function<bool(MathJaxSvgFragment const&, D2D1_POINT_2F, D2D1_COLOR_F)>;
        using DrawMathFallback = std::function<void(std::size_t, std::size_t, D2D1_POINT_2F)>;

        static float Render(
            elmd::RenderBlock const& block,
            std::u32string_view sourceText,
            std::size_t caret,
            std::size_t selectionStart,
            std::size_t selectionEnd,
            bool selectionEmpty,
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
            DrawMath const& drawMath,
            DrawMathFallback const& drawMathFallback);
    };
}
