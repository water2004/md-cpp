#pragma once

import folia.core.ids;
import folia.core.image_dimension;
import folia.core.render_model;
import folia.core.text_edit;
import folia.platform.editor_display_mapping;

#include "media/MathJaxRenderer.h"
#include "media/MermaidRenderer.h"
#include "media/SvgNormalizer.h"
#include "media/TreeSitterHighlighter.h"

namespace winrt::Folia
{
    using folia::platform::editor::EditorDisplayMapping;
    using folia::platform::editor::EditorDisplayPosition;
    using folia::platform::editor::EditorDisplayPositionKind;
    using folia::platform::editor::EditorFootnoteControlKind;

    inline std::optional<float> ResolveImageDimension(
        std::optional<folia::ImageDimension> const& dimension,
        std::optional<float> percentBasis = std::nullopt)
    {
        if (!dimension) return std::nullopt;
        if (dimension->unit == folia::ImageDimensionUnit::Pixels) return dimension->value;
        if (!percentBasis || *percentBasis <= 0.0f) return std::nullopt;
        return *percentBasis * dimension->value / 100.0f;
    }

    struct InlineStyleRange
    {
        UINT32 start = 0;
        UINT32 length = 0;
        folia::InlineStyle style;
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
            folia::TextSpan sourceSpan;
            float progressStart = 0.0f;
            float progressEnd = 1.0f;
            bool strikethrough = false;
            bool displayMath = false;
        };

        struct MathPreview
        {
            MathJaxSvg svg;
            folia::TextSpan contentSpan;
            bool strikethrough = false;
        };

        struct ImageOverlay
        {
            std::uint32_t displayStart = 0;
            folia::TextSpan sourceSpan;
            std::string source;
            std::string alt;
            std::optional<folia::ImageDimension> width;
            std::optional<folia::ImageDimension> height;
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
            folia::TextPosition sourcePosition;
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
            folia::TextSpan sourceSpan;
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
        std::vector<AsyncWorkDependency> pendingMathJaxDependencies;
        std::vector<AsyncWorkDependency> pendingSvgDependencies;
        bool pendingMath = false;
    };

    std::wstring ToWide(std::u32string_view text);
    std::optional<std::vector<std::uint8_t>> DecodeBase64(std::string_view source);
    std::optional<MathJaxSvg> NormalizeMathJaxSvg(
        MathJaxSvg const& source,
        SvgNormalizer& normalizer,
        D2D1_COLOR_F color,
        float fontSize,
        bool allowQueue,
        bool highPriority,
        std::vector<AsyncWorkDependency>* pendingDependencies = nullptr);
    std::optional<MermaidSvg> NormalizeMermaidSvg(MermaidSvg const& source, SvgNormalizer& normalizer, bool allowQueue);
    std::u32string InlineText(std::vector<folia::InlineRenderItem> const& items);
    folia::TextPosition InlineItemsEndPosition(std::vector<folia::InlineRenderItem> const& items, folia::TextPosition fallback);
    bool IsMermaidLanguage(std::optional<std::string> const& language);
    void MergeDisplayText(DisplayInlineText& target, DisplayInlineText source);
    void AppendSourceText(DisplayInlineText& display, std::u32string_view sourceText, folia::TextSpan sourceSpan, folia::InlineStyle style, bool marker);
    void AppendProjectedSourceText(
        DisplayInlineText& display,
        std::u32string_view text,
        folia::NodeId owner,
        std::vector<std::size_t> const& sourceOffsets,
        folia::InlineStyle style);
    void AppendGeneratedText(
        DisplayInlineText& display,
        std::u32string const& text,
        folia::TextPosition sourcePosition,
        folia::InlineStyle style,
        EditorDisplayPositionKind kind = EditorDisplayPositionKind::Generated);
    void AppendMathPlaceholder(DisplayInlineText& display, std::size_t count, folia::TextPosition sourcePosition);
    void AppendMathFragments(
        DisplayInlineText& display,
        MathJaxSvg const& math,
        folia::TextSpan sourceSpan,
        bool editing,
        folia::InlineStyle style);
    void ApplyInlinePlaceholder(IDWriteTextLayout* layout, UINT32 displayStart, float width, float height, float baseline);
    void ApplyMathInlineObjects(IDWriteTextLayout* layout, std::vector<DisplayInlineText::MathOverlay> const& overlays);
    void ApplyIndentInlineObjects(IDWriteTextLayout* layout, std::vector<DisplayInlineText::IndentOverlay> const& overlays);
    void ApplyTaskCheckboxInlineObjects(IDWriteTextLayout* layout, std::vector<DisplayInlineText::TaskCheckboxOverlay> const& overlays);
    DisplayInlineText BuildMathPreviewText(DisplayInlineText::MathPreview const& preview);
    DisplayInlineText BuildDisplayInlineText(
        std::vector<folia::InlineRenderItem> const& items,
        folia::TextPosition caret,
        folia::TextPosition sourceEnd,
        MathJaxRenderer& mathJax,
        SvgNormalizer& svgNormalizer,
        D2D1_COLOR_F svgColor,
        float fontSize,
        float containerWidth,
        bool svgSupported,
        bool requestMath,
        bool highPriority);
    DisplayInlineText BuildCodeBlockText(folia::RenderBlock const& block, folia::TextPosition caret, TreeSitterHighlighter& highlighter);
    DisplayInlineText BuildMathBlockText(
        folia::RenderBlock const& block,
        folia::TextPosition caret,
        MathJaxRenderer& mathJax,
        SvgNormalizer& svgNormalizer,
        D2D1_COLOR_F svgColor,
        float fontSize,
        float containerWidth,
        bool svgSupported,
        bool requestMath,
        bool highPriority);
}
