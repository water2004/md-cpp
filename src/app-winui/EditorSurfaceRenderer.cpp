#include "pch.h"
#include "EditorSession.h"
#include "EditorSurfaceRenderer.h"

import elmd.core.render_model;
import elmd.core.table_edit;
import elmd.core.utf;

namespace winrt::ElMd
{
    D2D1_COLOR_F Rgba(float red, float green, float blue, float alpha = 1.0f)
    {
        return D2D1::ColorF(red, green, blue, alpha);
    }

    D2D1_COLOR_F Rgb(std::uint32_t value)
    {
        return Rgba(static_cast<float>((value >> 16) & 0xff) / 255.0f, static_cast<float>((value >> 8) & 0xff) / 255.0f, static_cast<float>(value & 0xff) / 255.0f);
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

    struct InlineStyleRange
    {
        UINT32 start = 0;
        UINT32 length = 0;
        elmd::InlineStyle style;
        bool marker = false;
        SyntaxHighlightKind syntax = SyntaxHighlightKind::None;
    };

    struct DisplayInlineText
    {
        struct MathOverlay
        {
            std::uint32_t displayStart = 0;
            MathJaxSvgFragment fragment;
            float leadingSpace = 0.0f;
            std::size_t sourceStart = 0;
            std::size_t sourceEnd = 0;
            float progressStart = 0.0f;
            float progressEnd = 1.0f;
            bool strikethrough = false;
        };

        struct MathPreview
        {
            MathJaxSvg svg;
            std::uint32_t displayStart = 0;
            std::uint32_t displayLength = 0;
            std::size_t sourceStart = 0;
            std::size_t sourceEnd = 0;
            std::size_t contentStart = 0;
            std::size_t contentEnd = 0;
            bool strikethrough = false;
        };

        std::u32string text;
        std::vector<std::size_t> displayToSource;
        std::vector<InlineStyleRange> ranges;
        std::vector<MathOverlay> mathOverlays;
        std::vector<MathPreview> mathPreviews;
    };

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
        bool backticks = !item.text.empty();
        for (char32_t ch : item.text) if (ch != U'`') backticks = false;
        return item.text == U"*" || item.text == U"**" || item.text == U"~~" || backticks;
    }

    bool IsHeadingMarker(elmd::InlineRenderItem const& item)
    {
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
        for (std::size_t index = 0; index < items.size(); ++index)
        {
            auto const& item = items[index];
            if (!IsStyleMarker(item))
            {
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
            if (IsHeadingMarker(item) && !(item.source_range.start.v <= caret && caret <= sourceEnd))
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
                    MergeDisplayText(display, BuildDisplayInlineText(item.children, caret, item.source_range.end.v, sourceText, mathJax, svgNormalizer, svgColor, fontSize, containerWidth, svgSupported, requestMath));
                }
                continue;
            }
            if (item.kind == elmd::InlineRenderItem::Kind::Image)
            {
                if (CaretTouchesRange(caret, item.source_range)) AppendSourceText(display, sourceText, item.source_range.start.v, item.source_range.end.v, item.style, false);
                else AppendGeneratedText(display, item.alt.empty() ? U"image" : elmd::utf8_to_cps(item.alt), item.source_range.start.v, item.style);
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
            AppendDisplayText(display, InlineText(item), item.source_range.start.v, item.style, item.kind == elmd::InlineRenderItem::Kind::Marker);
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

    EditorSurfaceRenderer::EditorStyleSheet EditorSurfaceRenderer::CreateStyleSheet(Theme value)
    {
        EditorStyleSheet sheet;
        sheet.body = FontStyle{ L"Microsoft YaHei UI", 18.0f, 29.0f, DWRITE_FONT_WEIGHT_NORMAL };
        sheet.heading1 = FontStyle{ L"Microsoft YaHei UI", 38.0f, 46.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD };
        sheet.heading2 = FontStyle{ L"Microsoft YaHei UI", 30.0f, 37.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD };
        sheet.heading3 = FontStyle{ L"Microsoft YaHei UI", 24.0f, 30.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD };
        sheet.code = FontStyle{ L"Cascadia Code", 15.0f, 24.0f, DWRITE_FONT_WEIGHT_NORMAL };

        if (value == Theme::Light)
        {
            sheet.canvasColor = Rgba(0.982f, 0.984f, 0.988f);
            sheet.textColor = Rgba(0.125f, 0.137f, 0.160f);
            sheet.mutedColor = Rgba(0.420f, 0.455f, 0.520f);
            sheet.accentColor = Rgba(0.145f, 0.388f, 0.922f);
            sheet.codeTextColor = Rgba(0.180f, 0.205f, 0.250f);
            sheet.panelColor = Rgba(0.940f, 0.945f, 0.955f);
            sheet.nestedQuoteColor = Rgba(0.958f, 0.961f, 0.968f);
            sheet.selectionColor = Rgba(0.370f, 0.570f, 0.960f, 0.30f);
            sheet.caretColor = Rgba(0.065f, 0.075f, 0.090f);
            sheet.syntaxColors = { sheet.codeTextColor, Rgb(0xAF00DB), Rgb(0x267F99), Rgb(0x795E26), Rgb(0xA31515), Rgb(0x098658), Rgb(0x008000), Rgb(0x303030), Rgb(0x800080), Rgb(0x001080), Rgb(0x0070C1) };
            return sheet;
        }

        sheet.canvasColor = Rgba(0.070f, 0.078f, 0.098f);
        sheet.textColor = Rgba(0.895f, 0.910f, 0.940f);
        sheet.mutedColor = Rgba(0.545f, 0.585f, 0.665f);
        sheet.accentColor = Rgba(0.480f, 0.635f, 0.970f);
        sheet.codeTextColor = Rgba(0.875f, 0.895f, 0.925f);
        sheet.panelColor = Rgba(0.100f, 0.113f, 0.140f);
        sheet.nestedQuoteColor = Rgba(0.085f, 0.096f, 0.119f);
        sheet.selectionColor = Rgba(0.255f, 0.390f, 0.700f, 0.44f);
        sheet.caretColor = Rgba(0.965f, 0.975f, 1.000f);
        sheet.syntaxColors = { sheet.codeTextColor, Rgb(0xC586C0), Rgb(0x4EC9B0), Rgb(0xDCDCAA), Rgb(0xCE9178), Rgb(0xB5CEA8), Rgb(0x6A9955), Rgb(0xD4D4D4), Rgb(0xC586C0), Rgb(0x9CDCFE), Rgb(0x4FC1FF) };
        return sheet;
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

    std::size_t SourceStart(elmd::RenderBlock const& block)
    {
        return block.content_range.start.v;
    }

    std::size_t SourceEnd(elmd::RenderBlock const& block, std::u32string const& text)
    {
        (void)text;
        return block.content_range.end.v;
    }


    float EditorSurfaceRenderer::CompositionScaleX(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel) const
    {
        return (std::max)(1.0f, panel.CompositionScaleX());
    }

    float EditorSurfaceRenderer::CompositionScaleY(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel) const
    {
        return (std::max)(1.0f, panel.CompositionScaleY());
    }

    void EditorSurfaceRenderer::ApplySwapChainTransform()
    {
        if (!swapChain)
        {
            return;
        }

        ::Microsoft::WRL::ComPtr<IDXGISwapChain2> swapChain2;
        if (SUCCEEDED(swapChain.As(&swapChain2)))
        {
            DXGI_MATRIX_3X2_F matrix{};
            matrix._11 = 1.0f / surfaceScaleX;
            matrix._22 = 1.0f / surfaceScaleY;
            winrt::check_hresult(swapChain2->SetMatrixTransform(&matrix));
        }
    }

    void EditorSurfaceRenderer::ResetTargets()
    {
        if (d2dContext)
        {
            d2dContext->SetTarget(nullptr);
            d2dContext->Flush();
        }
        if (d3dContext)
        {
            d3dContext->OMSetRenderTargets(0, nullptr, nullptr);
            d3dContext->Flush();
        }
        renderTargetView = nullptr;
        d2dTarget = nullptr;
        ResetBrushes();
    }

    void EditorSurfaceRenderer::ResetBrushes()
    {
        textBrush = nullptr;
        mutedBrush = nullptr;
        accentBrush = nullptr;
        codeBrush = nullptr;
        panelBrush = nullptr;
        canvasBrush = nullptr;
        nestedQuoteBrush = nullptr;
        selectionBrush = nullptr;
        caretBrush = nullptr;
        for (auto& brush : syntaxBrushes) brush = nullptr;
    }

    void EditorSurfaceRenderer::RebuildTextFormats()
    {
        if (!dwriteFactory)
        {
            return;
        }

        auto createFormat = [&](FontStyle const& font, ::Microsoft::WRL::ComPtr<IDWriteTextFormat>& target)
        {
            target = nullptr;
            winrt::check_hresult(dwriteFactory->CreateTextFormat(
                font.family.c_str(),
                nullptr,
                font.weight,
                font.style,
                DWRITE_FONT_STRETCH_NORMAL,
                font.size,
                L"en-us",
                target.GetAddressOf()));
            winrt::check_hresult(target->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP));
            winrt::check_hresult(target->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, font.lineHeight, font.size * 1.2f));
        };

        createFormat(styleSheet.body, textFormat);
        createFormat(styleSheet.heading1, heading1Format);
        createFormat(styleSheet.heading2, heading2Format);
        createFormat(styleSheet.heading3, heading3Format);
        createFormat(styleSheet.code, codeFormat);
    }

    void EditorSurfaceRenderer::SetTheme(Theme value)
    {
        if (theme == value)
        {
            return;
        }

        theme = value;
        styleSheet = CreateStyleSheet(value);
        blockHeightCache.clear();
        svgDocumentCache.clear();
        svgDocumentCacheOrder.clear();
        svgDocumentCacheBytes = 0;
        RebuildTextFormats();
        ResetBrushes();
    }

    void EditorSurfaceRenderer::SetInvalidateCallback(std::function<void()> callback)
    {
        invalidateCallback = std::move(callback);
    }

    void EditorSurfaceRenderer::InitializeMermaid(winrt::Microsoft::UI::Xaml::Controls::WebView2 const& webView)
    {
        auto dispatcher = webView.DispatcherQueue();
        mermaid.Initialize(webView, [this, dispatcher]
        {
            if (mathInvalidationQueued.exchange(true)) return;
            if (!dispatcher.TryEnqueue([this]
            {
                mathInvalidationQueued = false;
                if (invalidateCallback) invalidateCallback();
            }))
            {
                mathInvalidationQueued = false;
            }
        });
    }

    void EditorSurfaceRenderer::Initialize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel)
    {
        if (swapChain)
        {
            return;
        }

        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        winrt::check_hresult(D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            featureLevels,
            static_cast<UINT>(sizeof(featureLevels) / sizeof(featureLevels[0])),
            D3D11_SDK_VERSION,
            d3dDevice.GetAddressOf(),
            nullptr,
            d3dContext.GetAddressOf()));

        ::Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        winrt::check_hresult(d3dDevice.As(&dxgiDevice));

        D2D1_FACTORY_OPTIONS d2dOptions{};
#if defined(_DEBUG)
        d2dOptions.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
        winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dOptions, d2dFactory.GetAddressOf()));
        winrt::check_hresult(d2dFactory->CreateDevice(dxgiDevice.Get(), d2dDevice.GetAddressOf()));
        winrt::check_hresult(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dContext.GetAddressOf()));

        winrt::check_hresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwriteFactory.GetAddressOf())));
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wicFactory.GetAddressOf()))))
        {
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wicFactory.GetAddressOf()));
        }
        RebuildTextFormats();

        ::Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        winrt::check_hresult(dxgiDevice->GetAdapter(adapter.GetAddressOf()));

        ::Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
        winrt::check_hresult(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(factory.GetAddressOf())));

        surfaceScaleX = CompositionScaleX(panel);
        surfaceScaleY = CompositionScaleY(panel);
        surfaceWidthDip = static_cast<float>((std::max)(1.0, panel.ActualWidth()));
        surfaceHeightDip = static_cast<float>((std::max)(1.0, panel.ActualHeight()));
        auto width = (std::max)(uint32_t{ 1 }, static_cast<uint32_t>(std::ceil(panel.ActualWidth() * surfaceScaleX)));
        auto height = (std::max)(uint32_t{ 1 }, static_cast<uint32_t>(std::ceil(panel.ActualHeight() * surfaceScaleY)));

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.Stereo = false;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        desc.Flags = 0;

        winrt::check_hresult(factory->CreateSwapChainForComposition(d3dDevice.Get(), &desc, nullptr, swapChain.GetAddressOf()));
        ApplySwapChainTransform();

        auto panelNative = panel.as<ISwapChainPanelNative>();
        winrt::check_hresult(panelNative->SetSwapChain(swapChain.Get()));

        surfaceWidth = width;
        surfaceHeight = height;
        auto dispatcher = panel.DispatcherQueue();
        renderDispatcher = dispatcher;
        auto completion = [this, dispatcher]
        {
            if (mathInvalidationQueued.exchange(true)) return;
            if (!dispatcher.TryEnqueue([this]
            {
                mathInvalidationQueued = false;
                if (invalidateCallback) invalidateCallback();
            }))
            {
                mathInvalidationQueued = false;
            }
        };
        mathJax.SetCompletionCallback(completion);
        svgNormalizer.SetCompletionCallback(std::move(completion));
    }

    void EditorSurfaceRenderer::Resize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel, double width, double height)
    {
        if (!swapChain || resizing || !std::isfinite(width) || !std::isfinite(height) || width <= 0.0 || height <= 0.0)
        {
            return;
        }

        resizing = true;
        struct ResetFlag { bool& value; ~ResetFlag() { value = false; } } resetFlag{ resizing };

        auto newScaleX = CompositionScaleX(panel);
        auto newScaleY = CompositionScaleY(panel);
        auto newWidthDip = static_cast<float>((std::max)(1.0, width));
        auto newHeightDip = static_cast<float>((std::max)(1.0, height));
        auto newWidth = (std::max)(uint32_t{ 1 }, static_cast<uint32_t>(std::ceil(width * newScaleX)));
        auto newHeight = (std::max)(uint32_t{ 1 }, static_cast<uint32_t>(std::ceil(height * newScaleY)));
        if (newWidth == surfaceWidth && newHeight == surfaceHeight && newWidthDip == surfaceWidthDip && newHeightDip == surfaceHeightDip && newScaleX == surfaceScaleX && newScaleY == surfaceScaleY)
        {
            return;
        }

        if (newWidthDip != surfaceWidthDip) blockHeightCache.clear();
        ResetTargets();
        auto resized = swapChain->ResizeBuffers(0, newWidth, newHeight, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(resized)) return;
        surfaceWidth = newWidth;
        surfaceHeight = newHeight;
        surfaceWidthDip = newWidthDip;
        surfaceHeightDip = newHeightDip;
        surfaceScaleX = newScaleX;
        surfaceScaleY = newScaleY;
        ApplySwapChainTransform();
        auto maxScroll = MaximumScrollOffset();
        scrollOffset = (std::min)(scrollOffset, maxScroll);
        scrollTarget = (std::min)(scrollTarget, maxScroll);
    }

    void EditorSurfaceRenderer::QueueRemoteImage(std::string source)
    {
        {
            std::scoped_lock lock(remoteImageMutex);
            if (remoteImageData.contains(source) || remoteImagePending.contains(source) || remoteImageFailed.contains(source)) return;
            remoteImagePending.insert(source);
        }
        try
        {
            auto operation = winrt::Windows::Web::Http::HttpClient{}.GetBufferAsync(winrt::Windows::Foundation::Uri(winrt::to_hstring(source)));
            operation.Completed([this, source = std::move(source)](auto const& async, winrt::Windows::Foundation::AsyncStatus status)
            {
                std::vector<std::uint8_t> bytes;
                if (status == winrt::Windows::Foundation::AsyncStatus::Completed)
                {
                    try
                    {
                        auto buffer = async.GetResults();
                        if (buffer.Length() <= 16 * 1024 * 1024)
                        {
                            bytes.resize(buffer.Length());
                            auto reader = winrt::Windows::Storage::Streams::DataReader::FromBuffer(buffer);
                            reader.ReadBytes(winrt::array_view<std::uint8_t>(bytes));
                        }
                    }
                    catch (...) {}
                }
                {
                    std::scoped_lock lock(remoteImageMutex);
                    remoteImagePending.erase(source);
                    if (bytes.empty())
                    {
                        remoteImageFailed.insert(source);
                    }
                    else
                    {
                        while (!remoteImageOrder.empty() && (remoteImageData.size() >= 16 || remoteImageBytes + bytes.size() > 32 * 1024 * 1024))
                        {
                            auto oldest = std::move(remoteImageOrder.front());
                            remoteImageOrder.pop_front();
                            auto found = remoteImageData.find(oldest);
                            if (found == remoteImageData.end()) continue;
                            remoteImageBytes -= found->second.size();
                            remoteImageData.erase(found);
                        }
                        if (bytes.size() <= 32 * 1024 * 1024)
                        {
                            remoteImageBytes += bytes.size();
                            remoteImageOrder.push_back(source);
                            remoteImageData.emplace(source, std::move(bytes));
                        }
                    }
                }
                if (renderDispatcher)
                {
                    renderDispatcher.TryEnqueue([this]
                    {
                        blockHeightCache.clear();
                        if (invalidateCallback) invalidateCallback();
                    });
                }
            });
        }
        catch (...)
        {
            std::scoped_lock lock(remoteImageMutex);
            remoteImagePending.erase(source);
            remoteImageFailed.insert(std::move(source));
        }
    }

    std::optional<EditorSurfaceRenderer::CachedRasterImage> EditorSurfaceRenderer::LoadRasterImage(std::wstring const& baseDirectory, std::string_view source)
    {
        if (!wicFactory || !d2dContext || source.empty()) return std::nullopt;
        std::wstring key;
        std::vector<std::uint8_t> encoded;
        auto remote = source.starts_with("https://") || source.starts_with("http://");
        auto data = source.starts_with("data:image/");
        std::filesystem::path path;
        if (remote)
        {
            key = L"url:" + std::wstring(winrt::to_hstring(std::string(source)));
            if (auto found = rasterImageCache.find(key); found != rasterImageCache.end()) return found->second;
            if (rasterImageFailed.contains(key)) return std::nullopt;
            {
                std::scoped_lock lock(remoteImageMutex);
                auto found = remoteImageData.find(std::string(source));
                if (found != remoteImageData.end()) encoded = found->second;
            }
            if (encoded.empty())
            {
                QueueRemoteImage(std::string(source));
                return std::nullopt;
            }
        }
        else if (data)
        {
            auto comma = source.find(',');
            if (comma == std::string_view::npos || source.substr(0, comma).find(";base64") == std::string_view::npos) return std::nullopt;
            key = L"data:" + std::to_wstring(source.size()) + L":" + std::to_wstring(std::hash<std::string_view>{}(source));
            if (auto found = rasterImageCache.find(key); found != rasterImageCache.end()) return found->second;
            if (rasterImageFailed.contains(key)) return std::nullopt;
            auto decoded = DecodeBase64(source.substr(comma + 1));
            if (!decoded || decoded->empty()) return std::nullopt;
            encoded = std::move(*decoded);
        }
        else
        {
            auto sourceText = winrt::to_hstring(std::string(source));
            path = std::filesystem::path(sourceText.c_str());
            if (path.has_root_directory() && !path.has_root_name() && !baseDirectory.empty()) path = std::filesystem::path(baseDirectory) / path.relative_path();
            if (path.is_relative())
            {
                if (baseDirectory.empty()) return std::nullopt;
                path = std::filesystem::path(baseDirectory) / path;
            }
            std::error_code error;
            auto absolute = std::filesystem::weakly_canonical(path, error);
            if (error) absolute = path.lexically_normal();
            key = absolute.wstring();
            path = std::move(absolute);
        }
        if (auto found = rasterImageCache.find(key); found != rasterImageCache.end()) return found->second;
        if (rasterImageFailed.contains(key)) return std::nullopt;
        auto fail = [&]() -> std::optional<CachedRasterImage>
        {
            rasterImageFailed.insert(key);
            return std::nullopt;
        };

        ::Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        ::Microsoft::WRL::ComPtr<IWICStream> stream;
        if (!encoded.empty())
        {
            if (encoded.size() > (std::numeric_limits<DWORD>::max)()) return fail();
            if (FAILED(wicFactory->CreateStream(stream.GetAddressOf())) || FAILED(stream->InitializeFromMemory(encoded.data(), static_cast<DWORD>(encoded.size())))) return fail();
            if (FAILED(wicFactory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()))) return fail();
        }
        else if (FAILED(wicFactory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()))) return fail();
        ::Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) return fail();
        UINT width = 0;
        UINT height = 0;
        if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0) return fail();
        auto pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
        if (pixels > 16ull * 1024ull * 1024ull) return fail();
        ::Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        if (FAILED(wicFactory->CreateFormatConverter(converter.GetAddressOf()))) return fail();
        if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut))) return fail();
        CachedRasterImage image;
        if (FAILED(d2dContext->CreateBitmapFromWicBitmap(converter.Get(), nullptr, image.bitmap.GetAddressOf())) || !image.bitmap) return fail();
        image.width = static_cast<float>(width);
        image.height = static_cast<float>(height);
        image.bytes = static_cast<std::size_t>(pixels * 4);
        while (!rasterImageCacheOrder.empty() && (rasterImageCache.size() >= 32 || rasterImageCacheBytes + image.bytes > 64 * 1024 * 1024))
        {
            auto oldest = std::move(rasterImageCacheOrder.front());
            rasterImageCacheOrder.pop_front();
            auto found = rasterImageCache.find(oldest);
            if (found == rasterImageCache.end()) continue;
            rasterImageCacheBytes -= found->second.bytes;
            rasterImageCache.erase(found);
        }
        if (image.bytes <= 64 * 1024 * 1024)
        {
            rasterImageCacheBytes += image.bytes;
            rasterImageCacheOrder.push_back(key);
            rasterImageCache.emplace(key, image);
        }
        return image;
    }

    void EditorSurfaceRenderer::DrawDocument(detail::EditorSessionCore const& sessionCore)
    {
        visualBlocks.clear();
        visualLines.clear();
        visualTables.clear();
        visualMathHits.clear();
        visualBlocks.reserve(sessionCore.renderModel.blocks.size());
        if (blockHeightCache.size() > 32768) blockHeightCache.clear();
        auto responsivePadding = (std::min)(styleSheet.horizontalPadding, (std::max)(12.0f, surfaceWidthDip * 0.06f));
        auto documentLeft = responsivePadding;
        auto documentTop = styleSheet.verticalPadding;
        auto documentRight = (std::max)(documentLeft + 1.0f, (std::min)(surfaceWidthDip - responsivePadding - 14.0f, documentLeft + styleSheet.documentWidth));
        auto y = documentTop - scrollOffset;
        auto selection = sessionCore.editor.selection().normalized_range();
        auto caret = sessionCore.editor.selection().active.v;
        auto sourceText = sessionCore.editor.buffer().text_cps();
        std::unordered_set<std::uint64_t> mathFallbacks;
        ::Microsoft::WRL::ComPtr<ID2D1DeviceContext5> svgContext;
        auto svgSupported = SUCCEEDED(d2dContext.As(&svgContext)) && svgContext;

        auto drawSvg = [&](std::uint64_t renderId, std::string const& source, float width, float height, D2D1_POINT_2F origin) -> bool
        {
            if (renderId == 0 || source.empty() || width <= 0.0f || height <= 0.0f || !svgSupported)
            {
                return false;
            }
            if (origin.x + width < 0.0f || origin.y + height < 0.0f || origin.x > surfaceWidthDip || origin.y > surfaceHeightDip)
            {
                return true;
            }

            ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> document;
            if (auto found = svgDocumentCache.find(renderId); found != svgDocumentCache.end())
            {
                document = found->second.document;
                if (auto order = std::find(svgDocumentCacheOrder.begin(), svgDocumentCacheOrder.end(), renderId); order != svgDocumentCacheOrder.end())
                {
                    svgDocumentCacheOrder.erase(order);
                    svgDocumentCacheOrder.push_back(renderId);
                }
            }
            else
            {
                auto allocation = GlobalAlloc(GMEM_MOVEABLE, source.size());
                if (!allocation) return false;
                auto bytes = static_cast<char*>(GlobalLock(allocation));
                if (!bytes)
                {
                    GlobalFree(allocation);
                    return false;
                }
                std::memcpy(bytes, source.data(), source.size());
                GlobalUnlock(allocation);
                ::Microsoft::WRL::ComPtr<IStream> stream;
                if (FAILED(CreateStreamOnHGlobal(allocation, TRUE, stream.GetAddressOf())) || !stream)
                {
                    GlobalFree(allocation);
                    return false;
                }
                if (FAILED(svgContext->CreateSvgDocument(stream.Get(), D2D1::SizeF(width, height), document.GetAddressOf())) || !document) return false;
                constexpr std::size_t budget = 24 * 1024 * 1024;
                constexpr std::size_t limit = 96;
                auto resourceCost = (std::max)(std::size_t{16 * 1024}, source.size() * 8);
                while (!svgDocumentCacheOrder.empty() && (svgDocumentCacheBytes + resourceCost > budget || svgDocumentCache.size() >= limit))
                {
                    auto oldest = std::move(svgDocumentCacheOrder.front());
                    svgDocumentCacheOrder.pop_front();
                    auto oldestDocument = svgDocumentCache.find(oldest);
                    if (oldestDocument == svgDocumentCache.end()) continue;
                    svgDocumentCacheBytes -= oldestDocument->second.bytes;
                    svgDocumentCache.erase(oldestDocument);
                }
                if (resourceCost <= budget)
                {
                    svgDocumentCacheBytes += resourceCost;
                    svgDocumentCacheOrder.push_back(renderId);
                    svgDocumentCache.emplace(renderId, CachedSvgDocument{ document, resourceCost });
                }
            }
            document->SetViewportSize(D2D1::SizeF(width, height));
            D2D1_MATRIX_3X2_F transform{};
            svgContext->GetTransform(&transform);
            svgContext->SetTransform(D2D1::Matrix3x2F::Translation(origin.x, origin.y) * transform);
            svgContext->DrawSvgDocument(document.Get());
            svgContext->SetTransform(transform);
            return true;
        };

        auto drawMathSvg = [&](MathJaxSvgFragment const& fragment, D2D1_POINT_2F origin, D2D1_COLOR_F color) -> bool
        {
            (void)color;
            return fragment.svg && drawSvg(fragment.renderId, *fragment.svg, fragment.width, fragment.height, origin);
        };

        auto drawMathFallback = [&](std::size_t start, std::size_t end, D2D1_POINT_2F origin)
        {
            start = (std::min)(start, sourceText.size());
            end = (std::min)((std::max)(end, start), sourceText.size());
            auto key = static_cast<std::uint64_t>(start) * 1099511628211ull ^ static_cast<std::uint64_t>(end);
            if (!mathFallbacks.insert(key).second) return;
            auto fallback = ToWide(std::u32string_view(sourceText).substr(start, end - start));
            if (fallback.empty()) fallback = L"formula";
            d2dContext->DrawTextW(fallback.c_str(), static_cast<UINT32>(fallback.size()), codeFormat.Get(), D2D1::RectF(origin.x, origin.y, documentRight, origin.y + styleSheet.code.lineHeight * 3.0f), textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        };

        auto createLayout = [&](std::wstring const& text, IDWriteTextFormat* format, float width)
        {
            ::Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            auto hr = dwriteFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, width, 100000.0f, layout.GetAddressOf());
            if (FAILED(hr) || !layout)
            {
                return ::Microsoft::WRL::ComPtr<IDWriteTextLayout>{};
            }
            return layout;
        };

        auto applyInlineStyles = [&](IDWriteTextLayout* layout, std::vector<InlineStyleRange> const& ranges)
        {
            if (!layout)
            {
                return;
            }

            for (auto const& range : ranges)
            {
                DWRITE_TEXT_RANGE textRange{ range.start, range.length };
                if (range.style.bold)
                {
                    layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, textRange);
                }
                if (range.style.italic)
                {
                    layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, textRange);
                }
                if (range.style.link)
                {
                    layout->SetUnderline(true, textRange);
                }
                if (range.style.strikethrough)
                {
                    layout->SetStrikethrough(true, textRange);
                }
                if (range.style.code)
                {
                    layout->SetFontFamilyName(styleSheet.code.family.c_str(), textRange);
                    layout->SetFontSize(styleSheet.code.size, textRange);
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
                if (range.marker && !range.style.heading_level)
                {
                    layout->SetFontSize(styleSheet.body.size * 0.82f, textRange);
                }
                auto syntaxIndex = static_cast<std::size_t>(range.syntax);
                if (range.syntax != SyntaxHighlightKind::None && syntaxIndex < syntaxBrushes.size() && syntaxBrushes[syntaxIndex])
                {
                    layout->SetDrawingEffect(syntaxBrushes[syntaxIndex].Get(), textRange);
                }
            }
        };

        auto measureTextHeight = [&](IDWriteTextLayout* layout, float fallbackHeight)
        {
            if (!layout)
            {
                return fallbackHeight;
            }

            DWRITE_TEXT_METRICS metrics{};
            if (FAILED(layout->GetMetrics(&metrics)))
            {
                return fallbackHeight;
            }

            return (std::max)(fallbackHeight, metrics.height);
        };

        auto addVisualLinesForBlock = [&](std::size_t blockIndex)
        {
            auto const& block = visualBlocks[blockIndex];
            if (!block.layout || block.displayToSource.empty())
            {
                return;
            }

            UINT32 lineCount = 0;
            auto hr = block.layout->GetLineMetrics(nullptr, 0, &lineCount);
            if (hr != E_NOT_SUFFICIENT_BUFFER || lineCount == 0)
            {
                return;
            }

            std::vector<DWRITE_LINE_METRICS> metrics(lineCount);
            if (FAILED(block.layout->GetLineMetrics(metrics.data(), lineCount, &lineCount)))
            {
                return;
            }

            UINT32 textPosition = 0;
            float lineTop = block.textOrigin.y;
            UINT32 prevNewlineLength = 0;
            for (UINT32 lineIndex = 0; lineIndex < lineCount; ++lineIndex)
            {
                auto const& line = metrics[lineIndex];
                auto lineEndPosition = textPosition + line.length;
                auto visibleEndPosition = lineEndPosition >= line.newlineLength ? lineEndPosition - line.newlineLength : lineEndPosition;
                auto startIndex = (std::min)(static_cast<std::size_t>(textPosition), block.displayToSource.size() - 1);
                auto endIndex = (std::min)(static_cast<std::size_t>(visibleEndPosition), block.displayToSource.size() - 1);
                VisualLine visualLine;
                visualLine.blockIndex = blockIndex;
                visualLine.sourceStart = block.displayToSource[startIndex];
                visualLine.sourceEnd = block.displayToSource[endIndex];
                visualLine.displayStart = textPosition;
                visualLine.displayEnd = visibleEndPosition;
                visualLine.wrapContinuation = lineIndex > 0 && prevNewlineLength == 0;
                visualLine.rect = D2D1::RectF(block.textOrigin.x, lineTop, block.textOrigin.x + block.textWidth, lineTop + line.height);
                visualLines.push_back(visualLine);
                textPosition = lineEndPosition;
                lineTop += line.height;
                prevNewlineLength = line.newlineLength;
            }
        };

        auto blockCacheKey = [&](elmd::RenderBlock const& block)
        {
            auto value = block.source_fingerprint;
            value ^= static_cast<std::uint64_t>(block.kind) * 0x9e3779b97f4a7c15ull;
            value ^= static_cast<std::uint64_t>(std::llround((documentRight - documentLeft) * 16.0f)) << 17;
            value ^= static_cast<std::uint64_t>(theme) << 61;
            return value;
        };

        auto estimatedBlockHeight = [&](elmd::RenderBlock const& block)
        {
            if (auto found = blockHeightCache.find(blockCacheKey(block)); found != blockHeightCache.end()) return found->second;
            switch (block.kind)
            {
                case elmd::RenderBlockKind::Blank:
                    return styleSheet.body.lineHeight;
                case elmd::RenderBlockKind::Code:
                    return (std::max)(64.0f, static_cast<float>((std::max)(std::size_t{1}, block.line_count)) * styleSheet.code.lineHeight + 32.0f);
                case elmd::RenderBlockKind::Math:
                    return 96.0f;
                case elmd::RenderBlockKind::Table:
                    return static_cast<float>((std::max)(std::size_t{2}, block.row_count)) * (styleSheet.body.lineHeight + 16.0f);
                case elmd::RenderBlockKind::Image:
                    return 160.0f;
                case elmd::RenderBlockKind::ThematicBreak:
                    return 48.0f;
                default:
                {
                    auto length = block.content_range.end.v > block.content_range.start.v ? block.content_range.end.v - block.content_range.start.v : std::size_t{1};
                    auto charactersPerLine = (std::max)(std::size_t{24}, static_cast<std::size_t>((documentRight - documentLeft) / (styleSheet.body.size * 0.56f)));
                    auto lines = (std::max)(std::size_t{1}, (length + charactersPerLine - 1) / charactersPerLine);
                    return static_cast<float>(lines) * styleSheet.body.lineHeight + 8.0f;
                }
            }
        };

        if (sessionCore.renderModel.blocks.empty() && sourceText.empty())
        {
            auto emptyText = winrt::hstring(L"Open a Markdown file or start editing to see the WYSIWYG surface.");
            auto rect = D2D1::RectF(documentLeft, y, documentRight, y + 80.0f);
            d2dContext->DrawTextW(emptyText.c_str(), static_cast<UINT32>(emptyText.size()), textFormat.Get(), rect, mutedBrush.Get());
            return;
        }

        bool previousBlank = false;
        for (std::size_t blockIndex = 0; blockIndex < sessionCore.renderModel.blocks.size(); ++blockIndex)
        {
            auto const& block = sessionCore.renderModel.blocks[blockIndex];
            bool currentBlank = block.kind == elmd::RenderBlockKind::Blank;
            if (blockIndex > 0 && !(previousBlank && currentBlank))
            {
                if (previousBlank || currentBlank)
                {
                    y += styleSheet.blockGap * 0.5f;
                }
                else
                {
                    y += styleSheet.blockGap;
                }
            }
            auto cacheKey = blockCacheKey(block);
            auto blockStartY = y;
            auto estimatedHeight = estimatedBlockHeight(block);
            auto caretInside = block.source_range.start.v <= caret && caret <= block.source_range.end.v;
            auto insidePrefetch = y + estimatedHeight >= -surfaceHeightDip && y <= surfaceHeightDip * 2.0f;
            auto requestEmbedded = y >= -surfaceHeightDip * 2.0f && y <= surfaceHeightDip * 3.0f;
            if (!caretInside && !insidePrefetch)
            {
                VisualBlock placeholder;
                placeholder.rect = D2D1::RectF(documentLeft, y, documentRight, y + estimatedHeight);
                placeholder.textOrigin = D2D1::Point2F(documentLeft, y);
                placeholder.textWidth = documentRight - documentLeft;
                placeholder.sourceStart = block.source_range.start.v;
                placeholder.sourceEnd = block.source_range.end.v;
                placeholder.documentY = y + scrollOffset;
                visualBlocks.push_back(std::move(placeholder));
                y += estimatedHeight;
                previousBlank = currentBlank;
                continue;
            }
            if (block.kind == elmd::RenderBlockKind::Table)
            {
                auto tableSource = elmd::table_source_at(sourceText, block.source_range.start.v);
                if (tableSource && tableSource->rows.size() >= 2 && tableSource->column_count > 0)
                {
                    VisualTable table;
                    table.sourceStart = block.source_range.start.v;
                    table.sourceEnd = block.source_range.end.v;
                    table.columnCount = tableSource->column_count;
                    table.rowCount = tableSource->rows.size() - 1;
                    auto tableWidth = (std::max)(1.0f, documentRight - documentLeft);
                    auto columnWidth = tableWidth / static_cast<float>(table.columnCount);
                    std::vector<float> rowHeights(table.rowCount, styleSheet.body.lineHeight + 16.0f);
                    std::vector<DisplayInlineText> tableDisplays;
                    tableDisplays.reserve(table.rowCount * table.columnCount);

                    for (std::size_t row = 0; row < table.rowCount; ++row)
                    {
                        auto sourceRowIndex = row == 0 ? 0 : row + 1;
                        auto const& sourceRow = tableSource->rows[sourceRowIndex];
                        for (std::size_t column = 0; column < table.columnCount; ++column)
                        {
                            auto sourceStart = sourceRow.line_range.end.v;
                            auto sourceEnd = sourceStart;
                            if (column < sourceRow.cells.size())
                            {
                                auto const& sourceCell = sourceRow.cells[column];
                                sourceStart = sourceCell.content_range.start.v;
                                sourceEnd = sourceCell.content_range.end.v;
                            }
                            DisplayInlineText display;
                            auto renderCellIndex = row * table.columnCount + column;
                            if (renderCellIndex < block.table_cells.size())
                            {
                                display = BuildDisplayInlineText(
                                    block.table_cells[renderCellIndex],
                                    caret,
                                    sourceEnd,
                                    sourceText,
                                    mathJax,
                                    svgNormalizer,
                                    styleSheet.textColor,
                                    styleSheet.body.size,
                                    (std::max)(1.0f, columnWidth - 20.0f),
                                    svgSupported,
                                    requestEmbedded);
                            }
                            else
                            {
                                AppendSourceText(display, sourceText, sourceStart, sourceEnd, elmd::InlineStyle::plain(), false);
                                display.displayToSource.push_back(sourceEnd);
                            }
                            auto wide = ToWide(display.text);
                            auto layout = createLayout(wide, textFormat.Get(), (std::max)(1.0f, columnWidth - 20.0f));
                            auto cellTextHeight = styleSheet.body.lineHeight;
                            if (layout)
                            {
                                layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                                applyInlineStyles(layout.Get(), display.ranges);
                                ApplyMathInlineObjects(layout.Get(), display.mathOverlays);
                                if (row == 0)
                                {
                                    layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_RANGE{0, static_cast<UINT32>(wide.size())});
                                }
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
                            VisualTableCell cell;
                            cell.sourceStart = sourceStart;
                            cell.sourceEnd = sourceEnd;
                            cell.row = row;
                            cell.column = column;
                            cell.text = std::move(display.text);
                            cell.displayToSource = std::move(display.displayToSource);
                            cell.textHeight = cellTextHeight;
                            cell.layout = std::move(layout);
                            tableDisplays.push_back(std::move(display));
                            table.cells.push_back(std::move(cell));
                        }
                    }

                    table.columnBoundaries.reserve(table.columnCount + 1);
                    for (std::size_t column = 0; column <= table.columnCount; ++column) table.columnBoundaries.push_back(documentLeft + columnWidth * static_cast<float>(column));
                    table.rowBoundaries.reserve(table.rowCount + 1);
                    table.rowBoundaries.push_back(y);
                    for (auto rowHeight : rowHeights) table.rowBoundaries.push_back(table.rowBoundaries.back() + rowHeight);
                    table.rect = D2D1::RectF(documentLeft, y, documentRight, table.rowBoundaries.back());

                    for (auto& cell : table.cells)
                    {
                        cell.rect = D2D1::RectF(
                            table.columnBoundaries[cell.column],
                            table.rowBoundaries[cell.row],
                            table.columnBoundaries[cell.column + 1],
                            table.rowBoundaries[cell.row + 1]);
                        auto verticalInset = (std::max)(0.0f, (cell.rect.bottom - cell.rect.top - cell.textHeight) * 0.5f);
                        cell.textOrigin = D2D1::Point2F(cell.rect.left + 10.0f, cell.rect.top + verticalInset);
                        cell.textWidth = (std::max)(1.0f, cell.rect.right - cell.rect.left - 20.0f);
                    }

                    auto tableIndex = visualTables.size();
                    visualTables.push_back(std::move(table));
                    auto& visualTable = visualTables.back();
                    VisualBlock visualBlock;
                    visualBlock.rect = visualTable.rect;
                    visualBlock.sourceStart = visualTable.sourceStart;
                    visualBlock.sourceEnd = visualTable.sourceEnd;
                    visualBlock.documentY = y + scrollOffset;
                    auto visualBlockIndex = visualBlocks.size();
                    visualBlocks.push_back(std::move(visualBlock));

                    d2dContext->FillRectangle(D2D1::RectF(visualTable.rect.left, visualTable.rowBoundaries[0], visualTable.rect.right, visualTable.rowBoundaries[1]), panelBrush.Get());
                    for (std::size_t cellIndex = 0; cellIndex < visualTable.cells.size(); ++cellIndex)
                    {
                        auto& cell = visualTable.cells[cellIndex];
                        auto& cellDisplay = tableDisplays[cellIndex];
                        if (!selection.is_empty() && selection.start.v < cell.sourceEnd && cell.sourceStart < selection.end.v)
                        {
                            d2dContext->FillRectangle(cell.rect, selectionBrush.Get());
                        }
                        if (cell.layout)
                        {
                            for (auto const& range : cellDisplay.ranges)
                            {
                                if (!range.style.code || range.length == 0) continue;
                                UINT32 actualCount = 0;
                                auto hr = cell.layout->HitTestTextRange(range.start, range.length, cell.textOrigin.x, cell.textOrigin.y, nullptr, 0, &actualCount);
                                if (hr != E_NOT_SUFFICIENT_BUFFER || actualCount == 0) continue;
                                std::vector<DWRITE_HIT_TEST_METRICS> metrics(actualCount);
                                if (FAILED(cell.layout->HitTestTextRange(range.start, range.length, cell.textOrigin.x, cell.textOrigin.y, metrics.data(), actualCount, &actualCount))) continue;
                                for (UINT32 index = 0; index < actualCount; ++index)
                                {
                                    auto const& metric = metrics[index];
                                    d2dContext->FillRoundedRectangle(
                                        D2D1::RoundedRect(
                                            D2D1::RectF(metric.left - 3.0f, metric.top + 2.0f, metric.left + metric.width + 3.0f, metric.top + metric.height - 1.0f),
                                            4.0f,
                                            4.0f),
                                        panelBrush.Get());
                                }
                            }
                            d2dContext->DrawTextLayout(cell.textOrigin, cell.layout.Get(), textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                            for (auto const& overlay : cellDisplay.mathOverlays)
                            {
                                float pointX = 0.0f;
                                float pointY = 0.0f;
                                DWRITE_HIT_TEST_METRICS metrics{};
                                if (FAILED(cell.layout->HitTestTextPosition(overlay.displayStart, FALSE, &pointX, &pointY, &metrics))) continue;
                                auto mathY = cell.textOrigin.y + metrics.top;
                                auto mathX = cell.textOrigin.x + pointX + overlay.leadingSpace;
                                if (!drawMathSvg(overlay.fragment, D2D1::Point2F(mathX, mathY), styleSheet.textColor))
                                {
                                    drawMathFallback(overlay.sourceStart, overlay.sourceEnd, D2D1::Point2F(mathX, mathY));
                                }
                                if (overlay.strikethrough)
                                {
                                    auto strikeY = mathY + overlay.fragment.height * 0.52f;
                                    d2dContext->DrawLine(D2D1::Point2F(cell.textOrigin.x + pointX, strikeY), D2D1::Point2F(mathX + overlay.fragment.width, strikeY), textBrush.Get(), 1.5f);
                                }
                                visualMathHits.push_back(VisualMathHit{
                                    D2D1::RectF(mathX, mathY, mathX + overlay.fragment.width, mathY + overlay.fragment.height),
                                    overlay.sourceStart,
                                    overlay.sourceEnd,
                                    overlay.progressStart,
                                    overlay.progressEnd,
                                });
                            }
                        }
                        bool addedVisualLine = false;
                        if (cell.layout && !cell.displayToSource.empty())
                        {
                            UINT32 lineCount = 0;
                            auto hr = cell.layout->GetLineMetrics(nullptr, 0, &lineCount);
                            if (hr == E_NOT_SUFFICIENT_BUFFER && lineCount > 0)
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
                                        VisualLine line;
                                        line.blockIndex = visualBlockIndex;
                                        line.tableIndex = tableIndex;
                                        line.cellIndex = cellIndex;
                                        line.sourceStart = cell.displayToSource[startIndex];
                                        line.sourceEnd = cell.displayToSource[endIndex];
                                        line.displayStart = textPosition;
                                        line.displayEnd = visibleEndPosition;
                                        line.wrapContinuation = lineIndex > 0 && previousNewlineLength == 0;
                                        line.rect = D2D1::RectF(cell.rect.left, lineTop, cell.rect.right, lineTop + metric.height);
                                        visualLines.push_back(std::move(line));
                                        addedVisualLine = true;
                                        textPosition = lineEndPosition;
                                        lineTop += metric.height;
                                        previousNewlineLength = metric.newlineLength;
                                    }
                                }
                            }
                        }
                        if (!addedVisualLine)
                        {
                            VisualLine line;
                            line.blockIndex = visualBlockIndex;
                            line.tableIndex = tableIndex;
                            line.cellIndex = cellIndex;
                            line.sourceStart = cell.sourceStart;
                            line.sourceEnd = cell.sourceEnd;
                            line.displayStart = 0;
                            line.displayEnd = static_cast<std::uint32_t>(cell.text.size());
                            line.rect = cell.rect;
                            visualLines.push_back(std::move(line));
                        }
                    }
                    for (auto boundary : visualTable.columnBoundaries)
                    {
                        d2dContext->DrawLine(D2D1::Point2F(boundary, visualTable.rect.top), D2D1::Point2F(boundary, visualTable.rect.bottom), mutedBrush.Get(), 1.0f);
                    }
                    for (auto boundary : visualTable.rowBoundaries)
                    {
                        d2dContext->DrawLine(D2D1::Point2F(visualTable.rect.left, boundary), D2D1::Point2F(visualTable.rect.right, boundary), mutedBrush.Get(), 1.0f);
                    }
                    y = visualTable.rect.bottom;
                    blockHeightCache[cacheKey] = y - blockStartY;
                    previousBlank = false;
                    continue;
                }
            }
            if (block.kind == elmd::RenderBlockKind::Quote)
            {
                struct QuoteBox
                {
                    D2D1_RECT_F rect{};
                    std::size_t depth = 0;
                    float borderWidth = 3.0f;
                };
                struct QuoteFragment
                {
                    DisplayInlineText display;
                    ::Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
                    D2D1_POINT_2F origin{};
                    D2D1_RECT_F rect{};
                    float textWidth = 0.0f;
                    std::size_t depth = 0;
                    std::size_t sourceStart = 0;
                    std::size_t sourceEnd = 0;
                    bool code = false;
                };
                std::vector<QuoteBox> quoteBoxes;
                std::vector<QuoteFragment> quoteFragments;
                auto depthInset = block.block_style.padding_left + 6.0f;
                auto cursorY = y + block.block_style.padding_top;
                for (std::size_t childIndex = 0; childIndex < block.child_blocks.size(); ++childIndex)
                {
                    auto const& child = block.child_blocks[childIndex];
                    if (childIndex > 0) cursorY += 8.0f;
                    auto contentLeft = documentLeft + block.block_style.padding_left + static_cast<float>(child.quote_depth) * depthInset;
                    auto contentRight = documentRight - block.block_style.padding_right;
                    DisplayInlineText display;
                    auto code = child.kind == elmd::RenderBlockKind::Code;
                    if (child.kind == elmd::RenderBlockKind::Blank)
                    {
                        AppendGeneratedText(display, U"\u200B", child.content_range.start.v, elmd::InlineStyle::plain());
                        display.displayToSource.push_back(child.content_range.end.v);
                    }
                    else if (code)
                    {
                        display = child.code_indented ? BuildIndentedCodeBlockText(child, sourceText) : BuildCodeBlockText(child, caret, sourceText);
                    }
                    else if (!child.inline_items.empty())
                    {
                        display = BuildDisplayInlineText(child.inline_items, caret, child.content_range.end.v, sourceText, mathJax, svgNormalizer, styleSheet.textColor, styleSheet.body.size, contentRight - contentLeft, svgSupported, requestEmbedded);
                    }
                    else
                    {
                        AppendSourceText(display, sourceText, child.content_range.start.v, child.content_range.end.v, elmd::InlineStyle::plain(), false);
                        display.displayToSource.push_back(child.content_range.end.v);
                    }
                    if (display.text.empty())
                    {
                        AppendGeneratedText(display, U"\u200B", child.content_range.start.v, elmd::InlineStyle::plain());
                        display.displayToSource.push_back(child.content_range.end.v);
                    }
                    auto format = code ? codeFormat.Get() : textFormat.Get();
                    auto horizontalPadding = code ? 12.0f : 0.0f;
                    auto verticalPadding = code ? 8.0f : 0.0f;
                    auto textWidth = (std::max)(1.0f, contentRight - contentLeft - horizontalPadding * 2.0f);
                    auto layout = createLayout(ToWide(display.text), format, textWidth);
                    applyInlineStyles(layout.Get(), display.ranges);
                    ApplyMathInlineObjects(layout.Get(), display.mathOverlays);
                    auto fallbackHeight = code ? styleSheet.code.lineHeight : styleSheet.body.lineHeight;
                    auto fragmentHeight = measureTextHeight(layout.Get(), fallbackHeight) + verticalPadding * 2.0f;
                    QuoteFragment fragment;
                    fragment.origin = D2D1::Point2F(contentLeft + horizontalPadding, cursorY + verticalPadding);
                    fragment.rect = D2D1::RectF(contentLeft, cursorY, contentRight, cursorY + fragmentHeight);
                    fragment.textWidth = textWidth;
                    fragment.depth = child.quote_depth;
                    fragment.sourceStart = child.content_range.start.v;
                    fragment.sourceEnd = child.content_range.end.v;
                    fragment.code = code;
                    fragment.display = std::move(display);
                    fragment.layout = std::move(layout);
                    quoteFragments.push_back(std::move(fragment));
                    cursorY += fragmentHeight;
                }
                if (quoteFragments.empty()) cursorY += styleSheet.body.lineHeight;
                auto quoteBottom = cursorY + block.block_style.padding_bottom;
                auto borderWidth = block.block_style.border_left ? block.block_style.border_left->width : 3.0f;
                quoteBoxes.push_back(QuoteBox{D2D1::RectF(documentLeft, y, documentRight, quoteBottom), 0, borderWidth});
                std::size_t maxDepth = 0;
                for (auto const& fragment : quoteFragments) maxDepth = (std::max)(maxDepth, fragment.depth);
                for (std::size_t level = 1; level <= maxDepth; ++level)
                {
                    std::size_t index = 0;
                    while (index < quoteFragments.size())
                    {
                        while (index < quoteFragments.size() && quoteFragments[index].depth < level) ++index;
                        if (index >= quoteFragments.size()) break;
                        auto first = index;
                        while (index < quoteFragments.size() && quoteFragments[index].depth >= level) ++index;
                        auto left = documentLeft + static_cast<float>(level) * depthInset;
                        quoteBoxes.push_back(QuoteBox{D2D1::RectF(left, quoteFragments[first].rect.top - 4.0f, documentRight, quoteFragments[index - 1].rect.bottom + 4.0f), level, borderWidth});
                    }
                }
                for (auto const& box : quoteBoxes)
                {
                    d2dContext->FillRectangle(box.rect, box.depth == 0 ? panelBrush.Get() : nestedQuoteBrush.Get());
                    auto lineX = box.rect.left + box.borderWidth * 0.5f;
                    d2dContext->DrawLine(D2D1::Point2F(lineX, box.rect.top + 4.0f), D2D1::Point2F(lineX, box.rect.bottom - 4.0f), mutedBrush.Get(), box.borderWidth);
                }
                for (auto& fragment : quoteFragments)
                {
                    auto sourceStart = fragment.sourceStart;
                    auto sourceEnd = fragment.sourceEnd;
                    if (fragment.code) d2dContext->FillRectangle(fragment.rect, canvasBrush.Get());
                    if (fragment.layout)
                    {
                        for (auto const& range : fragment.display.ranges)
                        {
                            if (!range.style.code || range.length == 0) continue;
                            UINT32 actualCount = 0;
                            auto hr = fragment.layout->HitTestTextRange(range.start, range.length, fragment.origin.x, fragment.origin.y, nullptr, 0, &actualCount);
                            if (hr != E_NOT_SUFFICIENT_BUFFER || actualCount == 0) continue;
                            std::vector<DWRITE_HIT_TEST_METRICS> metrics(actualCount);
                            if (FAILED(fragment.layout->HitTestTextRange(range.start, range.length, fragment.origin.x, fragment.origin.y, metrics.data(), actualCount, &actualCount))) continue;
                            for (UINT32 metricIndex = 0; metricIndex < actualCount; ++metricIndex)
                            {
                                auto const& metric = metrics[metricIndex];
                                d2dContext->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(metric.left - 3.0f, metric.top + 2.0f, metric.left + metric.width + 3.0f, metric.top + metric.height - 1.0f), 4.0f, 4.0f), panelBrush.Get());
                            }
                        }
                        if (!selection.is_empty() && selection.end.v > sourceStart && selection.start.v < sourceEnd)
                        {
                            auto displayStart = DisplayPositionForSource(fragment.display.displayToSource, (std::max)(selection.start.v, sourceStart));
                            auto displayEnd = DisplayPositionForSource(fragment.display.displayToSource, (std::min)(selection.end.v, sourceEnd));
                            UINT32 actualCount = 0;
                            auto hr = fragment.layout->HitTestTextRange(static_cast<UINT32>(displayStart), static_cast<UINT32>(displayEnd - displayStart), fragment.origin.x, fragment.origin.y, nullptr, 0, &actualCount);
                            if (hr == E_NOT_SUFFICIENT_BUFFER && actualCount > 0)
                            {
                                std::vector<DWRITE_HIT_TEST_METRICS> metrics(actualCount);
                                if (SUCCEEDED(fragment.layout->HitTestTextRange(static_cast<UINT32>(displayStart), static_cast<UINT32>(displayEnd - displayStart), fragment.origin.x, fragment.origin.y, metrics.data(), actualCount, &actualCount)))
                                {
                                    for (UINT32 metricIndex = 0; metricIndex < actualCount; ++metricIndex)
                                    {
                                        auto const& metric = metrics[metricIndex];
                                        d2dContext->FillRectangle(D2D1::RectF(metric.left, metric.top, metric.left + metric.width, metric.top + metric.height), selectionBrush.Get());
                                    }
                                }
                            }
                        }
                        d2dContext->DrawTextLayout(fragment.origin, fragment.layout.Get(), fragment.code ? codeBrush.Get() : textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                        for (auto const& overlay : fragment.display.mathOverlays)
                        {
                            float pointX = 0.0f;
                            float pointY = 0.0f;
                            DWRITE_HIT_TEST_METRICS metrics{};
                            if (FAILED(fragment.layout->HitTestTextPosition(overlay.displayStart, FALSE, &pointX, &pointY, &metrics))) continue;
                            auto mathX = fragment.origin.x + pointX + overlay.leadingSpace;
                            auto mathY = fragment.origin.y + metrics.top;
                            if (!drawMathSvg(overlay.fragment, D2D1::Point2F(mathX, mathY), styleSheet.textColor)) drawMathFallback(overlay.sourceStart, overlay.sourceEnd, D2D1::Point2F(mathX, mathY));
                            visualMathHits.push_back(VisualMathHit{D2D1::RectF(mathX, mathY, mathX + overlay.fragment.width, mathY + overlay.fragment.height), overlay.sourceStart, overlay.sourceEnd, overlay.progressStart, overlay.progressEnd});
                        }
                    }
                    VisualBlock visualBlock;
                    visualBlock.rect = fragment.rect;
                    visualBlock.textOrigin = fragment.origin;
                    visualBlock.textWidth = fragment.textWidth;
                    visualBlock.sourceStart = sourceStart;
                    visualBlock.sourceEnd = sourceEnd;
                    visualBlock.documentY = fragment.rect.top + scrollOffset;
                    visualBlock.text = std::move(fragment.display.text);
                    visualBlock.displayToSource = std::move(fragment.display.displayToSource);
                    visualBlock.layout = std::move(fragment.layout);
                    visualBlocks.push_back(std::move(visualBlock));
                    addVisualLinesForBlock(visualBlocks.size() - 1);
                }
                if (quoteFragments.empty())
                {
                    VisualBlock placeholder;
                    placeholder.rect = quoteBoxes.front().rect;
                    placeholder.textOrigin = D2D1::Point2F(documentLeft + block.block_style.padding_left, y + block.block_style.padding_top);
                    placeholder.textWidth = documentRight - placeholder.textOrigin.x;
                    placeholder.sourceStart = block.source_range.start.v;
                    placeholder.sourceEnd = block.source_range.end.v;
                    placeholder.documentY = y + scrollOffset;
                    visualBlocks.push_back(std::move(placeholder));
                }
                y = quoteBottom;
                blockHeightCache[cacheKey] = y - blockStartY;
                previousBlank = false;
                continue;
            }
            IDWriteTextFormat* format = textFormat.Get();
            ID2D1Brush* brush = textBrush.Get();
            float height = 48.0f;
            float inset = 0.0f;
            float textTop = 4.0f;
            bool fillPanel = false;
            bool measureHeight = true;
            std::u32string text;
            std::vector<InlineStyleRange> inlineRanges;
            std::vector<std::size_t> displayToSource;
            std::vector<DisplayInlineText::MathOverlay> inlineMathOverlays;
            std::vector<DisplayInlineText::MathPreview> inlineMathPreviews;
            std::optional<MathJaxSvg> blockMath;
            bool showRawMath = false;
            std::optional<MermaidSvg> blockMermaid;
            bool showRawMermaid = false;
            std::optional<CachedRasterImage> blockImage;
            bool showRawImage = false;
            bool thematicBreak = false;

            switch (block.kind)
            {
                case elmd::RenderBlockKind::Blank:
                    text = U" ";
                    displayToSource.push_back(block.content_range.start.v);
                    displayToSource.push_back(block.content_range.start.v);
                    height = styleSheet.body.lineHeight;
                    textTop = 0.0f;
                    measureHeight = false;
                    break;
                case elmd::RenderBlockKind::Text:
                {
                    inset = block.block_style.padding_left;
                    auto display = BuildDisplayInlineText(
                        block.inline_items,
                        caret,
                        block.content_range.end.v,
                        sourceText,
                        mathJax,
                        svgNormalizer,
                        styleSheet.textColor,
                        styleSheet.body.size,
                        documentRight - documentLeft,
                        svgSupported,
                        requestEmbedded);
                    text = std::move(display.text);
                    inlineRanges = std::move(display.ranges);
                    displayToSource = std::move(display.displayToSource);
                    inlineMathOverlays = std::move(display.mathOverlays);
                    inlineMathPreviews = std::move(display.mathPreviews);
                    if (block.block_style.margin_top >= 24.0f)
                    {
                        format = heading1Format.Get();
                        height = 58.0f;
                    }
                    else if (block.block_style.margin_top >= 20.0f)
                    {
                        format = heading2Format.Get();
                        height = 50.0f;
                    }
                    else if (block.block_style.margin_top >= 16.0f)
                    {
                        format = heading3Format.Get();
                    }
                    break;
                }
                case elmd::RenderBlockKind::Code:
                {
                    DisplayInlineText display;
                    if (IsMermaidLanguage(block.language))
                    {
                        if (svgSupported)
                        {
                            auto rawMermaid = mermaid.GetOrQueue(elmd::cps_to_utf8(block.code_text), theme == Theme::Dark, requestEmbedded);
                            blockMermaid = rawMermaid ? NormalizeMermaidSvg(*rawMermaid, svgNormalizer, requestEmbedded) : std::nullopt;
                        }
                        showRawMermaid = block.source_range.start.v <= caret && caret <= block.source_range.end.v;
                        if (showRawMermaid || !blockMermaid || !static_cast<bool>(*blockMermaid))
                        {
                            display = block.code_indented ? BuildIndentedCodeBlockText(block, sourceText) : BuildCodeBlockText(block, caret, sourceText);
                        }
                        else
                        {
                            AppendMathPlaceholder(display, 1, block.source_range.start.v);
                            display.displayToSource.push_back(block.source_range.end.v);
                        }
                    }
                    else
                    {
                        display = block.code_indented ? BuildIndentedCodeBlockText(block, sourceText) : BuildCodeBlockText(block, caret, sourceText);
                        if (requestEmbedded && block.language)
                        {
                            auto ranges = treeSitter.Highlight(*block.language, elmd::cps_to_utf8(block.code_text));
                            for (auto const& range : ranges)
                            {
                                auto sourceRangeStart = block.content_range.start.v + range.start;
                                auto sourceRangeEnd = sourceRangeStart + range.length;
                                auto displayStart = DisplayPositionForSource(display.displayToSource, sourceRangeStart);
                                auto displayEnd = DisplayPositionForSource(display.displayToSource, sourceRangeEnd);
                                if (displayStart < displayEnd && displayEnd <= display.text.size())
                                {
                                    display.ranges.push_back(InlineStyleRange{static_cast<UINT32>(displayStart), static_cast<UINT32>(displayEnd - displayStart), elmd::InlineStyle::plain(), false, range.kind});
                                }
                            }
                        }
                    }
                    text = std::move(display.text);
                    inlineRanges = std::move(display.ranges);
                    displayToSource = std::move(display.displayToSource);
                    format = codeFormat.Get();
                    brush = codeBrush.Get();
                    height = 64.0f;
                    inset = 16.0f;
                    textTop = 16.0f;
                    fillPanel = true;
                    break;
                }
                case elmd::RenderBlockKind::Math:
                {
                    if (svgSupported)
                    {
                        auto rawMath = mathJax.GetOrQueue(elmd::cps_to_utf8(block.tex), true, styleSheet.body.size, documentRight - documentLeft, requestEmbedded);
                        blockMath = rawMath ? NormalizeMathJaxSvg(*rawMath, svgNormalizer, styleSheet.textColor, styleSheet.body.size, requestEmbedded) : std::nullopt;
                    }
                    showRawMath = block.source_range.start.v <= caret && caret <= block.source_range.end.v;
                    DisplayInlineText display;
                    if (showRawMath || !blockMath || !static_cast<bool>(*blockMath))
                    {
                        auto visibleEnd = block.source_range.end.v;
                        if (visibleEnd > block.source_range.start.v && visibleEnd <= sourceText.size() && sourceText[visibleEnd - 1] == U'\n')
                        {
                            --visibleEnd;
                        }
                        AppendSourceText(display, sourceText, block.source_range.start.v, visibleEnd, elmd::InlineStyle::plain(), false);
                    }
                    else
                    {
                        AppendMathPlaceholder(display, 1, block.source_range.start.v);
                    }
                    display.displayToSource.push_back(block.source_range.end.v);
                    text = std::move(display.text);
                    inlineRanges = std::move(display.ranges);
                    displayToSource = std::move(display.displayToSource);
                    format = codeFormat.Get();
                    brush = codeBrush.Get();
                    inset = 16.0f;
                    textTop = 16.0f;
                    fillPanel = true;
                    break;
                }
                case elmd::RenderBlockKind::Callout:
                case elmd::RenderBlockKind::Footnote:
                {
                    DisplayInlineText display;
                    if (block.kind == elmd::RenderBlockKind::Callout)
                    {
                        auto label = elmd::utf8_to_cps(block.callout_kind.empty() ? "NOTE" : block.callout_kind);
                        if (block.callout_title) label += U": " + InlineText(*block.callout_title);
                        AppendGeneratedText(display, label + U"\n", block.source_range.start.v, elmd::InlineStyle::plain());
                    }
                    else if (block.kind == elmd::RenderBlockKind::Footnote)
                    {
                        AppendGeneratedText(display, U"[" + elmd::utf8_to_cps(block.footnote_label) + U"] ", block.source_range.start.v, elmd::InlineStyle::plain());
                    }
                    std::function<void(elmd::RenderBlock const&)> appendChild = [&](elmd::RenderBlock const& child) {
                        if (!child.inline_items.empty())
                        {
                            MergeDisplayText(display, BuildDisplayInlineText(child.inline_items, caret, child.content_range.end.v, sourceText, mathJax, svgNormalizer, styleSheet.textColor, styleSheet.body.size, documentRight - documentLeft, svgSupported, requestEmbedded));
                        }
                        else if (!child.child_blocks.empty())
                        {
                            for (std::size_t nestedIndex = 0; nestedIndex < child.child_blocks.size(); ++nestedIndex)
                            {
                                appendChild(child.child_blocks[nestedIndex]);
                                if (nestedIndex + 1 < child.child_blocks.size()) AppendGeneratedText(display, U"\n", child.child_blocks[nestedIndex].content_range.end.v, elmd::InlineStyle::plain());
                            }
                        }
                        else
                        {
                            AppendSourceText(display, sourceText, child.content_range.start.v, child.content_range.end.v, elmd::InlineStyle::plain(), false);
                        }
                    };
                    for (std::size_t childIndex = 0; childIndex < block.child_blocks.size(); ++childIndex)
                    {
                        auto const& child = block.child_blocks[childIndex];
                        appendChild(child);
                        if (childIndex + 1 < block.child_blocks.size()) AppendGeneratedText(display, U"\n", child.content_range.end.v, elmd::InlineStyle::plain());
                    }
                    display.displayToSource.push_back(block.content_range.end.v);
                    text = std::move(display.text);
                    inlineRanges = std::move(display.ranges);
                    displayToSource = std::move(display.displayToSource);
                    inlineMathOverlays = std::move(display.mathOverlays);
                    inlineMathPreviews = std::move(display.mathPreviews);
                    inset = 16.0f;
                    textTop = 12.0f;
                    fillPanel = true;
                    break;
                }
                case elmd::RenderBlockKind::Toc:
                {
                    DisplayInlineText display;
                    auto editing = block.source_range.start.v <= caret && caret <= block.source_range.end.v;
                    if (editing)
                    {
                        AppendSourceText(display, sourceText, block.source_range.start.v, block.source_range.end.v, elmd::InlineStyle::plain(), false);
                    }
                    else
                    {
                        for (auto const* item : sessionCore.renderModel.outline.flat_items())
                        {
                            std::u32string label(static_cast<std::size_t>((std::max)(0, static_cast<int>(item->level) - 1) * 2), U' ');
                            label += U"• " + elmd::utf8_to_cps(item->title_plain_text) + U"\n";
                            AppendGeneratedText(display, label, block.source_range.start.v, elmd::InlineStyle::plain());
                        }
                    }
                    display.displayToSource.push_back(block.source_range.end.v);
                    text = std::move(display.text);
                    inlineRanges = std::move(display.ranges);
                    displayToSource = std::move(display.displayToSource);
                    inset = 12.0f;
                    textTop = 10.0f;
                    fillPanel = true;
                    break;
                }
                case elmd::RenderBlockKind::Frontmatter:
                {
                    DisplayInlineText display;
                    AppendSourceText(display, sourceText, block.source_range.start.v, block.source_range.end.v, elmd::InlineStyle::plain(), false);
                    display.displayToSource.push_back(block.source_range.end.v);
                    text = std::move(display.text);
                    inlineRanges = std::move(display.ranges);
                    displayToSource = std::move(display.displayToSource);
                    format = codeFormat.Get();
                    brush = codeBrush.Get();
                    inset = 16.0f;
                    textTop = 12.0f;
                    fillPanel = true;
                    break;
                }
                case elmd::RenderBlockKind::Table:
                    text = U"Table";
                    displayToSource.push_back(block.source_range.start.v);
                    displayToSource.push_back(block.source_range.end.v);
                    break;
                case elmd::RenderBlockKind::ThematicBreak:
                    text = U"\u200B";
                    displayToSource.push_back(block.content_range.start.v);
                    displayToSource.push_back(block.content_range.end.v);
                    height = 48.0f;
                    textTop = 0.0f;
                    measureHeight = false;
                    thematicBreak = true;
                    break;
                case elmd::RenderBlockKind::Image:
                {
                    blockImage = LoadRasterImage(sessionCore.baseDirectory, block.src);
                    showRawImage = block.source_range.start.v <= caret && caret <= block.source_range.end.v;
                    DisplayInlineText display;
                    if (showRawImage || !blockImage)
                    {
                        auto visibleEnd = block.source_range.end.v;
                        if (visibleEnd > block.source_range.start.v && visibleEnd <= sourceText.size() && sourceText[visibleEnd - 1] == U'\n') --visibleEnd;
                        AppendSourceText(display, sourceText, block.source_range.start.v, visibleEnd, elmd::InlineStyle::plain(), false);
                    }
                    else
                    {
                        AppendMathPlaceholder(display, 1, block.source_range.start.v);
                    }
                    display.displayToSource.push_back(block.source_range.end.v);
                    text = std::move(display.text);
                    inlineRanges = std::move(display.ranges);
                    displayToSource = std::move(display.displayToSource);
                    brush = mutedBrush.Get();
                    inset = 8.0f;
                    textTop = showRawImage ? 8.0f : 0.0f;
                    break;
                }
                case elmd::RenderBlockKind::Unsupported:
                    text = elmd::utf8_to_cps(block.raw);
                    brush = mutedBrush.Get();
                    height = 64.0f;
                    break;
                default:
                {
                    auto display = BuildDisplayInlineText(
                        block.inline_items,
                        caret,
                        block.content_range.end.v,
                        sourceText,
                        mathJax,
                        svgNormalizer,
                        styleSheet.textColor,
                        styleSheet.body.size,
                        documentRight - documentLeft,
                        svgSupported,
                        requestEmbedded);
                    text = std::move(display.text);
                    inlineRanges = std::move(display.ranges);
                    displayToSource = std::move(display.displayToSource);
                    inlineMathOverlays = std::move(display.mathOverlays);
                    inlineMathPreviews = std::move(display.mathPreviews);
                    brush = mutedBrush.Get();
                    break;
                }
            }

            if (displayToSource.empty())
            {
                auto sourceStart = SourceStart(block);
                if (text.empty())
                {
                    text = U" ";
                    displayToSource.push_back(sourceStart);
                    displayToSource.push_back(SourceEnd(block, text));
                }
                else
                {
                    displayToSource.reserve(text.size() + 1);
                    for (std::size_t index = 0; index < text.size(); ++index)
                    {
                        displayToSource.push_back(sourceStart + index);
                    }
                    displayToSource.push_back(SourceEnd(block, text));
                }
            }
            if (text.empty())
            {
                auto sourceOffset = displayToSource.empty() ? SourceStart(block) : displayToSource.front();
                text = U" ";
                displayToSource.clear();
                displayToSource.push_back(sourceOffset);
                displayToSource.push_back(sourceOffset);
            }

            auto wide = ToWide(text);
            auto textWidth = (std::max)(1.0f, documentRight - documentLeft - inset * 2.0f);
            auto layout = createLayout(wide, format, textWidth);
            applyInlineStyles(layout.Get(), inlineRanges);
            ApplyMathInlineObjects(layout.Get(), inlineMathOverlays);
            std::optional<D2D1_POINT_2F> blockMathOrigin;
            std::vector<std::vector<D2D1_POINT_2F>> inlineMathPreviewOrigins;
            std::optional<D2D1_POINT_2F> blockMermaidOrigin;
            std::optional<D2D1_RECT_F> blockImageRect;
            float blockMermaidWidth = 0.0f;
            float blockMermaidHeight = 0.0f;
            if (measureHeight)
            {
                auto fallbackHeight = format == codeFormat.Get() ? styleSheet.code.lineHeight : styleSheet.body.lineHeight;
                auto bottomPadding = fillPanel ? 16.0f : 8.0f;
                height = textTop + measureTextHeight(layout.Get(), fallbackHeight) + bottomPadding;
            }
            if (!inlineMathPreviews.empty())
            {
                auto availableWidth = (std::max)(1.0f, documentRight - documentLeft - inset * 2.0f);
                auto previewY = y + textTop + measureTextHeight(layout.Get(), styleSheet.body.lineHeight) + 8.0f;
                inlineMathPreviewOrigins.reserve(inlineMathPreviews.size());
                for (auto const& preview : inlineMathPreviews)
                {
                    std::vector<D2D1_POINT_2F> origins;
                    origins.reserve(preview.svg.fragments.size());
                    auto previewX = documentLeft + inset;
                    auto lineTop = previewY;
                    auto lineHeight = 0.0f;
                    for (auto const& fragment : preview.svg.fragments)
                    {
                        if (fragment.breakBefore && previewX > documentLeft + inset)
                        {
                            lineTop += lineHeight + 4.0f;
                            previewX = documentLeft + inset;
                            lineHeight = 0.0f;
                        }
                        if (previewX + fragment.breakSpace + fragment.width > documentLeft + inset + availableWidth && previewX > documentLeft + inset)
                        {
                            lineTop += lineHeight + 4.0f;
                            previewX = documentLeft + inset;
                            lineHeight = 0.0f;
                        }
                        previewX += fragment.breakSpace;
                        origins.push_back(D2D1::Point2F(previewX, lineTop));
                        previewX += fragment.width;
                        lineHeight = (std::max)(lineHeight, fragment.height);
                    }
                    previewY = lineTop + lineHeight + 8.0f;
                    inlineMathPreviewOrigins.push_back(std::move(origins));
                }
                height = (std::max)(height, previewY - y + 8.0f);
            }
            if (block.kind == elmd::RenderBlockKind::Math && blockMath && static_cast<bool>(*blockMath))
            {
                auto previewWidth = blockMath->width;
                auto previewX = documentLeft + (std::max)(inset, (documentRight - documentLeft - previewWidth) * 0.5f);
                if (showRawMath)
                {
                    auto rawHeight = measureTextHeight(layout.Get(), styleSheet.code.lineHeight);
                    auto previewY = y + textTop + rawHeight + 10.0f;
                    height = textTop + rawHeight + 10.0f + blockMath->height + 16.0f;
                    blockMathOrigin = D2D1::Point2F(previewX, previewY);
                }
                else
                {
                    height = blockMath->height + 32.0f;
                    blockMathOrigin = D2D1::Point2F(previewX, y + 16.0f);
                }
            }
            if (block.kind == elmd::RenderBlockKind::Code && blockMermaid && static_cast<bool>(*blockMermaid))
            {
                auto availableWidth = (std::max)(1.0f, documentRight - documentLeft - inset * 2.0f);
                auto scale = (std::min)(1.0f, availableWidth / blockMermaid->width);
                blockMermaidWidth = blockMermaid->width * scale;
                blockMermaidHeight = blockMermaid->height * scale;
                auto previewX = documentLeft + (documentRight - documentLeft - blockMermaidWidth) * 0.5f;
                if (showRawMermaid)
                {
                    auto rawHeight = measureTextHeight(layout.Get(), styleSheet.code.lineHeight);
                    auto previewY = y + textTop + rawHeight + 10.0f;
                    height = textTop + rawHeight + 10.0f + blockMermaidHeight + 16.0f;
                    blockMermaidOrigin = D2D1::Point2F(previewX, previewY);
                }
                else
                {
                    height = blockMermaidHeight + 32.0f;
                    blockMermaidOrigin = D2D1::Point2F(previewX, y + 16.0f);
                }
            }
            if (block.kind == elmd::RenderBlockKind::Image && blockImage)
            {
                auto availableWidth = (std::max)(1.0f, documentRight - documentLeft - inset * 2.0f);
                auto scale = (std::min)(1.0f, (std::min)(availableWidth / blockImage->width, 600.0f / blockImage->height));
                auto imageWidth = blockImage->width * scale;
                auto imageHeight = blockImage->height * scale;
                auto imageX = documentLeft + (documentRight - documentLeft - imageWidth) * 0.5f;
                auto imageY = y + 8.0f;
                if (showRawImage)
                {
                    auto rawHeight = measureTextHeight(layout.Get(), styleSheet.body.lineHeight);
                    imageY = y + textTop + rawHeight + 8.0f;
                }
                blockImageRect = D2D1::RectF(imageX, imageY, imageX + imageWidth, imageY + imageHeight);
                height = imageY - y + imageHeight + 8.0f;
            }
            auto sourceStart = displayToSource.empty() ? SourceStart(block) : displayToSource.front();
            auto sourceEnd = displayToSource.empty() ? SourceEnd(block, text) : displayToSource.back();
            if (fillPanel)
            {
                d2dContext->FillRectangle(D2D1::RectF(documentLeft, y, documentRight, y + height), panelBrush.Get());
            }
            if (block.kind == elmd::RenderBlockKind::Callout)
            {
                d2dContext->DrawLine(D2D1::Point2F(documentLeft + 2.0f, y + 4.0f), D2D1::Point2F(documentLeft + 2.0f, y + height - 4.0f), accentBrush.Get(), 4.0f);
            }
            if (thematicBreak)
            {
                auto ruleY = y + height * 0.5f;
                d2dContext->DrawLine(D2D1::Point2F(documentLeft, ruleY), D2D1::Point2F(documentRight, ruleY), mutedBrush.Get(), 1.0f);
            }
            auto origin = D2D1::Point2F(documentLeft + inset, y + textTop);
            if (layout)
            {
                for (auto const& range : inlineRanges)
                {
                    if (!range.style.code || range.length == 0)
                    {
                        continue;
                    }

                    UINT32 actualCount = 0;
                    auto hr = layout->HitTestTextRange(range.start, range.length, origin.x, origin.y, nullptr, 0, &actualCount);
                    if (hr == E_NOT_SUFFICIENT_BUFFER && actualCount > 0)
                    {
                        std::vector<DWRITE_HIT_TEST_METRICS> metrics(actualCount);
                        if (SUCCEEDED(layout->HitTestTextRange(range.start, range.length, origin.x, origin.y, metrics.data(), actualCount, &actualCount)))
                        {
                            for (UINT32 index = 0; index < actualCount; ++index)
                            {
                                auto const& metric = metrics[index];
                                auto rect = D2D1::RoundedRect(
                                    D2D1::RectF(metric.left - 3.0f, metric.top + 2.0f, metric.left + metric.width + 3.0f, metric.top + metric.height - 1.0f),
                                    4.0f,
                                    4.0f);
                                d2dContext->FillRoundedRectangle(rect, panelBrush.Get());
                            }
                        }
                    }
                }
                if (!selection.is_empty() && selection.end.v > sourceStart && selection.start.v < sourceEnd)
                {
                    auto rangeStart = DisplayPositionForSource(displayToSource, (std::max)(selection.start.v, sourceStart));
                    auto rangeEnd = DisplayPositionForSource(displayToSource, (std::min)(selection.end.v, sourceEnd));
                    UINT32 actualCount = 0;
                    auto hr = layout->HitTestTextRange(static_cast<UINT32>(rangeStart), static_cast<UINT32>(rangeEnd - rangeStart), origin.x, origin.y, nullptr, 0, &actualCount);
                    if (hr == E_NOT_SUFFICIENT_BUFFER && actualCount > 0)
                    {
                        std::vector<DWRITE_HIT_TEST_METRICS> metrics(actualCount);
                        if (SUCCEEDED(layout->HitTestTextRange(static_cast<UINT32>(rangeStart), static_cast<UINT32>(rangeEnd - rangeStart), origin.x, origin.y, metrics.data(), actualCount, &actualCount)))
                        {
                            for (UINT32 i = 0; i < actualCount; ++i)
                            {
                                auto const& metric = metrics[i];
                                d2dContext->FillRectangle(D2D1::RectF(metric.left, metric.top, metric.left + metric.width, metric.top + metric.height), selectionBrush.Get());
                            }
                        }
                    }
                }

                d2dContext->DrawTextLayout(origin, layout.Get(), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);

                for (auto const& preview : inlineMathPreviews)
                {
                    if (preview.displayLength == 0) continue;
                    UINT32 actualCount = 0;
                    auto hr = layout->HitTestTextRange(preview.displayStart, preview.displayLength, origin.x, origin.y, nullptr, 0, &actualCount);
                    if (hr != E_NOT_SUFFICIENT_BUFFER || actualCount == 0) continue;
                    std::vector<DWRITE_HIT_TEST_METRICS> metrics(actualCount);
                    if (FAILED(layout->HitTestTextRange(preview.displayStart, preview.displayLength, origin.x, origin.y, metrics.data(), actualCount, &actualCount))) continue;
                    for (UINT32 metricIndex = 0; metricIndex < actualCount; ++metricIndex)
                    {
                        auto const& metric = metrics[metricIndex];
                        auto segmentStart = (std::max)(metric.textPosition, preview.displayStart);
                        auto segmentEnd = (std::min)(metric.textPosition + metric.length, preview.displayStart + preview.displayLength);
                        if (segmentStart >= segmentEnd) continue;
                        auto localStart = static_cast<std::size_t>(segmentStart - preview.displayStart);
                        auto localEnd = static_cast<std::size_t>(segmentEnd - preview.displayStart);
                        auto hitStart = (std::min)(preview.sourceStart + localStart, preview.sourceEnd);
                        auto hitEnd = (std::min)(preview.sourceStart + localEnd, preview.sourceEnd);
                        visualMathHits.push_back(VisualMathHit{
                            D2D1::RectF(metric.left - 2.0f, metric.top - 2.0f, metric.left + metric.width + 2.0f, metric.top + metric.height + 2.0f),
                            hitStart,
                            hitEnd,
                            0.0f,
                            1.0f,
                        });
                    }
                }

                for (auto const& overlay : inlineMathOverlays)
                {
                    float pointX = 0.0f;
                    float pointY = 0.0f;
                    DWRITE_HIT_TEST_METRICS metrics{};
                    if (SUCCEEDED(layout->HitTestTextPosition(overlay.displayStart, FALSE, &pointX, &pointY, &metrics)))
                    {
                        auto mathY = origin.y + metrics.top;
                        auto mathX = origin.x + pointX + overlay.leadingSpace;
                        if (!drawMathSvg(overlay.fragment, D2D1::Point2F(mathX, mathY), styleSheet.textColor))
                        {
                            drawMathFallback(overlay.sourceStart, overlay.sourceEnd, D2D1::Point2F(mathX, mathY));
                        }
                        if (overlay.strikethrough)
                        {
                            auto strikeY = mathY + overlay.fragment.height * 0.52f;
                            d2dContext->DrawLine(D2D1::Point2F(origin.x + pointX, strikeY), D2D1::Point2F(mathX + overlay.fragment.width, strikeY), textBrush.Get(), 1.5f);
                        }
                        visualMathHits.push_back(VisualMathHit{
                            D2D1::RectF(mathX, mathY, mathX + overlay.fragment.width, mathY + overlay.fragment.height),
                            overlay.sourceStart,
                            overlay.sourceEnd,
                            overlay.progressStart,
                            overlay.progressEnd,
                        });
                    }
                }
                for (std::size_t previewIndex = 0; previewIndex < inlineMathPreviews.size(); ++previewIndex)
                {
                    auto const& preview = inlineMathPreviews[previewIndex];
                    auto const& origins = inlineMathPreviewOrigins[previewIndex];
                    auto progress = 0.0f;
                    for (std::size_t fragmentIndex = 0; fragmentIndex < preview.svg.fragments.size() && fragmentIndex < origins.size(); ++fragmentIndex)
                    {
                        auto const& fragment = preview.svg.fragments[fragmentIndex];
                        auto progressStart = preview.svg.width > 0.0f ? progress / preview.svg.width : 0.0f;
                        progress += fragment.breakSpace + fragment.width;
                        auto progressEnd = preview.svg.width > 0.0f ? progress / preview.svg.width : 1.0f;
                        drawMathSvg(fragment, origins[fragmentIndex], styleSheet.textColor);
                        if (preview.strikethrough)
                        {
                            auto strikeY = origins[fragmentIndex].y + fragment.height * 0.52f;
                            d2dContext->DrawLine(D2D1::Point2F(origins[fragmentIndex].x, strikeY), D2D1::Point2F(origins[fragmentIndex].x + fragment.width, strikeY), textBrush.Get(), 1.5f);
                        }
                        visualMathHits.push_back(VisualMathHit{
                            D2D1::RectF(origins[fragmentIndex].x - 2.0f, origins[fragmentIndex].y - 2.0f, origins[fragmentIndex].x + fragment.width + 2.0f, origins[fragmentIndex].y + fragment.height + 2.0f),
                            preview.contentStart,
                            preview.contentEnd,
                            progressStart,
                            progressEnd,
                        });
                    }
                }
                if (blockMathOrigin && blockMath)
                {
                    auto mathX = blockMathOrigin->x;
                    for (auto const& fragment : blockMath->fragments)
                    {
                        mathX += fragment.breakSpace;
                        if (!drawMathSvg(fragment, D2D1::Point2F(mathX, blockMathOrigin->y), styleSheet.textColor))
                        {
                            drawMathFallback(block.content_range.start.v, block.content_range.end.v, D2D1::Point2F(mathX, blockMathOrigin->y));
                        }
                        mathX += fragment.width;
                    }
                }
                if (blockMermaidOrigin && blockMermaid)
                {
                    drawSvg(blockMermaid->renderId, blockMermaid->svg, blockMermaidWidth, blockMermaidHeight, *blockMermaidOrigin);
                }
                if (blockImageRect && blockImage && blockImage->bitmap)
                {
                    d2dContext->DrawBitmap(blockImage->bitmap.Get(), *blockImageRect, 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
                }

                VisualBlock visualBlock;
                visualBlock.rect = D2D1::RectF(documentLeft, y, documentRight, y + height);
                visualBlock.textOrigin = origin;
                visualBlock.textWidth = textWidth;
                visualBlock.sourceStart = sourceStart;
                visualBlock.sourceEnd = sourceEnd;
                visualBlock.documentY = y + scrollOffset;
                visualBlock.text = std::move(text);
                visualBlock.displayToSource = std::move(displayToSource);
                visualBlock.layout = layout;
                visualBlock.thematicBreak = thematicBreak;
                visualBlocks.push_back(std::move(visualBlock));
                if (thematicBreak)
                {
                    VisualLine visualLine;
                    visualLine.blockIndex = visualBlocks.size() - 1;
                    visualLine.sourceStart = sourceStart;
                    visualLine.sourceEnd = sourceEnd;
                    visualLine.displayStart = 0;
                    visualLine.displayEnd = 1;
                    visualLine.rect = visualBlocks.back().rect;
                    visualLines.push_back(std::move(visualLine));
                }
                else
                {
                    addVisualLinesForBlock(visualBlocks.size() - 1);
                }
            }
            y += height;
            blockHeightCache[cacheKey] = height;
            previousBlank = currentBlank;
        }

        auto drawPlus = [&](D2D1_POINT_2F center)
        {
            d2dContext->FillEllipse(D2D1::Ellipse(center, 9.0f, 9.0f), accentBrush.Get());
            d2dContext->DrawLine(D2D1::Point2F(center.x - 4.0f, center.y), D2D1::Point2F(center.x + 4.0f, center.y), textBrush.Get(), 1.5f);
            d2dContext->DrawLine(D2D1::Point2F(center.x, center.y - 4.0f), D2D1::Point2F(center.x, center.y + 4.0f), textBrush.Get(), 1.5f);
        };
        auto drawRowControls = [&](VisualTable const& table, std::size_t row)
        {
            auto centerY = (table.rowBoundaries[row] + table.rowBoundaries[row + 1]) * 0.5f;
            auto dragRect = D2D1::RectF(table.rect.left - 50.0f, centerY - 11.0f, table.rect.left - 28.0f, centerY + 11.0f);
            auto deleteRect = D2D1::RectF(table.rect.left - 25.0f, centerY - 11.0f, table.rect.left - 3.0f, centerY + 11.0f);
            d2dContext->FillRectangle(dragRect, panelBrush.Get());
            d2dContext->FillRectangle(deleteRect, panelBrush.Get());
            d2dContext->DrawRectangle(dragRect, mutedBrush.Get(), 1.0f);
            d2dContext->DrawRectangle(deleteRect, mutedBrush.Get(), 1.0f);
            for (int index = -1; index <= 1; ++index)
            {
                auto lineY = centerY + static_cast<float>(index * 4);
                d2dContext->DrawLine(D2D1::Point2F(dragRect.left + 6.0f, lineY), D2D1::Point2F(dragRect.right - 6.0f, lineY), mutedBrush.Get(), 1.5f);
            }
            d2dContext->DrawLine(D2D1::Point2F(deleteRect.left + 7.0f, centerY - 4.0f), D2D1::Point2F(deleteRect.right - 7.0f, centerY + 4.0f), accentBrush.Get(), 1.5f);
            d2dContext->DrawLine(D2D1::Point2F(deleteRect.right - 7.0f, centerY - 4.0f), D2D1::Point2F(deleteRect.left + 7.0f, centerY + 4.0f), accentBrush.Get(), 1.5f);
        };
        auto drawColumnControls = [&](VisualTable const& table, std::size_t column)
        {
            auto centerX = (table.columnBoundaries[column] + table.columnBoundaries[column + 1]) * 0.5f;
            auto dragRect = D2D1::RectF(centerX - 23.0f, table.rect.top - 29.0f, centerX - 1.0f, table.rect.top - 7.0f);
            auto deleteRect = D2D1::RectF(centerX + 2.0f, table.rect.top - 29.0f, centerX + 24.0f, table.rect.top - 7.0f);
            d2dContext->FillRectangle(dragRect, panelBrush.Get());
            d2dContext->FillRectangle(deleteRect, panelBrush.Get());
            d2dContext->DrawRectangle(dragRect, mutedBrush.Get(), 1.0f);
            d2dContext->DrawRectangle(deleteRect, mutedBrush.Get(), 1.0f);
            for (int index = -1; index <= 1; ++index)
            {
                auto lineY = (dragRect.top + dragRect.bottom) * 0.5f + static_cast<float>(index * 4);
                d2dContext->DrawLine(D2D1::Point2F(dragRect.left + 6.0f, lineY), D2D1::Point2F(dragRect.right - 6.0f, lineY), mutedBrush.Get(), 1.5f);
            }
            auto centerY = (deleteRect.top + deleteRect.bottom) * 0.5f;
            d2dContext->DrawLine(D2D1::Point2F(deleteRect.left + 7.0f, centerY - 4.0f), D2D1::Point2F(deleteRect.right - 7.0f, centerY + 4.0f), accentBrush.Get(), 1.5f);
            d2dContext->DrawLine(D2D1::Point2F(deleteRect.right - 7.0f, centerY - 4.0f), D2D1::Point2F(deleteRect.left + 7.0f, centerY + 4.0f), accentBrush.Get(), 1.5f);
        };
        if (pointerPosition)
        {
            for (auto const& table : visualTables)
            {
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
                            d2dContext->DrawLine(D2D1::Point2F(left, boundary), D2D1::Point2F(right, boundary), accentBrush.Get(), 2.0f);
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
                            d2dContext->DrawLine(D2D1::Point2F(boundary, top), D2D1::Point2F(boundary, bottom), accentBrush.Get(), 2.0f);
                            drawPlus(D2D1::Point2F(boundary, (top + bottom) * 0.5f));
                        }
                    }
                }
            }
        }
        if (draggedTableAction && tableDropIndex)
        {
            for (auto const& table : visualTables)
            {
                if (draggedTableAction->sourceOffset < table.sourceStart || draggedTableAction->sourceOffset > table.sourceEnd) continue;
                if (draggedTableAction->kind == TableActionKind::DragRow && *tableDropIndex < table.rowBoundaries.size())
                {
                    auto boundary = table.rowBoundaries[*tableDropIndex];
                    d2dContext->DrawLine(D2D1::Point2F(table.rect.left, boundary), D2D1::Point2F(table.rect.right, boundary), accentBrush.Get(), 3.0f);
                }
                if (draggedTableAction->kind == TableActionKind::DragColumn && *tableDropIndex < table.columnBoundaries.size())
                {
                    auto boundary = table.columnBoundaries[*tableDropIndex];
                    d2dContext->DrawLine(D2D1::Point2F(boundary, table.rect.top), D2D1::Point2F(boundary, table.rect.bottom), accentBrush.Get(), 3.0f);
                }
            }
        }

        totalDocumentHeight = y + scrollOffset + styleSheet.verticalPadding;
        auto maxScroll = MaximumScrollOffset();
        scrollOffset = (std::min)(scrollOffset, maxScroll);
        scrollTarget = (std::min)(scrollTarget, maxScroll);

        if (sessionCore.editor.selection().is_caret())
        {
            auto upstream = sessionCore.editor.selection().affinity == elmd::TextAffinity::Upstream;
            if (auto rect = CaretBounds(caret, upstream))
            {
                d2dContext->DrawLine(D2D1::Point2F(rect->left, rect->top), D2D1::Point2F(rect->left, rect->bottom), caretBrush.Get(), 1.5f);
            }
        }
    }

    void EditorSurfaceRenderer::ScrollBy(float delta)
    {
        SetScrollOffset(scrollOffset + delta);
    }

    void EditorSurfaceRenderer::QueueScrollBy(float delta)
    {
        scrollTarget = (std::min)(MaximumScrollOffset(), (std::max)(0.0f, scrollTarget + delta));
    }

    bool EditorSurfaceRenderer::AdvanceScrollAnimation(float elapsedSeconds)
    {
        auto distance = scrollTarget - scrollOffset;
        if (std::fabs(distance) < 0.25f)
        {
            scrollOffset = scrollTarget;
            return false;
        }
        auto blend = 1.0f - std::exp(-18.0f * (std::max)(0.0f, elapsedSeconds));
        scrollOffset += distance * blend;
        return true;
    }

    void EditorSurfaceRenderer::SetScrollOffset(float value)
    {
        scrollOffset = (std::min)(MaximumScrollOffset(), (std::max)(0.0f, value));
        scrollTarget = scrollOffset;
    }

    float EditorSurfaceRenderer::ScrollOffset() const
    {
        return scrollOffset;
    }

    float EditorSurfaceRenderer::MaximumScrollOffset() const
    {
        return (std::max)(0.0f, totalDocumentHeight - surfaceHeightDip);
    }

    float EditorSurfaceRenderer::ViewportHeight() const
    {
        return surfaceHeightDip;
    }

    void EditorSurfaceRenderer::UpdatePointer(float x, float y)
    {
        pointerPosition = D2D1::Point2F(x, y);
    }

    void EditorSurfaceRenderer::ClearPointer()
    {
        pointerPosition.reset();
    }

    void EditorSurfaceRenderer::SetTableDrag(std::optional<TableAction> action, std::optional<std::size_t> dropIndex)
    {
        draggedTableAction = std::move(action);
        tableDropIndex = dropIndex;
    }

    std::optional<EditorSurfaceRenderer::TableAction> EditorSurfaceRenderer::TableActionAt(float x, float y) const
    {
        for (auto const& table : visualTables)
        {
            auto source_for_row = [&](std::size_t row) {
                auto index = (std::min)(row, table.rowCount - 1) * table.columnCount;
                return index < table.cells.size() ? table.cells[index].sourceStart : table.sourceStart;
            };
            auto source_for_column = [&](std::size_t column) {
                auto index = (std::min)(column, table.columnCount - 1);
                return index < table.cells.size() ? table.cells[index].sourceStart : table.sourceStart;
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
                        auto source = source_for_row(boundary == table.rowCount ? table.rowCount - 1 : boundary);
                        return TableAction{TableActionKind::InsertRow, source, boundary};
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
                        auto source = source_for_column(boundary == table.columnCount ? table.columnCount - 1 : boundary);
                        return TableAction{TableActionKind::InsertColumn, source, boundary};
                    }
                }
            }
            if (table.rect.left - 52.0f <= x && x <= table.rect.left - 3.0f && table.rect.top <= y && y <= table.rect.bottom)
            {
                for (std::size_t row = 0; row < table.rowCount; ++row)
                {
                    if (table.rowBoundaries[row] > y || y > table.rowBoundaries[row + 1]) continue;
                    auto source = source_for_row(row);
                    if (x <= table.rect.left - 28.0f) return TableAction{TableActionKind::DragRow, source, row};
                    return TableAction{TableActionKind::DeleteRow, source, row};
                }
            }
            if (table.rect.top - 29.0f <= y && y <= table.rect.top - 7.0f && table.rect.left <= x && x <= table.rect.right)
            {
                for (std::size_t column = 0; column < table.columnCount; ++column)
                {
                    auto center = (table.columnBoundaries[column] + table.columnBoundaries[column + 1]) * 0.5f;
                    if (center - 23.0f <= x && x <= center - 1.0f) return TableAction{TableActionKind::DragColumn, source_for_column(column), column};
                    if (center + 2.0f <= x && x <= center + 24.0f) return TableAction{TableActionKind::DeleteColumn, source_for_column(column), column};
                }
            }
        }
        return std::nullopt;
    }

    std::optional<std::size_t> EditorSurfaceRenderer::TableDropIndexAt(float x, float y, bool rows) const
    {
        for (auto const& table : visualTables)
        {
            if (draggedTableAction && (draggedTableAction->sourceOffset < table.sourceStart || draggedTableAction->sourceOffset > table.sourceEnd)) continue;
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

    bool EditorSurfaceRenderer::ScrollToSourceOffset(std::size_t sourceOffset)
    {
        auto previous = scrollOffset;
        if (auto caretBounds = CaretBounds(sourceOffset))
        {
            auto margin = styleSheet.verticalPadding;
            auto maxScroll = (std::max)(0.0f, totalDocumentHeight - surfaceHeightDip);
            if (caretBounds->top < margin)
            {
                scrollOffset = (std::max)(0.0f, scrollOffset - (margin - caretBounds->top));
            }
            else if (caretBounds->bottom > surfaceHeightDip - margin)
            {
                scrollOffset = (std::min)(maxScroll, scrollOffset + caretBounds->bottom - (surfaceHeightDip - margin));
            }
            scrollTarget = scrollOffset;
            return scrollOffset != previous;
        }

        for (auto const& block : visualBlocks)
        {
            if (block.sourceStart <= sourceOffset && sourceOffset <= block.sourceEnd)
            {
                auto maxScroll = (std::max)(0.0f, totalDocumentHeight - surfaceHeightDip);
                scrollOffset = (std::min)(maxScroll, (std::max)(0.0f, block.documentY - styleSheet.verticalPadding));
                scrollTarget = scrollOffset;
                return scrollOffset != previous;
            }
        }
        return false;
    }

    std::optional<std::size_t> EditorSurfaceRenderer::LineIndexFor(std::size_t sourceOffset, bool upstream) const
    {
        if (visualLines.empty())
        {
            return std::nullopt;
        }

        std::optional<std::size_t> firstContaining;
        std::optional<std::size_t> lastContaining;
        for (std::size_t index = 0; index < visualLines.size(); ++index)
        {
            auto const& line = visualLines[index];
            if (line.sourceStart <= sourceOffset && sourceOffset <= line.sourceEnd)
            {
                if (!firstContaining)
                {
                    firstContaining = index;
                }
                lastContaining = index;
            }
        }
        if (firstContaining)
        {
            return upstream ? firstContaining : lastContaining;
        }

        std::optional<std::size_t> prev;
        std::optional<std::size_t> next;
        for (std::size_t index = 0; index < visualLines.size(); ++index)
        {
            if (visualLines[index].sourceEnd <= sourceOffset)
            {
                prev = index;
            }
            if (!next && visualLines[index].sourceStart >= sourceOffset)
            {
                next = index;
            }
        }
        if (prev && next)
        {
            auto distPrev = sourceOffset - visualLines[*prev].sourceEnd;
            auto distNext = visualLines[*next].sourceStart - sourceOffset;
            if (distPrev == distNext)
            {
                return upstream ? prev : next;
            }
            return distPrev <= distNext ? prev : next;
        }
        if (prev)
        {
            return prev;
        }
        return next;
    }

    std::optional<D2D1_RECT_F> EditorSurfaceRenderer::CaretRectOnLine(VisualLine const& line, std::size_t sourceOffset, bool upstream) const
    {
        if (line.blockIndex >= visualBlocks.size())
        {
            return std::nullopt;
        }
        auto const& block = visualBlocks[line.blockIndex];
        auto clamped = (std::min)((std::max)(sourceOffset, line.sourceStart), line.sourceEnd);
        if (block.thematicBreak)
        {
            auto lineHeight = (std::min)(styleSheet.body.lineHeight, (line.rect.bottom - line.rect.top) * 0.46f);
            auto top = clamped <= line.sourceStart ? line.rect.top : line.rect.bottom - lineHeight;
            return D2D1::RectF(line.rect.left, top, line.rect.left + 2.0f, top + lineHeight);
        }
        IDWriteTextLayout* layout = block.layout.Get();
        D2D1_POINT_2F textOrigin = block.textOrigin;
        std::vector<std::size_t> const* displayToSource = &block.displayToSource;
        if (line.tableIndex < visualTables.size() && line.cellIndex < visualTables[line.tableIndex].cells.size())
        {
            auto const& cell = visualTables[line.tableIndex].cells[line.cellIndex];
            layout = cell.layout.Get();
            textOrigin = cell.textOrigin;
            displayToSource = &cell.displayToSource;
        }
        if (!layout)
        {
            return D2D1::RectF(line.rect.left, line.rect.top, line.rect.left + 2.0f, line.rect.bottom);
        }

        auto displayPos = DisplayPositionForSource(*displayToSource, clamped);
        displayPos = (std::min)((std::max)(displayPos, static_cast<std::size_t>(line.displayStart)), static_cast<std::size_t>(line.displayEnd));

        UINT32 hitPos = static_cast<UINT32>(displayPos);
        BOOL trailing = FALSE;
        bool atLineEnd = displayPos == line.displayEnd;
        if (atLineEnd && upstream && displayPos > line.displayStart)
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
            auto top = line.rect.top;
            auto bottom = line.rect.bottom;
            return D2D1::RectF(left, top, left + 2.0f, bottom);
        }
        return D2D1::RectF(line.rect.left, line.rect.top, line.rect.left + 2.0f, line.rect.bottom);
    }

    std::optional<D2D1_RECT_F> EditorSurfaceRenderer::CaretBounds(std::size_t sourceOffset, bool upstream) const
    {
        auto index = LineIndexFor(sourceOffset, upstream);
        if (!index)
        {
            return std::nullopt;
        }
        return CaretRectOnLine(visualLines[*index], sourceOffset, upstream);
    }

    std::optional<std::size_t> EditorSurfaceRenderer::VisualLineStart(std::size_t sourceOffset, bool upstream) const
    {
        auto index = LineIndexFor(sourceOffset, upstream);
        if (!index)
        {
            return std::nullopt;
        }
        return visualLines[*index].sourceStart;
    }

    std::optional<std::size_t> EditorSurfaceRenderer::VisualLineEnd(std::size_t sourceOffset, bool upstream) const
    {
        auto index = LineIndexFor(sourceOffset, upstream);
        if (!index)
        {
            return std::nullopt;
        }
        return visualLines[*index].sourceEnd;
    }

    std::optional<std::size_t> EditorSurfaceRenderer::HitTest(float x, float y, bool* outUpstream) const
    {
        if (outUpstream)
        {
            *outUpstream = false;
        }
        for (auto hit = visualMathHits.rbegin(); hit != visualMathHits.rend(); ++hit)
        {
            if (x < hit->rect.left || x > hit->rect.right || y < hit->rect.top || y > hit->rect.bottom)
            {
                continue;
            }
            auto width = (std::max)(1.0f, hit->rect.right - hit->rect.left);
            auto local = (std::clamp)((x - hit->rect.left) / width, 0.0f, 1.0f);
            auto progress = hit->progressStart + local * (hit->progressEnd - hit->progressStart);
            auto length = hit->sourceEnd - hit->sourceStart;
            auto offset = hit->sourceStart + static_cast<std::size_t>(std::llround(progress * static_cast<float>(length)));
            return (std::min)(offset, hit->sourceEnd);
        }
        if (visualLines.empty())
        {
            return std::nullopt;
        }

        std::size_t best = 0;
        float bestDist = (std::numeric_limits<float>::max)();
        for (std::size_t index = 0; index < visualLines.size(); ++index)
        {
            auto const& rect = visualLines[index].rect;
            float dist = 0.0f;
            if (y < rect.top)
            {
                dist = rect.top - y;
            }
            else if (y > rect.bottom)
            {
                dist = y - rect.bottom;
            }
            if (visualLines[index].tableIndex < visualTables.size())
            {
                if (x < rect.left) dist += rect.left - x;
                else if (x > rect.right) dist += x - rect.right;
            }
            if (dist < bestDist)
            {
                bestDist = dist;
                best = index;
            }
            if (dist == 0.0f)
            {
                break;
            }
        }

        auto const& line = visualLines[best];
        auto const& block = visualBlocks[line.blockIndex];
        if (block.thematicBreak)
        {
            if (outUpstream) *outUpstream = false;
            auto midpoint = (block.rect.top + block.rect.bottom) * 0.5f;
            return y < midpoint ? line.sourceStart : line.sourceEnd;
        }
        IDWriteTextLayout* layout = block.layout.Get();
        D2D1_POINT_2F textOrigin = block.textOrigin;
        std::vector<std::size_t> const* displayToSource = &block.displayToSource;
        if (line.tableIndex < visualTables.size() && line.cellIndex < visualTables[line.tableIndex].cells.size())
        {
            auto const& cell = visualTables[line.tableIndex].cells[line.cellIndex];
            layout = cell.layout.Get();
            textOrigin = cell.textOrigin;
            displayToSource = &cell.displayToSource;
        }
        if (!layout)
        {
            if (outUpstream)
            {
                *outUpstream = false;
            }
            return line.sourceStart;
        }

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
                bool nextIsWrap = best + 1 < visualLines.size()
                    && visualLines[best + 1].blockIndex == line.blockIndex
                    && visualLines[best + 1].wrapContinuation
                    && visualLines[best + 1].displayStart == line.displayEnd;
                *outUpstream = srcOff == line.sourceEnd && nextIsWrap;
            }
            return srcOff;
        }

        if (outUpstream)
        {
            *outUpstream = false;
        }
        return line.sourceStart;
    }

    std::optional<EditorSurfaceRenderer::CaretMove> EditorSurfaceRenderer::MoveCaretVertically(std::size_t sourceOffset, bool upstream, bool down, float& goalX) const
    {
        auto current = LineIndexFor(sourceOffset, upstream);
        if (!current || visualLines.empty())
        {
            return std::nullopt;
        }

        float x = goalX;
        if (x < 0.0f)
        {
            if (auto rect = CaretRectOnLine(visualLines[*current], sourceOffset, upstream))
            {
                x = rect->left;
            }
            else
            {
                x = visualLines[*current].rect.left;
            }
            goalX = x;
        }

        if (!down && *current == 0)
        {
            return CaretMove{ visualLines.front().sourceStart, false };
        }
        if (down && *current + 1 >= visualLines.size())
        {
            return CaretMove{ visualLines.back().sourceEnd, true };
        }

        std::size_t targetIndex = down ? *current + 1 : *current - 1;
        auto const& currentLine = visualLines[*current];
        if (currentLine.tableIndex < visualTables.size() && currentLine.cellIndex < visualTables[currentLine.tableIndex].cells.size())
        {
            auto const& table = visualTables[currentLine.tableIndex];
            auto const& cell = table.cells[currentLine.cellIndex];
            auto adjacentIsSameCell = down
                ? *current + 1 < visualLines.size()
                    && visualLines[*current + 1].tableIndex == currentLine.tableIndex
                    && visualLines[*current + 1].cellIndex == currentLine.cellIndex
                : *current > 0
                    && visualLines[*current - 1].tableIndex == currentLine.tableIndex
                    && visualLines[*current - 1].cellIndex == currentLine.cellIndex;
            if (adjacentIsSameCell)
            {
                targetIndex = down ? *current + 1 : *current - 1;
            }
            else if (down && cell.row + 1 < table.rowCount)
            {
                auto targetCell = (cell.row + 1) * table.columnCount + cell.column;
                for (std::size_t index = 0; index < visualLines.size(); ++index)
                {
                    if (visualLines[index].tableIndex == currentLine.tableIndex && visualLines[index].cellIndex == targetCell) { targetIndex = index; break; }
                }
            }
            else if (!down && cell.row > 0)
            {
                auto targetCell = (cell.row - 1) * table.columnCount + cell.column;
                for (std::size_t index = visualLines.size(); index > 0; --index)
                {
                    if (visualLines[index - 1].tableIndex == currentLine.tableIndex && visualLines[index - 1].cellIndex == targetCell) { targetIndex = index - 1; break; }
                }
            }
            else if (down)
            {
                auto lastCell = table.rowCount * table.columnCount - 1;
                for (std::size_t index = *current; index < visualLines.size(); ++index)
                {
                    if (visualLines[index].tableIndex == currentLine.tableIndex && visualLines[index].cellIndex == lastCell)
                    {
                        targetIndex = (std::min)(index + 1, visualLines.size() - 1);
                        break;
                    }
                }
            }
            else
            {
                for (std::size_t index = *current; index > 0; --index)
                {
                    if (visualLines[index - 1].tableIndex != currentLine.tableIndex) { targetIndex = index - 1; break; }
                }
            }
        }
        auto const& target = visualLines[targetIndex];
        auto const& block = visualBlocks[target.blockIndex];

        std::size_t srcOff = target.sourceStart;
        IDWriteTextLayout* layout = block.layout.Get();
        D2D1_POINT_2F textOrigin = block.textOrigin;
        std::vector<std::size_t> const* displayToSource = &block.displayToSource;
        if (target.tableIndex < visualTables.size() && target.cellIndex < visualTables[target.tableIndex].cells.size())
        {
            auto const& cell = visualTables[target.tableIndex].cells[target.cellIndex];
            layout = cell.layout.Get();
            textOrigin = cell.textOrigin;
            displayToSource = &cell.displayToSource;
        }
        if (layout)
        {
            float targetX = (std::min)((std::max)(x, target.rect.left), target.rect.right - 1.0f);
            float localX = targetX - textOrigin.x;
            float localY = (target.rect.top + target.rect.bottom) * 0.5f - textOrigin.y;
            BOOL isTrailingHit = FALSE;
            BOOL isInside = FALSE;
            DWRITE_HIT_TEST_METRICS metrics{};
            if (SUCCEEDED(layout->HitTestPoint(localX, localY, &isTrailingHit, &isInside, &metrics)))
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
        return CaretMove{ srcOff, newUpstream };
    }

    void EditorSurfaceRenderer::Render(detail::EditorSessionCore const& sessionCore)
    {
        if (!swapChain || !d3dDevice || !d3dContext || resizing || rendering)
        {
            return;
        }

        rendering = true;
        struct ResetFlag { bool& value; ~ResetFlag() { value = false; } } resetFlag{ rendering };

        if (!renderTargetView)
        {
            ::Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
            winrt::check_hresult(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuffer.GetAddressOf())));
            winrt::check_hresult(d3dDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, renderTargetView.GetAddressOf()));
        }

        if (!d2dTarget)
        {
            ::Microsoft::WRL::ComPtr<IDXGISurface> surface;
            winrt::check_hresult(swapChain->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(surface.GetAddressOf())));

            D2D1_BITMAP_PROPERTIES1 properties{};
            properties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
            properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
            properties.dpiX = 96.0f * surfaceScaleX;
            properties.dpiY = 96.0f * surfaceScaleY;
            properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

            winrt::check_hresult(d2dContext->CreateBitmapFromDxgiSurface(surface.Get(), &properties, d2dTarget.GetAddressOf()));
            d2dContext->SetTarget(d2dTarget.Get());
            d2dContext->SetDpi(96.0f * surfaceScaleX, 96.0f * surfaceScaleY);
        }

        if (!textBrush)
        {
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.textColor, textBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.mutedColor, mutedBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.accentColor, accentBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.codeTextColor, codeBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.panelColor, panelBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.canvasColor, canvasBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.nestedQuoteColor, nestedQuoteBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.selectionColor, selectionBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.caretColor, caretBrush.GetAddressOf()));
            for (std::size_t index = 0; index < syntaxBrushes.size(); ++index)
            {
                winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.syntaxColors[index], syntaxBrushes[index].GetAddressOf()));
            }
        }

        d2dContext->BeginDraw();
        d2dContext->Clear(styleSheet.canvasColor);
        DrawDocument(sessionCore);
        auto ended = d2dContext->EndDraw();
        if (ended == D2DERR_RECREATE_TARGET)
        {
            ResetTargets();
            return;
        }
        if (FAILED(ended)) return;

        auto presented = swapChain->Present(1, 0);
        if (presented == DXGI_ERROR_DEVICE_REMOVED || presented == DXGI_ERROR_DEVICE_RESET) ResetTargets();
    }
}
