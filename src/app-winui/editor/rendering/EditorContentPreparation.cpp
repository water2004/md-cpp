#include "pch.h"
#include "editor/rendering/EditorContentPreparation.h"

import elmd.core.render_model;
import elmd.core.source_style;
import elmd.core.utf;

namespace winrt::ElMd
{
    namespace
    {
        SyntaxHighlightKind SourceSyntax(elmd::SourceSyntaxKind kind)
        {
            using K = elmd::SourceSyntaxKind;
            switch (kind)
            {
                case K::Marker: return SyntaxHighlightKind::Operator;
                case K::Heading: return SyntaxHighlightKind::Keyword;
                case K::Emphasis: return SyntaxHighlightKind::Property;
                case K::Strong: return SyntaxHighlightKind::Type;
                case K::Strikethrough: return SyntaxHighlightKind::Comment;
                case K::Link: return SyntaxHighlightKind::Property;
                case K::Code: return SyntaxHighlightKind::String;
                case K::Math: return SyntaxHighlightKind::Number;
                case K::Escape: return SyntaxHighlightKind::Preprocessor;
                case K::Entity: return SyntaxHighlightKind::Constant;
                case K::Error: return SyntaxHighlightKind::Preprocessor;
                case K::None: return SyntaxHighlightKind::None;
            }
            return SyntaxHighlightKind::None;
        }
    }

    std::u32string InlineText(elmd::InlineRenderItem const& item)
    {
        switch (item.kind)
        {
            case elmd::InlineRenderItem::Kind::Text:
                return item.text;
            case elmd::InlineRenderItem::Kind::Marker:
                return item.special().display_text.empty() ? item.text : item.special().display_text;
            case elmd::InlineRenderItem::Kind::Math:
                return U"$" + item.text + U"$";
            case elmd::InlineRenderItem::Kind::Image:
                return item.special().semantic().alt.empty()
                    ? U"image" : elmd::utf8_to_cps(item.special().semantic().alt);
            case elmd::InlineRenderItem::Kind::Link: {
                std::u32string text;
                for (auto const& child : item.special().semantic().children)
                {
                    text += InlineText(child);
                }
                return text;
            }
            case elmd::InlineRenderItem::Kind::FootnoteReference:
                return item.special().display_text.empty()
                    ? elmd::utf8_to_cps(item.special().semantic().footnote_label)
                    : item.special().display_text;
        }
        return {};
    }

    std::u32string InlineText(std::vector<elmd::InlineRenderItem> const& items)
    {
        std::u32string text;
        for (auto const& item : items)
        {
            text += InlineText(item);
        }
        return text;
    }

    std::u32string FootnoteSuperscript(std::u32string_view number)
    {
        std::u32string result;
        result.reserve(number.size());
        for (auto value : number)
        {
            switch (value)
            {
                case U'0': result.push_back(U'\u2070'); break;
                case U'1': result.push_back(U'\u00b9'); break;
                case U'2': result.push_back(U'\u00b2'); break;
                case U'3': result.push_back(U'\u00b3'); break;
                case U'4': result.push_back(U'\u2074'); break;
                case U'5': result.push_back(U'\u2075'); break;
                case U'6': result.push_back(U'\u2076'); break;
                case U'7': result.push_back(U'\u2077'); break;
                case U'8': result.push_back(U'\u2078'); break;
                case U'9': result.push_back(U'\u2079'); break;
                default: result.push_back(value); break;
            }
        }
        return result;
    }

    bool IsStyleMarker(elmd::InlineRenderItem const& item)
    {
        if (item.kind != elmd::InlineRenderItem::Kind::Marker) return false;
        if (item.special().marker_role == elmd::MarkerRole::Heading) return false;
        if (item.special().marker_owner) return true;
        bool backticks = !item.text.empty();
        for (char32_t ch : item.text) if (ch != U'`') backticks = false;
        return item.text == U"*" || item.text == U"**" || item.text == U"~~" || backticks;
    }

    bool IsHeadingMarker(elmd::InlineRenderItem const& item)
    {
        if (item.kind == elmd::InlineRenderItem::Kind::Marker
            && item.special().marker_role == elmd::MarkerRole::Heading) return true;
        if (item.kind != elmd::InlineRenderItem::Kind::Marker || item.text.size() < 2 || item.text.back() != U' ')
        {
            return false;
        }
        for (std::size_t index = 0; index + 1 < item.text.size(); ++index)
        {
            if (item.text[index] != U'#')
            {
                return false;
            }
        }
        return true;
    }

    bool CaretTouchesSpan(elmd::TextPosition caret, elmd::TextSpan const& span)
    {
        if (caret.container_id != span.container_id) return false;
        auto start = span.source_range.start;
        auto end = span.source_range.end;
        auto offset = caret.source_offset;
        if (start <= offset && offset <= end)
        {
            return true;
        }
        return (start > 0 && offset == start - 1) || (end < (std::numeric_limits<std::size_t>::max)() && offset == end + 1);
    }

    elmd::TextPosition InlineItemsEndPosition(std::vector<elmd::InlineRenderItem> const& items, elmd::TextPosition fallback)
    {
        auto result = fallback;
        for (auto const& item : items)
        {
            if (item.source_span.container_id.v != 0)
            {
                auto generatedPrefix = item.kind == elmd::InlineRenderItem::Kind::Marker
                    && item.source_span.source_range.empty();
                result = {
                    item.source_span.container_id,
                    item.source_span.source_range.end,
                    generatedPrefix ? elmd::TextAffinity::Downstream : elmd::TextAffinity::Upstream,
                };
            }
        }
        return result;
    }

    std::vector<bool> RevealedStyleMarkers(std::vector<elmd::InlineRenderItem> const& items, elmd::TextPosition caret)
    {
        std::vector<bool> visible(items.size(), true);
        std::vector<std::pair<std::u32string, std::size_t>> stack;
        std::unordered_map<std::uint64_t, std::size_t> owners;
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            auto const& item = items[index];
            if (!IsStyleMarker(item))
            {
                continue;
            }

            if (item.special().marker_owner)
            {
                auto found = owners.find(item.special().marker_owner->v);
                if (found == owners.end())
                {
                    owners.emplace(item.special().marker_owner->v, index);
                    visible[index] = false;
                }
                else
                {
                    auto openIndex = found->second;
                    auto reveal = CaretTouchesSpan(caret, {item.source_span.container_id, {items[openIndex].source_span.source_range.start, item.source_span.source_range.end}});
                    visible[openIndex] = reveal;
                    visible[index] = reveal;
                    owners.erase(found);
                }
                continue;
            }

            auto open = std::find_if(stack.rbegin(), stack.rend(), [&](auto const& entry)
            {
                return entry.first == item.text;
            });
            if (open == stack.rend())
            {
                stack.push_back({ item.text, index });
                visible[index] = false;
                continue;
            }

            auto openIndex = open->second;
            auto reveal = CaretTouchesSpan(caret, {item.source_span.container_id, {items[openIndex].source_span.source_range.start, item.source_span.source_range.end}});
            visible[openIndex] = reveal;
            visible[index] = reveal;
            stack.erase(std::next(open).base());
        }

        for (auto const& entry : stack)
        {
            visible[entry.second] = true;
        }
        for (auto const& entry : owners) visible[entry.second] = true;
        return visible;
    }

    void AppendDisplayText(DisplayInlineText& display, std::u32string const& text, elmd::TextSpan sourceSpan, elmd::InlineStyle style, bool marker)
    {
        auto start = static_cast<UINT32>(elmd::utf16_len(display.text));
        display.text += text;
        auto length = static_cast<UINT32>(elmd::utf16_len(text));
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            auto position = elmd::TextPosition{
                sourceSpan.container_id,
                sourceSpan.source_range.start + (std::min)(index, sourceSpan.source_range.length()),
                elmd::TextAffinity::Downstream};
            display.displayToSource.push_back(EditorDisplayPosition{
                position,
                EditorDisplayPositionKind::Source});
            if (text[index] > 0xffff) display.displayToSource.push_back(EditorDisplayPosition{
                position,
                EditorDisplayPositionKind::Source});
        }
        if (length > 0)
        {
            InlineStyleRange range;
            range.start = start;
            range.length = length;
            range.style = style;
            range.marker = marker;
            display.ranges.push_back(range);
        }
    }

    void AppendGeneratedText(
        DisplayInlineText& display,
        std::u32string const& text,
        elmd::TextPosition sourcePosition,
        elmd::InlineStyle style,
        EditorDisplayPositionKind kind)
    {
        auto start = static_cast<UINT32>(elmd::utf16_len(display.text));
        display.text += text;
        auto length = elmd::utf16_len(text);
        display.displayToSource.insert(
            display.displayToSource.end(),
            length,
            EditorDisplayPosition{sourcePosition, kind});
        if (!text.empty()) display.ranges.push_back(InlineStyleRange{start, static_cast<UINT32>(length), style, false, SyntaxHighlightKind::None});
    }

    void MergeDisplayText(DisplayInlineText& target, DisplayInlineText source)
    {
        auto offset = static_cast<UINT32>(elmd::utf16_len(target.text));
        auto characterCount = elmd::utf16_len(source.text);
        target.text += source.text;
        if (source.displayToSource.size() > characterCount) source.displayToSource.resize(characterCount);
        target.displayToSource.insert(target.displayToSource.end(), source.displayToSource.begin(), source.displayToSource.end());
        for (auto& range : source.ranges)
        {
            range.start += offset;
            target.ranges.push_back(std::move(range));
        }
        for (auto& overlay : source.mathOverlays)
        {
            overlay.displayStart += offset;
            target.mathOverlays.push_back(std::move(overlay));
        }
        for (auto& preview : source.mathPreviews)
        {
            target.mathPreviews.push_back(std::move(preview));
        }
        for (auto& overlay : source.imageOverlays)
        {
            overlay.displayStart += offset;
            target.imageOverlays.push_back(std::move(overlay));
        }
        for (auto& overlay : source.indentOverlays)
        {
            overlay.displayStart += offset;
            target.indentOverlays.push_back(std::move(overlay));
        }
        for (auto& overlay : source.taskCheckboxOverlays)
        {
            overlay.displayStart += offset;
            target.taskCheckboxOverlays.push_back(std::move(overlay));
        }
        for (auto& overlay : source.footnoteOverlays)
        {
            overlay.displayStart += offset;
            target.footnoteOverlays.push_back(std::move(overlay));
        }
        target.pendingMath = target.pendingMath || source.pendingMath;
    }

    void AppendSourceText(DisplayInlineText& display, std::u32string_view sourceText, elmd::TextSpan sourceSpan, elmd::InlineStyle style, bool marker)
    {
        auto length = (std::min)(sourceSpan.source_range.length(), sourceText.size());
        AppendDisplayText(display, std::u32string(sourceText.substr(0, length)), sourceSpan, style, marker);
    }

    void AppendProjectedSourceText(
        DisplayInlineText& display,
        std::u32string_view text,
        elmd::NodeId owner,
        std::vector<std::size_t> const& sourceOffsets,
        elmd::InlineStyle style)
    {
        auto start = static_cast<UINT32>(elmd::utf16_len(display.text));
        display.text.append(text);
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            auto offset = sourceOffsets.empty()
                ? index
                : sourceOffsets[(std::min)(index, sourceOffsets.size() - 1)];
            auto mapping = EditorDisplayPosition{
                {owner, offset, elmd::TextAffinity::Downstream},
                EditorDisplayPositionKind::Source};
            display.displayToSource.push_back(mapping);
            if (text[index] > 0xffff) display.displayToSource.push_back(mapping);
        }
        if (!text.empty())
        {
            display.ranges.push_back(InlineStyleRange{
                start,
                static_cast<UINT32>(elmd::utf16_len(text)),
                style,
                false,
                SyntaxHighlightKind::None});
        }
    }

    void AppendMathPlaceholder(DisplayInlineText& display, std::size_t count, elmd::TextPosition sourcePosition)
    {
        auto start = static_cast<UINT32>(elmd::utf16_len(display.text));
        display.text.append(count, U'\u2007');
        display.displayToSource.insert(
            display.displayToSource.end(),
            count,
            EditorDisplayPosition{sourcePosition, EditorDisplayPositionKind::Generated});
        if (count > 0)
        {
            display.ranges.push_back(InlineStyleRange{ start, static_cast<UINT32>(count), elmd::InlineStyle::plain(), false, SyntaxHighlightKind::None });
        }
    }

    void AppendMathFragments(DisplayInlineText& display, MathJaxSvg const& math, elmd::TextSpan sourceSpan, bool editing, elmd::InlineStyle style)
    {
        auto sourceStart = sourceSpan.source_range.start;
        auto sourceEnd = sourceSpan.source_range.end;
        if (editing)
        {
            AppendGeneratedText(display, U"\u2003", {sourceSpan.container_id, sourceEnd, elmd::TextAffinity::Downstream}, style);
        }
        float progress = 0.0f;
        for (std::size_t index = 0; index < math.fragments.size(); ++index)
        {
            auto const& fragment = math.fragments[index];
            auto length = sourceEnd - sourceStart;
            auto mappedOffset = editing || math.width <= 0.0f
                ? sourceEnd
                : sourceStart + static_cast<std::size_t>(std::floor((progress / math.width) * static_cast<float>(length)));
            if (index > 0 && fragment.breakBefore)
            {
                // MathJax emits fragments at legal line-break opportunities.
                // Keep them in one DirectWrite flow and expose an optional
                // break; a hard newline would force every fragment onto its
                // own line even when the remaining width is sufficient.
                AppendGeneratedText(display, U"\u200B", {sourceSpan.container_id, mappedOffset, elmd::TextAffinity::Downstream}, style);
            }
            auto start = static_cast<UINT32>(elmd::utf16_len(display.text));
            display.text.push_back(U'\uFFFC');
            display.displayToSource.push_back({
                sourceSpan.container_id,
                mappedOffset,
                elmd::TextAffinity::Downstream,
                EditorDisplayPositionKind::Generated});
            display.ranges.push_back(InlineStyleRange{ start, 1, style, false, SyntaxHighlightKind::None });
            auto fragmentStart = math.width > 0.0f ? progress / math.width : 0.0f;
            progress += fragment.breakSpace + fragment.width;
            auto fragmentEnd = math.width > 0.0f ? progress / math.width : 1.0f;
            display.mathOverlays.push_back(DisplayInlineText::MathOverlay{
                start,
                fragment,
                fragment.breakSpace,
                sourceSpan,
                fragmentStart,
                fragmentEnd,
                style.strikethrough,
                math.display,
            });
        }
    }

    DisplayInlineText BuildMathPreviewText(DisplayInlineText::MathPreview const& preview)
    {
        DisplayInlineText display;
        auto style = elmd::InlineStyle::plain();
        style.strikethrough = preview.strikethrough;
        AppendMathFragments(
            display,
            preview.svg,
            preview.contentSpan,
            false,
            style);
        display.displayToSource.push_back({
            preview.contentSpan.container_id,
            preview.contentSpan.source_range.end,
            elmd::TextAffinity::Downstream,
            EditorDisplayPositionKind::Generated});
        return display;
    }

    DisplayInlineText BuildDisplayInlineText(
        std::vector<elmd::InlineRenderItem> const& items,
        elmd::TextPosition caret,
        elmd::TextPosition sourceEnd,
        MathJaxRenderer& mathJax,
        SvgNormalizer& svgNormalizer,
        D2D1_COLOR_F svgColor,
        float fontSize,
        float containerWidth,
        bool svgSupported,
        bool requestMath)
    {
        DisplayInlineText display;
        auto markerVisibility = RevealedStyleMarkers(items, caret);
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            auto const& item = items[index];
            if (IsStyleMarker(item) && !markerVisibility[index])
            {
                continue;
            }
            if (IsHeadingMarker(item) && caret.container_id != item.source_span.container_id)
            {
                continue;
            }
            if (item.kind == elmd::InlineRenderItem::Kind::Link)
            {
                if (CaretTouchesSpan(caret, item.source_span))
                {
                    AppendSourceText(display, item.special().semantic().source_text, item.source_span, item.style, false);
                }
                else
                {
                    MergeDisplayText(display, BuildDisplayInlineText(item.special().semantic().children, caret, {item.source_span.container_id, item.source_span.source_range.end, elmd::TextAffinity::Downstream}, mathJax, svgNormalizer, svgColor, fontSize, containerWidth, svgSupported, requestMath));
                }
                continue;
            }
            if (item.kind == elmd::InlineRenderItem::Kind::Image)
            {
                if (CaretTouchesSpan(caret, item.source_span)) AppendSourceText(display, item.special().semantic().source_text, item.source_span, item.style, false);
                else
                {
                    auto displayStart = static_cast<std::uint32_t>(elmd::utf16_len(display.text));
                    AppendGeneratedText(display, U"\uFFFC", {item.source_span.container_id, item.source_span.source_range.start, elmd::TextAffinity::Downstream}, item.style);
                    display.imageOverlays.push_back(DisplayInlineText::ImageOverlay{
                        displayStart,
                        item.source_span,
                        item.special().semantic().src,
                        item.special().semantic().alt,
                        item.special().semantic().image_width,
                        item.special().semantic().image_height,
                        item.special().semantic().block_image,
                    });
                }
                continue;
            }
            if (item.kind == elmd::InlineRenderItem::Kind::Math)
            {
                if (!svgSupported)
                {
                    AppendSourceText(display, item.special().semantic().source_text, item.source_span, item.style, false);
                    continue;
                }
                auto displayMath = item.special().display == elmd::MathDisplayMode::Block;
                auto rawMath = mathJax.GetOrQueue(
                    elmd::cps_to_utf8(item.text),
                    displayMath,
                    fontSize,
                    containerWidth,
                    requestMath);
                auto math = rawMath ? NormalizeMathJaxSvg(*rawMath, svgNormalizer, svgColor, fontSize, requestMath) : std::nullopt;
                display.pendingMath = display.pendingMath || !rawMath || !math;
                auto editing = CaretTouchesSpan(caret, item.source_span);
                auto delimiterLength = item.special().display == elmd::MathDisplayMode::Block
                    ? std::size_t{0}
                    : item.special().math_delim == elmd::MathDelimiter::InlineParen ? std::size_t{2} : std::size_t{1};
                auto contentStart = (std::min)(item.source_span.source_range.start + delimiterLength, item.source_span.source_range.end);
                auto contentEnd = item.source_span.source_range.end >= delimiterLength
                    ? (std::max)(contentStart, item.source_span.source_range.end - delimiterLength)
                    : contentStart;
                if (!math || !static_cast<bool>(*math))
                {
                    AppendSourceText(display, item.special().semantic().source_text, item.source_span, item.style, false);
                    continue;
                }

                if (editing)
                {
                    AppendSourceText(display, item.special().semantic().source_text, item.source_span, item.style, false);
                    display.mathPreviews.push_back(DisplayInlineText::MathPreview{
                        *math,
                        elmd::TextSpan{item.source_span.container_id, {contentStart, contentEnd}},
                        item.style.strikethrough,
                    });
                    continue;
                }
                AppendMathFragments(display, *math, {item.source_span.container_id, {contentStart, contentEnd}}, false, item.style);
                continue;
            }
            if (item.kind == elmd::InlineRenderItem::Kind::FootnoteReference)
            {
                auto displayStart = static_cast<std::uint32_t>(elmd::utf16_len(display.text));
                auto ordinal = item.special().display_text.empty()
                    ? elmd::utf8_to_cps(item.special().semantic().footnote_label)
                    : item.special().display_text;
                auto label = FootnoteSuperscript(ordinal);
                AppendGeneratedText(
                    display,
                    label,
                    {item.source_span.container_id, item.source_span.source_range.start, elmd::TextAffinity::Downstream},
                    item.style,
                    EditorDisplayPositionKind::BoundaryDecoration);
                auto displayLength = static_cast<std::uint32_t>(elmd::utf16_len(label));
                if (!display.ranges.empty()) {
                    display.ranges.back().footnoteControl = EditorFootnoteControlKind::Reference;
                }
                display.footnoteOverlays.push_back({
                    displayStart,
                    displayLength,
                    item.source_span,
                    item.special().semantic().footnote_label,
                    EditorFootnoteControlKind::Reference});
                continue;
            }
            if (item.kind == elmd::InlineRenderItem::Kind::Marker && item.source_span.source_range.empty())
            {
                auto markerPosition = elmd::TextPosition{
                    item.source_span.container_id,
                    item.source_span.source_range.start,
                    item.special().generated_boundary_affinity.value_or(elmd::TextAffinity::Upstream),
                };
                if (item.special().marker_role == elmd::MarkerRole::TaskCheckbox)
                {
                    auto const& markerText = item.special().display_text.empty()
                        ? item.text : item.special().display_text;
                    auto prefixLength = markerText.size() >= item.text.size()
                        && std::equal(item.text.rbegin(), item.text.rend(), markerText.rbegin())
                        ? markerText.size() - item.text.size()
                        : std::size_t{0};
                    if (prefixLength > 0)
                    {
                        AppendGeneratedText(
                            display,
                            markerText.substr(0, prefixLength),
                            markerPosition,
                            item.style,
                            EditorDisplayPositionKind::BoundaryDecoration);
                    }
                    auto displayStart = static_cast<std::uint32_t>(elmd::utf16_len(display.text));
                    AppendGeneratedText(
                        display,
                        U"\uFFFC",
                        markerPosition,
                        item.style,
                        EditorDisplayPositionKind::BoundaryDecoration);
                    auto boxSize = (std::clamp)(fontSize * 0.86f, 14.0f, 18.0f);
                    auto height = (std::max)(boxSize, fontSize * 1.2f);
                    display.taskCheckboxOverlays.push_back({
                        displayStart,
                        markerPosition,
                        item.special().task_checked,
                        boxSize + (std::max)(7.0f, fontSize * 0.42f),
                        height,
                        fontSize * 0.95f,
                        boxSize,
                    });
                    continue;
                }
                auto const& markerText = item.special().display_text.empty()
                    ? item.text : item.special().display_text;
                auto displayStart = static_cast<std::uint32_t>(elmd::utf16_len(display.text));
                AppendGeneratedText(
                    display,
                    markerText,
                    markerPosition,
                    item.style,
                    EditorDisplayPositionKind::BoundaryDecoration);
                if (item.special().marker_role == elmd::MarkerRole::FootnoteLabel)
                {
                    auto displayLength = static_cast<std::uint32_t>(elmd::utf16_len(markerText));
                    if (!display.ranges.empty()) {
                        display.ranges.back().footnoteControl = EditorFootnoteControlKind::DefinitionLabel;
                    }
                    display.footnoteOverlays.push_back({
                        displayStart,
                        displayLength,
                        item.source_span,
                        item.special().semantic().footnote_label,
                        EditorFootnoteControlKind::DefinitionLabel});
                }
            }
            else
            {
                auto rangeStart = display.ranges.size();
                AppendDisplayText(display, InlineText(item), item.source_span, item.style, item.kind == elmd::InlineRenderItem::Kind::Marker);
                auto syntax = SourceSyntax(item.source_syntax);
                for (auto rangeIndex = rangeStart; rangeIndex < display.ranges.size(); ++rangeIndex)
                {
                    display.ranges[rangeIndex].syntax = syntax;
                }
            }
        }
        if (!display.text.empty() && display.text.back() == U'\n') AppendGeneratedText(display, U"\u200B", sourceEnd, elmd::InlineStyle::plain());
        display.displayToSource.push_back(sourceEnd);
        return display;
    }

}
