#include "pch.h"
#include "editor/rendering/EditorContentPreparation.h"

import folia.core.render_model;
import folia.core.utf;

namespace winrt::Folia
{
    DisplayInlineText BuildCodeBlockText(folia::RenderBlock const& block, folia::TextPosition caret, TreeSitterHighlighter& highlighter)
    {
        DisplayInlineText display;
        auto const& special = block.special();
        // An indented code block has no user-visible delimiter. Its leading
        // indentation is structural Markdown syntax and must stay hidden even
        // while the block owns the caret. content_to_source keeps every
        // displayed boundary in the one authoritative source coordinate.
        auto editingRawFence = caret.container_id == block.id && !special.code_indented;
        auto code = special.code_text;
        if (!editingRawFence)
        {
            // The final physical line ending terminates the last code line;
            // it does not introduce another presentation line.  Use the same
            // LF/CRLF/CR-aware projection as the core render model instead of
            // removing just '\n' (which left a visible DirectWrite line for
            // CRLF documents).
            auto const lines = folia::code_presentation_lines(code);
            if (!lines.empty()) code.resize(lines.back().content_range.end);
        }
        if (editingRawFence)
        {
            AppendSourceText(
                display,
                special.raw_source,
                {block.id, {0, special.raw_source.size()}},
                folia::InlineStyle::plain(),
                false);
        }
        else
        {
            AppendProjectedSourceText(
                display,
                code,
                block.id,
                special.content_to_source,
                folia::InlineStyle::plain());
        }
        if (special.language && !code.empty())
        {
            auto highlights = highlighter.Highlight(*special.language, folia::cps_to_utf8(code));
            for (auto const& highlight : highlights)
            {
                auto start = (std::min)(static_cast<std::size_t>(highlight.start), code.size());
                auto end = (std::min)(start + static_cast<std::size_t>(highlight.length), code.size());
                auto displayStart = editingRawFence
                    ? static_cast<std::uint32_t>(folia::char_index_to_utf16(
                        special.raw_source,
                        special.content_to_source.empty() ? start : special.content_to_source[start]))
                    : static_cast<std::uint32_t>(folia::char_index_to_utf16(code, start));
                auto displayEnd = editingRawFence
                    ? static_cast<std::uint32_t>(folia::char_index_to_utf16(
                        special.raw_source,
                        special.content_to_source.empty() ? end : special.content_to_source[end]))
                    : static_cast<std::uint32_t>(folia::char_index_to_utf16(code, end));
                if (displayStart < displayEnd)
                {
                    display.ranges.push_back(InlineStyleRange{
                        displayStart,
                        displayEnd - displayStart,
                        folia::InlineStyle::plain(),
                        false,
                        highlight.kind,
                    });
                }
            }
        }
        const auto endpoint = editingRawFence
            ? special.raw_source.size()
            : (special.content_to_source.empty()
                ? code.size()
                : special.content_to_source[(std::min)(code.size(), special.content_to_source.size() - 1)]);
        if (display.text.empty())
        {
            AppendGeneratedText(
                display,
                U"\u200B",
                {block.id, endpoint, folia::TextAffinity::Downstream},
                folia::InlineStyle::plain(),
                EditorDisplayPositionKind::Source);
        }
        display.displayToSource.push_back({block.id, endpoint, folia::TextAffinity::Downstream});
        return display;
    }

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
        bool highPriority)
    {
        DisplayInlineText display;
        auto const& special = block.special();
        auto span = block.content_span;
        auto editing = caret.container_id == block.id;
        if (!svgSupported)
        {
            const auto endpoint = editing
                ? special.raw_source.size()
                : special.content_to_source.empty()
                    ? special.tex.size()
                    : special.content_to_source.back();
            if (editing)
                AppendSourceText(display, special.raw_source, {block.id, {0, endpoint}}, folia::InlineStyle::plain(), false);
            else
                AppendProjectedSourceText(display, special.tex, block.id, special.content_to_source, folia::InlineStyle::plain());
            if (display.text.empty())
            {
                AppendGeneratedText(
                    display,
                    U"\u200B",
                    {block.id, endpoint, folia::TextAffinity::Downstream},
                    folia::InlineStyle::plain(),
                    EditorDisplayPositionKind::Source);
            }
            display.displayToSource.push_back({block.id, endpoint, folia::TextAffinity::Downstream});
            return display;
        }

        auto rawMath = mathJax.GetOrQueue(
            folia::cps_to_utf8(special.tex),
            true,
            fontSize,
            containerWidth,
            requestMath,
            highPriority);
        auto math = rawMath ? NormalizeMathJaxSvg(
            *rawMath,
            svgNormalizer,
            svgColor,
            fontSize,
            requestMath,
            highPriority) : std::nullopt;
        display.pendingMath = !rawMath || !math;
        if (editing)
        {
            AppendSourceText(
                display,
                special.raw_source,
                {block.id, {0, special.raw_source.size()}},
                folia::InlineStyle::plain(),
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
                special.tex,
                block.id,
                special.content_to_source,
                folia::InlineStyle::plain());
        }
        else
        {
            AppendMathFragments(display, *math, span, false, folia::InlineStyle::plain());
        }
        const auto endpoint = editing
            ? special.raw_source.size()
            : special.content_to_source.empty()
                ? special.tex.size()
                : special.content_to_source.back();
        if (display.text.empty())
        {
            AppendGeneratedText(
                display,
                U"\u200B",
                {block.id, endpoint, folia::TextAffinity::Downstream},
                folia::InlineStyle::plain(),
                EditorDisplayPositionKind::Source);
        }
        display.displayToSource.push_back({block.id, endpoint, folia::TextAffinity::Downstream});
        return display;
    }

}
