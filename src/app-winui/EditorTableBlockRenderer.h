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
        struct PreparedTable
        {
            EditorVisualTable visual;
            std::vector<DisplayInlineText> displays;
            std::vector<std::vector<EditorInlineImageRenderer::ImageDraw>> imageDraws;
            float height = 0.0f;
            bool pendingMath = false;
        };

        static std::optional<PreparedTable> Prepare(
            elmd::RenderBlock const& block,
            elmd::TextPosition caret,
            float tableWidth,
            bool svgSupported,
            bool requestEmbedded,
            EditorRenderResources& resources,
            EditorStyleSheet const& styleSheet,
            EditorTextLayoutEngine& textLayoutEngine,
            EditorInlineImageRenderer& inlineImageRenderer,
            MathJaxRenderer& mathJax,
            SvgNormalizer& svgNormalizer);

        static void Paint(
            elmd::RenderBlock const& block,
            PreparedTable const& prepared,
            elmd::TextSelection selection,
            float documentLeft,
            float top,
            float scrollOffset,
            EditorRenderResources& resources,
            EditorStyleSheet const& styleSheet,
            EditorInteractionMap& interactionMap,
            EditorInlineImageRenderer& inlineImageRenderer,
            EditorDrawMath const& drawMath,
            EditorDrawMathFallback const& drawMathFallback);
    };
}
