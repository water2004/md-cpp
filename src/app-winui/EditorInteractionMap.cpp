#include "pch.h"
#include "EditorInteractionMap.h"

namespace winrt::ElMd
{
    namespace
    {
        std::size_t DisplayPositionForSource(std::vector<std::size_t> const& displayToSource, std::size_t sourceOffset)
        {
            if (displayToSource.empty()) return 0;
            auto it = std::lower_bound(displayToSource.begin(), displayToSource.end(), sourceOffset);
            if (it == displayToSource.end()) return displayToSource.size() - 1;
            return static_cast<std::size_t>(it - displayToSource.begin());
        }
    }

    void EditorInteractionMap::Clear(std::size_t blockCapacity)
    {
        blocks.clear();
        lines.clear();
        tables.clear();
        mathHits.clear();
        blocks.reserve(blockCapacity);
    }

    void EditorInteractionMap::AddBlockLines(std::size_t blockIndex)
    {
        if (blockIndex >= blocks.size()) return;
        auto const& block = blocks[blockIndex];
        if (!block.layout || block.displayToSource.empty()) return;
        UINT32 lineCount = 0;
        auto result = block.layout->GetLineMetrics(nullptr, 0, &lineCount);
        if (result != E_NOT_SUFFICIENT_BUFFER || lineCount == 0) return;
        std::vector<DWRITE_LINE_METRICS> metrics(lineCount);
        if (FAILED(block.layout->GetLineMetrics(metrics.data(), lineCount, &lineCount))) return;
        UINT32 textPosition = 0;
        UINT32 previousNewlineLength = 0;
        float lineTop = block.textOrigin.y;
        for (UINT32 lineIndex = 0; lineIndex < lineCount; ++lineIndex)
        {
            auto const& metric = metrics[lineIndex];
            auto lineEndPosition = textPosition + metric.length;
            auto visibleEndPosition = lineEndPosition >= metric.newlineLength ? lineEndPosition - metric.newlineLength : lineEndPosition;
            auto startIndex = (std::min)(static_cast<std::size_t>(textPosition), block.displayToSource.size() - 1);
            auto endIndex = (std::min)(static_cast<std::size_t>(visibleEndPosition), block.displayToSource.size() - 1);
            EditorVisualLine line;
            line.blockIndex = blockIndex;
            line.sourceStart = block.displayToSource[startIndex];
            line.sourceEnd = block.displayToSource[endIndex];
            line.displayStart = textPosition;
            line.displayEnd = visibleEndPosition;
            line.wrapContinuation = lineIndex > 0 && previousNewlineLength == 0;
            line.rect = D2D1::RectF(block.textOrigin.x, lineTop, block.textOrigin.x + block.textWidth, lineTop + metric.height);
            lines.push_back(std::move(line));
            textPosition = lineEndPosition;
            lineTop += metric.height;
            previousNewlineLength = metric.newlineLength;
        }
    }

    void EditorInteractionMap::AddTableCellLines(std::size_t blockIndex, std::size_t tableIndex, std::size_t cellIndex)
    {
        if (blockIndex >= blocks.size() || tableIndex >= tables.size() || cellIndex >= tables[tableIndex].cells.size()) return;
        auto const& cell = tables[tableIndex].cells[cellIndex];
        auto initialCount = lines.size();
        if (cell.layout && !cell.displayToSource.empty())
        {
            UINT32 lineCount = 0;
            auto result = cell.layout->GetLineMetrics(nullptr, 0, &lineCount);
            if (result == E_NOT_SUFFICIENT_BUFFER && lineCount > 0)
            {
                std::vector<DWRITE_LINE_METRICS> metrics(lineCount);
                if (SUCCEEDED(cell.layout->GetLineMetrics(metrics.data(), lineCount, &lineCount)))
                {
                    UINT32 textPosition = 0;
                    UINT32 previousNewlineLength = 0;
                    float lineTop = cell.textOrigin.y;
                    for (UINT32 lineIndex = 0; lineIndex < lineCount; ++lineIndex)
                    {
                        auto const& metric = metrics[lineIndex];
                        auto lineEndPosition = textPosition + metric.length;
                        auto visibleEndPosition = lineEndPosition >= metric.newlineLength ? lineEndPosition - metric.newlineLength : lineEndPosition;
                        auto startIndex = (std::min)(static_cast<std::size_t>(textPosition), cell.displayToSource.size() - 1);
                        auto endIndex = (std::min)(static_cast<std::size_t>(visibleEndPosition), cell.displayToSource.size() - 1);
                        EditorVisualLine line;
                        line.blockIndex = blockIndex;
                        line.tableIndex = tableIndex;
                        line.cellIndex = cellIndex;
                        line.sourceStart = cell.displayToSource[startIndex];
                        line.sourceEnd = cell.displayToSource[endIndex];
                        line.displayStart = textPosition;
                        line.displayEnd = visibleEndPosition;
                        line.wrapContinuation = lineIndex > 0 && previousNewlineLength == 0;
                        line.rect = D2D1::RectF(cell.rect.left, lineTop, cell.rect.right, lineTop + metric.height);
                        lines.push_back(std::move(line));
                        textPosition = lineEndPosition;
                        lineTop += metric.height;
                        previousNewlineLength = metric.newlineLength;
                    }
                }
            }
        }
        if (lines.size() != initialCount) return;
        EditorVisualLine line;
        line.blockIndex = blockIndex;
        line.tableIndex = tableIndex;
        line.cellIndex = cellIndex;
        line.sourceStart = cell.sourceStart;
        line.sourceEnd = cell.sourceEnd;
        line.displayEnd = static_cast<std::uint32_t>(cell.text.size());
        line.rect = cell.rect;
        lines.push_back(std::move(line));
    }

    std::optional<std::size_t> EditorInteractionMap::LineIndexFor(std::size_t sourceOffset, bool upstream) const
    {
        if (lines.empty()) return std::nullopt;
        std::optional<std::size_t> firstContaining;
        std::optional<std::size_t> lastContaining;
        for (std::size_t index = 0; index < lines.size(); ++index)
        {
            auto const& line = lines[index];
            if (line.sourceStart <= sourceOffset && sourceOffset <= line.sourceEnd)
            {
                if (!firstContaining) firstContaining = index;
                lastContaining = index;
            }
        }
        if (firstContaining) return upstream ? firstContaining : lastContaining;
        std::optional<std::size_t> prev;
        std::optional<std::size_t> next;
        for (std::size_t index = 0; index < lines.size(); ++index)
        {
            if (lines[index].sourceEnd <= sourceOffset) prev = index;
            if (!next && lines[index].sourceStart >= sourceOffset) next = index;
        }
        if (prev && next)
        {
            auto distPrev = sourceOffset - lines[*prev].sourceEnd;
            auto distNext = lines[*next].sourceStart - sourceOffset;
            if (distPrev == distNext) return upstream ? prev : next;
            return distPrev <= distNext ? prev : next;
        }
        if (prev) return prev;
        return next;
    }

    std::optional<D2D1_RECT_F> EditorInteractionMap::CaretRectOnLine(EditorVisualLine const& line, std::size_t sourceOffset, bool upstream, float bodyLineHeight) const
    {
        if (line.blockIndex >= blocks.size()) return std::nullopt;
        auto const& block = blocks[line.blockIndex];
        auto clamped = (std::min)((std::max)(sourceOffset, line.sourceStart), line.sourceEnd);
        if (block.thematicBreak)
        {
            auto lineHeight = (std::min)(bodyLineHeight, (line.rect.bottom - line.rect.top) * 0.46f);
            auto top = clamped <= line.sourceStart ? line.rect.top : line.rect.bottom - lineHeight;
            return D2D1::RectF(line.rect.left, top, line.rect.left + 2.0f, top + lineHeight);
        }
        IDWriteTextLayout* layout = block.layout.Get();
        D2D1_POINT_2F textOrigin = block.textOrigin;
        std::vector<std::size_t> const* displayToSource = &block.displayToSource;
        if (line.tableIndex < tables.size() && line.cellIndex < tables[line.tableIndex].cells.size())
        {
            auto const& cell = tables[line.tableIndex].cells[line.cellIndex];
            layout = cell.layout.Get();
            textOrigin = cell.textOrigin;
            displayToSource = &cell.displayToSource;
        }
        if (!layout) return D2D1::RectF(line.rect.left, line.rect.top, line.rect.left + 2.0f, line.rect.bottom);
        auto displayPos = DisplayPositionForSource(*displayToSource, clamped);
        displayPos = (std::min)((std::max)(displayPos, static_cast<std::size_t>(line.displayStart)), static_cast<std::size_t>(line.displayEnd));
        UINT32 hitPos = static_cast<UINT32>(displayPos);
        BOOL trailing = FALSE;
        if (displayPos == line.displayEnd && upstream && displayPos > line.displayStart)
        {
            hitPos = static_cast<UINT32>(displayPos - 1);
            trailing = TRUE;
        }
        FLOAT caretX = 0.0f;
        FLOAT caretY = 0.0f;
        DWRITE_HIT_TEST_METRICS metrics{};
        if (SUCCEEDED(layout->HitTestTextPosition(hitPos, trailing, &caretX, &caretY, &metrics)))
        {
            auto left = textOrigin.x + caretX;
            return D2D1::RectF(left, line.rect.top, left + 2.0f, line.rect.bottom);
        }
        return D2D1::RectF(line.rect.left, line.rect.top, line.rect.left + 2.0f, line.rect.bottom);
    }

    std::optional<D2D1_RECT_F> EditorInteractionMap::CaretBounds(std::size_t sourceOffset, bool upstream, float bodyLineHeight) const
    {
        auto index = LineIndexFor(sourceOffset, upstream);
        if (!index) return std::nullopt;
        return CaretRectOnLine(lines[*index], sourceOffset, upstream, bodyLineHeight);
    }

    std::optional<std::size_t> EditorInteractionMap::VisualLineStart(std::size_t sourceOffset, bool upstream) const
    {
        auto index = LineIndexFor(sourceOffset, upstream);
        if (!index) return std::nullopt;
        return lines[*index].sourceStart;
    }

    std::optional<std::size_t> EditorInteractionMap::VisualLineEnd(std::size_t sourceOffset, bool upstream) const
    {
        auto index = LineIndexFor(sourceOffset, upstream);
        if (!index) return std::nullopt;
        return lines[*index].sourceEnd;
    }

    std::optional<std::size_t> EditorInteractionMap::HitTest(float x, float y, bool* outUpstream) const
    {
        if (outUpstream) *outUpstream = false;
        for (auto hit = mathHits.rbegin(); hit != mathHits.rend(); ++hit)
        {
            if (x < hit->rect.left || x > hit->rect.right || y < hit->rect.top || y > hit->rect.bottom) continue;
            auto width = (std::max)(1.0f, hit->rect.right - hit->rect.left);
            auto local = (std::clamp)((x - hit->rect.left) / width, 0.0f, 1.0f);
            auto progress = hit->progressStart + local * (hit->progressEnd - hit->progressStart);
            auto length = hit->sourceEnd - hit->sourceStart;
            auto offset = hit->sourceStart + static_cast<std::size_t>(std::llround(progress * static_cast<float>(length)));
            return (std::min)(offset, hit->sourceEnd);
        }
        if (lines.empty()) return std::nullopt;
        std::size_t best = 0;
        float bestDist = (std::numeric_limits<float>::max)();
        for (std::size_t index = 0; index < lines.size(); ++index)
        {
            auto const& rect = lines[index].rect;
            float dist = y < rect.top ? rect.top - y : y > rect.bottom ? y - rect.bottom : 0.0f;
            if (lines[index].tableIndex < tables.size())
            {
                if (x < rect.left) dist += rect.left - x;
                else if (x > rect.right) dist += x - rect.right;
            }
            if (dist < bestDist) { bestDist = dist; best = index; }
            if (dist == 0.0f) break;
        }
        auto const& line = lines[best];
        if (line.blockIndex >= blocks.size()) return std::nullopt;
        auto const& block = blocks[line.blockIndex];
        if (block.thematicBreak)
        {
            auto midpoint = (block.rect.top + block.rect.bottom) * 0.5f;
            return y < midpoint ? line.sourceStart : line.sourceEnd;
        }
        IDWriteTextLayout* layout = block.layout.Get();
        D2D1_POINT_2F textOrigin = block.textOrigin;
        std::vector<std::size_t> const* displayToSource = &block.displayToSource;
        if (line.tableIndex < tables.size() && line.cellIndex < tables[line.tableIndex].cells.size())
        {
            auto const& cell = tables[line.tableIndex].cells[line.cellIndex];
            layout = cell.layout.Get();
            textOrigin = cell.textOrigin;
            displayToSource = &cell.displayToSource;
        }
        if (!layout) return line.sourceStart;
        BOOL isTrailingHit = FALSE;
        BOOL isInside = FALSE;
        DWRITE_HIT_TEST_METRICS metrics{};
        float localX = x - textOrigin.x;
        float localY = (line.rect.top + line.rect.bottom) * 0.5f - textOrigin.y;
        if (SUCCEEDED(layout->HitTestPoint(localX, localY, &isTrailingHit, &isInside, &metrics)))
        {
            auto displayPos = static_cast<std::size_t>(metrics.textPosition + (isTrailingHit ? metrics.length : 0));
            displayPos = (std::min)((std::max)(displayPos, static_cast<std::size_t>(line.displayStart)), static_cast<std::size_t>(line.displayEnd));
            std::size_t srcOff = displayPos < displayToSource->size() ? (*displayToSource)[displayPos] : line.sourceEnd;
            srcOff = (std::min)((std::max)(srcOff, line.sourceStart), line.sourceEnd);
            if (outUpstream)
            {
                bool nextIsWrap = best + 1 < lines.size() && lines[best + 1].blockIndex == line.blockIndex
                    && lines[best + 1].wrapContinuation && lines[best + 1].displayStart == line.displayEnd;
                *outUpstream = srcOff == line.sourceEnd && nextIsWrap;
            }
            return srcOff;
        }
        return line.sourceStart;
    }

    std::optional<EditorCaretMove> EditorInteractionMap::MoveCaretVertically(std::size_t sourceOffset, bool upstream, bool down, float& goalX, float bodyLineHeight) const
    {
        auto current = LineIndexFor(sourceOffset, upstream);
        if (!current || lines.empty()) return std::nullopt;
        float x = goalX;
        if (x < 0.0f)
        {
            if (auto rect = CaretRectOnLine(lines[*current], sourceOffset, upstream, bodyLineHeight)) x = rect->left;
            else x = lines[*current].rect.left;
            goalX = x;
        }
        if (!down && *current == 0) return EditorCaretMove{ lines.front().sourceStart, false };
        if (down && *current + 1 >= lines.size()) return EditorCaretMove{ lines.back().sourceEnd, true };
        std::size_t targetIndex = down ? *current + 1 : *current - 1;
        auto const& currentLine = lines[*current];
        if (currentLine.tableIndex < tables.size() && currentLine.cellIndex < tables[currentLine.tableIndex].cells.size())
        {
            auto const& table = tables[currentLine.tableIndex];
            auto const& cell = table.cells[currentLine.cellIndex];
            auto adjacentIsSameCell = down
                ? *current + 1 < lines.size() && lines[*current + 1].tableIndex == currentLine.tableIndex && lines[*current + 1].cellIndex == currentLine.cellIndex
                : *current > 0 && lines[*current - 1].tableIndex == currentLine.tableIndex && lines[*current - 1].cellIndex == currentLine.cellIndex;
            if (adjacentIsSameCell) targetIndex = down ? *current + 1 : *current - 1;
            else if (down && cell.row + 1 < table.rowCount)
            {
                auto targetCell = (cell.row + 1) * table.columnCount + cell.column;
                for (std::size_t index = 0; index < lines.size(); ++index)
                    if (lines[index].tableIndex == currentLine.tableIndex && lines[index].cellIndex == targetCell) { targetIndex = index; break; }
            }
            else if (!down && cell.row > 0)
            {
                auto targetCell = (cell.row - 1) * table.columnCount + cell.column;
                for (std::size_t index = lines.size(); index > 0; --index)
                    if (lines[index - 1].tableIndex == currentLine.tableIndex && lines[index - 1].cellIndex == targetCell) { targetIndex = index - 1; break; }
            }
            else if (down)
            {
                auto lastCell = table.rowCount * table.columnCount - 1;
                for (std::size_t index = *current; index < lines.size(); ++index)
                    if (lines[index].tableIndex == currentLine.tableIndex && lines[index].cellIndex == lastCell) { targetIndex = (std::min)(index + 1, lines.size() - 1); break; }
            }
            else
            {
                for (std::size_t index = *current; index > 0; --index)
                    if (lines[index - 1].tableIndex != currentLine.tableIndex) { targetIndex = index - 1; break; }
            }
        }
        auto const& target = lines[targetIndex];
        if (target.blockIndex >= blocks.size()) return std::nullopt;
        auto const& block = blocks[target.blockIndex];
        std::size_t srcOff = target.sourceStart;
        IDWriteTextLayout* layout = block.layout.Get();
        D2D1_POINT_2F textOrigin = block.textOrigin;
        std::vector<std::size_t> const* displayToSource = &block.displayToSource;
        if (target.tableIndex < tables.size() && target.cellIndex < tables[target.tableIndex].cells.size())
        {
            auto const& cell = tables[target.tableIndex].cells[target.cellIndex];
            layout = cell.layout.Get();
            textOrigin = cell.textOrigin;
            displayToSource = &cell.displayToSource;
        }
        if (layout)
        {
            float targetX = (std::min)((std::max)(x, target.rect.left), target.rect.right - 1.0f);
            BOOL isTrailingHit = FALSE;
            BOOL isInside = FALSE;
            DWRITE_HIT_TEST_METRICS metrics{};
            if (SUCCEEDED(layout->HitTestPoint(targetX - textOrigin.x, (target.rect.top + target.rect.bottom) * 0.5f - textOrigin.y, &isTrailingHit, &isInside, &metrics)))
            {
                auto displayPos = static_cast<std::size_t>(metrics.textPosition + (isTrailingHit ? metrics.length : 0));
                displayPos = (std::min)((std::max)(displayPos, static_cast<std::size_t>(target.displayStart)), static_cast<std::size_t>(target.displayEnd));
                srcOff = displayPos < displayToSource->size() ? (*displayToSource)[displayPos] : target.sourceEnd;
            }
        }
        srcOff = (std::min)((std::max)(srcOff, target.sourceStart), target.sourceEnd);
        bool newUpstream = false;
        auto downstreamLine = LineIndexFor(srcOff, false);
        auto upstreamLine = LineIndexFor(srcOff, true);
        if ((!downstreamLine || *downstreamLine != targetIndex) && upstreamLine && *upstreamLine == targetIndex) newUpstream = true;
        return EditorCaretMove{ srcOff, newUpstream };
    }
}
