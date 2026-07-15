#pragma once

#include "editor/rendering/EditorEmbeddedDraw.h"
#include "editor/rendering/EditorInlineImageRenderer.h"
#include "editor/interaction/EditorInteractionMap.h"
#include "editor/rendering/EditorRenderResources.h"
#include "editor/rendering/EditorStyleSheet.h"
#include "editor/rendering/EditorTextLayoutEngine.h"
#include "media/MathJaxRenderer.h"
#include "media/SvgNormalizer.h"

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
            struct MathPreview
            {
                DisplayInlineText display;
                ::Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
                float height = 0.0f;
            };

            EditorVisualTable visual;
            std::vector<DisplayInlineText> displays;
            std::vector<std::vector<EditorInlineImageRenderer::ImageDraw>> imageDraws;
            std::vector<std::vector<MathPreview>> mathPreviews;
            std::vector<float> textHeights;
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
            EditorDrawMathFallback const& drawMathFallback,
            std::vector<D2D1_RECT_F>& nonInteractiveRegions);
    };
}
