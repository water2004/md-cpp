#include "pch.h"
#include "editor/rendering/EditorDocumentRenderPass.h"
#include "editor/interaction/EditorTableInteraction.h"

namespace winrt::Folia
{
    EditorDocumentRenderPass::EditorDocumentRenderPass(
        EditorRenderResources& valueResources,
        EditorStyleSheet const& valueStyleSheet,
        EditorInteractionMap& valueInteractionMap,
        std::vector<D2D1_RECT_F>& valueNonInteractiveRegions,
        EditorInlineImageRenderer& valueInlineImages,
        EditorDocumentPainter& valueDocumentPainter,
        EditorDrawMath valueDrawMath,
        EditorDrawMathFallback valueDrawMathFallback)
        : resources(valueResources),
          styleSheet(valueStyleSheet),
          interactionMap(valueInteractionMap),
          nonInteractiveRegions(valueNonInteractiveRegions),
          inlineImages(valueInlineImages),
          documentPainter(valueDocumentPainter),
          drawMath(std::move(valueDrawMath)),
          drawMathFallback(std::move(valueDrawMathFallback))
    {
    }

    void EditorDocumentRenderPass::Paint(
        detail::EditorRenderFrame const& frame,
        EditorPreparedDocument& preparedDocument,
        folia::TextSelection selection,
        folia::TextPosition caret,
        float documentLeft,
        float documentRight,
        float documentWidth,
        float sourceGutterLeft,
        float sourceGutterWidth,
        float scrollOffset,
        std::size_t viewportBegin,
        std::size_t viewportEnd,
        bool printMode,
        std::optional<D2D1_POINT_2F> pointerPosition,
        std::optional<EditorTableAction> const& draggedTableAction,
        std::optional<std::size_t> tableDropIndex)
    {
        if (sourceGutterWidth > 0.0f && resources.lineNumberBackgroundBrush)
        {
            resources.d2dContext->FillRectangle(
                D2D1::RectF(0.0f, 0.0f, sourceGutterWidth, resources.surfaceHeightDip),
                resources.lineNumberBackgroundBrush.Get());
        }

        for (auto blockIndex = viewportBegin; blockIndex < viewportEnd; ++blockIndex)
        {
            auto placement = preparedDocument.geometry.At(blockIndex);
            auto const& block = frame.renderModel.blocks[blockIndex];
            auto& prepared = preparedDocument.blocks[blockIndex];
            auto top = placement.top - scrollOffset;
            auto bottom = placement.bottom - scrollOffset;
            auto documentY = placement.top;
            if (block.kind == folia::RenderBlockKind::ThematicBreak)
            {
                auto center = (top + bottom) * 0.5f;
                resources.d2dContext->DrawLine(
                    D2D1::Point2F(documentLeft, center),
                    D2D1::Point2F(documentRight, center),
                    resources.mutedBrush.Get(),
                    1.0f);
                EditorVisualBlock visual;
                visual.rect = D2D1::RectF(documentLeft, top, documentRight, bottom);
                visual.textOrigin = D2D1::Point2F(documentLeft, top);
                visual.textWidth = documentWidth;
                visual.sourceSpan = block.source_span;
                visual.documentY = documentY;
                visual.thematicBreak = true;
                interactionMap.blocks.push_back(visual);
                EditorVisualLine line;
                line.blockIndex = interactionMap.blocks.size() - 1;
                line.sourceSpans = {block.source_span};
                line.rect = visual.rect;
                interactionMap.lines.push_back(line);
                continue;
            }
            if (prepared.table)
            {
                EditorTableBlockRenderer::Paint(
                    block,
                    *prepared.table,
                    selection,
                    documentLeft,
                    top,
                    scrollOffset,
                    resources,
                    styleSheet,
                    interactionMap,
                    inlineImages,
                    drawMath,
                    drawMathFallback,
                    nonInteractiveRegions);
                continue;
            }
            auto flowContainer = !block.inline_items.empty()
                && (block.kind == folia::RenderBlockKind::Quote
                    || block.kind == folia::RenderBlockKind::Callout);
            auto paddingTop = flowContainer ? 0.0f : block.block_style.padding_top;
            auto paddingLeft = flowContainer ? 0.0f : block.block_style.padding_left;
            auto paddingRight = flowContainer ? 0.0f : block.block_style.padding_right;
            auto contentWidth = (std::max)(1.0f, documentWidth - paddingLeft - paddingRight);
            auto origin = D2D1::Point2F(documentLeft + paddingLeft, top + paddingTop);
            auto rect = D2D1::RectF(documentLeft, top, documentRight, bottom);

            if (sourceGutterWidth > 0.0f
                && block.source_mode
                && block.source_line_number != 0
                && resources.lineNumberFormat
                && resources.lineNumberBrush)
            {
                auto number = std::to_wstring(block.source_line_number);
                resources.d2dContext->DrawTextW(
                    number.c_str(),
                    static_cast<UINT32>(number.size()),
                    resources.lineNumberFormat.Get(),
                    D2D1::RectF(
                        sourceGutterLeft,
                        origin.y,
                        sourceGutterWidth - 5.0f,
                        origin.y + styleSheet.code.lineHeight),
                    resources.lineNumberBrush.Get(),
                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            if (prepared.code || block.block_style.background)
                resources.d2dContext->FillRectangle(rect, resources.panelBrush.Get());
            documentPainter.DrawFlowDecorations(prepared.layout.Get(), origin, block, prepared.display);
            DrawInlinePresentationBackgrounds(
                resources,
                styleSheet,
                prepared.layout.Get(),
                origin,
                prepared.display.ranges);
            documentPainter.DrawSelection(prepared.layout.Get(), origin, prepared.display.displayToSource);
            documentPainter.DrawSearchHighlights(
                prepared.layout.Get(), origin, prepared.display.displayToSource);
            if (prepared.layout)
                resources.d2dContext->DrawTextLayout(
                    origin,
                    prepared.layout.Get(),
                    prepared.code ? resources.codeBrush.Get() : resources.textBrush.Get(),
                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
            documentPainter.DrawTaskCheckboxes(
                prepared.layout.Get(), origin, prepared.display.taskCheckboxOverlays);
            documentPainter.RegisterFootnotes(
                prepared.layout.Get(), origin, prepared.display.footnoteOverlays);
            inlineImages.Draw(prepared.layout.Get(), origin, prepared.images);
            for (auto const& positioned : EditorDocumentPainter::PositionMath(
                    prepared.layout.Get(),
                    prepared.display.mathOverlays,
                    contentWidth))
            {
                auto const& overlay = *positioned.overlay;
                auto mathOrigin = D2D1::Point2F(
                    origin.x + positioned.localX,
                    origin.y + positioned.localTop);
                if (!drawMath(overlay.fragment, mathOrigin, styleSheet.textColor))
                    drawMathFallback(overlay.sourceSpan, mathOrigin);
                interactionMap.mathHits.push_back({
                    D2D1::RectF(
                        mathOrigin.x,
                        mathOrigin.y,
                        mathOrigin.x + overlay.fragment.width,
                        mathOrigin.y + overlay.fragment.height),
                    overlay.sourceSpan,
                    overlay.progressStart,
                    overlay.progressEnd,
                });
            }
            auto previewTop = origin.y + prepared.textHeight;
            for (auto const& preview : prepared.mathPreviews)
            {
                auto nonInteractiveTop = previewTop;
                previewTop += 8.0f;
                auto previewRect = D2D1::RectF(
                    documentLeft + paddingLeft,
                    previewTop,
                    documentLeft + paddingLeft + contentWidth,
                    previewTop + preview.height + 16.0f);
                resources.d2dContext->FillRectangle(previewRect, resources.nestedQuoteBrush.Get());
                auto previewOrigin = D2D1::Point2F(previewRect.left + 8.0f, previewRect.top + 8.0f);
                if (preview.layout)
                    resources.d2dContext->DrawTextLayout(
                        previewOrigin,
                        preview.layout.Get(),
                        resources.textBrush.Get(),
                        D2D1_DRAW_TEXT_OPTIONS_CLIP);
                auto previewContentWidth = (std::max)(
                    1.0f,
                    previewRect.right - previewRect.left - 16.0f);
                for (auto const& positioned : EditorDocumentPainter::PositionMath(
                        preview.layout.Get(),
                        preview.display.mathOverlays,
                        previewContentWidth))
                {
                    auto const& overlay = *positioned.overlay;
                    auto mathOrigin = D2D1::Point2F(
                        previewOrigin.x + positioned.localX,
                        previewOrigin.y + positioned.localTop);
                    if (!drawMath(overlay.fragment, mathOrigin, styleSheet.textColor))
                        drawMathFallback(overlay.sourceSpan, mathOrigin);
                }
                nonInteractiveRegions.push_back(D2D1::RectF(
                    previewRect.left,
                    nonInteractiveTop,
                    previewRect.right,
                    previewRect.bottom));
                previewTop = previewRect.bottom;
            }
            EditorVisualBlock visual;
            visual.rect = rect;
            visual.textOrigin = origin;
            visual.textWidth = contentWidth;
            visual.sourceSpan = block.source_span;
            visual.documentY = documentY;
            visual.text = prepared.display.text;
            visual.displayToSource = prepared.display.displayToSource;
            visual.layout = prepared.layout;
            interactionMap.blocks.push_back(std::move(visual));
            interactionMap.AddBlockLines(interactionMap.blocks.size() - 1);
        }

        EditorTableInteraction::Paint(
            resources,
            interactionMap,
            printMode ? std::nullopt : pointerPosition,
            draggedTableAction,
            tableDropIndex);
        if (!printMode && selection.is_caret())
        {
            if (auto rect = interactionMap.CaretBounds(caret, styleSheet.body.lineHeight))
                resources.d2dContext->DrawLine(
                    D2D1::Point2F(rect->left, rect->top),
                    D2D1::Point2F(rect->left, rect->bottom),
                    resources.caretBrush.Get(),
                    1.5f);
        }
    }
}
