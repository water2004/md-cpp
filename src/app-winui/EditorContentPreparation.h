#pragma once

import elmd.core.ids;
import elmd.core.text_edit;

#include "MathJaxRenderer.h"
#include "MermaidRenderer.h"
#include "SvgNormalizer.h"
#include "TreeSitterHighlighter.h"

namespace winrt::ElMd
{
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
            elmd::TextSpan sourceSpan;
            float progressStart = 0.0f;
            float progressEnd = 1.0f;
            bool strikethrough = false;
        };

        struct MathPreview
        {
            MathJaxSvg svg;
            std::uint32_t displayStart = 0;
            std::uint32_t displayLength = 0;
            elmd::TextSpan sourceSpan;
            elmd::TextSpan contentSpan;
            bool strikethrough = false;
        };

        struct ImageOverlay
        {
            std::uint32_t displayStart = 0;
            elmd::TextSpan sourceSpan;
            std::string source;
            std::string alt;
            std::optional<float> width;
            std::optional<float> height;
        };

        std::u32string text;
        std::vector<elmd::TextPosition> displayToSource;
        std::vector<InlineStyleRange> ranges;
        std::vector<MathOverlay> mathOverlays;
        std::vector<MathPreview> mathPreviews;
        std::vector<ImageOverlay> imageOverlays;
    };

    std::wstring ToWide(std::u32string_view text);
    std::optional<std::vector<std::uint8_t>> DecodeBase64(std::string_view source);
    std::optional<MathJaxSvg> NormalizeMathJaxSvg(MathJaxSvg const& source, SvgNormalizer& normalizer, D2D1_COLOR_F color, float fontSize, bool allowQueue);
    std::optional<MermaidSvg> NormalizeMermaidSvg(MermaidSvg const& source, SvgNormalizer& normalizer, bool allowQueue);
    std::u32string InlineText(std::vector<elmd::InlineRenderItem> const& items);
    elmd::TextPosition InlineItemsEndPosition(std::vector<elmd::InlineRenderItem> const& items, elmd::TextPosition fallback);
    bool IsMermaidLanguage(std::optional<std::string> const& language);
    void MergeDisplayText(DisplayInlineText& target, DisplayInlineText source);
    void AppendSourceText(DisplayInlineText& display, std::u32string_view sourceText, elmd::TextSpan sourceSpan, elmd::InlineStyle style, bool marker);
    void AppendGeneratedText(DisplayInlineText& display, std::u32string const& text, elmd::TextPosition sourcePosition, elmd::InlineStyle style);
    void AppendMathPlaceholder(DisplayInlineText& display, std::size_t count, elmd::TextPosition sourcePosition);
    void ApplyInlinePlaceholder(IDWriteTextLayout* layout, UINT32 displayStart, float width, float height, float baseline);
    void ApplyMathInlineObjects(IDWriteTextLayout* layout, std::vector<DisplayInlineText::MathOverlay> const& overlays);
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
        bool requestMath,
        std::optional<elmd::NodeId> focusContainer = std::nullopt);
    DisplayInlineText BuildCodeBlockText(elmd::RenderBlock const& block, elmd::TextPosition caret);
    DisplayInlineText BuildIndentedCodeBlockText(elmd::RenderBlock const& block);
    std::size_t DisplayPositionForSource(std::vector<elmd::TextPosition> const& displayToSource, elmd::TextPosition sourcePosition);
}
