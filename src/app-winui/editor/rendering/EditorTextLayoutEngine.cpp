#include "pch.h"

import elmd.core.render_model;
import elmd.core.theme;
import elmd.core.types;
import elmd.core.utf;

#include "editor/rendering/EditorContentPreparation.h"
#include "editor/rendering/EditorTextLayoutEngine.h"

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
            for (auto& overlay : display.footnoteOverlays) if (overlay.displayStart >= position) ++overlay.displayStart;
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
            for (auto overlay : source.footnoteOverlays)
            {
                auto overlayEnd = overlay.displayStart + overlay.displayLength;
                if (overlayEnd <= start || overlay.displayStart >= end) continue;
                overlay.displayStart = (std::max)(overlay.displayStart, start) - start;
                overlay.displayLength = (std::min)(overlayEnd, end) - (std::max)(overlayEnd - overlay.displayLength, start);
                result.footnoteOverlays.push_back(std::move(overlay));
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

        std::optional<std::uint8_t> HomogeneousHeadingLevel(std::vector<InlineStyleRange> const& ranges)
        {
            std::optional<std::uint8_t> level;
            for (auto const& range : ranges)
            {
                if (range.length == 0) continue;
                if (!range.style.heading_level) return std::nullopt;
                if (!level) level = *range.style.heading_level;
                else if (*level != *range.style.heading_level) return std::nullopt;
            }
            return level;
        }

        D2D1_COLOR_F ToD2D(elmd::Color color)
        {
            constexpr auto scale = 1.0f / 255.0f;
            return D2D1::ColorF(
                color.r * scale,
                color.g * scale,
                color.b * scale,
                color.a * scale);
        }

        float BaseFontSize(elmd::InlineStyle const& style, EditorStyleSheet const& styleSheet)
        {
            if (style.heading_level)
            {
                switch (*style.heading_level)
                {
                    case 1: return styleSheet.heading1.size;
                    case 2: return styleSheet.heading2.size;
                    case 3: return styleSheet.heading3.size;
                    case 4: return styleSheet.body.size * 1.15f;
                    default: return styleSheet.body.size;
                }
            }
            return style.code ? styleSheet.code.size : styleSheet.body.size;
        }

        float BaseLineHeight(elmd::InlineStyle const& style, EditorStyleSheet const& styleSheet)
        {
            if (style.heading_level)
            {
                switch (*style.heading_level)
                {
                    case 1: return styleSheet.heading1.lineHeight;
                    case 2: return styleSheet.heading2.lineHeight;
                    case 3: return styleSheet.heading3.lineHeight;
                    default: return styleSheet.body.lineHeight;
                }
            }
            return style.code ? styleSheet.code.lineHeight : styleSheet.body.lineHeight;
        }
    }

    void DrawInlinePresentationBackgrounds(
        EditorRenderResources& resources,
        EditorStyleSheet const& styleSheet,
        IDWriteTextLayout* layout,
        D2D1_POINT_2F origin,
        std::vector<InlineStyleRange> const& ranges)
    {
        if (!layout || !resources.d2dContext) return;
        ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
        for (auto const& range : ranges)
        {
            auto const& presentation = range.style.presentation;
            if (range.length == 0 || !presentation
                || (!presentation->background && !presentation->highlight)) continue;

            const auto color = presentation->background
                ? ToD2D(*presentation->background)
                : styleSheet.calloutWarningBackgroundColor;
            if (!brush)
            {
                if (FAILED(resources.d2dContext->CreateSolidColorBrush(
                        color,
                        brush.GetAddressOf()))) return;
            }
            else brush->SetColor(color);

            UINT32 count = 0;
            auto result = layout->HitTestTextRange(
                range.start,
                range.length,
                origin.x,
                origin.y,
                nullptr,
                0,
                &count);
            if (result != E_NOT_SUFFICIENT_BUFFER || count == 0) continue;
            std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
            if (FAILED(layout->HitTestTextRange(
                    range.start,
                    range.length,
                    origin.x,
                    origin.y,
                    metrics.data(),
                    count,
                    &count))) continue;
            for (UINT32 index = 0; index < count; ++index)
            {
                auto const& metric = metrics[index];
                resources.d2dContext->FillRectangle(
                    D2D1::RectF(
                        metric.left,
                        metric.top,
                        metric.left + metric.width,
                        metric.top + metric.height),
                    brush.Get());
            }
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
        if (auto const level = HomogeneousHeadingLevel(ranges))
        {
            switch (*level)
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
        if (auto const level = HomogeneousHeadingLevel(ranges))
        {
            switch (*level)
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
        bool requiresDefaultLineSpacing = false;
        for (auto const& range : ranges)
        {
            DWRITE_TEXT_RANGE textRange{ range.start, range.length };
            if (range.style.bold) layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, textRange);
            if (range.style.italic) layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, textRange);
            if (range.style.link || range.style.underline) layout->SetUnderline(true, textRange);
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
                auto const* heading = level == 1 ? &styleSheet.heading1
                    : level == 2 ? &styleSheet.heading2
                    : level == 3 ? &styleSheet.heading3
                    : nullptr;
                auto size = heading ? heading->size
                    : level == 4 ? styleSheet.body.size * 1.15f
                    : styleSheet.body.size;
                if (heading && !range.style.code)
                    layout->SetFontFamilyName(heading->family.c_str(), textRange);
                layout->SetFontSize(size, textRange);
                auto weight = heading ? heading->weight : DWRITE_FONT_WEIGHT_SEMI_BOLD;
                if (range.style.bold && weight < DWRITE_FONT_WEIGHT_SEMI_BOLD)
                    weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
                layout->SetFontWeight(weight, textRange);
                auto fontStyle = heading ? heading->style : styleSheet.body.style;
                if (range.style.italic) fontStyle = DWRITE_FONT_STYLE_ITALIC;
                layout->SetFontStyle(fontStyle, textRange);
            }
            if (range.marker && !range.style.heading_level) layout->SetFontSize(styleSheet.body.size * 0.82f, textRange);
            if (range.footnoteControl)
            {
                auto const reference = *range.footnoteControl == EditorFootnoteControlKind::Reference;
                layout->SetFontSize(styleSheet.body.size * (reference ? 1.1f : 0.82f), textRange);
                layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, textRange);
                if (resources.accentBrush) layout->SetDrawingEffect(resources.accentBrush.Get(), textRange);
            }
            auto syntaxIndex = static_cast<std::size_t>(range.syntax);
            if (range.syntax != SyntaxHighlightKind::None && syntaxIndex < resources.syntaxBrushes.size() && resources.syntaxBrushes[syntaxIndex])
            {
                layout->SetDrawingEffect(resources.syntaxBrushes[syntaxIndex].Get(), textRange);
            }
            if (auto const& presentation = range.style.presentation)
            {
                if (presentation->font_family)
                {
                    auto family = winrt::to_hstring(*presentation->font_family);
                    layout->SetFontFamilyName(family.c_str(), textRange);
                }
                if (presentation->font_weight)
                {
                    layout->SetFontWeight(
                        static_cast<DWRITE_FONT_WEIGHT>(*presentation->font_weight),
                        textRange);
                }
                if (presentation->font_italic)
                {
                    layout->SetFontStyle(
                        *presentation->font_italic
                            ? DWRITE_FONT_STYLE_ITALIC
                            : DWRITE_FONT_STYLE_NORMAL,
                        textRange);
                }
                const auto fontSize = presentation->absolute_font_size.value_or(
                    BaseFontSize(range.style, styleSheet) * presentation->relative_font_scale);
                if (presentation->absolute_font_size
                    || std::abs(presentation->relative_font_scale - 1.0f) > 0.001f)
                {
                    layout->SetFontSize(std::clamp(fontSize, 6.0f, 144.0f), textRange);
                    requiresDefaultLineSpacing = requiresDefaultLineSpacing
                        || fontSize > BaseLineHeight(range.style, styleSheet);
                }
                if (presentation->foreground)
                {
                    ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
                    if (SUCCEEDED(resources.d2dContext->CreateSolidColorBrush(
                            ToD2D(*presentation->foreground),
                            brush.GetAddressOf())))
                    {
                        layout->SetDrawingEffect(brush.Get(), textRange);
                    }
                }
                if (presentation->baseline != elmd::InlineBaseline::Normal)
                {
                    ::Microsoft::WRL::ComPtr<IDWriteTypography> typography;
                    if (SUCCEEDED(resources.dwriteFactory->CreateTypography(typography.GetAddressOf())))
                    {
                        DWRITE_FONT_FEATURE feature{
                            presentation->baseline == elmd::InlineBaseline::Superscript
                                ? DWRITE_FONT_FEATURE_TAG_SUPERSCRIPT
                                : DWRITE_FONT_FEATURE_TAG_SUBSCRIPT,
                            1};
                        if (SUCCEEDED(typography->AddFontFeature(feature)))
                            layout->SetTypography(typography.Get(), textRange);
                    }
                }
            }
        }
        if (requiresDefaultLineSpacing)
            layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_DEFAULT, 0.0f, 0.0f);
    }

    float EditorTextLayoutEngine::MeasureHeight(IDWriteTextLayout* layout, float fallbackHeight) const
    {
        if (!layout) return fallbackHeight;
        DWRITE_TEXT_METRICS metrics{};
        if (FAILED(layout->GetMetrics(&metrics))) return fallbackHeight;
        return (std::max)(fallbackHeight, metrics.height);
    }
}
