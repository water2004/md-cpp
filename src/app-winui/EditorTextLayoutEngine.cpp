#include "pch.h"

import elmd.core.render_model;
import elmd.core.types;
import elmd.core.utf;

#include "EditorContentPreparation.h"
#include "EditorTextLayoutEngine.h"

namespace winrt::ElMd
{
    EditorTextLayoutEngine::EditorTextLayoutEngine(EditorRenderResources& resources, EditorStyleSheet const& styleSheet)
        : resources(resources), styleSheet(styleSheet)
    {
    }

    ::Microsoft::WRL::ComPtr<IDWriteTextLayout> EditorTextLayoutEngine::Create(std::wstring const& text, IDWriteTextFormat* format, float width) const
    {
        ::Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        auto result = resources.dwriteFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, width, 100000.0f, layout.GetAddressOf());
        if (FAILED(result)) return {};
        return layout;
    }

    namespace
    {
        struct LogicalIndent
        {
            UINT32 lineStart = 0;
            UINT32 contentStart = 0;
            UINT32 lineEnd = 0;
            float width = 0.0f;
        };

        std::vector<LogicalIndent> FindLogicalIndents(DisplayInlineText const& display, std::wstring const& text)
        {
            std::vector<LogicalIndent> result;
            UINT32 lineStart = 0;
            while (lineStart <= text.size())
            {
                auto newline = text.find(L'\n', lineStart);
                auto lineEnd = newline == std::wstring::npos ? static_cast<UINT32>(text.size()) : static_cast<UINT32>(newline);
                auto contentStart = lineStart;
                while (contentStart < lineEnd && contentStart < display.displayToSource.size()
                    && display.displayToSource[contentStart].kind == EditorDisplayPositionKind::BoundaryDecoration
                    && display.displayToSource[contentStart].affinity == elmd::TextAffinity::Downstream)
                {
                    ++contentStart;
                }
                if (contentStart > lineStart) result.push_back({lineStart, contentStart, lineEnd, 0.0f});
                if (newline == std::wstring::npos) break;
                lineStart = static_cast<UINT32>(newline + 1);
            }
            return result;
        }

        std::size_t CodepointIndexAtUtf16(std::u32string_view text, UINT32 utf16Offset)
        {
            std::size_t index = 0;
            UINT32 current = 0;
            while (index < text.size() && current < utf16Offset)
            {
                current += text[index] > 0xffff ? 2u : 1u;
                ++index;
            }
            return index;
        }

        void ShiftRangesForInsertion(DisplayInlineText& display, UINT32 position)
        {
            for (auto& range : display.ranges)
            {
                if (range.start >= position) ++range.start;
                else if (range.start + range.length > position) ++range.length;
            }
            for (auto& overlay : display.mathOverlays) if (overlay.displayStart >= position) ++overlay.displayStart;
            for (auto& overlay : display.imageOverlays) if (overlay.displayStart >= position) ++overlay.displayStart;
            for (auto& overlay : display.indentOverlays) if (overlay.displayStart >= position) ++overlay.displayStart;
            for (auto& overlay : display.taskCheckboxOverlays) if (overlay.displayStart >= position) ++overlay.displayStart;
        }

        void InsertIndent(DisplayInlineText& display, UINT32 position, float width)
        {
            const auto textLength = static_cast<UINT32>(elmd::utf16_len(display.text));
            position = (std::min)(position, textLength);
            EditorDisplayPosition mapping = position < display.displayToSource.size()
                ? display.displayToSource[position]
                : display.displayToSource.empty()
                    ? EditorDisplayPosition{}
                    : display.displayToSource.back();
            mapping.affinity = elmd::TextAffinity::Downstream;
            mapping.kind = EditorDisplayPositionKind::BoundaryDecoration;
            display.text.insert(display.text.begin() + static_cast<std::ptrdiff_t>(CodepointIndexAtUtf16(display.text, position)), U'\uFFFC');
            auto mappingIndex = (std::min)(static_cast<std::size_t>(position), display.displayToSource.size());
            display.displayToSource.insert(display.displayToSource.begin() + static_cast<std::ptrdiff_t>(mappingIndex), mapping);
            ShiftRangesForInsertion(display, position);
            display.indentOverlays.push_back({position, width});
        }

        DisplayInlineText SliceDisplay(DisplayInlineText const& source, UINT32 start, UINT32 end)
        {
            DisplayInlineText result;
            result.pendingMath = source.pendingMath;
            const auto sourceLength = static_cast<UINT32>(elmd::utf16_len(source.text));
            start = (std::min)(start, sourceLength);
            end = (std::clamp)(end, start, sourceLength);
            const auto beginCodepoint = CodepointIndexAtUtf16(source.text, start);
            const auto endCodepoint = CodepointIndexAtUtf16(source.text, end);
            result.text = source.text.substr(beginCodepoint, endCodepoint - beginCodepoint);
            if (start < source.displayToSource.size())
            {
                const auto mappingEnd = (std::min)(static_cast<std::size_t>(end), source.displayToSource.size());
                result.displayToSource.insert(result.displayToSource.end(),
                    source.displayToSource.begin() + static_cast<std::ptrdiff_t>(start),
                    source.displayToSource.begin() + static_cast<std::ptrdiff_t>(mappingEnd));
            }
            EditorDisplayPosition endPosition = end < source.displayToSource.size()
                ? source.displayToSource[end]
                : source.displayToSource.empty() ? EditorDisplayPosition{} : source.displayToSource.back();
            result.displayToSource.push_back(endPosition);

            for (auto const& range : source.ranges)
            {
                const auto rangeEnd = range.start + range.length;
                const auto overlapStart = (std::max)(start, range.start);
                const auto overlapEnd = (std::min)(end, rangeEnd);
                if (overlapStart >= overlapEnd) continue;
                auto sliced = range;
                sliced.start = overlapStart - start;
                sliced.length = overlapEnd - overlapStart;
                result.ranges.push_back(std::move(sliced));
            }
            for (auto overlay : source.mathOverlays)
            {
                if (overlay.displayStart < start || overlay.displayStart >= end) continue;
                overlay.displayStart -= start;
                result.mathOverlays.push_back(std::move(overlay));
            }
            for (auto overlay : source.imageOverlays)
            {
                if (overlay.displayStart < start || overlay.displayStart >= end) continue;
                overlay.displayStart -= start;
                result.imageOverlays.push_back(std::move(overlay));
            }
            for (auto overlay : source.taskCheckboxOverlays)
            {
                if (overlay.displayStart < start || overlay.displayStart >= end) continue;
                overlay.displayStart -= start;
                result.taskCheckboxOverlays.push_back(std::move(overlay));
            }
            return result;
        }

        std::optional<std::size_t> LogicalIndentAt(std::vector<LogicalIndent> const& indents, UINT32 position)
        {
            for (std::size_t index = 0; index < indents.size(); ++index)
            {
                if (position >= indents[index].contentStart && position < indents[index].lineEnd) return index;
            }
            return std::nullopt;
        }
    }

    ::Microsoft::WRL::ComPtr<IDWriteTextLayout> EditorTextLayoutEngine::CreateFlow(
        DisplayInlineText& display,
        IDWriteTextFormat* format,
        float width,
        std::function<void(IDWriteTextLayout*, DisplayInlineText const&)> const& configure) const
    {
        auto build = [&](DisplayInlineText const& value)
        {
            auto layout = Create(ToWide(value.text), format, width);
            ApplyStyles(layout.Get(), value.ranges);
            ApplyMathInlineObjects(layout.Get(), value.mathOverlays);
            ApplyIndentInlineObjects(layout.Get(), value.indentOverlays);
            ApplyTaskCheckboxInlineObjects(layout.Get(), value.taskCheckboxOverlays);
            if (configure) configure(layout.Get(), value);
            return layout;
        };

        const auto base = display;
        const auto baseText = ToWide(base.text);
        auto indents = FindLogicalIndents(base, baseText);
        if (indents.empty()) return build(display);

        auto initial = build(base);
        if (!initial) return initial;
        for (auto& indent : indents)
        {
            FLOAT x = 0.0f;
            FLOAT y = 0.0f;
            DWRITE_HIT_TEST_METRICS metrics{};
            if (SUCCEEDED(initial->HitTestTextPosition(indent.contentStart, FALSE, &x, &y, &metrics))) indent.width = x;
        }
        std::erase_if(indents, [](auto const& value) { return value.width <= 0.0f; });
        if (indents.empty()) return initial;

        UINT32 initialLineCount = 0;
        if (initial->GetLineMetrics(nullptr, 0, &initialLineCount) != E_NOT_SUFFICIENT_BUFFER || initialLineCount == 0) return initial;
        std::vector<DWRITE_LINE_METRICS> initialMetrics(initialLineCount);
        if (FAILED(initial->GetLineMetrics(initialMetrics.data(), initialLineCount, &initialLineCount))) return initial;
        std::unordered_set<std::size_t> wrappedIndents;
        UINT32 initialLineStart = 0;
        for (UINT32 line = 0; line < initialLineCount; ++line)
        {
            if (initialLineStart > 0 && initialLineStart < baseText.size() && baseText[initialLineStart - 1] != L'\n')
            {
                if (auto indentIndex = LogicalIndentAt(indents, initialLineStart)) wrappedIndents.insert(*indentIndex);
            }
            initialLineStart += initialMetrics[line].length;
        }

        std::vector<std::pair<UINT32, float>> breaks;
        for (auto indentIndex : wrappedIndents)
        {
            auto const& indent = indents[indentIndex];
            auto content = SliceDisplay(base, indent.contentStart, indent.lineEnd);
            auto contentLayout = Create(ToWide(content.text), format, (std::max)(1.0f, width - indent.width));
            ApplyStyles(contentLayout.Get(), content.ranges);
            ApplyMathInlineObjects(contentLayout.Get(), content.mathOverlays);
            ApplyTaskCheckboxInlineObjects(contentLayout.Get(), content.taskCheckboxOverlays);
            if (configure) configure(contentLayout.Get(), content);
            if (!contentLayout) continue;
            UINT32 lineCount = 0;
            if (contentLayout->GetLineMetrics(nullptr, 0, &lineCount) != E_NOT_SUFFICIENT_BUFFER || lineCount <= 1) continue;
            std::vector<DWRITE_LINE_METRICS> metrics(lineCount);
            if (FAILED(contentLayout->GetLineMetrics(metrics.data(), lineCount, &lineCount))) continue;
            UINT32 lineStart = 0;
            for (UINT32 line = 0; line < lineCount; ++line)
            {
                if (lineStart > 0 && indent.contentStart + lineStart < indent.lineEnd) {
                    breaks.push_back({indent.contentStart + lineStart, indent.width});
                }
                lineStart += metrics[line].length;
            }
        }
        if (breaks.empty()) return build(display);
        std::ranges::sort(breaks, {}, &std::pair<UINT32, float>::first);
        breaks.erase(std::unique(breaks.begin(), breaks.end(), [](auto const& left, auto const& right) {
            return left.first == right.first;
        }), breaks.end());
        UINT32 inserted = 0;
        for (auto const& [basePosition, indentWidth] : breaks)
        {
            InsertIndent(display, basePosition + inserted, indentWidth);
            ++inserted;
        }
        std::ranges::sort(display.indentOverlays, {}, &DisplayInlineText::IndentOverlay::displayStart);
        return build(display);
    }

    IDWriteTextFormat* EditorTextLayoutEngine::FormatFor(bool code, std::vector<InlineStyleRange> const& ranges) const
    {
        if (code) return resources.codeFormat.Get();
        for (auto const& range : ranges)
        {
            if (!range.style.heading_level) continue;
            switch (*range.style.heading_level)
            {
                case 1: return resources.heading1Format.Get();
                case 2: return resources.heading2Format.Get();
                case 3: return resources.heading3Format.Get();
                default: return resources.textFormat.Get();
            }
        }
        return resources.textFormat.Get();
    }

    float EditorTextLayoutEngine::LineHeightFor(bool code, std::vector<InlineStyleRange> const& ranges) const
    {
        if (code) return styleSheet.code.lineHeight;
        for (auto const& range : ranges)
        {
            if (!range.style.heading_level) continue;
            switch (*range.style.heading_level)
            {
                case 1: return styleSheet.heading1.lineHeight;
                case 2: return styleSheet.heading2.lineHeight;
                case 3: return styleSheet.heading3.lineHeight;
                default: return styleSheet.body.lineHeight;
            }
        }
        return styleSheet.body.lineHeight;
    }

    void EditorTextLayoutEngine::ApplyStyles(IDWriteTextLayout* layout, std::vector<InlineStyleRange> const& ranges) const
    {
        if (!layout) return;
        for (auto const& range : ranges)
        {
            DWRITE_TEXT_RANGE textRange{ range.start, range.length };
            if (range.style.bold) layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, textRange);
            if (range.style.italic) layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, textRange);
            if (range.style.link) layout->SetUnderline(true, textRange);
            if (range.style.strikethrough) layout->SetStrikethrough(true, textRange);
            if (range.style.code)
            {
                layout->SetFontFamilyName(styleSheet.code.family.c_str(), textRange);
                layout->SetFontSize(styleSheet.code.size, textRange);
                if (resources.codeBrush) layout->SetDrawingEffect(resources.codeBrush.Get(), textRange);
            }
            if (range.style.heading_level)
            {
                auto level = *range.style.heading_level;
                auto size = level == 1 ? styleSheet.heading1.size
                    : level == 2 ? styleSheet.heading2.size
                    : level == 3 ? styleSheet.heading3.size
                    : level == 4 ? styleSheet.body.size * 1.15f
                    : styleSheet.body.size;
                layout->SetFontSize(size, textRange);
                layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, textRange);
            }
            if (range.marker && !range.style.heading_level) layout->SetFontSize(styleSheet.body.size * 0.82f, textRange);
            auto syntaxIndex = static_cast<std::size_t>(range.syntax);
            if (range.syntax != SyntaxHighlightKind::None && syntaxIndex < resources.syntaxBrushes.size() && resources.syntaxBrushes[syntaxIndex])
            {
                layout->SetDrawingEffect(resources.syntaxBrushes[syntaxIndex].Get(), textRange);
            }
        }
    }

    float EditorTextLayoutEngine::MeasureHeight(IDWriteTextLayout* layout, float fallbackHeight) const
    {
        if (!layout) return fallbackHeight;
        DWRITE_TEXT_METRICS metrics{};
        if (FAILED(layout->GetMetrics(&metrics))) return fallbackHeight;
        return (std::max)(fallbackHeight, metrics.height);
    }
}
