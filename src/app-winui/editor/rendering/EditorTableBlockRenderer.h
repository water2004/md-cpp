#pragma once

import folia.platform.editor_interaction;

#include "editor/rendering/EditorEmbeddedDraw.h"
#include "editor/rendering/EditorInlineImageRenderer.h"
#include "editor/rendering/EditorRenderResources.h"
#include "editor/rendering/EditorStyleSheet.h"
#include "editor/rendering/EditorTextLayoutEngine.h"
#include "media/MathJaxRenderer.h"
#include "media/SvgNormalizer.h"

namespace folia
{
    struct RenderBlock;
}

namespace winrt::Folia
{
    using folia::platform::editor::EditorInteractionMap;
    using folia::platform::editor::EditorVisualBlock;
    using folia::platform::editor::EditorVisualMathHit;
    using folia::platform::editor::EditorVisualTable;
    using folia::platform::editor::EditorVisualTableCell;

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
            std::vector<AsyncWorkDependency> pendingMathJaxDependencies;
            std::vector<AsyncWorkDependencyGroup> pendingSvgDependencyGroups;
            std::vector<float> textHeights;
            float height = 0.0f;
            bool pendingMath = false;
            bool pendingImage = false;
        };

        static std::optional<PreparedTable> Prepare(
            folia::RenderBlock const& block,
            folia::TextPosition caret,
            float tableWidth,
            bool svgSupported,
            bool requestEmbedded,
            bool highPriority,
            EditorRenderResources& resources,
            EditorStyleSheet const& styleSheet,
            EditorTextLayoutEngine& textLayoutEngine,
            EditorInlineImageRenderer& inlineImageRenderer,
            MathJaxRenderer& mathJax,
            SvgNormalizer& svgNormalizer);

        static void Paint(
            folia::RenderBlock const& block,
            PreparedTable const& prepared,
            folia::TextSelection selection,
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
