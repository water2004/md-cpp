#include "pch.h"
#include "EditorTableInteraction.h"

namespace winrt::ElMd
{
    void EditorTableInteraction::Paint(EditorRenderResources& resources, EditorInteractionMap const& interactionMap, std::optional<D2D1_POINT_2F> pointerPosition, std::optional<EditorTableAction> const& draggedAction, std::optional<std::size_t> dropIndex)
    {
        auto drawPlus = [&](D2D1_POINT_2F center)
        {
            resources.d2dContext->FillEllipse(D2D1::Ellipse(center, 9.0f, 9.0f), resources.accentBrush.Get());
            resources.d2dContext->DrawLine(D2D1::Point2F(center.x - 4.0f, center.y), D2D1::Point2F(center.x + 4.0f, center.y), resources.textBrush.Get(), 1.5f);
            resources.d2dContext->DrawLine(D2D1::Point2F(center.x, center.y - 4.0f), D2D1::Point2F(center.x, center.y + 4.0f), resources.textBrush.Get(), 1.5f);
        };
        auto drawRowControls = [&](EditorVisualTable const& table, std::size_t row)
        {
            auto centerY = (table.rowBoundaries[row] + table.rowBoundaries[row + 1]) * 0.5f;
            auto dragRect = D2D1::RectF(table.rect.left - 50.0f, centerY - 11.0f, table.rect.left - 28.0f, centerY + 11.0f);
            auto deleteRect = D2D1::RectF(table.rect.left - 25.0f, centerY - 11.0f, table.rect.left - 3.0f, centerY + 11.0f);
            resources.d2dContext->FillRectangle(dragRect, resources.panelBrush.Get());
            resources.d2dContext->FillRectangle(deleteRect, resources.panelBrush.Get());
            resources.d2dContext->DrawRectangle(dragRect, resources.mutedBrush.Get(), 1.0f);
            resources.d2dContext->DrawRectangle(deleteRect, resources.mutedBrush.Get(), 1.0f);
            for (int index = -1; index <= 1; ++index)
            {
                auto lineY = centerY + static_cast<float>(index * 4);
                resources.d2dContext->DrawLine(D2D1::Point2F(dragRect.left + 6.0f, lineY), D2D1::Point2F(dragRect.right - 6.0f, lineY), resources.mutedBrush.Get(), 1.5f);
            }
            resources.d2dContext->DrawLine(D2D1::Point2F(deleteRect.left + 7.0f, centerY - 4.0f), D2D1::Point2F(deleteRect.right - 7.0f, centerY + 4.0f), resources.accentBrush.Get(), 1.5f);
            resources.d2dContext->DrawLine(D2D1::Point2F(deleteRect.right - 7.0f, centerY - 4.0f), D2D1::Point2F(deleteRect.left + 7.0f, centerY + 4.0f), resources.accentBrush.Get(), 1.5f);
        };
        auto drawColumnControls = [&](EditorVisualTable const& table, std::size_t column)
        {
            auto centerX = (table.columnBoundaries[column] + table.columnBoundaries[column + 1]) * 0.5f;
            auto dragRect = D2D1::RectF(centerX - 23.0f, table.rect.top - 29.0f, centerX - 1.0f, table.rect.top - 7.0f);
            auto deleteRect = D2D1::RectF(centerX + 2.0f, table.rect.top - 29.0f, centerX + 24.0f, table.rect.top - 7.0f);
            resources.d2dContext->FillRectangle(dragRect, resources.panelBrush.Get());
            resources.d2dContext->FillRectangle(deleteRect, resources.panelBrush.Get());
            resources.d2dContext->DrawRectangle(dragRect, resources.mutedBrush.Get(), 1.0f);
            resources.d2dContext->DrawRectangle(deleteRect, resources.mutedBrush.Get(), 1.0f);
            for (int index = -1; index <= 1; ++index)
            {
                auto lineY = (dragRect.top + dragRect.bottom) * 0.5f + static_cast<float>(index * 4);
                resources.d2dContext->DrawLine(D2D1::Point2F(dragRect.left + 6.0f, lineY), D2D1::Point2F(dragRect.right - 6.0f, lineY), resources.mutedBrush.Get(), 1.5f);
            }
            auto centerY = (deleteRect.top + deleteRect.bottom) * 0.5f;
            resources.d2dContext->DrawLine(D2D1::Point2F(deleteRect.left + 7.0f, centerY - 4.0f), D2D1::Point2F(deleteRect.right - 7.0f, centerY + 4.0f), resources.accentBrush.Get(), 1.5f);
            resources.d2dContext->DrawLine(D2D1::Point2F(deleteRect.right - 7.0f, centerY - 4.0f), D2D1::Point2F(deleteRect.left + 7.0f, centerY + 4.0f), resources.accentBrush.Get(), 1.5f);
        };
        if (pointerPosition)
        {
            for (auto const& table : interactionMap.tables)
            {
                if (!table.editable) continue;
                auto pointer = *pointerPosition;
                if (table.rect.top <= pointer.y && pointer.y <= table.rect.bottom && table.rect.left - 56.0f <= pointer.x && pointer.x <= table.rect.left + 8.0f)
                {
                    for (std::size_t row = 0; row < table.rowCount; ++row)
                    {
                        if (table.rowBoundaries[row] <= pointer.y && pointer.y <= table.rowBoundaries[row + 1]) drawRowControls(table, row);
                    }
                }
                if (table.rect.left <= pointer.x && pointer.x <= table.rect.right && table.rect.top - 35.0f <= pointer.y && pointer.y <= table.rect.top + 8.0f)
                {
                    for (std::size_t column = 0; column < table.columnCount; ++column)
                    {
                        if (table.columnBoundaries[column] <= pointer.x && pointer.x <= table.columnBoundaries[column + 1]) drawColumnControls(table, column);
                    }
                }
                if (table.rect.left <= pointer.x && pointer.x <= table.rect.right)
                {
                    std::size_t column = table.columnCount - 1;
                    for (std::size_t index = 0; index < table.columnCount; ++index)
                    {
                        if (table.columnBoundaries[index] <= pointer.x && pointer.x <= table.columnBoundaries[index + 1]) { column = index; break; }
                    }
                    for (auto boundary : table.rowBoundaries)
                    {
                        if (std::fabs(pointer.y - boundary) <= 10.0f)
                        {
                            auto left = table.columnBoundaries[column];
                            auto right = table.columnBoundaries[column + 1];
                            resources.d2dContext->DrawLine(D2D1::Point2F(left, boundary), D2D1::Point2F(right, boundary), resources.accentBrush.Get(), 2.0f);
                            drawPlus(D2D1::Point2F((left + right) * 0.5f, boundary));
                        }
                    }
                }
                if (table.rect.top <= pointer.y && pointer.y <= table.rect.bottom)
                {
                    std::size_t row = table.rowCount - 1;
                    for (std::size_t index = 0; index < table.rowCount; ++index)
                    {
                        if (table.rowBoundaries[index] <= pointer.y && pointer.y <= table.rowBoundaries[index + 1]) { row = index; break; }
                    }
                    for (auto boundary : table.columnBoundaries)
                    {
                        if (std::fabs(pointer.x - boundary) <= 10.0f)
                        {
                            auto top = table.rowBoundaries[row];
                            auto bottom = table.rowBoundaries[row + 1];
                            resources.d2dContext->DrawLine(D2D1::Point2F(boundary, top), D2D1::Point2F(boundary, bottom), resources.accentBrush.Get(), 2.0f);
                            drawPlus(D2D1::Point2F(boundary, (top + bottom) * 0.5f));
                        }
                    }
                }
            }
        }
        if (!draggedAction || !dropIndex) return;
        for (auto const& table : interactionMap.tables)
        {
            auto belongs = std::any_of(table.sourceSpans.begin(), table.sourceSpans.end(), [&](auto const& span) {
                return span.container_id == draggedAction->sourcePosition.container_id;
            });
            if (!belongs) continue;
            if (draggedAction->kind == EditorTableActionKind::DragRow && *dropIndex < table.rowBoundaries.size())
            {
                auto boundary = table.rowBoundaries[*dropIndex];
                resources.d2dContext->DrawLine(D2D1::Point2F(table.rect.left, boundary), D2D1::Point2F(table.rect.right, boundary), resources.accentBrush.Get(), 3.0f);
            }
            if (draggedAction->kind == EditorTableActionKind::DragColumn && *dropIndex < table.columnBoundaries.size())
            {
                auto boundary = table.columnBoundaries[*dropIndex];
                resources.d2dContext->DrawLine(D2D1::Point2F(boundary, table.rect.top), D2D1::Point2F(boundary, table.rect.bottom), resources.accentBrush.Get(), 3.0f);
            }
        }
    }

    std::optional<EditorTableAction> EditorTableInteraction::ActionAt(EditorInteractionMap const& interactionMap, float x, float y)
    {
        for (auto const& table : interactionMap.tables)
        {
            if (!table.editable) continue;
            auto sourceForRow = [&](std::size_t row)
            {
                auto index = (std::min)(row, table.rowCount - 1) * table.columnCount;
                auto span = index < table.cells.size() ? table.cells[index].sourceSpan : table.sourceSpans.front();
                return elmd::TextPosition{span.container_id, span.source_range.start, elmd::TextAffinity::Downstream};
            };
            auto sourceForColumn = [&](std::size_t column)
            {
                auto index = (std::min)(column, table.columnCount - 1);
                auto span = index < table.cells.size() ? table.cells[index].sourceSpan : table.sourceSpans.front();
                return elmd::TextPosition{span.container_id, span.source_range.start, elmd::TextAffinity::Downstream};
            };
            for (std::size_t boundary = 0; boundary < table.rowBoundaries.size(); ++boundary)
            {
                for (std::size_t column = 0; column < table.columnCount; ++column)
                {
                    auto centerX = (table.columnBoundaries[column] + table.columnBoundaries[column + 1]) * 0.5f;
                    auto centerY = table.rowBoundaries[boundary];
                    auto dx = x - centerX;
                    auto dy = y - centerY;
                    if (dx * dx + dy * dy <= 81.0f)
                    {
                        auto source = sourceForRow(boundary == table.rowCount ? table.rowCount - 1 : boundary);
                        return EditorTableAction{ EditorTableActionKind::InsertRow, source, boundary };
                    }
                }
            }
            for (std::size_t boundary = 0; boundary < table.columnBoundaries.size(); ++boundary)
            {
                for (std::size_t row = 0; row < table.rowCount; ++row)
                {
                    auto centerX = table.columnBoundaries[boundary];
                    auto centerY = (table.rowBoundaries[row] + table.rowBoundaries[row + 1]) * 0.5f;
                    auto dx = x - centerX;
                    auto dy = y - centerY;
                    if (dx * dx + dy * dy <= 81.0f)
                    {
                        auto source = sourceForColumn(boundary == table.columnCount ? table.columnCount - 1 : boundary);
                        return EditorTableAction{ EditorTableActionKind::InsertColumn, source, boundary };
                    }
                }
            }
            if (table.rect.left - 52.0f <= x && x <= table.rect.left - 3.0f && table.rect.top <= y && y <= table.rect.bottom)
            {
                for (std::size_t row = 0; row < table.rowCount; ++row)
                {
                    if (table.rowBoundaries[row] > y || y > table.rowBoundaries[row + 1]) continue;
                    auto source = sourceForRow(row);
                    if (x <= table.rect.left - 28.0f) return EditorTableAction{ EditorTableActionKind::DragRow, source, row };
                    return EditorTableAction{ EditorTableActionKind::DeleteRow, source, row };
                }
            }
            if (table.rect.top - 29.0f <= y && y <= table.rect.top - 7.0f && table.rect.left <= x && x <= table.rect.right)
            {
                for (std::size_t column = 0; column < table.columnCount; ++column)
                {
                    auto center = (table.columnBoundaries[column] + table.columnBoundaries[column + 1]) * 0.5f;
                    if (center - 23.0f <= x && x <= center - 1.0f) return EditorTableAction{ EditorTableActionKind::DragColumn, sourceForColumn(column), column };
                    if (center + 2.0f <= x && x <= center + 24.0f) return EditorTableAction{ EditorTableActionKind::DeleteColumn, sourceForColumn(column), column };
                }
            }
        }
        return std::nullopt;
    }

    std::optional<std::size_t> EditorTableInteraction::DropIndexAt(EditorInteractionMap const& interactionMap, std::optional<EditorTableAction> const& draggedAction, float x, float y, bool rows)
    {
        for (auto const& table : interactionMap.tables)
        {
            if (!table.editable) continue;
            if (draggedAction && !std::any_of(table.sourceSpans.begin(), table.sourceSpans.end(), [&](auto const& span) {
                return span.container_id == draggedAction->sourcePosition.container_id;
            })) continue;
            if (rows)
            {
                if (x < table.rect.left - 60.0f || x > table.rect.right + 20.0f) continue;
                if (y <= table.rect.top) return 0;
                if (y >= table.rect.bottom) return table.rowCount;
                std::size_t nearest = 0;
                float distance = (std::numeric_limits<float>::max)();
                for (std::size_t index = 0; index < table.rowBoundaries.size(); ++index)
                {
                    auto candidate = std::fabs(y - table.rowBoundaries[index]);
                    if (candidate < distance) { distance = candidate; nearest = index; }
                }
                return nearest;
            }
            if (y < table.rect.top - 40.0f || y > table.rect.bottom + 20.0f) continue;
            if (x <= table.rect.left) return 0;
            if (x >= table.rect.right) return table.columnCount;
            std::size_t nearest = 0;
            float distance = (std::numeric_limits<float>::max)();
            for (std::size_t index = 0; index < table.columnBoundaries.size(); ++index)
            {
                auto candidate = std::fabs(x - table.columnBoundaries[index]);
                if (candidate < distance) { distance = candidate; nearest = index; }
            }
            return nearest;
        }
        return std::nullopt;
    }
}
