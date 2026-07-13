#include "pch.h"

import elmd.core.render_model;
import elmd.core.types;
import elmd.core.utf;

#include "EditorContentPreparation.h"
#include "EditorQuoteBlockRenderer.h"

namespace winrt::ElMd
{
    namespace
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
            elmd::TextSpan sourceSpan;
            bool code = false;
            std::vector<EditorInlineImageRenderer::ImageDraw> images;
        };
    }

    float EditorQuoteBlockRenderer::Render(
        elmd::RenderBlock const& block,
        elmd::TextPosition caret,
        elmd::TextSelection selection,
        float documentLeft,
        float documentRight,
        float top,
        float scrollOffset,
        bool svgSupported,
        bool requestEmbedded,
        EditorRenderResources& resources,
        EditorStyleSheet const& styleSheet,
        EditorInteractionMap& interactionMap,
        EditorTextLayoutEngine& textLayoutEngine,
        EditorInlineImageRenderer& inlineImageRenderer,
        MathJaxRenderer& mathJax,
        SvgNormalizer& svgNormalizer,
        EditorDrawMath const& drawMath,
        EditorDrawMathFallback const& drawMathFallback)
    {
        std::vector<QuoteBox> boxes;
        std::vector<QuoteFragment> fragments;
        auto depthInset = block.block_style.padding_left + 6.0f;
        auto cursorY = top + block.block_style.padding_top;
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
                AppendGeneratedText(display, U"\u200B", {child.id, 0, elmd::TextAffinity::Downstream}, elmd::InlineStyle::plain());
                display.displayToSource.push_back({child.id, 0, elmd::TextAffinity::Downstream});
            }
            else if (code)
            {
                display = child.code_indented ? BuildIndentedCodeBlockText(child) : BuildCodeBlockText(child, caret);
            }
            else if (!child.inline_items.empty())
            {
                display = BuildDisplayInlineText(child.inline_items, caret, {child.id, child.content_span.source_range.end, elmd::TextAffinity::Downstream}, mathJax, svgNormalizer, styleSheet.textColor, styleSheet.body.size, contentRight - contentLeft, svgSupported, requestEmbedded, child.id);
            }
            else
            {
                AppendGeneratedText(display, elmd::utf8_to_cps(child.raw), {child.id, 0, elmd::TextAffinity::Downstream}, elmd::InlineStyle::plain());
                display.displayToSource.push_back({child.id, child.content_span.source_range.end, elmd::TextAffinity::Downstream});
            }
            if (display.text.empty())
            {
                AppendGeneratedText(display, U"\u200B", {child.id, 0, elmd::TextAffinity::Downstream}, elmd::InlineStyle::plain());
                display.displayToSource.push_back({child.id, 0, elmd::TextAffinity::Downstream});
            }
            auto format = textLayoutEngine.FormatFor(code, display.ranges);
            auto horizontalPadding = code ? 12.0f : 0.0f;
            auto verticalPadding = code ? 8.0f : 0.0f;
            auto textWidth = (std::max)(1.0f, contentRight - contentLeft - horizontalPadding * 2.0f);
            auto layout = textLayoutEngine.Create(ToWide(display.text), format, textWidth);
            textLayoutEngine.ApplyStyles(layout.Get(), display.ranges);
            ApplyMathInlineObjects(layout.Get(), display.mathOverlays);
            auto images = inlineImageRenderer.Resolve(layout.Get(), display.imageOverlays, textWidth);
            auto fallbackHeight = textLayoutEngine.LineHeightFor(code, display.ranges);
            auto fragmentHeight = textLayoutEngine.MeasureHeight(layout.Get(), fallbackHeight) + verticalPadding * 2.0f;
            QuoteFragment fragment;
            fragment.origin = D2D1::Point2F(contentLeft + horizontalPadding, cursorY + verticalPadding);
            fragment.rect = D2D1::RectF(contentLeft, cursorY, contentRight, cursorY + fragmentHeight);
            fragment.textWidth = textWidth;
            fragment.depth = child.quote_depth;
            fragment.sourceSpan = child.content_span;
            fragment.code = code;
            fragment.images = std::move(images);
            fragment.display = std::move(display);
            fragment.layout = std::move(layout);
            fragments.push_back(std::move(fragment));
            cursorY += fragmentHeight;
        }
        if (fragments.empty()) cursorY += styleSheet.body.lineHeight;
        auto bottom = cursorY + block.block_style.padding_bottom;
        auto borderWidth = block.block_style.border_left ? block.block_style.border_left->width : 3.0f;
        boxes.push_back(QuoteBox{D2D1::RectF(documentLeft, top, documentRight, bottom), 0, borderWidth});
        std::size_t maxDepth = 0;
        for (auto const& fragment : fragments) maxDepth = (std::max)(maxDepth, fragment.depth);
        for (std::size_t level = 1; level <= maxDepth; ++level)
        {
            std::size_t index = 0;
            while (index < fragments.size())
            {
                while (index < fragments.size() && fragments[index].depth < level) ++index;
                if (index >= fragments.size()) break;
                auto first = index;
                while (index < fragments.size() && fragments[index].depth >= level) ++index;
                auto left = documentLeft + static_cast<float>(level) * depthInset;
                boxes.push_back(QuoteBox{D2D1::RectF(left, fragments[first].rect.top - 4.0f, documentRight, fragments[index - 1].rect.bottom + 4.0f), level, borderWidth});
            }
        }
        for (auto const& box : boxes)
        {
            resources.d2dContext->FillRectangle(box.rect, box.depth == 0 ? resources.panelBrush.Get() : resources.nestedQuoteBrush.Get());
            auto lineX = box.rect.left + box.borderWidth * 0.5f;
            resources.d2dContext->DrawLine(D2D1::Point2F(lineX, box.rect.top + 4.0f), D2D1::Point2F(lineX, box.rect.bottom - 4.0f), resources.mutedBrush.Get(), box.borderWidth);
        }
        for (auto& fragment : fragments)
        {
            if (fragment.code) resources.d2dContext->FillRectangle(fragment.rect, resources.canvasBrush.Get());
            if (fragment.layout)
            {
                for (auto const& range : fragment.display.ranges)
                {
                    if (!range.style.code || range.length == 0) continue;
                    UINT32 count = 0;
                    auto result = fragment.layout->HitTestTextRange(range.start, range.length, fragment.origin.x, fragment.origin.y, nullptr, 0, &count);
                    if (result != E_NOT_SUFFICIENT_BUFFER || count == 0) continue;
                    std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
                    if (FAILED(fragment.layout->HitTestTextRange(range.start, range.length, fragment.origin.x, fragment.origin.y, metrics.data(), count, &count))) continue;
                    for (UINT32 index = 0; index < count; ++index)
                    {
                        auto const& metric = metrics[index];
                        resources.d2dContext->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(metric.left - 3.0f, metric.top + 2.0f, metric.left + metric.width + 3.0f, metric.top + metric.height - 1.0f), 4.0f, 4.0f), resources.panelBrush.Get());
                    }
                }
                if (!selection.is_caret() && selection.anchor.container_id == fragment.sourceSpan.container_id
                    && selection.active.container_id == fragment.sourceSpan.container_id)
                {
                    auto selectionStart = (std::min)(selection.anchor.source_offset, selection.active.source_offset);
                    auto selectionEnd = (std::max)(selection.anchor.source_offset, selection.active.source_offset);
                    auto displayStart = DisplayPositionForSource(fragment.display.displayToSource, {fragment.sourceSpan.container_id, selectionStart, elmd::TextAffinity::Downstream});
                    auto displayEnd = DisplayPositionForSource(fragment.display.displayToSource, {fragment.sourceSpan.container_id, selectionEnd, elmd::TextAffinity::Downstream});
                    UINT32 count = 0;
                    auto result = fragment.layout->HitTestTextRange(static_cast<UINT32>(displayStart), static_cast<UINT32>(displayEnd - displayStart), fragment.origin.x, fragment.origin.y, nullptr, 0, &count);
                    if (result == E_NOT_SUFFICIENT_BUFFER && count > 0)
                    {
                        std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
                        if (SUCCEEDED(fragment.layout->HitTestTextRange(static_cast<UINT32>(displayStart), static_cast<UINT32>(displayEnd - displayStart), fragment.origin.x, fragment.origin.y, metrics.data(), count, &count)))
                        {
                            for (UINT32 index = 0; index < count; ++index)
                            {
                                auto const& metric = metrics[index];
                                resources.d2dContext->FillRectangle(D2D1::RectF(metric.left, metric.top, metric.left + metric.width, metric.top + metric.height), resources.selectionBrush.Get());
                            }
                        }
                    }
                }
                resources.d2dContext->DrawTextLayout(fragment.origin, fragment.layout.Get(), fragment.code ? resources.codeBrush.Get() : resources.textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                inlineImageRenderer.Draw(fragment.layout.Get(), fragment.origin, fragment.images);
                for (auto const& overlay : fragment.display.mathOverlays)
                {
                    float pointX = 0.0f;
                    float pointY = 0.0f;
                    DWRITE_HIT_TEST_METRICS metrics{};
                    if (FAILED(fragment.layout->HitTestTextPosition(overlay.displayStart, FALSE, &pointX, &pointY, &metrics))) continue;
                    auto mathX = fragment.origin.x + pointX + overlay.leadingSpace;
                    auto mathY = fragment.origin.y + metrics.top;
                    if (!drawMath(overlay.fragment, D2D1::Point2F(mathX, mathY), styleSheet.textColor)) drawMathFallback(overlay.sourceSpan, D2D1::Point2F(mathX, mathY));
                    interactionMap.mathHits.push_back(EditorVisualMathHit{D2D1::RectF(mathX, mathY, mathX + overlay.fragment.width, mathY + overlay.fragment.height), overlay.sourceSpan, overlay.progressStart, overlay.progressEnd});
                }
            }
            EditorVisualBlock visualBlock;
            visualBlock.rect = fragment.rect;
            visualBlock.textOrigin = fragment.origin;
            visualBlock.textWidth = fragment.textWidth;
            visualBlock.sourceSpan = fragment.sourceSpan;
            visualBlock.documentY = fragment.rect.top + scrollOffset;
            visualBlock.text = std::move(fragment.display.text);
            visualBlock.displayToSource = std::move(fragment.display.displayToSource);
            visualBlock.layout = std::move(fragment.layout);
            interactionMap.blocks.push_back(std::move(visualBlock));
            interactionMap.AddBlockLines(interactionMap.blocks.size() - 1);
        }
        if (fragments.empty())
        {
            EditorVisualBlock placeholder;
            placeholder.rect = boxes.front().rect;
            placeholder.textOrigin = D2D1::Point2F(documentLeft + block.block_style.padding_left, top + block.block_style.padding_top);
            placeholder.textWidth = documentRight - placeholder.textOrigin.x;
            placeholder.sourceSpan = block.source_span;
            placeholder.documentY = top + scrollOffset;
            interactionMap.blocks.push_back(std::move(placeholder));
        }
        return bottom;
    }
}
