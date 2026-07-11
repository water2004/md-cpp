#include "pch.h"
#include "EditorSurfaceRenderer.h"

import elmd.core.render_model;
import elmd.core.layout_plan;
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

    std::size_t SourceStart(elmd::RenderBlock const& block)
    {
        return block.content_range.start.v;
    }

    std::size_t SourceEnd(elmd::RenderBlock const& block, std::u32string const& text)
    {
        (void)text;
        return block.content_range.end.v;
    }


    void EditorSurfaceRenderer::SetTheme(Theme value)
    {
        if (theme == value)
        {
            return;
        }

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
        if (rendering)
        {
            deferredInvalidate = true;
            return;
        }
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
            if (!dispatcher.TryEnqueue([this]
            {
                mathInvalidationQueued = false;
                Invalidate();
            }))
            {
                mathInvalidationQueued = false;
            }
        };
        mathJax.SetCompletionCallback(completion);
        svgNormalizer.SetCompletionCallback(completion);
        mermaid.SetCompletionCallback(std::move(completion));
    }

    void EditorSurfaceRenderer::Resize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel, double width, double height)
    {
        if (resizing) return;
        resizing = true;
        struct ResetFlag { bool& value; ~ResetFlag() { value = false; } } resetFlag{ resizing };
        auto result = resources.Resize(panel, width, height);
        if (!result.resized) return;
        if (result.widthChanged)
        {
            blockLayoutCache.Clear();
            renderCache.ClearTextLayouts();
        }
        auto maxScroll = MaximumScrollOffset();
        scrollOffset = (std::min)(scrollOffset, maxScroll);
        scrollTarget = (std::min)(scrollTarget, maxScroll);
    }
    void EditorSurfaceRenderer::DrawDocument(detail::EditorRenderFrame const& frame)
    {
        auto remoteGeneration = renderCache.RemoteImageGeneration();
        if (remoteGeneration != observedRemoteImageGeneration)
        {
            observedRemoteImageGeneration = remoteGeneration;
            blockLayoutCache.Clear();
        }
        interactionMap.Clear(frame.renderModel.blocks.size());
        blockLayoutCache.Trim(32768);
        auto responsivePadding = (std::min)(styleSheet.horizontalPadding, (std::max)(12.0f, resources.surfaceWidthDip * 0.06f));
        auto documentLeft = responsivePadding;
        auto documentTop = styleSheet.verticalPadding;
        auto documentRight = (std::max)(documentLeft + 1.0f, (std::min)(resources.surfaceWidthDip - responsivePadding - 14.0f, documentLeft + styleSheet.documentWidth));
        auto y = documentTop - scrollOffset;
        auto selectionState = frame.selection;
        auto selection = selectionState.normalized_range();
        auto caret = selectionState.active.v;
        auto sourceText = frame.sourceText;
        std::unordered_set<std::uint64_t> mathFallbacks;
        ::Microsoft::WRL::ComPtr<ID2D1DeviceContext5> svgContext;
        auto svgSupported = SUCCEEDED(resources.d2dContext.As(&svgContext)) && svgContext;
        EditorSvgPainter svgPainter(resources, renderCache);
        EditorTextLayoutEngine textLayoutEngine(resources, styleSheet);
        EditorInlineImageRenderer inlineImageRenderer(resources, renderCache, styleSheet, frame.baseDirectory);

        auto drawMathSvg = [&](MathJaxSvgFragment const& fragment, D2D1_POINT_2F origin, D2D1_COLOR_F color) -> bool
        {
            (void)color;
            return fragment.svg && svgPainter.Draw(fragment.renderId, *fragment.svg, fragment.width, fragment.height, origin);
        };

        auto drawMathFallback = [&](std::size_t start, std::size_t end, D2D1_POINT_2F origin)
        {
            start = (std::min)(start, sourceText.size());
            end = (std::min)((std::max)(end, start), sourceText.size());
            auto key = static_cast<std::uint64_t>(start) * 1099511628211ull ^ static_cast<std::uint64_t>(end);
            if (!mathFallbacks.insert(key).second) return;
            auto fallback = ToWide(std::u32string_view(sourceText).substr(start, end - start));
            if (fallback.empty()) fallback = L"formula";
            resources.d2dContext->DrawTextW(fallback.c_str(), static_cast<UINT32>(fallback.size()), resources.codeFormat.Get(), D2D1::RectF(origin.x, origin.y, documentRight, origin.y + styleSheet.code.lineHeight * 3.0f), resources.textBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        };

        auto blockContentWidth = documentRight - documentLeft;

        std::vector<elmd::BlockLayoutInput> layoutInputs;
        layoutInputs.reserve(frame.renderModel.blocks.size());
        for (auto const& block : frame.renderModel.blocks)
        {
            layoutInputs.push_back(elmd::BlockLayoutInput{
                elmd::BlockId{ block.id.v },
                blockLayoutCache.Estimate(block, blockContentWidth, static_cast<std::uint64_t>(theme), styleSheet),
                block.kind == elmd::RenderBlockKind::Blank,
                block.source_range.start.v <= caret && caret <= block.source_range.end.v,
            });
        }
        auto layoutPlan = elmd::plan_document_layout(layoutInputs, elmd::LayoutPlanSettings{
            documentTop,
            styleSheet.verticalPadding,
            scrollOffset,
            resources.surfaceHeightDip,
            styleSheet.blockGap,
            styleSheet.blockGap * 0.5f,
            1.0f,
            2.0f,
        });
        bool layoutPlanChanged = false;
        auto cacheMeasuredHeight = [&](std::uint64_t key, float measured)
        {
            if (blockLayoutCache.Record(key, measured)) layoutPlanChanged = true;
        };

        if (frame.renderModel.blocks.empty() && sourceText.empty())
        {
            auto emptyText = winrt::hstring(L"Open a Markdown file or start editing to see the WYSIWYG surface.");
            auto rect = D2D1::RectF(documentLeft, y, documentRight, y + 80.0f);
            resources.d2dContext->DrawTextW(emptyText.c_str(), static_cast<UINT32>(emptyText.size()), resources.textFormat.Get(), rect, resources.mutedBrush.Get());
            return;
        }

        for (std::size_t blockIndex = 0; blockIndex < frame.renderModel.blocks.size(); ++blockIndex)
        {
            auto const& block = frame.renderModel.blocks[blockIndex];
            auto const& placement = layoutPlan.blocks[blockIndex];
            y = placement.top - scrollOffset;
            auto cacheKey = blockLayoutCache.Key(block, blockContentWidth, static_cast<std::uint64_t>(theme));
            auto blockStartY = y;
            auto estimatedHeight = placement.height;
            auto requestEmbedded = placement.request_embedded;
            if (!placement.measure)
            {
                VisualBlock placeholder;
                placeholder.rect = D2D1::RectF(documentLeft, y, documentRight, y + estimatedHeight);
                placeholder.textOrigin = D2D1::Point2F(documentLeft, y);
                placeholder.textWidth = documentRight - documentLeft;
                placeholder.sourceStart = block.source_range.start.v;
                placeholder.sourceEnd = block.source_range.end.v;
                placeholder.documentY = y + scrollOffset;
                interactionMap.blocks.push_back(std::move(placeholder));
                continue;
            }
            if (block.kind == elmd::RenderBlockKind::Table)
            {
                auto tableBottom = EditorTableBlockRenderer::Render(
                    block,
                    sourceText,
                    caret,
                    selection.start.v,
                    selection.end.v,
                    selection.is_empty(),
                    documentLeft,
                    documentRight,
                    y,
                    scrollOffset,
                    svgSupported,
                    requestEmbedded,
                    resources,
                    styleSheet,
                    interactionMap,
                    textLayoutEngine,
                    inlineImageRenderer,
                    mathJax,
                    svgNormalizer,
                    drawMathSvg,
                    drawMathFallback);
                if (tableBottom)
                {
                    y = *tableBottom;
                    cacheMeasuredHeight(cacheKey, y - blockStartY);
                    continue;
                }
            }            if (block.kind == elmd::RenderBlockKind::Quote)
            {
                y = EditorQuoteBlockRenderer::Render(
                    block,
                    sourceText,
                    caret,
                    selection.start.v,
                    selection.end.v,
                    selection.is_empty(),
                    documentLeft,
                    documentRight,
                    y,
                    scrollOffset,
                    svgSupported,
                    requestEmbedded,
                    resources,
                    styleSheet,
                    interactionMap,
                    textLayoutEngine,
                    inlineImageRenderer,
                    mathJax,
                    svgNormalizer,
                    drawMathSvg,
                    drawMathFallback);
                cacheMeasuredHeight(cacheKey, y - blockStartY);
                continue;
            }            IDWriteTextFormat* format = resources.textFormat.Get();
            ID2D1Brush* brush = resources.textBrush.Get();
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
            std::vector<DisplayInlineText::ImageOverlay> inlineImageOverlays;
            std::optional<MathJaxSvg> blockMath;
            bool showRawMath = false;
            std::optional<MermaidSvg> blockMermaid;
            bool showRawMermaid = false;
            std::optional<EditorRenderCache::RasterImage> blockImage;
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
                        requestEmbedded,
                        block.source_range);
                    text = std::move(display.text);
                    inlineRanges = std::move(display.ranges);
                    displayToSource = std::move(display.displayToSource);
                    inlineMathOverlays = std::move(display.mathOverlays);
                    inlineMathPreviews = std::move(display.mathPreviews);
                    inlineImageOverlays = std::move(display.imageOverlays);
                    if (block.block_style.margin_top >= 24.0f)
                    {
                        format = resources.heading1Format.Get();
                        height = 58.0f;
                    }
                    else if (block.block_style.margin_top >= 20.0f)
                    {
                        format = resources.heading2Format.Get();
                        height = 50.0f;
                    }
                    else if (block.block_style.margin_top >= 16.0f)
                    {
                        format = resources.heading3Format.Get();
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
                    format = resources.codeFormat.Get();
                    brush = resources.codeBrush.Get();
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
                    format = resources.codeFormat.Get();
                    brush = resources.codeBrush.Get();
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
                            MergeDisplayText(display, BuildDisplayInlineText(child.inline_items, caret, child.content_range.end.v, sourceText, mathJax, svgNormalizer, styleSheet.textColor, styleSheet.body.size, documentRight - documentLeft, svgSupported, requestEmbedded, child.source_range));
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
                    inlineImageOverlays = std::move(display.imageOverlays);
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
                        for (auto const* item : frame.renderModel.outline.flat_items())
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
                    format = resources.codeFormat.Get();
                    brush = resources.codeBrush.Get();
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
                    blockImage = renderCache.LoadRasterImage(resources, frame.baseDirectory, block.src);
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
                    brush = resources.mutedBrush.Get();
                    inset = 8.0f;
                    textTop = showRawImage ? 8.0f : 0.0f;
                    break;
                }
                case elmd::RenderBlockKind::Unsupported:
                    text = elmd::utf8_to_cps(block.raw);
                    brush = resources.mutedBrush.Get();
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
                        requestEmbedded,
                        block.source_range);
                    text = std::move(display.text);
                    inlineRanges = std::move(display.ranges);
                    displayToSource = std::move(display.displayToSource);
                    inlineMathOverlays = std::move(display.mathOverlays);
                    inlineMathPreviews = std::move(display.mathPreviews);
                    inlineImageOverlays = std::move(display.imageOverlays);
                    brush = resources.mutedBrush.Get();
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

            auto textWidth = (std::max)(1.0f, documentRight - documentLeft - inset * 2.0f);
            auto cacheableLayout = inlineMathOverlays.empty() && inlineImageOverlays.empty()
                && !blockMath && !blockMermaid && !blockImage;
            auto layoutKey = cacheKey;
            auto mixLayoutKey = [&](std::uint64_t value)
            {
                layoutKey ^= value + 0x9e3779b97f4a7c15ull + (layoutKey << 6) + (layoutKey >> 2);
            };
            mixLayoutKey(static_cast<std::uint64_t>(std::hash<std::u32string>{}(text)));
            mixLayoutKey(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(format)));
            mixLayoutKey(static_cast<std::uint64_t>(std::llround(textWidth * 16.0f)));
            for (auto const& range : inlineRanges)
            {
                std::uint64_t style = range.start;
                style = style * 1315423911u + range.length;
                style = style * 33u + static_cast<std::uint64_t>(range.style.bold);
                style = style * 33u + static_cast<std::uint64_t>(range.style.italic);
                style = style * 33u + static_cast<std::uint64_t>(range.style.underline);
                style = style * 33u + static_cast<std::uint64_t>(range.style.strikethrough);
                style = style * 33u + static_cast<std::uint64_t>(range.style.code);
                style = style * 33u + static_cast<std::uint64_t>(range.style.link);
                style = style * 33u + static_cast<std::uint64_t>(range.marker);
                style = style * 33u + static_cast<std::uint64_t>(range.syntax);
                mixLayoutKey(style);
            }
            ::Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            if (cacheableLayout)
            {
                layout = renderCache.FindTextLayout(layoutKey);
            }
            if (!layout)
            {
                layout = textLayoutEngine.Create(ToWide(text), format, textWidth);
                textLayoutEngine.ApplyStyles(layout.Get(), inlineRanges);
                ApplyMathInlineObjects(layout.Get(), inlineMathOverlays);
                if (cacheableLayout && layout)
                {
                    auto bytes = (std::max)(std::size_t{4096}, text.size() * 12);
                    renderCache.StoreTextLayout(layoutKey, layout, bytes);
                }
            }
            auto inlineImageDraws = inlineImageRenderer.Resolve(layout.Get(), inlineImageOverlays, textWidth);
            std::optional<D2D1_POINT_2F> blockMathOrigin;
            std::vector<std::vector<D2D1_POINT_2F>> inlineMathPreviewOrigins;
            std::optional<D2D1_POINT_2F> blockMermaidOrigin;
            std::optional<D2D1_RECT_F> blockImageRect;
            float blockMermaidWidth = 0.0f;
            float blockMermaidHeight = 0.0f;
            if (measureHeight)
            {
                auto fallbackHeight = format == resources.codeFormat.Get() ? styleSheet.code.lineHeight : styleSheet.body.lineHeight;
                auto bottomPadding = fillPanel ? 16.0f : 8.0f;
                height = textTop + textLayoutEngine.MeasureHeight(layout.Get(), fallbackHeight) + bottomPadding;
            }
            if (!inlineMathPreviews.empty())
            {
                auto availableWidth = (std::max)(1.0f, documentRight - documentLeft - inset * 2.0f);
                auto previewY = y + textTop + textLayoutEngine.MeasureHeight(layout.Get(), styleSheet.body.lineHeight) + 8.0f;
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
                    auto rawHeight = textLayoutEngine.MeasureHeight(layout.Get(), styleSheet.code.lineHeight);
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
                    auto rawHeight = textLayoutEngine.MeasureHeight(layout.Get(), styleSheet.code.lineHeight);
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
                auto imageWidth = block.image_width.value_or(blockImage->width);
                auto imageHeight = block.image_height.value_or(blockImage->height);
                if (block.image_width && !block.image_height) imageHeight = blockImage->height * imageWidth / blockImage->width;
                if (!block.image_width && block.image_height) imageWidth = blockImage->width * imageHeight / blockImage->height;
                auto scale = (std::min)(1.0f, (std::min)(availableWidth / imageWidth, 600.0f / imageHeight));
                imageWidth *= scale;
                imageHeight *= scale;
                auto imageX = documentLeft + (documentRight - documentLeft - imageWidth) * 0.5f;
                auto imageY = y + 8.0f;
                if (showRawImage)
                {
                    auto rawHeight = textLayoutEngine.MeasureHeight(layout.Get(), styleSheet.body.lineHeight);
                    imageY = y + textTop + rawHeight + 8.0f;
                }
                blockImageRect = D2D1::RectF(imageX, imageY, imageX + imageWidth, imageY + imageHeight);
                height = imageY - y + imageHeight + 8.0f;
            }
            auto sourceStart = displayToSource.empty() ? SourceStart(block) : displayToSource.front();
            auto sourceEnd = displayToSource.empty() ? SourceEnd(block, text) : displayToSource.back();
            if (fillPanel)
            {
                resources.d2dContext->FillRectangle(D2D1::RectF(documentLeft, y, documentRight, y + height), resources.panelBrush.Get());
            }
            if (block.kind == elmd::RenderBlockKind::Callout)
            {
                resources.d2dContext->DrawLine(D2D1::Point2F(documentLeft + 2.0f, y + 4.0f), D2D1::Point2F(documentLeft + 2.0f, y + height - 4.0f), resources.accentBrush.Get(), 4.0f);
            }
            if (thematicBreak)
            {
                auto ruleY = y + height * 0.5f;
                resources.d2dContext->DrawLine(D2D1::Point2F(documentLeft, ruleY), D2D1::Point2F(documentRight, ruleY), resources.mutedBrush.Get(), 1.0f);
            }
            auto origin = D2D1::Point2F(documentLeft + inset, y + textTop);
            if (layout)
            {
                std::vector<std::pair<UINT32, UINT32>> nestedCodeDisplayRanges;
                for (auto const& child : block.child_blocks)
                {
                    if (child.kind != elmd::RenderBlockKind::Code && child.kind != elmd::RenderBlockKind::Quote) continue;
                    auto displayStart = DisplayPositionForSource(displayToSource, child.content_range.start.v);
                    auto displayEnd = DisplayPositionForSource(displayToSource, child.content_range.end.v);
                    if (displayStart >= displayEnd || displayStart > (std::numeric_limits<UINT32>::max)() || displayEnd > (std::numeric_limits<UINT32>::max)()) continue;
                    auto start = static_cast<UINT32>(displayStart);
                    auto length = static_cast<UINT32>(displayEnd - displayStart);
                    UINT32 actualCount = 0;
                    auto hr = layout->HitTestTextRange(start, length, origin.x, origin.y, nullptr, 0, &actualCount);
                    if (hr != E_NOT_SUFFICIENT_BUFFER || actualCount == 0) continue;
                    std::vector<DWRITE_HIT_TEST_METRICS> metrics(actualCount);
                    if (FAILED(layout->HitTestTextRange(start, length, origin.x, origin.y, metrics.data(), actualCount, &actualCount))) continue;
                    auto top = (std::numeric_limits<float>::max)();
                    auto bottom = (std::numeric_limits<float>::lowest)();
                    for (UINT32 metricIndex = 0; metricIndex < actualCount; ++metricIndex)
                    {
                        auto const& metric = metrics[metricIndex];
                        top = (std::min)(top, metric.top);
                        bottom = (std::max)(bottom, metric.top + metric.height);
                    }
                    if (top <= bottom)
                    {
                        auto indent = static_cast<float>(child.container_indent_columns) * styleSheet.body.size * 0.55f;
                        auto left = (std::min)(documentRight - 1.0f, documentLeft + inset + indent);
                        if (child.kind == elmd::RenderBlockKind::Code)
                        {
                            auto rect = D2D1::RoundedRect(D2D1::RectF(left, top - 6.0f, documentRight, bottom + 6.0f), 5.0f, 5.0f);
                            resources.d2dContext->FillRoundedRectangle(rect, resources.panelBrush.Get());
                            nestedCodeDisplayRanges.push_back({start, start + length});
                        }
                        else
                        {
                            auto rect = D2D1::RectF(left, top - 4.0f, documentRight, bottom + 4.0f);
                            resources.d2dContext->FillRectangle(rect, resources.nestedQuoteBrush.Get());
                            resources.d2dContext->DrawLine(D2D1::Point2F(left + 1.5f, rect.top), D2D1::Point2F(left + 1.5f, rect.bottom), resources.mutedBrush.Get(), 3.0f);
                        }
                    }
                }
                for (auto const& range : inlineRanges)
                {
                    if (!range.style.code || range.length == 0)
                    {
                        continue;
                    }
                    auto rangeEnd = range.start + range.length;
                    auto nestedCode = std::any_of(nestedCodeDisplayRanges.begin(), nestedCodeDisplayRanges.end(), [&](auto const& nested) {
                        return nested.first <= range.start && rangeEnd <= nested.second;
                    });
                    if (nestedCode) continue;

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
                                resources.d2dContext->FillRoundedRectangle(rect, resources.panelBrush.Get());
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
                                resources.d2dContext->FillRectangle(D2D1::RectF(metric.left, metric.top, metric.left + metric.width, metric.top + metric.height), resources.selectionBrush.Get());
                            }
                        }
                    }
                }

                resources.d2dContext->DrawTextLayout(origin, layout.Get(), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                inlineImageRenderer.Draw(layout.Get(), origin, inlineImageDraws);

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
                        interactionMap.mathHits.push_back(VisualMathHit{
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
                            resources.d2dContext->DrawLine(D2D1::Point2F(origin.x + pointX, strikeY), D2D1::Point2F(mathX + overlay.fragment.width, strikeY), resources.textBrush.Get(), 1.5f);
                        }
                        interactionMap.mathHits.push_back(VisualMathHit{
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
                            resources.d2dContext->DrawLine(D2D1::Point2F(origins[fragmentIndex].x, strikeY), D2D1::Point2F(origins[fragmentIndex].x + fragment.width, strikeY), resources.textBrush.Get(), 1.5f);
                        }
                        interactionMap.mathHits.push_back(VisualMathHit{
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
                    svgPainter.Draw(blockMermaid->renderId, blockMermaid->svg, blockMermaidWidth, blockMermaidHeight, *blockMermaidOrigin);
                }
                if (blockImageRect && blockImage && blockImage->bitmap)
                {
                    resources.d2dContext->DrawBitmap(blockImage->bitmap.Get(), *blockImageRect, 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
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
                interactionMap.blocks.push_back(std::move(visualBlock));
                if (thematicBreak)
                {
                    VisualLine visualLine;
                    visualLine.blockIndex = interactionMap.blocks.size() - 1;
                    visualLine.sourceStart = sourceStart;
                    visualLine.sourceEnd = sourceEnd;
                    visualLine.displayStart = 0;
                    visualLine.displayEnd = 1;
                    visualLine.rect = interactionMap.blocks.back().rect;
                    interactionMap.lines.push_back(std::move(visualLine));
                }
                else
                {
                    interactionMap.AddBlockLines(interactionMap.blocks.size() - 1);
                }
            }
            y += height;
            cacheMeasuredHeight(cacheKey, height);
        }

        EditorTableInteraction::Paint(resources, interactionMap, pointerPosition, draggedTableAction, tableDropIndex);

        totalDocumentHeight = layoutPlan.total_height;
        if (layoutPlanChanged) Invalidate();
        auto maxScroll = MaximumScrollOffset();
        scrollOffset = (std::min)(scrollOffset, maxScroll);
        scrollTarget = (std::min)(scrollTarget, maxScroll);

        if (selectionState.is_caret())
        {
            auto upstream = selectionState.affinity == elmd::TextAffinity::Upstream;
            if (auto rect = CaretBounds(caret, upstream))
            {
                resources.d2dContext->DrawLine(D2D1::Point2F(rect->left, rect->top), D2D1::Point2F(rect->left, rect->bottom), resources.caretBrush.Get(), 1.5f);
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
        return (std::max)(0.0f, totalDocumentHeight - resources.surfaceHeightDip);
    }

    float EditorSurfaceRenderer::ViewportHeight() const
    {
        return resources.surfaceHeightDip;
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
        return EditorTableInteraction::ActionAt(interactionMap, x, y);
    }

    std::optional<std::size_t> EditorSurfaceRenderer::TableDropIndexAt(float x, float y, bool rows) const
    {
        return EditorTableInteraction::DropIndexAt(interactionMap, draggedTableAction, x, y, rows);
    }

    bool EditorSurfaceRenderer::ScrollToSourceOffset(std::size_t sourceOffset)
    {
        auto previous = scrollOffset;
        if (auto caretBounds = CaretBounds(sourceOffset))
        {
            auto margin = styleSheet.verticalPadding;
            auto maxScroll = (std::max)(0.0f, totalDocumentHeight - resources.surfaceHeightDip);
            if (caretBounds->top < margin)
            {
                scrollOffset = (std::max)(0.0f, scrollOffset - (margin - caretBounds->top));
            }
            else if (caretBounds->bottom > resources.surfaceHeightDip - margin)
            {
                scrollOffset = (std::min)(maxScroll, scrollOffset + caretBounds->bottom - (resources.surfaceHeightDip - margin));
            }
            scrollTarget = scrollOffset;
            return scrollOffset != previous;
        }

        for (auto const& block : interactionMap.blocks)
        {
            if (block.sourceStart <= sourceOffset && sourceOffset <= block.sourceEnd)
            {
                auto maxScroll = (std::max)(0.0f, totalDocumentHeight - resources.surfaceHeightDip);
                scrollOffset = (std::min)(maxScroll, (std::max)(0.0f, block.documentY - styleSheet.verticalPadding));
                scrollTarget = scrollOffset;
                return scrollOffset != previous;
            }
        }
        return false;
    }

    std::optional<std::size_t> EditorSurfaceRenderer::HitTest(float x, float y, bool* outUpstream) const
    {
        return interactionMap.HitTest(x, y, outUpstream);
    }

    std::optional<D2D1_RECT_F> EditorSurfaceRenderer::CaretBounds(std::size_t sourceOffset, bool upstream) const
    {
        return interactionMap.CaretBounds(sourceOffset, upstream, styleSheet.body.lineHeight);
    }

    std::optional<EditorSurfaceRenderer::CaretMove> EditorSurfaceRenderer::MoveCaretVertically(std::size_t sourceOffset, bool upstream, bool down, float& goalX) const
    {
        return interactionMap.MoveCaretVertically(sourceOffset, upstream, down, goalX, styleSheet.body.lineHeight);
    }

    std::optional<std::size_t> EditorSurfaceRenderer::VisualLineStart(std::size_t sourceOffset, bool upstream) const
    {
        return interactionMap.VisualLineStart(sourceOffset, upstream);
    }

    std::optional<std::size_t> EditorSurfaceRenderer::VisualLineEnd(std::size_t sourceOffset, bool upstream) const
    {
        return interactionMap.VisualLineEnd(sourceOffset, upstream);
    }
    void EditorSurfaceRenderer::Render(detail::EditorRenderFrame const& frame)
    {
        if (!resources.Ready() || resizing || rendering)
        {
            return;
        }

        rendering = true;
        struct ResetFlag
        {
            EditorSurfaceRenderer& owner;
            ~ResetFlag()
            {
                owner.rendering = false;
                if (owner.deferredInvalidate.exchange(false)) owner.Invalidate();
            }
        } resetFlag{ *this };

        resources.EnsureFrameResources(styleSheet);

        resources.d2dContext->BeginDraw();
        resources.d2dContext->Clear(styleSheet.canvasColor);
        DrawDocument(frame);
        auto ended = resources.d2dContext->EndDraw();
        if (ended == D2DERR_RECREATE_TARGET)
        {
            resources.ResetTargets();
            return;
        }
        if (FAILED(ended)) return;

        auto presented = resources.swapChain->Present(1, 0);
        if (presented == DXGI_ERROR_DEVICE_REMOVED || presented == DXGI_ERROR_DEVICE_RESET) resources.ResetTargets();
    }
}
