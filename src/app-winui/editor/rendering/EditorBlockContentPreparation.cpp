#include "pch.h"
#include "editor/rendering/EditorContentPreparation.h"

import elmd.core.render_model;
import elmd.core.utf;

namespace winrt::ElMd
{
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
