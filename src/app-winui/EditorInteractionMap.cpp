#include "pch.h"
#include "EditorInteractionMap.h"

namespace winrt::ElMd
{
    namespace
    {
        bool Contains(elmd::TextSpan const& span, elmd::TextPosition position)
        {
            return span.container_id == position.container_id && span.source_range.covers(position.source_offset);
        }

        std::size_t DisplayPositionForSource(
            std::vector<elmd::TextPosition> const& mapping,
            elmd::TextPosition position)
        {
            if (mapping.empty()) return 0;
            std::optional<std::size_t> lastInContainer;
            for (std::size_t index = 0; index < mapping.size(); ++index)
            {
                if (mapping[index].container_id != position.container_id) continue;
                lastInContainer = index;
                if (mapping[index].source_offset >= position.source_offset) return index;
            }
            return lastInContainer.value_or(0);
        }

        elmd::TextSpan SpanFromMapping(
            std::vector<elmd::TextPosition> const& mapping,
            std::size_t start,
            std::size_t end,
            elmd::TextSpan fallback)
        {
            if (mapping.empty()) return fallback;
            start = (std::min)(start, mapping.size() - 1);
            end = (std::min)(end, mapping.size() - 1);
            auto const& first = mapping[start];
            auto const& last = mapping[end];
            if (first.container_id != last.container_id) return {first.container_id, {first.source_offset, first.source_offset}};
            return {first.container_id, {(std::min)(first.source_offset, last.source_offset), (std::max)(first.source_offset, last.source_offset)}};
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
        if (block.layout->GetLineMetrics(nullptr, 0, &lineCount) != E_NOT_SUFFICIENT_BUFFER || lineCount == 0) return;
        std::vector<DWRITE_LINE_METRICS> metrics(lineCount);
        if (FAILED(block.layout->GetLineMetrics(metrics.data(), lineCount, &lineCount))) return;
        UINT32 textPosition = 0;
        UINT32 previousNewlineLength = 0;
        float lineTop = block.textOrigin.y;
        for (UINT32 lineIndex = 0; lineIndex < lineCount; ++lineIndex)
        {
            auto const& metric = metrics[lineIndex];
            auto lineEnd = textPosition + metric.length;
            auto visibleEnd = lineEnd >= metric.newlineLength ? lineEnd - metric.newlineLength : lineEnd;
            EditorVisualLine line;
            line.blockIndex = blockIndex;
            line.sourceSpan = SpanFromMapping(block.displayToSource, textPosition, visibleEnd, block.sourceSpan);
            line.displayStart = textPosition;
            line.displayEnd = visibleEnd;
            line.wrapContinuation = lineIndex > 0 && previousNewlineLength == 0;
            line.rect = D2D1::RectF(block.textOrigin.x, lineTop, block.textOrigin.x + block.textWidth, lineTop + metric.height);
            lines.push_back(line);
            textPosition = lineEnd;
            lineTop += metric.height;
            previousNewlineLength = metric.newlineLength;
        }
    }

    void EditorInteractionMap::AddTableCellLines(std::size_t blockIndex, std::size_t tableIndex, std::size_t cellIndex)
    {
        if (blockIndex >= blocks.size() || tableIndex >= tables.size() || cellIndex >= tables[tableIndex].cells.size()) return;
        auto const& cell = tables[tableIndex].cells[cellIndex];
        auto before = lines.size();
        if (cell.layout && !cell.displayToSource.empty())
        {
            UINT32 lineCount = 0;
            if (cell.layout->GetLineMetrics(nullptr, 0, &lineCount) == E_NOT_SUFFICIENT_BUFFER && lineCount > 0)
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
                        auto lineEnd = textPosition + metric.length;
                        auto visibleEnd = lineEnd >= metric.newlineLength ? lineEnd - metric.newlineLength : lineEnd;
                        EditorVisualLine line;
                        line.blockIndex = blockIndex;
                        line.tableIndex = tableIndex;
                        line.cellIndex = cellIndex;
                        line.sourceSpan = SpanFromMapping(cell.displayToSource, textPosition, visibleEnd, cell.sourceSpan);
                        line.displayStart = textPosition;
                        line.displayEnd = visibleEnd;
                        line.wrapContinuation = lineIndex > 0 && previousNewlineLength == 0;
                        line.rect = D2D1::RectF(cell.rect.left, lineTop, cell.rect.right, lineTop + metric.height);
                        lines.push_back(line);
                        textPosition = lineEnd;
                        lineTop += metric.height;
                        previousNewlineLength = metric.newlineLength;
                    }
                }
            }
        }
        if (lines.size() != before) return;
        EditorVisualLine line;
        line.blockIndex = blockIndex;
        line.tableIndex = tableIndex;
        line.cellIndex = cellIndex;
        line.sourceSpan = cell.sourceSpan;
        line.displayEnd = static_cast<std::uint32_t>(cell.text.size());
        line.rect = cell.rect;
        lines.push_back(line);
    }

    std::optional<std::size_t> EditorInteractionMap::LineIndexFor(elmd::TextPosition position, bool upstream) const
    {
        std::optional<std::size_t> first;
        std::optional<std::size_t> last;
        for (std::size_t index = 0; index < lines.size(); ++index)
        {
            if (!Contains(lines[index].sourceSpan, position)) continue;
            if (!first) first = index;
            last = index;
        }
        if (!first) return std::nullopt;
        return upstream ? first : last;
    }

    std::optional<D2D1_RECT_F> EditorInteractionMap::CaretRectOnLine(
        EditorVisualLine const& line,
        elmd::TextPosition position,
        bool upstream,
        float bodyLineHeight) const
    {
        if (line.blockIndex >= blocks.size() || line.sourceSpan.container_id != position.container_id) return std::nullopt;
        auto const& block = blocks[line.blockIndex];
        position.source_offset = (std::clamp)(position.source_offset, line.sourceSpan.source_range.start, line.sourceSpan.source_range.end);
        if (block.thematicBreak)
        {
            auto lineHeight = (std::min)(bodyLineHeight, (line.rect.bottom - line.rect.top) * 0.46f);
            auto top = position.source_offset <= line.sourceSpan.source_range.start ? line.rect.top : line.rect.bottom - lineHeight;
            return D2D1::RectF(line.rect.left, top, line.rect.left + 2.0f, top + lineHeight);
        }
        IDWriteTextLayout* layout = block.layout.Get();
        auto textOrigin = block.textOrigin;
        auto const* mapping = &block.displayToSource;
        if (line.tableIndex < tables.size() && line.cellIndex < tables[line.tableIndex].cells.size())
        {
            auto const& cell = tables[line.tableIndex].cells[line.cellIndex];
            layout = cell.layout.Get();
            textOrigin = cell.textOrigin;
            mapping = &cell.displayToSource;
        }
        if (!layout) return D2D1::RectF(line.rect.left, line.rect.top, line.rect.left + 2.0f, line.rect.bottom);
        auto displayPos = DisplayPositionForSource(*mapping, position);
        displayPos = (std::clamp)(displayPos, static_cast<std::size_t>(line.displayStart), static_cast<std::size_t>(line.displayEnd));
        UINT32 hitPos = static_cast<UINT32>(displayPos);
        BOOL trailing = FALSE;
        if (displayPos == line.displayEnd && upstream && displayPos > line.displayStart) { --hitPos; trailing = TRUE; }
        FLOAT x = 0.0f, y = 0.0f;
        DWRITE_HIT_TEST_METRICS metrics{};
        if (FAILED(layout->HitTestTextPosition(hitPos, trailing, &x, &y, &metrics))) return std::nullopt;
        auto left = textOrigin.x + x;
        return D2D1::RectF(left, line.rect.top, left + 2.0f, line.rect.bottom);
    }

    std::optional<D2D1_RECT_F> EditorInteractionMap::CaretBounds(elmd::TextPosition position, bool upstream, float bodyLineHeight) const
    {
        auto line = LineIndexFor(position, upstream);
        return line ? CaretRectOnLine(lines[*line], position, upstream, bodyLineHeight) : std::nullopt;
    }

    std::optional<elmd::TextPosition> EditorInteractionMap::VisualLineStart(elmd::TextPosition position, bool upstream) const
    {
        auto line = LineIndexFor(position, upstream);
        if (!line) return std::nullopt;
        return elmd::TextPosition{lines[*line].sourceSpan.container_id, lines[*line].sourceSpan.source_range.start, elmd::TextAffinity::Downstream};
    }

    std::optional<elmd::TextPosition> EditorInteractionMap::VisualLineEnd(elmd::TextPosition position, bool upstream) const
    {
        auto line = LineIndexFor(position, upstream);
        if (!line) return std::nullopt;
        return elmd::TextPosition{lines[*line].sourceSpan.container_id, lines[*line].sourceSpan.source_range.end, elmd::TextAffinity::Upstream};
    }

    std::optional<elmd::TextPosition> EditorInteractionMap::HitTest(float x, float y, bool* outUpstream) const
    {
        if (outUpstream) *outUpstream = false;
        for (auto hit = mathHits.rbegin(); hit != mathHits.rend(); ++hit)
        {
            if (x < hit->rect.left || x > hit->rect.right || y < hit->rect.top || y > hit->rect.bottom) continue;
            auto width = (std::max)(1.0f, hit->rect.right - hit->rect.left);
            auto local = (std::clamp)((x - hit->rect.left) / width, 0.0f, 1.0f);
            auto progress = hit->progressStart + local * (hit->progressEnd - hit->progressStart);
            auto length = hit->sourceSpan.source_range.length();
            auto offset = hit->sourceSpan.source_range.start + static_cast<std::size_t>(std::llround(progress * static_cast<float>(length)));
            return elmd::TextPosition{hit->sourceSpan.container_id, (std::min)(offset, hit->sourceSpan.source_range.end), elmd::TextAffinity::Downstream};
        }
        if (lines.empty()) return std::nullopt;
        std::size_t best = 0;
        float bestDistance = (std::numeric_limits<float>::max)();
        for (std::size_t index = 0; index < lines.size(); ++index)
        {
            auto const& rect = lines[index].rect;
            auto distance = y < rect.top ? rect.top - y : y > rect.bottom ? y - rect.bottom : 0.0f;
            if (lines[index].tableIndex < tables.size())
            {
                if (x < rect.left) distance += rect.left - x;
                else if (x > rect.right) distance += x - rect.right;
            }
            if (distance < bestDistance) { bestDistance = distance; best = index; }
            if (distance == 0.0f) break;
        }
        auto const& line = lines[best];
        if (line.blockIndex >= blocks.size()) return std::nullopt;
        auto const& block = blocks[line.blockIndex];
        auto start = elmd::TextPosition{line.sourceSpan.container_id, line.sourceSpan.source_range.start, elmd::TextAffinity::Downstream};
        auto end = elmd::TextPosition{line.sourceSpan.container_id, line.sourceSpan.source_range.end, elmd::TextAffinity::Upstream};
        if (block.thematicBreak) return y < (block.rect.top + block.rect.bottom) * 0.5f ? start : end;
        IDWriteTextLayout* layout = block.layout.Get();
        auto textOrigin = block.textOrigin;
        auto const* mapping = &block.displayToSource;
        if (line.tableIndex < tables.size() && line.cellIndex < tables[line.tableIndex].cells.size())
        {
            auto const& cell = tables[line.tableIndex].cells[line.cellIndex];
            layout = cell.layout.Get();
            textOrigin = cell.textOrigin;
            mapping = &cell.displayToSource;
        }
        if (!layout) return start;
        BOOL trailing = FALSE, inside = FALSE;
        DWRITE_HIT_TEST_METRICS metrics{};
        auto localY = (line.rect.top + line.rect.bottom) * 0.5f - textOrigin.y;
        if (FAILED(layout->HitTestPoint(x - textOrigin.x, localY, &trailing, &inside, &metrics))) return start;
        auto displayPos = static_cast<std::size_t>(metrics.textPosition + (trailing ? metrics.length : 0));
        displayPos = (std::clamp)(displayPos, static_cast<std::size_t>(line.displayStart), static_cast<std::size_t>(line.displayEnd));
        auto position = displayPos < mapping->size() ? (*mapping)[displayPos] : end;
        position.source_offset = (std::clamp)(position.source_offset, line.sourceSpan.source_range.start, line.sourceSpan.source_range.end);
        if (outUpstream)
        {
            auto nextWrap = best + 1 < lines.size() && lines[best + 1].blockIndex == line.blockIndex
                && lines[best + 1].wrapContinuation && lines[best + 1].displayStart == line.displayEnd;
            *outUpstream = position.source_offset == line.sourceSpan.source_range.end && nextWrap;
        }
        return position;
    }

    std::optional<EditorCaretMove> EditorInteractionMap::MoveCaretVertically(
        elmd::TextPosition position,
        bool upstream,
        bool down,
        float& goalX,
        float bodyLineHeight) const
    {
        auto current = LineIndexFor(position, upstream);
        if (!current || lines.empty()) return std::nullopt;
        auto x = goalX;
        if (x < 0.0f)
        {
            auto rect = CaretRectOnLine(lines[*current], position, upstream, bodyLineHeight);
            x = rect ? rect->left : lines[*current].rect.left;
            goalX = x;
        }
        if (!down && *current == 0) return EditorCaretMove{{lines.front().sourceSpan.container_id, lines.front().sourceSpan.source_range.start, elmd::TextAffinity::Downstream}, false};
        if (down && *current + 1 >= lines.size()) return EditorCaretMove{{lines.back().sourceSpan.container_id, lines.back().sourceSpan.source_range.end, elmd::TextAffinity::Upstream}, true};
        auto targetIndex = down ? *current + 1 : *current - 1;
        auto const& currentLine = lines[*current];
        if (currentLine.tableIndex < tables.size() && currentLine.cellIndex < tables[currentLine.tableIndex].cells.size())
        {
            auto const& table = tables[currentLine.tableIndex];
            auto const& cell = table.cells[currentLine.cellIndex];
            auto sameCell = lines[targetIndex].tableIndex == currentLine.tableIndex && lines[targetIndex].cellIndex == currentLine.cellIndex;
            if (!sameCell && down && cell.row + 1 < table.rowCount)
            {
                auto targetCell = (cell.row + 1) * table.columnCount + cell.column;
                for (std::size_t i = 0; i < lines.size(); ++i) if (lines[i].tableIndex == currentLine.tableIndex && lines[i].cellIndex == targetCell) { targetIndex = i; break; }
            }
            else if (!sameCell && !down && cell.row > 0)
            {
                auto targetCell = (cell.row - 1) * table.columnCount + cell.column;
                for (std::size_t i = lines.size(); i > 0; --i) if (lines[i - 1].tableIndex == currentLine.tableIndex && lines[i - 1].cellIndex == targetCell) { targetIndex = i - 1; break; }
            }
        }
        auto const& target = lines[targetIndex];
        auto targetPosition = elmd::TextPosition{target.sourceSpan.container_id, target.sourceSpan.source_range.start, elmd::TextAffinity::Downstream};
        auto const& block = blocks[target.blockIndex];
        IDWriteTextLayout* layout = block.layout.Get();
        auto textOrigin = block.textOrigin;
        auto const* mapping = &block.displayToSource;
        if (target.tableIndex < tables.size() && target.cellIndex < tables[target.tableIndex].cells.size())
        {
            auto const& cell = tables[target.tableIndex].cells[target.cellIndex];
            layout = cell.layout.Get();
            textOrigin = cell.textOrigin;
            mapping = &cell.displayToSource;
        }
        if (layout)
        {
            BOOL trailing = FALSE, inside = FALSE;
            DWRITE_HIT_TEST_METRICS metrics{};
            auto targetX = (std::clamp)(x, target.rect.left, target.rect.right - 1.0f);
            if (SUCCEEDED(layout->HitTestPoint(targetX - textOrigin.x, (target.rect.top + target.rect.bottom) * 0.5f - textOrigin.y, &trailing, &inside, &metrics)))
            {
                auto displayPos = static_cast<std::size_t>(metrics.textPosition + (trailing ? metrics.length : 0));
                displayPos = (std::clamp)(displayPos, static_cast<std::size_t>(target.displayStart), static_cast<std::size_t>(target.displayEnd));
                if (displayPos < mapping->size()) targetPosition = (*mapping)[displayPos];
            }
        }
        targetPosition.source_offset = (std::clamp)(targetPosition.source_offset, target.sourceSpan.source_range.start, target.sourceSpan.source_range.end);
        auto downstreamLine = LineIndexFor(targetPosition, false);
        auto upstreamLine = LineIndexFor(targetPosition, true);
        auto newUpstream = (!downstreamLine || *downstreamLine != targetIndex) && upstreamLine && *upstreamLine == targetIndex;
        targetPosition.affinity = newUpstream ? elmd::TextAffinity::Upstream : elmd::TextAffinity::Downstream;
        return EditorCaretMove{targetPosition, newUpstream};
    }
}
