#include "pch.h"
#include "EditorDocumentPainter.h"

import folia.core.callout;
import folia.core.utf;

namespace winrt::Folia
{
    EditorDocumentPainter::EditorDocumentPainter(
        EditorRenderResources& resources,
        EditorStyleSheet const& styleSheet,
        EditorInteractionMap& interactionMap,
        TreeSitterHighlighter& treeSitter,
        std::optional<D2D1_POINT_2F> pointerPosition,
        bool printMode,
        float documentRight,
        folia::TextSelection selection,
        std::span<const detail::EditorSearchHighlight> searchHighlights,
        std::unordered_map<std::uint64_t, std::size_t> const& editableIndex)
        : resources(resources),
          styleSheet(styleSheet),
          interactionMap(interactionMap),
          treeSitter(treeSitter),
          pointerPosition(pointerPosition),
          printMode(printMode),
          documentRight(documentRight),
          selection(selection),
          searchHighlights(searchHighlights),
          editableIndex(editableIndex),
          selectionStart(PositionLess(selection.active, selection.anchor) ? selection.active : selection.anchor),
          selectionEnd(PositionLess(selection.active, selection.anchor) ? selection.anchor : selection.active)
    {
    }

    bool EditorDocumentPainter::PositionLess(folia::TextPosition left, folia::TextPosition right) const
    {
        if (left.container_id == right.container_id) return left.source_offset < right.source_offset;
        auto leftOrder = editableIndex.find(left.container_id.v);
        auto rightOrder = editableIndex.find(right.container_id.v);
        if (leftOrder == editableIndex.end()) return false;
        if (rightOrder == editableIndex.end()) return true;
        return leftOrder->second < rightOrder->second;
    }

    bool EditorDocumentPainter::Selected(folia::TextPosition position) const
    {
        if (selection.is_caret()) return false;
        return !PositionLess(position, selectionStart) && PositionLess(position, selectionEnd);
    }

    void EditorDocumentPainter::DrawTaskCheckboxes(
        IDWriteTextLayout* layout,
        D2D1_POINT_2F origin,
        std::vector<DisplayInlineText::TaskCheckboxOverlay> const& overlays)
    {
        if (!layout) return;
        for (auto const& overlay : overlays)
        {
            FLOAT x = 0.0f;
            FLOAT lineY = 0.0f;
            DWRITE_HIT_TEST_METRICS metrics{};
            if (FAILED(layout->HitTestTextPosition(overlay.displayStart, FALSE, &x, &lineY, &metrics))) continue;
            auto objectRect = D2D1::RectF(
                origin.x + x,
                origin.y + metrics.top,
                origin.x + x + overlay.advance,
                origin.y + metrics.top + overlay.height);
            auto boxTop = objectRect.top + (overlay.height - overlay.boxSize) * 0.5f;
            auto boxRect = D2D1::RectF(
                objectRect.left + 0.5f,
                boxTop,
                objectRect.left + 0.5f + overlay.boxSize,
                boxTop + overlay.boxSize);
            auto hovered = !printMode && pointerPosition
                && pointerPosition->x >= objectRect.left && pointerPosition->x <= objectRect.right
                && pointerPosition->y >= objectRect.top && pointerPosition->y <= objectRect.bottom;
            auto rounded = D2D1::RoundedRect(boxRect, 3.0f, 3.0f);
            if (overlay.checked)
                resources.d2dContext->FillRoundedRectangle(rounded, resources.accentBrush.Get());
            else if (hovered)
                resources.d2dContext->FillRoundedRectangle(rounded, resources.selectionBrush.Get());
            resources.d2dContext->DrawRoundedRectangle(
                rounded,
                overlay.checked || hovered ? resources.accentBrush.Get() : resources.mutedBrush.Get(),
                hovered ? 2.0f : 1.5f);
            if (overlay.checked)
            {
                auto left = boxRect.left + overlay.boxSize * 0.23f;
                auto middleX = boxRect.left + overlay.boxSize * 0.43f;
                auto middleY = boxRect.top + overlay.boxSize * 0.70f;
                auto right = boxRect.right - overlay.boxSize * 0.18f;
                resources.d2dContext->DrawLine(
                    D2D1::Point2F(left, boxRect.top + overlay.boxSize * 0.52f),
                    D2D1::Point2F(middleX, middleY),
                    resources.canvasBrush.Get(),
                    2.0f);
                resources.d2dContext->DrawLine(
                    D2D1::Point2F(middleX, middleY),
                    D2D1::Point2F(right, boxRect.top + overlay.boxSize * 0.29f),
                    resources.canvasBrush.Get(),
                    2.0f);
            }
            interactionMap.taskCheckboxHits.push_back({objectRect, overlay.sourcePosition});
        }
    }

    void EditorDocumentPainter::RegisterFootnotes(
        IDWriteTextLayout* layout,
        D2D1_POINT_2F origin,
        std::vector<DisplayInlineText::FootnoteOverlay> const& overlays)
    {
        if (!layout) return;
        for (auto const& overlay : overlays)
        {
            UINT32 count = 0;
            auto result = layout->HitTestTextRange(
                overlay.displayStart,
                overlay.displayLength,
                origin.x,
                origin.y,
                nullptr,
                0,
                &count);
            if (result != E_NOT_SUFFICIENT_BUFFER || count == 0) continue;
            std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
            if (FAILED(layout->HitTestTextRange(
                    overlay.displayStart,
                    overlay.displayLength,
                    origin.x,
                    origin.y,
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
    }

    std::vector<EditorDocumentPainter::PositionedMath> EditorDocumentPainter::PositionMath(
        IDWriteTextLayout* layout,
        std::vector<DisplayInlineText::MathOverlay> const& overlays,
        float width)
    {
        std::vector<PositionedMath> positioned;
        if (!layout) return positioned;
        positioned.reserve(overlays.size());
        for (auto const& overlay : overlays)
        {
            FLOAT x = 0.0f;
            FLOAT lineY = 0.0f;
            DWRITE_HIT_TEST_METRICS metrics{};
            if (FAILED(layout->HitTestTextPosition(overlay.displayStart, FALSE, &x, &lineY, &metrics))) continue;
            positioned.push_back({&overlay, x + overlay.leadingSpace, metrics.top});
        }
        auto sameSpan = [](folia::TextSpan const& left, folia::TextSpan const& right)
        {
            return left.container_id == right.container_id
                && left.source_range.start == right.source_range.start
                && left.source_range.end == right.source_range.end;
        };
        std::vector<bool> centered(positioned.size(), false);
        for (std::size_t index = 0; index < positioned.size(); ++index)
        {
            auto const* overlay = positioned[index].overlay;
            if (!overlay || !overlay->displayMath || centered[index]) continue;
            auto left = positioned[index].localX;
            auto right = left + overlay->fragment.width;
            auto lineInset = positioned[index].localX - overlay->leadingSpace;
            std::vector<std::size_t> line{index};
            for (std::size_t other = index + 1; other < positioned.size(); ++other)
            {
                auto const* candidate = positioned[other].overlay;
                if (!candidate || !candidate->displayMath
                    || !sameSpan(candidate->sourceSpan, overlay->sourceSpan)
                    || std::fabs(positioned[other].localTop - positioned[index].localTop) > 0.5f) continue;
                left = (std::min)(left, positioned[other].localX);
                right = (std::max)(right, positioned[other].localX + candidate->fragment.width);
                lineInset = (std::min)(
                    lineInset,
                    positioned[other].localX - candidate->leadingSpace);
                line.push_back(other);
            }
            auto lineWidth = right - left;
            auto availableWidth = (std::max)(0.0f, width - lineInset);
            auto desiredLeft = lineWidth <= availableWidth
                ? lineInset + (availableWidth - lineWidth) * 0.5f
                : lineInset;
            auto shift = desiredLeft - left;
            for (auto member : line)
            {
                positioned[member].localX += shift;
                centered[member] = true;
            }
        }
        return positioned;
    }

    void EditorDocumentPainter::DrawSelection(
        IDWriteTextLayout* layout,
        D2D1_POINT_2F origin,
        EditorDisplayMapping const& mapping)
    {
        if (!layout || selection.is_caret() || mapping.empty()) return;
        std::size_t index = 0;
        while (index + 1 < mapping.size())
        {
            while (index + 1 < mapping.size() && !Selected(mapping[index])) ++index;
            auto start = index;
            while (index + 1 < mapping.size() && Selected(mapping[index])) ++index;
            if (index <= start) continue;
            UINT32 count = 0;
            auto length = static_cast<UINT32>(index - start);
            if (layout->HitTestTextRange(static_cast<UINT32>(start), length, origin.x, origin.y, nullptr, 0, &count) != E_NOT_SUFFICIENT_BUFFER || count == 0) continue;
            std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
            if (FAILED(layout->HitTestTextRange(static_cast<UINT32>(start), length, origin.x, origin.y, metrics.data(), count, &count))) continue;
            for (UINT32 metricIndex = 0; metricIndex < count; ++metricIndex)
            {
                auto const& metric = metrics[metricIndex];
                resources.d2dContext->FillRectangle(
                    D2D1::RectF(metric.left, metric.top, metric.left + metric.width, metric.top + metric.height),
                    resources.selectionBrush.Get());
            }
        }
    }

    void EditorDocumentPainter::DrawSearchHighlights(
        IDWriteTextLayout* layout,
        D2D1_POINT_2F origin,
        EditorDisplayMapping const& mapping)
    {
        if (!layout || mapping.empty() || searchHighlights.empty()) return;
        for (auto const& highlight : searchHighlights)
        {
            auto const& span = highlight.span;
            if (span.source_range.empty()) continue;
            std::size_t index = 0;
            while (index + 1 < mapping.size())
            {
                while (index + 1 < mapping.size()
                    && !(mapping[index].container_id == span.container_id
                        && span.source_range.contains(mapping[index].source_offset))) ++index;
                auto const start = index;
                while (index + 1 < mapping.size()
                    && mapping[index].container_id == span.container_id
                    && span.source_range.contains(mapping[index].source_offset)) ++index;
                if (index <= start) continue;
                UINT32 count = 0;
                auto const length = static_cast<UINT32>(index - start);
                if (layout->HitTestTextRange(
                        static_cast<UINT32>(start), length, origin.x, origin.y,
                        nullptr, 0, &count) != E_NOT_SUFFICIENT_BUFFER || count == 0) continue;
                std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
                if (FAILED(layout->HitTestTextRange(
                        static_cast<UINT32>(start), length, origin.x, origin.y,
                        metrics.data(), count, &count))) continue;
                for (UINT32 metricIndex = 0; metricIndex < count; ++metricIndex)
                {
                    auto const& metric = metrics[metricIndex];
                    auto const rect = D2D1::RectF(
                        metric.left,
                        metric.top,
                        metric.left + metric.width,
                        metric.top + metric.height);
                    resources.d2dContext->FillRectangle(rect, resources.selectionBrush.Get());
                    if (highlight.current)
                        resources.d2dContext->DrawRectangle(rect, resources.accentBrush.Get(), 1.5f);
                }
            }
        }
    }

    void EditorDocumentPainter::ApplyNestedCodeHighlights(
        DisplayInlineText& display,
        folia::RenderBlock const& parent)
    {
        auto apply = [&](auto& self, folia::RenderBlock const& block) -> void
        {
            auto const& special = block.special();
            if (block.kind == folia::RenderBlockKind::Code && special.language && !special.code_text.empty())
            {
                auto highlights = treeSitter.Highlight(*special.language, folia::cps_to_utf8(special.code_text));
                for (auto const& highlight : highlights)
                {
                    auto contentStart = (std::min)(static_cast<std::size_t>(highlight.start), special.code_text.size());
                    auto contentEnd = (std::min)(contentStart + static_cast<std::size_t>(highlight.length), special.code_text.size());
                    auto sourceStart = special.content_to_source.empty()
                        ? contentStart
                        : special.content_to_source[(std::min)(contentStart, special.content_to_source.size() - 1)];
                    auto sourceEnd = special.content_to_source.empty()
                        ? contentEnd
                        : special.content_to_source[(std::min)(contentEnd, special.content_to_source.size() - 1)];
                    auto start = DisplayPositionForSource(display.displayToSource, {block.id, sourceStart, folia::TextAffinity::Downstream});
                    auto end = DisplayPositionForSource(display.displayToSource, {block.id, sourceEnd, folia::TextAffinity::Downstream});
                    if (end <= start) continue;
                    folia::InlineStyle style = folia::InlineStyle::plain();
                    style.code = true;
                    display.ranges.push_back({
                        static_cast<UINT32>(start),
                        static_cast<UINT32>(end - start),
                        style,
                        false,
                        highlight.kind,
                    });
                }
            }
            for (auto const& child : block.child_blocks) self(self, child);
        };
        for (auto const& child : parent.child_blocks) apply(apply, child);
    }

    void EditorDocumentPainter::DrawFlowDecorations(
        IDWriteTextLayout* layout,
        D2D1_POINT_2F origin,
        folia::RenderBlock const& parent,
        DisplayInlineText const& display)
    {
        if (!layout || display.displayToSource.empty()) return;
        auto displayLimit = (std::min)(display.displayToSource.size(), folia::utf16_len(display.text));
        auto displayRange = [&](folia::RenderBlock const& nested) -> std::optional<std::pair<std::size_t, std::size_t>>
        {
            std::unordered_set<std::uint64_t> owners;
            auto collectOwners = [&](auto& self, folia::RenderBlock const& block) -> void
            {
                if (block.source_span.container_id.v != 0) owners.insert(block.source_span.container_id.v);
                for (auto const& child : block.child_blocks) self(self, child);
            };
            collectOwners(collectOwners, nested);
            std::optional<std::size_t> first;
            std::size_t last = 0;
            for (std::size_t index = 0; index < displayLimit; ++index)
            {
                auto const& position = display.displayToSource[index];
                if (!owners.contains(position.container_id.v)) continue;
                if (!first) first = index;
                last = index + 1;
            }
            return first ? std::optional{std::pair{*first, last}} : std::nullopt;
        };
        auto contentLeftFor = [&](folia::RenderBlock const& nested, std::pair<std::size_t, std::size_t> range, std::size_t indentColumns) -> std::optional<float>
        {
            std::optional<std::size_t> anchorStart;
            for (std::size_t index = 0; index < displayLimit; ++index)
            {
                if (display.displayToSource[index].container_id == nested.flow_anchor_owner_id)
                {
                    anchorStart = index;
                    break;
                }
            }
            if (!anchorStart) return std::nullopt;
            auto contentColumn = (std::min)(*anchorStart + indentColumns, range.second);
            FLOAT x = 0.0f;
            FLOAT lineY = 0.0f;
            DWRITE_HIT_TEST_METRICS hit{};
            if (FAILED(layout->HitTestTextPosition(static_cast<UINT32>(contentColumn), FALSE, &x, &lineY, &hit))) return std::nullopt;
            return origin.x + x;
        };
        auto accumulateRange = [&](std::pair<std::size_t, std::size_t> range, float& top, float& bottom)
        {
            auto [displayStart, displayEnd] = range;
            UINT32 count = 0;
            auto result = layout->HitTestTextRange(static_cast<UINT32>(displayStart), static_cast<UINT32>(displayEnd - displayStart), origin.x, origin.y, nullptr, 0, &count);
            if (result != E_NOT_SUFFICIENT_BUFFER || count == 0) return false;
            std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
            if (FAILED(layout->HitTestTextRange(static_cast<UINT32>(displayStart), static_cast<UINT32>(displayEnd - displayStart), origin.x, origin.y, metrics.data(), count, &count))) return false;
            for (UINT32 index = 0; index < count; ++index)
            {
                top = (std::min)(top, metrics[index].top);
                bottom = (std::max)(bottom, metrics[index].top + metrics[index].height);
            }
            return true;
        };
        auto calloutBrushes = [&](std::string_view kind)
        {
            switch (folia::callout_visual_kind(kind))
            {
            case folia::CalloutVisualKind::Tip:
                return std::pair{
                    resources.calloutTipBackgroundBrush.Get(),
                    resources.calloutTipBorderBrush.Get()};
            case folia::CalloutVisualKind::Warning:
                return std::pair{
                    resources.calloutWarningBackgroundBrush.Get(),
                    resources.calloutWarningBorderBrush.Get()};
            case folia::CalloutVisualKind::Note:
            default:
                return std::pair{
                    resources.calloutNoteBackgroundBrush.Get(),
                    resources.calloutNoteBorderBrush.Get()};
            }
        };
        auto draw = [&](auto& self, folia::RenderBlock const& nested, bool root, std::size_t parentIndentColumns) -> void
        {
            auto indentColumns = parentIndentColumns + nested.flow_local_indent_columns;
            auto decorated = nested.kind == folia::RenderBlockKind::Code
                || nested.kind == folia::RenderBlockKind::Quote
                || nested.kind == folia::RenderBlockKind::Callout
                || nested.kind == folia::RenderBlockKind::Footnote;
            if (decorated && !(root && (nested.kind == folia::RenderBlockKind::Code
                || nested.kind == folia::RenderBlockKind::Footnote)))
            {
                auto range = displayRange(nested);
                if (range)
                {
                    auto contentLeft = contentLeftFor(nested, *range, indentColumns);
                    float top = (std::numeric_limits<float>::max)();
                    float bottom = (std::numeric_limits<float>::lowest)();
                    if (contentLeft && accumulateRange(*range, top, bottom))
                    {
                        auto left = (std::clamp)(*contentLeft, origin.x, documentRight);
                        auto rect = D2D1::RectF(left, top - 4.0f, documentRight, bottom + 4.0f);
                        auto calloutPalette = calloutBrushes(nested.special().callout_kind);
                        auto brush = nested.kind == folia::RenderBlockKind::Callout
                            ? calloutPalette.first
                            : nested.kind == folia::RenderBlockKind::Code
                                ? resources.panelBrush.Get()
                                : resources.nestedQuoteBrush.Get();
                        if (nested.kind == folia::RenderBlockKind::Callout)
                        {
                            resources.d2dContext->FillRoundedRectangle(
                                D2D1::RoundedRect(rect, 6.0f, 6.0f),
                                brush);
                        }
                        else
                        {
                            resources.d2dContext->FillRectangle(rect, brush);
                        }
                        if (nested.kind == folia::RenderBlockKind::Quote || nested.kind == folia::RenderBlockKind::Callout)
                        {
                            auto borderWidth = nested.block_style.border_left ? nested.block_style.border_left->width : 3.0f;
                            auto lineX = left + borderWidth * 0.5f;
                            resources.d2dContext->DrawLine(
                                D2D1::Point2F(lineX, rect.top + 3.0f),
                                D2D1::Point2F(lineX, rect.bottom - 3.0f),
                                nested.kind == folia::RenderBlockKind::Callout
                                    ? calloutPalette.second
                                    : resources.mutedBrush.Get(),
                                borderWidth);
                        }
                    }
                }
            }
            for (auto const& child : nested.child_blocks) self(self, child, false, indentColumns);
        };
        draw(draw, parent, true, 0);
    }
}
