#include "pch.h"

import elmd.core.render_model;
import elmd.core.types;

#include "EditorContentPreparation.h"
#include "EditorTableBlockRenderer.h"

namespace winrt::ElMd
{
    std::optional<float> EditorTableBlockRenderer::Render(
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
        EditorDrawMath const& drawMath,
        EditorDrawMathFallback const& drawMathFallback)
    {
        auto modeledTable = block.row_count > 0 && block.column_count > 0 && !block.table_cells.empty();
        if (!modeledTable) return std::nullopt;
        EditorVisualTable table;
        table.sourceStart = block.source_range.start.v;
        table.sourceEnd = block.source_range.end.v;
        table.editable = true;
        table.columnCount = block.column_count;
        table.rowCount = block.row_count;
        auto tableWidth = (std::max)(1.0f, documentRight - documentLeft);
        auto columnWidth = tableWidth / static_cast<float>(table.columnCount);
        std::vector<float> rowHeights(table.rowCount, styleSheet.body.lineHeight + 16.0f);
        std::vector<DisplayInlineText> displays;
        std::vector<std::vector<EditorInlineImageRenderer::ImageDraw>> imageDraws;
        displays.reserve(table.rowCount * table.columnCount);
        imageDraws.reserve(table.rowCount * table.columnCount);
        for (std::size_t row = 0; row < table.rowCount; ++row)
        {
            for (std::size_t column = 0; column < table.columnCount; ++column)
            {
                auto sourceStart = block.source_range.start.v;
                auto sourceEnd = sourceStart;
                auto rangeIndex = row * table.columnCount + column;
                if (rangeIndex < block.table_cell_ranges.size())
                {
                    sourceStart = block.table_cell_ranges[rangeIndex].start.v;
                    sourceEnd = block.table_cell_ranges[rangeIndex].end.v;
                }
                DisplayInlineText display;
                auto renderCellIndex = row * table.columnCount + column;
                if (renderCellIndex < block.table_cells.size())
                {
                    display = BuildDisplayInlineText(block.table_cells[renderCellIndex], caret, sourceEnd, sourceText, mathJax, svgNormalizer, styleSheet.textColor, styleSheet.body.size, (std::max)(1.0f, columnWidth - 20.0f), svgSupported, requestEmbedded);
                }
                else
                {
                    AppendSourceText(display, sourceText, sourceStart, sourceEnd, elmd::InlineStyle::plain(), false);
                    display.displayToSource.push_back(sourceEnd);
                }
                auto wide = ToWide(display.text);
                auto layout = textLayoutEngine.Create(wide, resources.textFormat.Get(), (std::max)(1.0f, columnWidth - 20.0f));
                auto cellTextHeight = styleSheet.body.lineHeight;
                if (layout)
                {
                    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                    textLayoutEngine.ApplyStyles(layout.Get(), display.ranges);
                    ApplyMathInlineObjects(layout.Get(), display.mathOverlays);
                    if (row == 0 && block.table_header_row) layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_RANGE{0, static_cast<UINT32>(wide.size())});
                    auto alignment = column < block.table_aligns.size() ? block.table_aligns[column] : elmd::TableAlignment::None;
                    if (alignment == elmd::TableAlignment::Center) layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    else if (alignment == elmd::TableAlignment::Right) layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                    else layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    DWRITE_TEXT_METRICS metrics{};
                    if (SUCCEEDED(layout->GetMetrics(&metrics)))
                    {
                        cellTextHeight = metrics.height;
                        rowHeights[row] = (std::max)(rowHeights[row], metrics.height + 16.0f);
                    }
                }
                auto images = inlineImageRenderer.Resolve(layout.Get(), display.imageOverlays, (std::max)(1.0f, columnWidth - 20.0f));
                EditorVisualTableCell cell;
                cell.sourceStart = sourceStart;
                cell.sourceEnd = sourceEnd;
                cell.row = row;
                cell.column = column;
                cell.text = std::move(display.text);
                cell.displayToSource = std::move(display.displayToSource);
                cell.textHeight = cellTextHeight;
                cell.layout = std::move(layout);
                displays.push_back(std::move(display));
                imageDraws.push_back(std::move(images));
                table.cells.push_back(std::move(cell));
            }
        }
        table.columnBoundaries.reserve(table.columnCount + 1);
        for (std::size_t column = 0; column <= table.columnCount; ++column) table.columnBoundaries.push_back(documentLeft + columnWidth * static_cast<float>(column));
        table.rowBoundaries.reserve(table.rowCount + 1);
        table.rowBoundaries.push_back(top);
        for (auto rowHeight : rowHeights) table.rowBoundaries.push_back(table.rowBoundaries.back() + rowHeight);
        table.rect = D2D1::RectF(documentLeft, top, documentRight, table.rowBoundaries.back());
        for (auto& cell : table.cells)
        {
            cell.rect = D2D1::RectF(table.columnBoundaries[cell.column], table.rowBoundaries[cell.row], table.columnBoundaries[cell.column + 1], table.rowBoundaries[cell.row + 1]);
            auto verticalInset = (std::max)(0.0f, (cell.rect.bottom - cell.rect.top - cell.textHeight) * 0.5f);
            cell.textOrigin = D2D1::Point2F(cell.rect.left + 10.0f, cell.rect.top + verticalInset);
            cell.textWidth = (std::max)(1.0f, cell.rect.right - cell.rect.left - 20.0f);
        }
        auto tableIndex = interactionMap.tables.size();
        interactionMap.tables.push_back(std::move(table));
        auto& visualTable = interactionMap.tables.back();
        EditorVisualBlock visualBlock;
        visualBlock.rect = visualTable.rect;
        visualBlock.sourceStart = visualTable.sourceStart;
        visualBlock.sourceEnd = visualTable.sourceEnd;
        visualBlock.documentY = top + scrollOffset;
        auto visualBlockIndex = interactionMap.blocks.size();
        interactionMap.blocks.push_back(std::move(visualBlock));
        resources.d2dContext->FillRectangle(D2D1::RectF(visualTable.rect.left, visualTable.rowBoundaries[0], visualTable.rect.right, visualTable.rowBoundaries[1]), resources.panelBrush.Get());
        for (std::size_t cellIndex = 0; cellIndex < visualTable.cells.size(); ++cellIndex)
        {
            auto& cell = visualTable.cells[cellIndex];
            auto& display = displays[cellIndex];
            if (!selectionEmpty && selectionStart < cell.sourceEnd && cell.sourceStart < selectionEnd) resources.d2dContext->FillRectangle(cell.rect, resources.selectionBrush.Get());
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
                inlineImageRenderer.Draw(cell.layout.Get(), cell.textOrigin, imageDraws[cellIndex]);
                for (auto const& overlay : display.mathOverlays)
                {
                    float pointX = 0.0f;
                    float pointY = 0.0f;
                    DWRITE_HIT_TEST_METRICS metrics{};
                    if (FAILED(cell.layout->HitTestTextPosition(overlay.displayStart, FALSE, &pointX, &pointY, &metrics))) continue;
                    auto mathY = cell.textOrigin.y + metrics.top;
                    auto mathX = cell.textOrigin.x + pointX + overlay.leadingSpace;
                    if (!drawMath(overlay.fragment, D2D1::Point2F(mathX, mathY), styleSheet.textColor)) drawMathFallback(overlay.sourceStart, overlay.sourceEnd, D2D1::Point2F(mathX, mathY));
                    if (overlay.strikethrough)
                    {
                        auto strikeY = mathY + overlay.fragment.height * 0.52f;
                        resources.d2dContext->DrawLine(D2D1::Point2F(cell.textOrigin.x + pointX, strikeY), D2D1::Point2F(mathX + overlay.fragment.width, strikeY), resources.textBrush.Get(), 1.5f);
                    }
                    interactionMap.mathHits.push_back(EditorVisualMathHit{D2D1::RectF(mathX, mathY, mathX + overlay.fragment.width, mathY + overlay.fragment.height), overlay.sourceStart, overlay.sourceEnd, overlay.progressStart, overlay.progressEnd});
                }
            }
            interactionMap.AddTableCellLines(visualBlockIndex, tableIndex, cellIndex);
        }
        for (auto boundary : visualTable.columnBoundaries) resources.d2dContext->DrawLine(D2D1::Point2F(boundary, visualTable.rect.top), D2D1::Point2F(boundary, visualTable.rect.bottom), resources.mutedBrush.Get(), 1.0f);
        for (auto boundary : visualTable.rowBoundaries) resources.d2dContext->DrawLine(D2D1::Point2F(visualTable.rect.left, boundary), D2D1::Point2F(visualTable.rect.right, boundary), resources.mutedBrush.Get(), 1.0f);
        return visualTable.rect.bottom;
    }
}
