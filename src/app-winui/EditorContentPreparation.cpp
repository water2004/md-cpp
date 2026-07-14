#include "pch.h"
#include "EditorSession.h"

import elmd.core.render_model;
import elmd.core.source_style;
import elmd.core.utf;

#include "EditorContentPreparation.h"

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

    std::wstring ToWide(std::u32string_view text)
    {
        std::wstring wide;
        wide.reserve(text.size());
        for (auto codepoint : text)
        {
            if (codepoint <= 0xFFFF)
            {
                wide.push_back(static_cast<wchar_t>(codepoint));
            }
            else
            {
                codepoint -= 0x10000;
                wide.push_back(static_cast<wchar_t>(0xD800 + (codepoint >> 10)));
                wide.push_back(static_cast<wchar_t>(0xDC00 + (codepoint & 0x3FF)));
            }
        }
        return wide;
    }

    std::string SvgColor(D2D1_COLOR_F color)
    {
        auto component = [](float value)
        {
            return static_cast<unsigned>((std::clamp)(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        char result[8]{};
        std::snprintf(result, sizeof(result), "#%02X%02X%02X", component(color.r), component(color.g), component(color.b));
        return result;
    }

    std::string ResolveSvgColor(std::string svg, D2D1_COLOR_F color)
    {
        auto replacement = SvgColor(color);
        constexpr std::string_view needle = "currentColor";
        std::size_t offset = 0;
        while ((offset = svg.find(needle, offset)) != std::string::npos)
        {
            svg.replace(offset, needle.size(), replacement);
            offset += replacement.size();
        }
        return svg;
    }

    std::optional<std::vector<std::uint8_t>> DecodeBase64(std::string_view source)
    {
        static constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        if (source.size() > 24 * 1024 * 1024) return std::nullopt;
        std::vector<std::uint8_t> result;
        result.reserve(source.size() * 3 / 4);
        std::uint32_t accumulator = 0;
        unsigned bits = 0;
        for (auto ch : source)
        {
            if (ch == '=') break;
            if (ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t') continue;
            auto position = alphabet.find(ch);
            if (position == std::string_view::npos) return std::nullopt;
            accumulator = (accumulator << 6) | static_cast<std::uint32_t>(position);
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                result.push_back(static_cast<std::uint8_t>((accumulator >> bits) & 0xff));
                if (result.size() > 16 * 1024 * 1024) return std::nullopt;
            }
        }
        return result;
    }

    std::optional<MathJaxSvg> NormalizeMathJaxSvg(MathJaxSvg const& source, SvgNormalizer& normalizer, D2D1_COLOR_F color, float fontSize, bool allowQueue)
    {
        MathJaxSvg result = source;
        for (auto& fragment : result.fragments)
        {
            if (!fragment.svg) return std::nullopt;
            auto normalized = normalizer.GetOrQueue(ResolveSvgColor(*fragment.svg, color), fontSize, allowQueue);
            if (!normalized) return std::nullopt;
            if (!static_cast<bool>(*normalized))
            {
                result.fragments.clear();
                result.error = normalized->error;
                return result;
            }
            fragment.renderId = normalized->id;
            fragment.svg = normalized->svg;
        }
        return result;
    }

    std::optional<MermaidSvg> NormalizeMermaidSvg(MermaidSvg const& source, SvgNormalizer& normalizer, bool allowQueue)
    {
        auto normalized = normalizer.GetOrQueue(source.svg, 16.0f, allowQueue);
        if (!normalized) return std::nullopt;
        MermaidSvg result = source;
        if (static_cast<bool>(*normalized))
        {
            result.renderId = normalized->id;
            result.svg = *normalized->svg;
        }
        else
        {
            result.svg.clear();
            result.error = normalized->error;
        }
        return result;
    }

    std::u32string InlineText(elmd::InlineRenderItem const& item)
    {
        switch (item.kind)
        {
            case elmd::InlineRenderItem::Kind::Text:
                return item.text;
            case elmd::InlineRenderItem::Kind::Marker:
                return item.display_text.empty() ? item.text : item.display_text;
            case elmd::InlineRenderItem::Kind::Math:
                return U"$" + item.text + U"$";
            case elmd::InlineRenderItem::Kind::Image:
                return item.alt.empty() ? U"image" : elmd::utf8_to_cps(item.alt);
            case elmd::InlineRenderItem::Kind::Link: {
                std::u32string text;
                for (auto const& child : item.children)
                {
                    text += InlineText(child);
                }
                return text;
            }
            case elmd::InlineRenderItem::Kind::FootnoteReference:
                return item.display_text.empty()
                    ? elmd::utf8_to_cps(item.footnote_label)
                    : item.display_text;
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

    class MathInlineObject final : public ::Microsoft::WRL::RuntimeClass<::Microsoft::WRL::RuntimeClassFlags<::Microsoft::WRL::ClassicCom>, IDWriteInlineObject>
    {
    public:
        MathInlineObject(float width, float height, float baseline, bool breakBefore = false)
            : width(width), height(height), baseline(baseline), breakBefore(breakBefore) {}

        IFACEMETHODIMP Draw(void*, IDWriteTextRenderer*, FLOAT, FLOAT, BOOL, BOOL, IUnknown*) override
        {
            return S_OK;
        }

        IFACEMETHODIMP GetMetrics(DWRITE_INLINE_OBJECT_METRICS* metrics) override
        {
            if (!metrics) return E_POINTER;
            metrics->width = width;
            metrics->height = height;
            metrics->baseline = baseline;
            metrics->supportsSideways = FALSE;
            return S_OK;
        }

        IFACEMETHODIMP GetOverhangMetrics(DWRITE_OVERHANG_METRICS* overhangs) override
        {
            if (!overhangs) return E_POINTER;
            *overhangs = {};
            return S_OK;
        }

        IFACEMETHODIMP GetBreakConditions(DWRITE_BREAK_CONDITION* before, DWRITE_BREAK_CONDITION* after) override
        {
            if (!before || !after) return E_POINTER;
            *before = breakBefore ? DWRITE_BREAK_CONDITION_CAN_BREAK : DWRITE_BREAK_CONDITION_NEUTRAL;
            *after = DWRITE_BREAK_CONDITION_NEUTRAL;
            return S_OK;
        }

    private:
        float width;
        float height;
        float baseline;
        bool breakBefore;
    };

    class IndentInlineObject final : public ::Microsoft::WRL::RuntimeClass<::Microsoft::WRL::RuntimeClassFlags<::Microsoft::WRL::ClassicCom>, IDWriteInlineObject>
    {
    public:
        IndentInlineObject(float width, float height, float baseline) : width(width), height(height), baseline(baseline) {}

        IFACEMETHODIMP Draw(void*, IDWriteTextRenderer*, FLOAT, FLOAT, BOOL, BOOL, IUnknown*) override
        {
            return S_OK;
        }

        IFACEMETHODIMP GetMetrics(DWRITE_INLINE_OBJECT_METRICS* metrics) override
        {
            if (!metrics) return E_POINTER;
            metrics->width = width;
            metrics->height = height;
            metrics->baseline = baseline;
            metrics->supportsSideways = FALSE;
            return S_OK;
        }

        IFACEMETHODIMP GetOverhangMetrics(DWRITE_OVERHANG_METRICS* overhangs) override
        {
            if (!overhangs) return E_POINTER;
            *overhangs = {};
            return S_OK;
        }

        IFACEMETHODIMP GetBreakConditions(DWRITE_BREAK_CONDITION* before, DWRITE_BREAK_CONDITION* after) override
        {
            if (!before || !after) return E_POINTER;
            *before = DWRITE_BREAK_CONDITION_MUST_BREAK;
            *after = DWRITE_BREAK_CONDITION_MAY_NOT_BREAK;
            return S_OK;
        }

    private:
        float width;
        float height;
        float baseline;
    };

    bool IsStyleMarker(elmd::InlineRenderItem const& item)
    {
        if (item.kind != elmd::InlineRenderItem::Kind::Marker) return false;
        if (item.marker_role == elmd::MarkerRole::Heading) return false;
        if (item.marker_owner) return true;
        bool backticks = !item.text.empty();
        for (char32_t ch : item.text) if (ch != U'`') backticks = false;
        return item.text == U"*" || item.text == U"**" || item.text == U"~~" || backticks;
    }

    bool IsHeadingMarker(elmd::InlineRenderItem const& item)
    {
        if (item.kind == elmd::InlineRenderItem::Kind::Marker && item.marker_role == elmd::MarkerRole::Heading) return true;
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

    bool IsMermaidLanguage(std::optional<std::string> const& language)
    {
        if (!language) return false;
        std::string normalized = *language;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        return normalized == "mermaid";
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

            if (item.marker_owner)
            {
                auto found = owners.find(item.marker_owner->v);
                if (found == owners.end())
                {
                    owners.emplace(item.marker_owner->v, index);
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

    void ApplyMathInlineObjects(IDWriteTextLayout* layout, std::vector<DisplayInlineText::MathOverlay> const& overlays)
    {
        if (!layout) return;
        if (!overlays.empty()) layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_DEFAULT, 0.0f, 0.0f);
        for (auto const& overlay : overlays)
        {
            auto const& fragment = overlay.fragment;
            auto baseline = (std::clamp)(fragment.height + fragment.verticalAlign, 0.0f, fragment.height);
            auto object = ::Microsoft::WRL::Make<MathInlineObject>(
                overlay.leadingSpace + fragment.width,
                fragment.height,
                baseline,
                fragment.breakBefore);
            if (object)
            {
                layout->SetInlineObject(object.Get(), DWRITE_TEXT_RANGE{ overlay.displayStart, 1 });
            }
        }
    }

    void ApplyInlinePlaceholder(IDWriteTextLayout* layout, UINT32 displayStart, float width, float height, float baseline)
    {
        if (!layout) return;
        auto object = ::Microsoft::WRL::Make<MathInlineObject>(width, height, baseline);
        if (object) layout->SetInlineObject(object.Get(), DWRITE_TEXT_RANGE{ displayStart, 1 });
    }

    void ApplyTaskCheckboxInlineObjects(
        IDWriteTextLayout* layout,
        std::vector<DisplayInlineText::TaskCheckboxOverlay> const& overlays)
    {
        if (!layout) return;
        for (auto const& overlay : overlays)
            ApplyInlinePlaceholder(layout, overlay.displayStart, overlay.advance, overlay.height, overlay.baseline);
    }

    void ApplyIndentInlineObjects(IDWriteTextLayout* layout, std::vector<DisplayInlineText::IndentOverlay> const& overlays)
    {
        if (!layout) return;
        for (auto const& overlay : overlays)
        {
            auto object = ::Microsoft::WRL::Make<IndentInlineObject>(overlay.width, 1.0f, 1.0f);
            if (object) layout->SetInlineObject(object.Get(), DWRITE_TEXT_RANGE{ overlay.displayStart, 1 });
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
                    AppendSourceText(display, item.source_text, item.source_span, item.style, false);
                }
                else
                {
                    MergeDisplayText(display, BuildDisplayInlineText(item.children, caret, {item.source_span.container_id, item.source_span.source_range.end, elmd::TextAffinity::Downstream}, mathJax, svgNormalizer, svgColor, fontSize, containerWidth, svgSupported, requestMath));
                }
                continue;
            }
            if (item.kind == elmd::InlineRenderItem::Kind::Image)
            {
                if (CaretTouchesSpan(caret, item.source_span)) AppendSourceText(display, item.source_text, item.source_span, item.style, false);
                else
                {
                    auto displayStart = static_cast<std::uint32_t>(elmd::utf16_len(display.text));
                    AppendGeneratedText(display, U"\uFFFC", {item.source_span.container_id, item.source_span.source_range.start, elmd::TextAffinity::Downstream}, item.style);
                    display.imageOverlays.push_back(DisplayInlineText::ImageOverlay{ displayStart, item.source_span, item.src, item.alt, item.image_width, item.image_height });
                }
                continue;
            }
            if (item.kind == elmd::InlineRenderItem::Kind::Math)
            {
                if (!svgSupported)
                {
                    AppendSourceText(display, item.source_text, item.source_span, item.style, false);
                    continue;
                }
                auto displayMath = item.display == elmd::MathDisplayMode::Block;
                auto rawMath = mathJax.GetOrQueue(
                    elmd::cps_to_utf8(item.text),
                    displayMath,
                    fontSize,
                    containerWidth,
                    requestMath);
                auto math = rawMath ? NormalizeMathJaxSvg(*rawMath, svgNormalizer, svgColor, fontSize, requestMath) : std::nullopt;
                display.pendingMath = display.pendingMath || !rawMath || !math;
                auto editing = CaretTouchesSpan(caret, item.source_span);
                auto delimiterLength = item.display == elmd::MathDisplayMode::Block
                    ? std::size_t{0}
                    : item.math_delim == elmd::MathDelimiter::InlineParen ? std::size_t{2} : std::size_t{1};
                auto contentStart = (std::min)(item.source_span.source_range.start + delimiterLength, item.source_span.source_range.end);
                auto contentEnd = item.source_span.source_range.end >= delimiterLength
                    ? (std::max)(contentStart, item.source_span.source_range.end - delimiterLength)
                    : contentStart;
                if (!math || !static_cast<bool>(*math))
                {
                    AppendSourceText(display, item.source_text, item.source_span, item.style, false);
                    continue;
                }

                if (editing)
                {
                    AppendSourceText(display, item.source_text, item.source_span, item.style, false);
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
                auto ordinal = item.display_text.empty()
                    ? elmd::utf8_to_cps(item.footnote_label)
                    : item.display_text;
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
                    item.footnote_label,
                    EditorFootnoteControlKind::Reference});
                continue;
            }
            if (item.kind == elmd::InlineRenderItem::Kind::Marker && item.source_span.source_range.empty())
            {
                auto markerPosition = elmd::TextPosition{
                    item.source_span.container_id,
                    item.source_span.source_range.start,
                    item.generated_boundary_affinity.value_or(elmd::TextAffinity::Upstream),
                };
                if (item.marker_role == elmd::MarkerRole::TaskCheckbox)
                {
                    auto const& markerText = item.display_text.empty() ? item.text : item.display_text;
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
                        item.task_checked,
                        boxSize + (std::max)(7.0f, fontSize * 0.42f),
                        height,
                        fontSize * 0.95f,
                        boxSize,
                    });
                    continue;
                }
                auto const& markerText = item.display_text.empty() ? item.text : item.display_text;
                auto displayStart = static_cast<std::uint32_t>(elmd::utf16_len(display.text));
                AppendGeneratedText(
                    display,
                    markerText,
                    markerPosition,
                    item.style,
                    EditorDisplayPositionKind::BoundaryDecoration);
                if (item.marker_role == elmd::MarkerRole::FootnoteLabel)
                {
                    auto displayLength = static_cast<std::uint32_t>(elmd::utf16_len(markerText));
                    if (!display.ranges.empty()) {
                        display.ranges.back().footnoteControl = EditorFootnoteControlKind::DefinitionLabel;
                    }
                    display.footnoteOverlays.push_back({
                        displayStart,
                        displayLength,
                        item.source_span,
                        item.footnote_label,
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

    DisplayInlineText BuildCodeBlockText(elmd::RenderBlock const& block, elmd::TextPosition caret, TreeSitterHighlighter& highlighter)
    {
        DisplayInlineText display;
        // An indented code block has no user-visible delimiter. Its leading
        // indentation is structural Markdown syntax and must stay hidden even
        // while the block owns the caret. content_to_source keeps every
        // displayed boundary in the one authoritative source coordinate.
        auto editingRawFence = caret.container_id == block.id && !block.code_indented;
        auto code = block.code_text;
        if (!editingRawFence && !code.empty() && code.back() == U'\n') code.pop_back();
        if (editingRawFence)
        {
            AppendSourceText(
                display,
                block.raw_source,
                {block.id, {0, block.raw_source.size()}},
                elmd::InlineStyle::plain(),
                false);
        }
        else
        {
            AppendProjectedSourceText(
                display,
                code,
                block.id,
                block.content_to_source,
                elmd::InlineStyle::plain());
        }
        if (block.language && !code.empty())
        {
            auto highlights = highlighter.Highlight(*block.language, elmd::cps_to_utf8(code));
            for (auto const& highlight : highlights)
            {
                auto start = (std::min)(static_cast<std::size_t>(highlight.start), code.size());
                auto end = (std::min)(start + static_cast<std::size_t>(highlight.length), code.size());
                auto displayStart = editingRawFence
                    ? static_cast<std::uint32_t>(elmd::char_index_to_utf16(
                        block.raw_source,
                        block.content_to_source.empty() ? start : block.content_to_source[start]))
                    : static_cast<std::uint32_t>(elmd::char_index_to_utf16(code, start));
                auto displayEnd = editingRawFence
                    ? static_cast<std::uint32_t>(elmd::char_index_to_utf16(
                        block.raw_source,
                        block.content_to_source.empty() ? end : block.content_to_source[end]))
                    : static_cast<std::uint32_t>(elmd::char_index_to_utf16(code, end));
                if (displayStart < displayEnd)
                {
                    display.ranges.push_back(InlineStyleRange{
                        displayStart,
                        displayEnd - displayStart,
                        elmd::InlineStyle::plain(),
                        false,
                        highlight.kind,
                    });
                }
            }
        }
        const auto endpoint = editingRawFence
            ? block.raw_source.size()
            : (block.content_to_source.empty()
                ? code.size()
                : block.content_to_source[(std::min)(code.size(), block.content_to_source.size() - 1)]);
        if (display.text.empty())
        {
            AppendGeneratedText(
                display,
                U"\u200B",
                {block.id, endpoint, elmd::TextAffinity::Downstream},
                elmd::InlineStyle::plain(),
                EditorDisplayPositionKind::Source);
        }
        display.displayToSource.push_back({block.id, endpoint, elmd::TextAffinity::Downstream});
        return display;
    }

    DisplayInlineText BuildMathBlockText(
        elmd::RenderBlock const& block,
        elmd::TextPosition caret,
        MathJaxRenderer& mathJax,
        SvgNormalizer& svgNormalizer,
        D2D1_COLOR_F svgColor,
        float fontSize,
        float containerWidth,
        bool svgSupported,
        bool requestMath)
    {
        DisplayInlineText display;
        auto span = block.content_span;
        auto editing = caret.container_id == block.id;
        if (!svgSupported)
        {
            const auto endpoint = editing
                ? block.raw_source.size()
                : block.content_to_source.empty()
                    ? block.tex.size()
                    : block.content_to_source.back();
            if (editing)
                AppendSourceText(display, block.raw_source, {block.id, {0, endpoint}}, elmd::InlineStyle::plain(), false);
            else
                AppendProjectedSourceText(display, block.tex, block.id, block.content_to_source, elmd::InlineStyle::plain());
            if (display.text.empty())
            {
                AppendGeneratedText(
                    display,
                    U"\u200B",
                    {block.id, endpoint, elmd::TextAffinity::Downstream},
                    elmd::InlineStyle::plain(),
                    EditorDisplayPositionKind::Source);
            }
            display.displayToSource.push_back({block.id, endpoint, elmd::TextAffinity::Downstream});
            return display;
        }

        auto rawMath = mathJax.GetOrQueue(elmd::cps_to_utf8(block.tex), true, fontSize, containerWidth, requestMath);
        auto math = rawMath ? NormalizeMathJaxSvg(*rawMath, svgNormalizer, svgColor, fontSize, requestMath) : std::nullopt;
        display.pendingMath = !rawMath || !math;
        if (editing)
        {
            AppendSourceText(
                display,
                block.raw_source,
                {block.id, {0, block.raw_source.size()}},
                elmd::InlineStyle::plain(),
                false);
            // The preview is rendered below this editable source as its own
            // non-interactive visual block. It must never enter the source
            // display mapping or become a caret target.
            if (math && static_cast<bool>(*math))
                display.mathPreviews.push_back(DisplayInlineText::MathPreview{
                    *math,
                    span,
                    false,
                });
        }
        else if (!math || !static_cast<bool>(*math))
        {
            AppendProjectedSourceText(
                display,
                block.tex,
                block.id,
                block.content_to_source,
                elmd::InlineStyle::plain());
        }
        else
        {
            AppendMathFragments(display, *math, span, false, elmd::InlineStyle::plain());
        }
        const auto endpoint = editing
            ? block.raw_source.size()
            : block.content_to_source.empty()
                ? block.tex.size()
                : block.content_to_source.back();
        if (display.text.empty())
        {
            AppendGeneratedText(
                display,
                U"\u200B",
                {block.id, endpoint, elmd::TextAffinity::Downstream},
                elmd::InlineStyle::plain(),
                EditorDisplayPositionKind::Source);
        }
        display.displayToSource.push_back({block.id, endpoint, elmd::TextAffinity::Downstream});
        return display;
    }

}
