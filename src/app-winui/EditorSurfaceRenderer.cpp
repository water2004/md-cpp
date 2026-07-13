#include "pch.h"
#include "EditorSurfaceRenderer.h"

import elmd.core.render_model;
import elmd.core.utf;

#include "EditorContentPreparation.h"
#include "EditorInlineImageRenderer.h"
#include "EditorQuoteBlockRenderer.h"
#include "EditorSvgPainter.h"
#include "EditorTableBlockRenderer.h"
#include "EditorTextLayoutEngine.h"

namespace winrt::ElMd
{
    EditorSurfaceRenderer::~EditorSurfaceRenderer()
    {
        renderCache.Detach();
        mathJax.SetCompletionCallback({});
        svgNormalizer.SetCompletionCallback({});
        mermaid.SetCompletionCallback({});
        std::scoped_lock lock(invalidationState->mutex);
        invalidationState->active = false;
        invalidationState->callback = {};
    }

    void EditorSurfaceRenderer::SetTheme(Theme value)
    {
        if (theme == value) return;
        theme = value;
        styleSheet = CreateEditorStyleSheet(value == Theme::Dark);
        blockLayoutCache.Clear();
        renderCache.ClearTextLayouts();
        renderCache.ClearSvgDocuments();
        resources.RebuildTextFormats(styleSheet);
        resources.ResetBrushes();
    }

    void EditorSurfaceRenderer::SetInvalidateCallback(std::function<void()> callback)
    {
        std::scoped_lock lock(invalidationState->mutex);
        invalidationState->callback = std::move(callback);
    }

    void EditorSurfaceRenderer::Invalidate()
    {
        if (rendering) { deferredInvalidate = true; return; }
        std::function<void()> callback;
        {
            std::scoped_lock lock(invalidationState->mutex);
            if (invalidationState->active) callback = invalidationState->callback;
        }
        if (callback) callback();
    }

    void EditorSurfaceRenderer::Initialize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel)
    {
        if (resources.Ready()) return;
        resources.Initialize(panel, styleSheet);
        auto dispatcher = panel.DispatcherQueue();
        renderCache.Attach(dispatcher, [this] { Invalidate(); });
        auto completion = [this, dispatcher]
        {
            if (mathInvalidationQueued.exchange(true)) return;
            if (!dispatcher.TryEnqueue([this] { mathInvalidationQueued = false; Invalidate(); })) mathInvalidationQueued = false;
        };
        mathJax.SetCompletionCallback(completion);
        svgNormalizer.SetCompletionCallback(completion);
        mermaid.SetCompletionCallback(std::move(completion));
    }

    void EditorSurfaceRenderer::Resize(
        winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel,
        double width,
        double height)
    {
        if (resizing) return;
        resizing = true;
        struct ResetFlag { bool& value; ~ResetFlag() { value = false; } } reset{resizing};
        auto result = resources.Resize(panel, width, height);
        if (!result.resized) return;
        if (result.widthChanged) { blockLayoutCache.Clear(); renderCache.ClearTextLayouts(); }
        scrollOffset = (std::min)(scrollOffset, MaximumScrollOffset());
        scrollTarget = (std::min)(scrollTarget, MaximumScrollOffset());
    }

    void EditorSurfaceRenderer::DrawDocument(detail::EditorRenderFrame const& frame)
    {
        interactionMap.Clear(frame.renderModel.blocks.size());
        auto padding = (std::min)(styleSheet.horizontalPadding, (std::max)(12.0f, resources.surfaceWidthDip * 0.06f));
        auto documentLeft = padding;
        auto documentRight = (std::max)(documentLeft + 1.0f, (std::min)(resources.surfaceWidthDip - padding - 14.0f, documentLeft + styleSheet.documentWidth));
        auto documentWidth = documentRight - documentLeft;
        auto y = styleSheet.verticalPadding - scrollOffset;
        auto caret = frame.selection.active;

        std::unordered_map<std::uint64_t, std::size_t> order;
        for (std::size_t index = 0; index < frame.renderModel.editable_order.size(); ++index) order.emplace(frame.renderModel.editable_order[index].v, index);
        auto positionLess = [&](elmd::TextPosition left, elmd::TextPosition right)
        {
            if (left.container_id == right.container_id) return left.source_offset < right.source_offset;
            auto leftOrder = order.find(left.container_id.v);
            auto rightOrder = order.find(right.container_id.v);
            if (leftOrder == order.end()) return false;
            if (rightOrder == order.end()) return true;
            return leftOrder->second < rightOrder->second;
        };
        auto selectionStart = positionLess(frame.selection.active, frame.selection.anchor) ? frame.selection.active : frame.selection.anchor;
        auto selectionEnd = positionLess(frame.selection.active, frame.selection.anchor) ? frame.selection.anchor : frame.selection.active;
        auto selected = [&](elmd::TextPosition position)
        {
            if (frame.selection.is_caret()) return false;
            return !positionLess(position, selectionStart) && positionLess(position, selectionEnd);
        };

        ::Microsoft::WRL::ComPtr<ID2D1DeviceContext5> svgContext;
        auto svgSupported = SUCCEEDED(resources.d2dContext.As(&svgContext)) && svgContext;
        EditorSvgPainter svgPainter(resources, renderCache);
        EditorTextLayoutEngine textLayoutEngine(resources, styleSheet);
        EditorInlineImageRenderer inlineImages(resources, renderCache, styleSheet, frame.baseDirectory);
        auto drawMath = [&](MathJaxSvgFragment const& fragment, D2D1_POINT_2F origin, D2D1_COLOR_F) {
            return fragment.svg && svgPainter.Draw(fragment.renderId, *fragment.svg, fragment.width, fragment.height, origin);
        };
        auto drawMathFallback = [&](elmd::TextSpan, D2D1_POINT_2F origin) {
            constexpr wchar_t label[] = L"formula";
            resources.d2dContext->DrawTextW(label, 7, resources.codeFormat.Get(), D2D1::RectF(origin.x, origin.y, documentRight, origin.y + styleSheet.code.lineHeight), resources.textBrush.Get());
        };

        std::function<DisplayInlineText(elmd::RenderBlock const&, float)> prepare;
        prepare = [&](elmd::RenderBlock const& block, float width) -> DisplayInlineText
        {
            if (block.kind == elmd::RenderBlockKind::Blank)
            {
                DisplayInlineText display;
                AppendGeneratedText(display, U"\u200B", {block.id, 0, elmd::TextAffinity::Downstream}, elmd::InlineStyle::plain());
                display.displayToSource.push_back({block.id, 0, elmd::TextAffinity::Downstream});
                return display;
            }
            if (block.kind == elmd::RenderBlockKind::Code)
                return block.code_indented ? BuildIndentedCodeBlockText(block) : BuildCodeBlockText(block, caret);
            if (!block.inline_items.empty())
            {
                auto sourceEnd = InlineItemsEndPosition(block.inline_items, {block.id, block.content_span.source_range.end, elmd::TextAffinity::Downstream});
                return BuildDisplayInlineText(block.inline_items, caret, sourceEnd, mathJax, svgNormalizer, styleSheet.textColor, styleSheet.body.size, width, svgSupported, true, block.id);
            }

            DisplayInlineText display;
            if (block.kind == elmd::RenderBlockKind::Math)
            {
                AppendSourceText(display, block.tex, {block.id, {0, block.tex.size()}}, elmd::InlineStyle::plain(), false);
            }
            else if (block.kind == elmd::RenderBlockKind::Image)
            {
                auto position = elmd::TextPosition{block.id, 0, elmd::TextAffinity::Downstream};
                auto start = static_cast<std::uint32_t>(display.displayToSource.size());
                AppendGeneratedText(display, U"\uFFFC", position, elmd::InlineStyle::plain());
                display.imageOverlays.push_back({start, block.source_span, block.src, block.alt, block.image_width, block.image_height});
            }
            else if (block.kind == elmd::RenderBlockKind::Toc)
            {
                for (auto const* item : frame.renderModel.outline.flat_items())
                {
                    AppendGeneratedText(display, elmd::utf8_to_cps(item->title_plain_text) + U"\n", {block.id, 0, elmd::TextAffinity::Downstream}, elmd::InlineStyle{.link = true});
                }
            }
            else if (block.kind == elmd::RenderBlockKind::Callout || block.kind == elmd::RenderBlockKind::Footnote)
            {
                if (block.callout_title)
                    MergeDisplayText(display, BuildDisplayInlineText(*block.callout_title, caret, {block.id, 0, elmd::TextAffinity::Downstream}, mathJax, svgNormalizer, styleSheet.textColor, styleSheet.body.size, width, svgSupported, true, block.id));
                for (auto const& child : block.child_blocks)
                {
                    if (!display.text.empty()) AppendGeneratedText(display, U"\n", {child.id, 0, elmd::TextAffinity::Downstream}, elmd::InlineStyle::plain());
                    MergeDisplayText(display, prepare(child, width));
                }
            }
            else
            {
                auto raw = elmd::utf8_to_cps(block.raw.empty() ? block.reason_text : block.raw);
                AppendSourceText(display, raw, {block.id, {0, raw.size()}}, elmd::InlineStyle::plain(), false);
            }
            if (display.text.empty()) AppendGeneratedText(display, U"\u200B", {block.id, 0, elmd::TextAffinity::Downstream}, elmd::InlineStyle::plain());
            display.displayToSource.push_back({block.id, block.source_span.source_range.end, elmd::TextAffinity::Downstream});
            return display;
        };

        auto drawSelection = [&](IDWriteTextLayout* layout, D2D1_POINT_2F origin, std::vector<elmd::TextPosition> const& mapping)
        {
            if (!layout || frame.selection.is_caret() || mapping.empty()) return;
            std::size_t index = 0;
            while (index + 1 < mapping.size())
            {
                while (index + 1 < mapping.size() && !selected(mapping[index])) ++index;
                auto start = index;
                while (index + 1 < mapping.size() && selected(mapping[index])) ++index;
                if (index <= start) continue;
                UINT32 count = 0;
                auto length = static_cast<UINT32>(index - start);
                if (layout->HitTestTextRange(static_cast<UINT32>(start), length, origin.x, origin.y, nullptr, 0, &count) != E_NOT_SUFFICIENT_BUFFER || count == 0) continue;
                std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
                if (FAILED(layout->HitTestTextRange(static_cast<UINT32>(start), length, origin.x, origin.y, metrics.data(), count, &count))) continue;
                for (UINT32 metricIndex = 0; metricIndex < count; ++metricIndex)
                {
                    auto const& metric = metrics[metricIndex];
                    resources.d2dContext->FillRectangle(D2D1::RectF(metric.left, metric.top, metric.left + metric.width, metric.top + metric.height), resources.selectionBrush.Get());
                }
            }
        };

        if (frame.renderModel.blocks.empty())
        {
            constexpr wchar_t message[] = L"Open a Markdown file or start editing.";
            resources.d2dContext->DrawTextW(message, 38, resources.textFormat.Get(), D2D1::RectF(documentLeft, y, documentRight, y + 80.0f), resources.mutedBrush.Get());
        }

        for (auto const& block : frame.renderModel.blocks)
        {
            auto top = y;
            if (block.kind == elmd::RenderBlockKind::Table)
            {
                auto bottom = EditorTableBlockRenderer::Render(block, caret, frame.selection, documentLeft, documentRight, y, scrollOffset, svgSupported, true, resources, styleSheet, interactionMap, textLayoutEngine, inlineImages, mathJax, svgNormalizer, drawMath, drawMathFallback);
                if (bottom) { y = *bottom + styleSheet.blockGap; continue; }
            }
            if (block.kind == elmd::RenderBlockKind::Quote)
            {
                y = EditorQuoteBlockRenderer::Render(block, caret, frame.selection, documentLeft, documentRight, y, scrollOffset, svgSupported, true, resources, styleSheet, interactionMap, textLayoutEngine, inlineImages, mathJax, svgNormalizer, drawMath, drawMathFallback) + styleSheet.blockGap;
                continue;
            }
            if (block.kind == elmd::RenderBlockKind::ThematicBreak)
            {
                auto height = 40.0f;
                auto center = y + height * 0.5f;
                resources.d2dContext->DrawLine(D2D1::Point2F(documentLeft, center), D2D1::Point2F(documentRight, center), resources.mutedBrush.Get(), 1.0f);
                EditorVisualBlock visual;
                visual.rect = D2D1::RectF(documentLeft, y, documentRight, y + height);
                visual.textOrigin = D2D1::Point2F(documentLeft, y);
                visual.textWidth = documentWidth;
                visual.sourceSpan = block.source_span;
                visual.documentY = y + scrollOffset;
                visual.thematicBreak = true;
                interactionMap.blocks.push_back(visual);
                EditorVisualLine line;
                line.blockIndex = interactionMap.blocks.size() - 1;
                line.sourceSpan = block.source_span;
                line.rect = visual.rect;
                interactionMap.lines.push_back(line);
                y += height + styleSheet.blockGap;
                continue;
            }

            auto display = prepare(block, documentWidth);
            auto code = block.kind == elmd::RenderBlockKind::Code || block.kind == elmd::RenderBlockKind::Frontmatter || block.kind == elmd::RenderBlockKind::Unsupported;
            auto format = code ? resources.codeFormat.Get() : resources.textFormat.Get();
            auto layout = textLayoutEngine.Create(ToWide(display.text), format, documentWidth);
            textLayoutEngine.ApplyStyles(layout.Get(), display.ranges);
            ApplyMathInlineObjects(layout.Get(), display.mathOverlays);
            auto images = inlineImages.Resolve(layout.Get(), display.imageOverlays, documentWidth);
            auto height = textLayoutEngine.MeasureHeight(layout.Get(), code ? styleSheet.code.lineHeight : styleSheet.body.lineHeight);
            height += block.block_style.padding_top + block.block_style.padding_bottom;
            auto origin = D2D1::Point2F(documentLeft + block.block_style.padding_left, y + block.block_style.padding_top);
            auto rect = D2D1::RectF(documentLeft, y, documentRight, y + height);
            if (code || block.block_style.background) resources.d2dContext->FillRectangle(rect, resources.panelBrush.Get());
            drawSelection(layout.Get(), origin, display.displayToSource);
            if (layout) resources.d2dContext->DrawTextLayout(origin, layout.Get(), code ? resources.codeBrush.Get() : resources.textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            inlineImages.Draw(layout.Get(), origin, images);
            for (auto const& overlay : display.mathOverlays)
            {
                FLOAT x = 0.0f, lineY = 0.0f;
                DWRITE_HIT_TEST_METRICS metrics{};
                if (!layout || FAILED(layout->HitTestTextPosition(overlay.displayStart, FALSE, &x, &lineY, &metrics))) continue;
                auto mathOrigin = D2D1::Point2F(origin.x + x + overlay.leadingSpace, origin.y + metrics.top);
                if (!drawMath(overlay.fragment, mathOrigin, styleSheet.textColor)) drawMathFallback(overlay.sourceSpan, mathOrigin);
                interactionMap.mathHits.push_back({D2D1::RectF(mathOrigin.x, mathOrigin.y, mathOrigin.x + overlay.fragment.width, mathOrigin.y + overlay.fragment.height), overlay.sourceSpan, overlay.progressStart, overlay.progressEnd});
            }
            EditorVisualBlock visual;
            visual.rect = rect;
            visual.textOrigin = origin;
            visual.textWidth = documentWidth;
            visual.sourceSpan = block.source_span;
            visual.documentY = top + scrollOffset;
            visual.text = std::move(display.text);
            visual.displayToSource = std::move(display.displayToSource);
            visual.layout = std::move(layout);
            interactionMap.blocks.push_back(std::move(visual));
            interactionMap.AddBlockLines(interactionMap.blocks.size() - 1);
            y += height + block.block_style.margin_bottom + styleSheet.blockGap;
        }

        EditorTableInteraction::Paint(resources, interactionMap, pointerPosition, draggedTableAction, tableDropIndex);
        totalDocumentHeight = y + scrollOffset + styleSheet.verticalPadding;
        scrollOffset = (std::min)(scrollOffset, MaximumScrollOffset());
        scrollTarget = (std::min)(scrollTarget, MaximumScrollOffset());
        if (frame.selection.is_caret())
        {
            auto upstream = caret.affinity == elmd::TextAffinity::Upstream;
            if (auto rect = CaretBounds(caret, upstream))
                resources.d2dContext->DrawLine(D2D1::Point2F(rect->left, rect->top), D2D1::Point2F(rect->left, rect->bottom), resources.caretBrush.Get(), 1.5f);
        }
    }

    void EditorSurfaceRenderer::ScrollBy(float delta) { SetScrollOffset(scrollOffset + delta); }
    void EditorSurfaceRenderer::QueueScrollBy(float delta) { scrollTarget = (std::clamp)(scrollTarget + delta, 0.0f, MaximumScrollOffset()); }

    bool EditorSurfaceRenderer::AdvanceScrollAnimation(float elapsedSeconds)
    {
        auto distance = scrollTarget - scrollOffset;
        if (std::fabs(distance) < 0.25f) { scrollOffset = scrollTarget; return false; }
        scrollOffset += distance * (1.0f - std::exp(-18.0f * (std::max)(0.0f, elapsedSeconds)));
        return true;
    }

    void EditorSurfaceRenderer::SetScrollOffset(float value) { scrollOffset = (std::clamp)(value, 0.0f, MaximumScrollOffset()); scrollTarget = scrollOffset; }
    float EditorSurfaceRenderer::ScrollOffset() const { return scrollOffset; }
    float EditorSurfaceRenderer::MaximumScrollOffset() const { return (std::max)(0.0f, totalDocumentHeight - resources.surfaceHeightDip); }
    float EditorSurfaceRenderer::ViewportHeight() const { return resources.surfaceHeightDip; }
    void EditorSurfaceRenderer::UpdatePointer(float x, float y) { pointerPosition = D2D1::Point2F(x, y); }
    void EditorSurfaceRenderer::ClearPointer() { pointerPosition.reset(); }
    void EditorSurfaceRenderer::SetTableDrag(std::optional<TableAction> action, std::optional<std::size_t> dropIndex) { draggedTableAction = std::move(action); tableDropIndex = dropIndex; }
    std::optional<EditorSurfaceRenderer::TableAction> EditorSurfaceRenderer::TableActionAt(float x, float y) const { return EditorTableInteraction::ActionAt(interactionMap, x, y); }
    std::optional<std::size_t> EditorSurfaceRenderer::TableDropIndexAt(float x, float y, bool rows) const { return EditorTableInteraction::DropIndexAt(interactionMap, draggedTableAction, x, y, rows); }

    bool EditorSurfaceRenderer::ScrollToPosition(elmd::TextPosition position)
    {
        auto previous = scrollOffset;
        if (auto bounds = CaretBounds(position))
        {
            auto margin = styleSheet.verticalPadding;
            if (bounds->top < margin) scrollOffset = (std::max)(0.0f, scrollOffset - (margin - bounds->top));
            else if (bounds->bottom > resources.surfaceHeightDip - margin) scrollOffset = (std::min)(MaximumScrollOffset(), scrollOffset + bounds->bottom - (resources.surfaceHeightDip - margin));
            scrollTarget = scrollOffset;
            return scrollOffset != previous;
        }
        for (auto const& block : interactionMap.blocks)
        {
            auto mapped = std::any_of(block.displayToSource.begin(), block.displayToSource.end(), [&](auto value) { return value.container_id == position.container_id; });
            if (block.sourceSpan.container_id != position.container_id && !mapped) continue;
            scrollOffset = (std::clamp)(block.documentY - styleSheet.verticalPadding, 0.0f, MaximumScrollOffset());
            scrollTarget = scrollOffset;
            return scrollOffset != previous;
        }
        return false;
    }

    std::optional<elmd::TextPosition> EditorSurfaceRenderer::HitTest(float x, float y, bool* upstream) const { return interactionMap.HitTest(x, y, upstream); }
    std::optional<D2D1_RECT_F> EditorSurfaceRenderer::CaretBounds(elmd::TextPosition position, bool upstream) const { return interactionMap.CaretBounds(position, upstream, styleSheet.body.lineHeight); }
    std::optional<EditorSurfaceRenderer::CaretMove> EditorSurfaceRenderer::MoveCaretVertically(elmd::TextPosition position, bool upstream, bool down, float& goalX) const { return interactionMap.MoveCaretVertically(position, upstream, down, goalX, styleSheet.body.lineHeight); }
    std::optional<elmd::TextPosition> EditorSurfaceRenderer::VisualLineStart(elmd::TextPosition position, bool upstream) const { return interactionMap.VisualLineStart(position, upstream); }
    std::optional<elmd::TextPosition> EditorSurfaceRenderer::VisualLineEnd(elmd::TextPosition position, bool upstream) const { return interactionMap.VisualLineEnd(position, upstream); }

    void EditorSurfaceRenderer::Render(detail::EditorRenderFrame const& frame)
    {
        if (!resources.Ready() || resizing || rendering) return;
        rendering = true;
        struct Reset { EditorSurfaceRenderer& owner; ~Reset() { owner.rendering = false; if (owner.deferredInvalidate.exchange(false)) owner.Invalidate(); } } reset{*this};
        resources.EnsureFrameResources(styleSheet);
        resources.d2dContext->BeginDraw();
        resources.d2dContext->Clear(styleSheet.canvasColor);
        DrawDocument(frame);
        auto ended = resources.d2dContext->EndDraw();
        if (ended == D2DERR_RECREATE_TARGET) { resources.ResetTargets(); return; }
        if (FAILED(ended)) return;
        auto presented = resources.swapChain->Present(1, 0);
        if (presented == DXGI_ERROR_DEVICE_REMOVED || presented == DXGI_ERROR_DEVICE_RESET) resources.ResetTargets();
    }
}
