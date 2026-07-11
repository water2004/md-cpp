#pragma once

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

        struct ImageOverlay
        {
            std::uint32_t displayStart = 0;
            std::size_t sourceStart = 0;
            std::size_t sourceEnd = 0;
            std::string source;
            std::string alt;
            std::optional<float> width;
            std::optional<float> height;
        };

        std::u32string text;
        std::vector<std::size_t> displayToSource;
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
    bool IsMermaidLanguage(std::optional<std::string> const& language);
    void MergeDisplayText(DisplayInlineText& target, DisplayInlineText source);
    void AppendSourceText(DisplayInlineText& display, std::u32string_view sourceText, std::size_t start, std::size_t end, elmd::InlineStyle style, bool marker);
    void AppendGeneratedText(DisplayInlineText& display, std::u32string const& text, std::size_t sourceOffset, elmd::InlineStyle style);
    void AppendMathPlaceholder(DisplayInlineText& display, std::size_t count, std::size_t sourceOffset);
    void ApplyInlinePlaceholder(IDWriteTextLayout* layout, UINT32 displayStart, float width, float height, float baseline);
    void ApplyMathInlineObjects(IDWriteTextLayout* layout, std::vector<DisplayInlineText::MathOverlay> const& overlays);
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
        std::optional<elmd::CharRange> focusRange = std::nullopt);
    DisplayInlineText BuildCodeBlockText(elmd::RenderBlock const& block, std::size_t caret, std::u32string_view sourceText);
    DisplayInlineText BuildIndentedCodeBlockText(elmd::RenderBlock const& block, std::u32string_view sourceText);
    std::size_t DisplayPositionForSource(std::vector<std::size_t> const& displayToSource, std::size_t sourceOffset);
}
