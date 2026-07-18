#include "pch.h"
#include "editor/rendering/EditorSurfaceRenderer.h"
#include "editor/rendering/EditorPreparedDocument.h"
#include "localization/Localization.h"

import folia.core.render_model;

#include "editor/rendering/EditorContentPreparation.h"
#include "editor/rendering/EditorDocumentBlockPreparer.h"
#include "editor/rendering/EditorDocumentPainter.h"
#include "editor/rendering/EditorDocumentPreparationPass.h"
#include "editor/rendering/EditorDocumentRenderPass.h"
#include "editor/rendering/EditorInlineImageRenderer.h"
#include "editor/rendering/EditorSvgPainter.h"
#include "editor/rendering/EditorTableBlockRenderer.h"
#include "editor/rendering/EditorTextLayoutEngine.h"

namespace winrt::Folia
{
    void EditorSurfaceRenderer::DrawDocument(detail::EditorRenderFrame const& frame)
    {
        interactionMap.Clear();
        nonInteractiveRegions.clear();
        auto padding = (std::min)(styleSheet.horizontalPadding, (std::max)(12.0f, resources.surfaceWidthDip * 0.06f));
        auto sourceDocument = !frame.renderModel.blocks.empty()
            && frame.renderModel.blocks.front().source_mode;
        auto sourceLineCount = sourceDocument
            ? (std::max)(std::uint32_t{1}, frame.renderModel.blocks.back().source_line_number)
            : std::uint32_t{0};
        auto sourceLineDigits = sourceDocument
            ? std::to_wstring(sourceLineCount).size()
            : std::size_t{0};
        auto desiredSourceGutterWidth = sourceDocument
            ? (std::max)(24.0f, styleSheet.code.size * 0.64f * static_cast<float>(sourceLineDigits) + 12.0f)
            : 0.0f;
        auto availableSourceGutterWidth = (std::max)(
            0.0f,
            resources.surfaceWidthDip - padding - 14.0f - 80.0f - 8.0f);
        auto sourceGutterWidth = (std::min)(desiredSourceGutterWidth, availableSourceGutterWidth);
        if (sourceGutterWidth < 20.0f) sourceGutterWidth = 0.0f;
        auto sourceGutterLeft = 4.0f;
        auto documentLeft = sourceGutterWidth > 0.0f ? sourceGutterWidth + 8.0f : padding;
        auto documentRight = (std::max)(documentLeft + 1.0f, resources.surfaceWidthDip - padding - 14.0f);
        auto documentWidth = documentRight - documentLeft;
        auto scrollOffset = scrollState.Offset();
        auto y = styleSheet.verticalPadding - scrollOffset;
        auto selection = printMode ? folia::TextSelection{} : frame.selection;
        auto caret = selection.active;

        EditorSvgPainter svgPainter(resources, renderCache);
        auto mathSvgSupported = svgPainter.Supported() && mathJax.Enabled();
        EditorTextLayoutEngine textLayoutEngine(resources, styleSheet);
        EditorInlineImageRenderer inlineImages(
            resources,
            renderCache,
            styleSheet,
            svgNormalizer,
            svgPainter,
            frame.baseDirectory,
            !printMode);
        auto drawMath = [&](MathJaxSvgFragment const& fragment, D2D1_POINT_2F origin, D2D1_COLOR_F) {
            if (!fragment.svg) return false;
            if (printMode)
                return svgPainter.Draw(
                    fragment.renderId,
                    *fragment.svg,
                    fragment.width,
                    fragment.height,
                    origin);
            return svgPainter.DrawCached(
                fragment.renderId,
                fragment.width,
                fragment.height,
                origin);
        };
        auto drawMathFallback = [&](folia::TextSpan, D2D1_POINT_2F origin) {
            auto label = Localize(L"Formula");
            resources.d2dContext->DrawTextW(
                label.c_str(),
                static_cast<std::uint32_t>(label.size()),
                resources.codeFormat.Get(),
                D2D1::RectF(origin.x, origin.y, documentRight, origin.y + styleSheet.code.lineHeight),
                resources.textBrush.Get());
        };
        EditorDocumentPainter documentPainter(
            resources,
            styleSheet,
            interactionMap,
            treeSitter,
            pointerPosition,
            printMode,
            documentRight,
            selection,
            printMode ? std::span<const detail::EditorSearchHighlight>{} : frame.searchHighlights,
            frame.renderModel.editable_index);

        if (frame.renderModel.blocks.empty() && !printMode)
        {
            auto message = Localize(L"EmptyDocumentHint");
            resources.d2dContext->DrawTextW(
                message.c_str(),
                static_cast<std::uint32_t>(message.size()),
                resources.textFormat.Get(),
                D2D1::RectF(documentLeft, y, documentRight, y + 80.0f),
                resources.mutedBrush.Get());
        }

        auto remoteImageGeneration = renderCache.RemoteImageGeneration();
        EditorDocumentBlockPreparer blockPreparer(
            frame,
            resources,
            renderCache,
            styleSheet,
            textLayoutEngine,
            inlineImages,
            documentPainter,
            mathJax,
            svgNormalizer,
            treeSitter,
            caret,
            documentWidth,
            mathSvgSupported,
            embeddedGeneration,
            remoteImageGeneration);
        EditorDocumentPreparationPass preparationPass(
            frame,
            resources,
            styleSheet,
            inlineImages,
            svgPainter,
            mathJax,
            svgNormalizer,
            blockPreparer,
            scrollState,
            preparedDocument,
            selection,
            documentWidth,
            themeRevision,
            embeddedGeneration,
            remoteImageGeneration,
            scrollOffset,
            printMode);
        auto preparation = preparationPass.Prepare();
        scrollOffset = preparation.scrollOffset;
        if (preparation.invalidateRequested) Invalidate();
        EditorDocumentRenderPass renderPass(
            resources,
            styleSheet,
            interactionMap,
            nonInteractiveRegions,
            inlineImages,
            documentPainter,
            drawMath,
            drawMathFallback);
        renderPass.Paint(
            frame,
            *preparedDocument,
            selection,
            caret,
            documentLeft,
            documentRight,
            documentWidth,
            sourceGutterLeft,
            sourceGutterWidth,
            scrollOffset,
            preparation.visible.begin,
            preparation.visible.end,
            printMode,
            pointerPosition,
            draggedTableAction,
            tableDropIndex);
        totalDocumentHeight = preparedDocument->totalHeight;
        scrollState.Clamp(MaximumScrollOffset());
    }

}
