#include "pch.h"

import elmd.core.render_model;
import elmd.core.types;

#include "editor/rendering/EditorContentPreparation.h"
#include "editor/rendering/EditorTableBlockRenderer.h"

namespace winrt::ElMd
{
    std::optional<EditorTableBlockRenderer::PreparedTable> EditorTableBlockRenderer::Prepare(
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
        SvgNormalizer& svgNormalizer)
    {
        auto const& special = block.special();
        auto modeledTable = special.row_count > 0 && special.column_count > 0 && !special.table_cells.empty();
        if (!modeledTable) return std::nullopt;
        PreparedTable prepared;
        auto& table = prepared.visual;
        table.sourceSpans = special.table_cell_spans;
        table.editable = true;
        table.columnCount = special.column_count;
        table.rowCount = special.row_count;
        tableWidth = (std::max)(1.0f, tableWidth);
        auto columnWidth = tableWidth / static_cast<float>(table.columnCount);
        std::vector<float> rowHeights(table.rowCount, styleSheet.body.lineHeight + 16.0f);
        prepared.displays.reserve(table.rowCount * table.columnCount);
        prepared.imageDraws.reserve(table.rowCount * table.columnCount);
        prepared.mathPreviews.reserve(table.rowCount * table.columnCount);
        prepared.textHeights.reserve(table.rowCount * table.columnCount);
        for (std::size_t row = 0; row < table.rowCount; ++row)
        {
            for (std::size_t column = 0; column < table.columnCount; ++column)
            {
                auto sourceSpan = elmd::TextSpan{block.id, {0, 0}};
                auto rangeIndex = row * table.columnCount + column;
                if (rangeIndex < special.table_cell_spans.size())
                {
                    sourceSpan = special.table_cell_spans[rangeIndex];
                }
                DisplayInlineText display;
                auto renderCellIndex = row * table.columnCount + column;
                if (renderCellIndex < special.table_cells.size())
                {
                    display = BuildDisplayInlineText(special.table_cells[renderCellIndex], caret, {sourceSpan.container_id, sourceSpan.source_range.end, elmd::TextAffinity::Downstream}, mathJax, svgNormalizer, styleSheet.textColor, styleSheet.body.size, (std::max)(1.0f, columnWidth - 20.0f), svgSupported, requestEmbedded);
                }
                else
                {
                    AppendGeneratedText(display, U"\u200B", {sourceSpan.container_id, sourceSpan.source_range.start, elmd::TextAffinity::Downstream}, elmd::InlineStyle::plain());
                    display.displayToSource.push_back({sourceSpan.container_id, sourceSpan.source_range.end, elmd::TextAffinity::Downstream});
                }
                prepared.pendingMath = prepared.pendingMath || display.pendingMath;
                auto wide = ToWide(display.text);
                auto layout = textLayoutEngine.Create(wide, resources.textFormat.Get(), (std::max)(1.0f, columnWidth - 20.0f));
                auto cellTextHeight = styleSheet.body.lineHeight;
                if (layout)
                {
                    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                    textLayoutEngine.ApplyStyles(layout.Get(), display.ranges);
                    ApplyMathInlineObjects(layout.Get(), display.mathOverlays);
                    if (row == 0 && special.table_header_row) layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_RANGE{0, static_cast<UINT32>(wide.size())});
                    auto alignment = column < special.table_aligns.size() ? special.table_aligns[column] : elmd::TableAlignment::None;
                    if (alignment == elmd::TableAlignment::Center) layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    else if (alignment == elmd::TableAlignment::Right) layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                    else layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    DWRITE_TEXT_METRICS metrics{};
                    if (SUCCEEDED(layout->GetMetrics(&metrics)))
                    {
                        cellTextHeight = metrics.height;
                    }
                }
                std::vector<PreparedTable::MathPreview> previews;
                auto previewHeight = 0.0f;
                previews.reserve(display.mathPreviews.size());
                for (auto const& preview : display.mathPreviews)
                {
                    auto previewDisplay = BuildMathPreviewText(preview);
                    auto previewLayout = textLayoutEngine.CreateFlow(
                        previewDisplay,
                        resources.textFormat.Get(),
                        (std::max)(1.0f, columnWidth - 36.0f),
                        {});
                    if (previewLayout && preview.svg.display)
                        previewLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    auto height = textLayoutEngine.MeasureHeight(
                        previewLayout.Get(),
                        styleSheet.body.lineHeight);
                    previewHeight += height + 24.0f;
                    previews.push_back({
                        std::move(previewDisplay),
                        std::move(previewLayout),
                        height,
                    });
                }
                rowHeights[row] = (std::max)(
                    rowHeights[row],
                    cellTextHeight + previewHeight + 16.0f);
                auto images = inlineImageRenderer.Resolve(
                    layout.Get(),
                    display.imageOverlays,
                    (std::max)(1.0f, columnWidth - 20.0f),
                    requestEmbedded);
                prepared.pendingImage = prepared.pendingImage || std::ranges::any_of(
                    images,
                    [](auto const& image) { return image.pending; });
                EditorVisualTableCell cell;
                cell.sourceSpan = sourceSpan;
                cell.row = row;
                cell.column = column;
                cell.text = std::move(display.text);
                cell.displayToSource = std::move(display.displayToSource);
                cell.textHeight = cellTextHeight + previewHeight;
                cell.layout = std::move(layout);
                prepared.displays.push_back(std::move(display));
                prepared.imageDraws.push_back(std::move(images));
                prepared.mathPreviews.push_back(std::move(previews));
                prepared.textHeights.push_back(cellTextHeight);
                table.cells.push_back(std::move(cell));
            }
        }
        table.columnBoundaries.reserve(table.columnCount + 1);
        for (std::size_t column = 0; column <= table.columnCount; ++column) table.columnBoundaries.push_back(columnWidth * static_cast<float>(column));
        table.rowBoundaries.reserve(table.rowCount + 1);
        table.rowBoundaries.push_back(0.0f);
        for (auto rowHeight : rowHeights) table.rowBoundaries.push_back(table.rowBoundaries.back() + rowHeight);
        table.rect = D2D1::RectF(0.0f, 0.0f, tableWidth, table.rowBoundaries.back());
        for (auto& cell : table.cells)
        {
            cell.rect = D2D1::RectF(table.columnBoundaries[cell.column], table.rowBoundaries[cell.row], table.columnBoundaries[cell.column + 1], table.rowBoundaries[cell.row + 1]);
            auto verticalInset = (std::max)(0.0f, (cell.rect.bottom - cell.rect.top - cell.textHeight) * 0.5f);
            cell.textOrigin = D2D1::Point2F(cell.rect.left + 10.0f, cell.rect.top + verticalInset);
            cell.textWidth = (std::max)(1.0f, cell.rect.right - cell.rect.left - 20.0f);
        }
        prepared.height = table.rect.bottom;
        return prepared;
    }

    void EditorTableBlockRenderer::Paint(
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
        std::vector<D2D1_RECT_F>& nonInteractiveRegions)
    {
        auto translatedTable = prepared.visual;
        translatedTable.rect.left += documentLeft;
        translatedTable.rect.right += documentLeft;
        translatedTable.rect.top += top;
        translatedTable.rect.bottom += top;
        for (auto& boundary : translatedTable.columnBoundaries) boundary += documentLeft;
        for (auto& boundary : translatedTable.rowBoundaries) boundary += top;
        for (auto& cell : translatedTable.cells)
        {
            cell.rect.left += documentLeft;
            cell.rect.right += documentLeft;
            cell.rect.top += top;
            cell.rect.bottom += top;
            cell.textOrigin.x += documentLeft;
            cell.textOrigin.y += top;
        }
        auto tableIndex = interactionMap.tables.size();
        interactionMap.tables.push_back(std::move(translatedTable));
        auto& visualTable = interactionMap.tables.back();
        EditorVisualBlock visualBlock;
        visualBlock.rect = visualTable.rect;
        visualBlock.sourceSpan = visualTable.sourceSpans.empty() ? block.source_span : visualTable.sourceSpans.front();
        visualBlock.documentY = top + scrollOffset;
        auto visualBlockIndex = interactionMap.blocks.size();
        interactionMap.blocks.push_back(std::move(visualBlock));
        resources.d2dContext->FillRectangle(D2D1::RectF(visualTable.rect.left, visualTable.rowBoundaries[0], visualTable.rect.right, visualTable.rowBoundaries[1]), resources.panelBrush.Get());
        for (std::size_t cellIndex = 0; cellIndex < visualTable.cells.size(); ++cellIndex)
        {
            auto& cell = visualTable.cells[cellIndex];
            auto const& display = prepared.displays[cellIndex];
            DrawInlinePresentationBackgrounds(
                resources,
                styleSheet,
                cell.layout.Get(),
                cell.textOrigin,
                display.ranges);
            if (!selection.is_caret() && selection.anchor.container_id == cell.sourceSpan.container_id
                && selection.active.container_id == cell.sourceSpan.container_id)
            {
                auto start = (std::min)(selection.anchor.source_offset, selection.active.source_offset);
                auto end = (std::max)(selection.anchor.source_offset, selection.active.source_offset);
                if (start < cell.sourceSpan.source_range.end && cell.sourceSpan.source_range.start < end)
                    resources.d2dContext->FillRectangle(cell.rect, resources.selectionBrush.Get());
            }
            if (cell.layout)
            {
                for (auto const& range : display.ranges)
                {
                    if (!range.style.code || range.length == 0) continue;
                    UINT32 count = 0;
                    auto result = cell.layout->HitTestTextRange(range.start, range.length, cell.textOrigin.x, cell.textOrigin.y, nullptr, 0, &count);
                    if (result != E_NOT_SUFFICIENT_BUFFER || count == 0) continue;
                    std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
                    if (FAILED(cell.layout->HitTestTextRange(range.start, range.length, cell.textOrigin.x, cell.textOrigin.y, metrics.data(), count, &count))) continue;
                    for (UINT32 index = 0; index < count; ++index)
                    {
                        auto const& metric = metrics[index];
                        resources.d2dContext->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(metric.left - 3.0f, metric.top + 2.0f, metric.left + metric.width + 3.0f, metric.top + metric.height - 1.0f), 4.0f, 4.0f), resources.panelBrush.Get());
                    }
                }
                resources.d2dContext->DrawTextLayout(cell.textOrigin, cell.layout.Get(), resources.textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                for (auto const& overlay : display.footnoteOverlays)
                {
                    UINT32 count = 0;
                    auto result = cell.layout->HitTestTextRange(
                        overlay.displayStart,
                        overlay.displayLength,
                        cell.textOrigin.x,
                        cell.textOrigin.y,
                        nullptr,
                        0,
                        &count);
                    if (result != E_NOT_SUFFICIENT_BUFFER || count == 0) continue;
                    std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
                    if (FAILED(cell.layout->HitTestTextRange(
                            overlay.displayStart,
                            overlay.displayLength,
                            cell.textOrigin.x,
                            cell.textOrigin.y,
                            metrics.data(),
                            count,
                            &count))) continue;
                    for (UINT32 index = 0; index < count; ++index)
                    {
                        auto const& metric = metrics[index];
                        interactionMap.footnoteHits.push_back({
                            D2D1::RectF(
                                metric.left,
                                metric.top,
                                metric.left + metric.width,
                                metric.top + metric.height),
                            overlay.sourceSpan,
                            overlay.label,
                            overlay.kind});
                    }
                }
                inlineImageRenderer.Draw(cell.layout.Get(), cell.textOrigin, prepared.imageDraws[cellIndex]);
                for (auto const& overlay : display.mathOverlays)
                {
                    float pointX = 0.0f;
                    float pointY = 0.0f;
                    DWRITE_HIT_TEST_METRICS metrics{};
                    if (FAILED(cell.layout->HitTestTextPosition(overlay.displayStart, FALSE, &pointX, &pointY, &metrics))) continue;
                    auto mathY = cell.textOrigin.y + metrics.top;
                    auto mathX = cell.textOrigin.x + pointX + overlay.leadingSpace;
                    if (!drawMath(overlay.fragment, D2D1::Point2F(mathX, mathY), styleSheet.textColor)) drawMathFallback(overlay.sourceSpan, D2D1::Point2F(mathX, mathY));
                    if (overlay.strikethrough)
                    {
                        auto strikeY = mathY + overlay.fragment.height * 0.52f;
                        resources.d2dContext->DrawLine(D2D1::Point2F(cell.textOrigin.x + pointX, strikeY), D2D1::Point2F(mathX + overlay.fragment.width, strikeY), resources.textBrush.Get(), 1.5f);
                    }
                    interactionMap.mathHits.push_back(EditorVisualMathHit{D2D1::RectF(mathX, mathY, mathX + overlay.fragment.width, mathY + overlay.fragment.height), overlay.sourceSpan, overlay.progressStart, overlay.progressEnd});
                }
            }
            if (cellIndex < prepared.mathPreviews.size()
                && cellIndex < prepared.textHeights.size())
            {
                auto previewTop = cell.textOrigin.y + prepared.textHeights[cellIndex];
                for (auto const& preview : prepared.mathPreviews[cellIndex])
                {
                    auto nonInteractiveTop = previewTop;
                    previewTop += 8.0f;
                    auto previewRect = D2D1::RectF(
                        cell.rect.left + 6.0f,
                        previewTop,
                        cell.rect.right - 6.0f,
                        previewTop + preview.height + 16.0f);
                    resources.d2dContext->FillRectangle(previewRect, resources.nestedQuoteBrush.Get());
                    auto previewOrigin = D2D1::Point2F(previewRect.left + 8.0f, previewRect.top + 8.0f);
                    if (preview.layout)
                        resources.d2dContext->DrawTextLayout(
                            previewOrigin,
                            preview.layout.Get(),
                            resources.textBrush.Get(),
                            D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    for (auto const& overlay : preview.display.mathOverlays)
                    {
                        float pointX = 0.0f;
                        float pointY = 0.0f;
                        DWRITE_HIT_TEST_METRICS metrics{};
                        if (!preview.layout || FAILED(preview.layout->HitTestTextPosition(
                                overlay.displayStart,
                                FALSE,
                                &pointX,
                                &pointY,
                                &metrics))) continue;
                        auto mathOrigin = D2D1::Point2F(
                            previewOrigin.x + pointX + overlay.leadingSpace,
                            previewOrigin.y + metrics.top);
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
            }
            interactionMap.AddTableCellLines(visualBlockIndex, tableIndex, cellIndex);
        }
        for (auto boundary : visualTable.columnBoundaries) resources.d2dContext->DrawLine(D2D1::Point2F(boundary, visualTable.rect.top), D2D1::Point2F(boundary, visualTable.rect.bottom), resources.mutedBrush.Get(), 1.0f);
        for (auto boundary : visualTable.rowBoundaries) resources.d2dContext->DrawLine(D2D1::Point2F(visualTable.rect.left, boundary), D2D1::Point2F(visualTable.rect.right, boundary), resources.mutedBrush.Get(), 1.0f);
    }
}
