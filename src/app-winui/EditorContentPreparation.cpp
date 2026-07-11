#include "pch.h"
#include "EditorSession.h"

import elmd.core.render_model;
import elmd.core.utf;

#include "EditorContentPreparation.h"

namespace winrt::ElMd
{
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

    class MathInlineObject final : public ::Microsoft::WRL::RuntimeClass<::Microsoft::WRL::RuntimeClassFlags<::Microsoft::WRL::ClassicCom>, IDWriteInlineObject>
    {
    public:
        MathInlineObject(float width, float height, float baseline) : width(width), height(height), baseline(baseline) {}

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
            *before = DWRITE_BREAK_CONDITION_NEUTRAL;
            *after = DWRITE_BREAK_CONDITION_NEUTRAL;
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

    bool CaretTouchesRange(std::size_t caret, elmd::CharRange const& range)
    {
        auto start = range.start.v;
        auto end = range.end.v;
        if (start <= caret && caret <= end)
        {
            return true;
        }
        return (start > 0 && caret == start - 1) || (end < (std::numeric_limits<std::size_t>::max)() && caret == end + 1);
    }

    std::vector<bool> RevealedStyleMarkers(std::vector<elmd::InlineRenderItem> const& items, std::size_t caret)
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
                    auto reveal = items[openIndex].source_range.start.v <= caret && caret <= item.source_range.end.v;
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
            auto reveal = items[openIndex].source_range.start.v <= caret && caret <= item.source_range.end.v;
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

    void AppendDisplayText(DisplayInlineText& display, std::u32string const& text, std::size_t sourceStart, elmd::InlineStyle style, bool marker)
    {
        auto start = static_cast<UINT32>(display.text.size());
        display.text += text;
        auto length = static_cast<UINT32>(display.text.size()) - start;
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            display.displayToSource.push_back(sourceStart + index);
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

    void AppendGeneratedText(DisplayInlineText& display, std::u32string const& text, std::size_t sourceOffset, elmd::InlineStyle style)
    {
        auto start = static_cast<UINT32>(display.text.size());
        display.text += text;
        display.displayToSource.insert(display.displayToSource.end(), text.size(), sourceOffset);
        if (!text.empty()) display.ranges.push_back(InlineStyleRange{start, static_cast<UINT32>(text.size()), style, false, SyntaxHighlightKind::None});
    }

    void MergeDisplayText(DisplayInlineText& target, DisplayInlineText source)
    {
        auto offset = static_cast<UINT32>(target.text.size());
        auto characterCount = source.text.size();
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
            preview.displayStart += offset;
            target.mathPreviews.push_back(std::move(preview));
        }
        for (auto& overlay : source.imageOverlays)
        {
            overlay.displayStart += offset;
            target.imageOverlays.push_back(std::move(overlay));
        }
    }

    void AppendSourceText(DisplayInlineText& display, std::u32string_view sourceText, std::size_t start, std::size_t end, elmd::InlineStyle style, bool marker)
    {
        start = (std::min)(start, sourceText.size());
        end = (std::min)((std::max)(end, start), sourceText.size());
        AppendDisplayText(display, std::u32string(sourceText.begin() + start, sourceText.begin() + end), start, style, marker);
    }

    void AppendMathPlaceholder(DisplayInlineText& display, std::size_t count, std::size_t sourceOffset)
    {
        auto start = static_cast<UINT32>(display.text.size());
        display.text.append(count, U'\u2007');
        display.displayToSource.insert(display.displayToSource.end(), count, sourceOffset);
        if (count > 0)
        {
            display.ranges.push_back(InlineStyleRange{ start, static_cast<UINT32>(count), elmd::InlineStyle::plain(), false, SyntaxHighlightKind::None });
        }
    }

    void AppendMathFragments(DisplayInlineText& display, MathJaxSvg const& math, std::size_t sourceStart, std::size_t sourceEnd, bool editing, elmd::InlineStyle style)
    {
        if (editing)
        {
            AppendDisplayText(display, U"\u2003", sourceEnd, style, false);
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
                AppendDisplayText(display, U"\u200B", mappedOffset, style, false);
            }
            auto start = static_cast<UINT32>(display.text.size());
            display.text.push_back(U'\uFFFC');
            display.displayToSource.push_back(mappedOffset);
            display.ranges.push_back(InlineStyleRange{ start, 1, style, false, SyntaxHighlightKind::None });
            auto fragmentStart = math.width > 0.0f ? progress / math.width : 0.0f;
            progress += fragment.breakSpace + fragment.width;
            auto fragmentEnd = math.width > 0.0f ? progress / math.width : 1.0f;
            display.mathOverlays.push_back(DisplayInlineText::MathOverlay{ start, fragment, fragment.breakSpace, sourceStart, sourceEnd, fragmentStart, fragmentEnd, style.strikethrough });
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
            auto object = ::Microsoft::WRL::Make<MathInlineObject>(overlay.leadingSpace + fragment.width, fragment.height, baseline);
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

    DisplayInlineText BuildDisplayInlineText(
        std::vector<elmd::InlineRenderItem> const& items,
        std::size_t caret,
        std::size_t sourceEnd,
        std::u32string_view sourceText,
        MathJaxRenderer& mathJax,
        SvgNormalizer& svgNormalizer,
        D2D1_COLOR_F svgColor,
        float fontSize,
        float containerWidth,
        bool svgSupported,
        bool requestMath,
        std::optional<elmd::CharRange> focusRange)
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
            if (IsHeadingMarker(item) && (!focusRange || !(focusRange->start.v <= caret && caret <= focusRange->end.v)))
            {
                continue;
            }
            if (item.kind == elmd::InlineRenderItem::Kind::Link)
            {
                if (CaretTouchesRange(caret, item.source_range))
                {
                    AppendSourceText(display, sourceText, item.source_range.start.v, item.source_range.end.v, item.style, false);
                }
                else
                {
                    MergeDisplayText(display, BuildDisplayInlineText(item.children, caret, item.source_range.end.v, sourceText, mathJax, svgNormalizer, svgColor, fontSize, containerWidth, svgSupported, requestMath, focusRange));
                }
                continue;
            }
            if (item.kind == elmd::InlineRenderItem::Kind::Image)
            {
                if (CaretTouchesRange(caret, item.source_range)) AppendSourceText(display, sourceText, item.source_range.start.v, item.source_range.end.v, item.style, false);
                else
                {
                    auto displayStart = static_cast<std::uint32_t>(display.text.size());
                    AppendGeneratedText(display, U"\uFFFC", item.source_range.start.v, item.style);
                    display.imageOverlays.push_back(DisplayInlineText::ImageOverlay{ displayStart, item.source_range.start.v, item.source_range.end.v, item.src, item.alt, item.image_width, item.image_height });
                }
                continue;
            }
            if (item.kind == elmd::InlineRenderItem::Kind::Math)
            {
                if (!svgSupported)
                {
                    AppendSourceText(display, sourceText, item.source_range.start.v, item.source_range.end.v, item.style, false);
                    continue;
                }
                auto rawMath = mathJax.GetOrQueue(elmd::cps_to_utf8(item.text), false, fontSize, containerWidth, requestMath);
                auto math = rawMath ? NormalizeMathJaxSvg(*rawMath, svgNormalizer, svgColor, fontSize, requestMath) : std::nullopt;
                auto editing = CaretTouchesRange(caret, item.source_range);
                auto delimiterLength = item.math_delim == elmd::MathDelimiter::InlineParen ? std::size_t{2} : std::size_t{1};
                auto contentStart = (std::min)(item.source_range.start.v + delimiterLength, item.source_range.end.v);
                auto contentEnd = item.source_range.end.v >= delimiterLength
                    ? (std::max)(contentStart, item.source_range.end.v - delimiterLength)
                    : contentStart;
                if (!math || !static_cast<bool>(*math))
                {
                    AppendSourceText(display, sourceText, item.source_range.start.v, item.source_range.end.v, item.style, false);
                    continue;
                }

                if (editing)
                {
                    auto displayStart = static_cast<std::uint32_t>(display.text.size());
                    AppendSourceText(display, sourceText, item.source_range.start.v, item.source_range.end.v, item.style, false);
                    display.mathPreviews.push_back(DisplayInlineText::MathPreview{
                        *math,
                        displayStart,
                        static_cast<std::uint32_t>(item.source_range.len()),
                        item.source_range.start.v,
                        item.source_range.end.v,
                        contentStart,
                        contentEnd,
                        item.style.strikethrough,
                    });
                    continue;
                }
                AppendMathFragments(display, *math, contentStart, contentEnd, false, item.style);
                continue;
            }
            if (item.kind == elmd::InlineRenderItem::Kind::Marker && item.source_range.start == item.source_range.end && !item.display_text.empty())
            {
                AppendGeneratedText(display, item.display_text, item.source_range.start.v, item.style);
            }
            else
            {
                AppendDisplayText(display, InlineText(item), item.source_range.start.v, item.style, item.kind == elmd::InlineRenderItem::Kind::Marker);
            }
        }
        if (!display.text.empty() && display.text.back() == U'\n') AppendGeneratedText(display, U"\u200B", sourceEnd, elmd::InlineStyle::plain());
        display.displayToSource.push_back(sourceEnd);
        return display;
    }

    DisplayInlineText BuildCodeBlockText(elmd::RenderBlock const& block, std::size_t caret, std::u32string_view sourceText)
    {
        DisplayInlineText display;
        auto showFence = block.source_range.start.v <= caret && caret <= block.source_range.end.v;
        if (showFence)
        {
            AppendSourceText(display, sourceText, block.source_range.start.v, block.content_range.start.v, elmd::InlineStyle::plain(), true);
        }
        std::size_t contentVisibleEnd = block.content_range.end.v;
        if (!showFence && contentVisibleEnd > block.content_range.start.v && contentVisibleEnd <= sourceText.size() && sourceText[contentVisibleEnd - 1] == U'\n') --contentVisibleEnd;
        AppendSourceText(display, sourceText, block.content_range.start.v, contentVisibleEnd, elmd::InlineStyle::plain(), false);
        std::size_t visibleEnd = contentVisibleEnd;
        if (showFence)
        {
            visibleEnd = block.source_range.end.v;
            if (visibleEnd > block.content_range.end.v && visibleEnd <= sourceText.size() && sourceText[visibleEnd - 1] == U'\n') --visibleEnd;
            AppendSourceText(display, sourceText, block.content_range.end.v, visibleEnd, elmd::InlineStyle::plain(), true);
        }
        display.displayToSource.push_back(visibleEnd);
        return display;
    }

    DisplayInlineText BuildIndentedCodeBlockText(elmd::RenderBlock const& block, std::u32string_view sourceText)
    {
        DisplayInlineText display;
        for (std::size_t index = 0; index < block.code_marker_ranges.size(); ++index)
        {
            auto const& marker = block.code_marker_ranges[index];
            auto contentStart = (std::min)(marker.end.v, sourceText.size());
            auto contentEnd = contentStart;
            while (contentEnd < sourceText.size() && sourceText[contentEnd] != U'\n') ++contentEnd;
            AppendSourceText(display, sourceText, contentStart, contentEnd, elmd::InlineStyle::plain(), false);
            if (index + 1 < block.code_marker_ranges.size()) AppendDisplayText(display, U"\n", contentEnd, elmd::InlineStyle::plain(), false);
            else if (contentStart == contentEnd) AppendDisplayText(display, U" ", contentStart, elmd::InlineStyle::plain(), false);
        }
        display.displayToSource.push_back(block.content_range.end.v);
        return display;
    }

    std::size_t DisplayPositionForSource(std::vector<std::size_t> const& displayToSource, std::size_t sourceOffset)
    {
        if (displayToSource.empty())
        {
            return 0;
        }

        for (std::size_t index = 0; index < displayToSource.size(); ++index)
        {
            if (displayToSource[index] >= sourceOffset)
            {
                return index;
            }
        }
        return displayToSource.size() - 1;
    }
}
