#pragma once

import elmd.core.ids;
import elmd.core.text_edit;

#include "media/MathJaxRenderer.h"
#include "media/MermaidRenderer.h"
#include "media/SvgNormalizer.h"
#include "media/TreeSitterHighlighter.h"
#include "editor/session/EditorDisplayMapping.h"

namespace winrt::ElMd
{
    struct InlineStyleRange
    {
        UINT32 start = 0;
        UINT32 length = 0;
        elmd::InlineStyle style;
        bool marker = false;
        SyntaxHighlightKind syntax = SyntaxHighlightKind::None;
        std::optional<EditorFootnoteControlKind> footnoteControl;
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
            bool displayMath = false;
        };

        struct MathPreview
        {
            MathJaxSvg svg;
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
            bool block = false;
        };

        struct IndentOverlay
        {
            std::uint32_t displayStart = 0;
            float width = 0.0f;
        };

        struct TaskCheckboxOverlay
        {
            std::uint32_t displayStart = 0;
            elmd::TextPosition sourcePosition;
            bool checked = false;
            float advance = 0.0f;
            float height = 0.0f;
            float baseline = 0.0f;
            float boxSize = 0.0f;
        };

        struct FootnoteOverlay
        {
            std::uint32_t displayStart = 0;
            std::uint32_t displayLength = 0;
            elmd::TextSpan sourceSpan;
            std::string label;
            EditorFootnoteControlKind kind = EditorFootnoteControlKind::Reference;
        };

        std::u32string text;
        EditorDisplayMapping displayToSource;
        std::vector<InlineStyleRange> ranges;
        std::vector<MathOverlay> mathOverlays;
        std::vector<MathPreview> mathPreviews;
        std::vector<ImageOverlay> imageOverlays;
        std::vector<IndentOverlay> indentOverlays;
        std::vector<TaskCheckboxOverlay> taskCheckboxOverlays;
        std::vector<FootnoteOverlay> footnoteOverlays;
        bool pendingMath = false;
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
    void AppendProjectedSourceText(
        DisplayInlineText& display,
        std::u32string_view text,
        elmd::NodeId owner,
        std::vector<std::size_t> const& sourceOffsets,
        elmd::InlineStyle style);
    void AppendGeneratedText(
        DisplayInlineText& display,
        std::u32string const& text,
        elmd::TextPosition sourcePosition,
        elmd::InlineStyle style,
        EditorDisplayPositionKind kind = EditorDisplayPositionKind::Generated);
    void AppendMathPlaceholder(DisplayInlineText& display, std::size_t count, elmd::TextPosition sourcePosition);
    void ApplyInlinePlaceholder(IDWriteTextLayout* layout, UINT32 displayStart, float width, float height, float baseline);
    void ApplyMathInlineObjects(IDWriteTextLayout* layout, std::vector<DisplayInlineText::MathOverlay> const& overlays);
    void ApplyIndentInlineObjects(IDWriteTextLayout* layout, std::vector<DisplayInlineText::IndentOverlay> const& overlays);
    void ApplyTaskCheckboxInlineObjects(IDWriteTextLayout* layout, std::vector<DisplayInlineText::TaskCheckboxOverlay> const& overlays);
    DisplayInlineText BuildMathPreviewText(DisplayInlineText::MathPreview const& preview);
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
        bool requestMath);
    DisplayInlineText BuildCodeBlockText(elmd::RenderBlock const& block, elmd::TextPosition caret, TreeSitterHighlighter& highlighter);
    DisplayInlineText BuildMathBlockText(
        elmd::RenderBlock const& block,
        elmd::TextPosition caret,
        MathJaxRenderer& mathJax,
        SvgNormalizer& svgNormalizer,
        D2D1_COLOR_F svgColor,
        float fontSize,
        float containerWidth,
        bool svgSupported,
        bool requestMath);
}
