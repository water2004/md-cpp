#include "pch.h"
#include "EditorSurfaceRenderer.h"

import elmd.core.render_model;
import elmd.core.utf;

#include "EditorContentPreparation.h"
#include "EditorInlineImageRenderer.h"
#include "EditorSvgPainter.h"
#include "EditorTableBlockRenderer.h"
#include "EditorTextLayoutEngine.h"

namespace winrt::ElMd
{
    struct EditorSurfaceRenderer::PreparedDocument
    {
        struct MathPreview
        {
            DisplayInlineText display;
            ::Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            float height = 0.0f;
        };

        struct Block
        {
            DisplayInlineText display;
            ::Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            std::vector<EditorInlineImageRenderer::ImageDraw> images;
            std::vector<MathPreview> mathPreviews;
            std::optional<EditorTableBlockRenderer::PreparedTable> table;
            std::vector<elmd::NodeId> owners;
            float textHeight = 0.0f;
            float height = 0.0f;
            bool code = false;
            bool containsMath = false;
            bool containsImage = false;
            bool embeddedRequested = false;
            bool pendingMath = false;
            bool valid = false;
            std::uint64_t embeddedGeneration = 0;
            std::uint64_t remoteImageGeneration = 0;
        };

        struct Placement
        {
            float top = 0.0f;
            float bottom = 0.0f;
        };

        std::uint64_t modelRevision = 0;
        elmd::TextSelection selection{};
        float documentWidth = 0.0f;
        float totalHeight = 0.0f;
        Theme theme = Theme::Dark;
        std::vector<Block> blocks;
        std::vector<Placement> placements;
        bool geometryValid = false;
    };

    namespace
    {
        bool SamePosition(elmd::TextPosition left, elmd::TextPosition right)
        {
            return left.container_id == right.container_id
                && left.source_offset == right.source_offset
                && left.affinity == right.affinity;
        }

        bool SameSelection(elmd::TextSelection left, elmd::TextSelection right)
        {
            return SamePosition(left.anchor, right.anchor)
                && SamePosition(left.active, right.active);
        }

        bool InlineItemsContain(
            std::vector<elmd::InlineRenderItem> const& items,
            elmd::InlineRenderItem::Kind kind)
        {
            for (auto const& item : items)
            {
                if (item.kind == kind || InlineItemsContain(item.children, kind)) return true;
            }
            return false;
        }

        bool RenderBlockContainsMath(elmd::RenderBlock const& block)
        {
            if (block.kind == elmd::RenderBlockKind::Math
                || InlineItemsContain(block.inline_items, elmd::InlineRenderItem::Kind::Math)) return true;
            for (auto const& cell : block.table_cells)
                if (InlineItemsContain(cell, elmd::InlineRenderItem::Kind::Math)) return true;
            for (auto const& child : block.child_blocks)
                if (RenderBlockContainsMath(child)) return true;
            return false;
        }

        bool RenderBlockContainsImage(elmd::RenderBlock const& block)
        {
            if (block.kind == elmd::RenderBlockKind::Image
                || InlineItemsContain(block.inline_items, elmd::InlineRenderItem::Kind::Image)) return true;
            for (auto const& cell : block.table_cells)
                if (InlineItemsContain(cell, elmd::InlineRenderItem::Kind::Image)) return true;
            for (auto const& child : block.child_blocks)
                if (RenderBlockContainsImage(child)) return true;
            return false;
        }
    }

    EditorSurfaceRenderer::EditorSurfaceRenderer() = default;

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
        renderCache.ClearTextLayouts();
        renderCache.ClearSvgDocuments();
        resources.RebuildTextFormats(styleSheet);
        resources.ResetBrushes();
        ClearPreparedDocument();
    }

    void EditorSurfaceRenderer::ResetDocumentCaches()
    {
        mathJax.Clear();
        treeSitter.Clear();
        renderCache.ClearTextLayouts();
        renderCache.ClearSvgDocuments();
        ClearPreparedDocument();
    }

    void EditorSurfaceRenderer::ClearPreparedDocument()
    {
        preparedDocument.reset();
        documentOwnerY.clear();
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
            if (!dispatcher.TryEnqueue([this]
                {
                    mathInvalidationQueued = false;
                    ++embeddedGeneration;
                    Invalidate();
                })) mathInvalidationQueued = false;
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
        if (result.widthChanged)
        {
            renderCache.ClearTextLayouts();
            ClearPreparedDocument();
        }
        scrollOffset = (std::min)(scrollOffset, MaximumScrollOffset());
        scrollTarget = (std::min)(scrollTarget, MaximumScrollOffset());
    }

    void EditorSurfaceRenderer::DrawDocument(detail::EditorRenderFrame const& frame)
    {
        interactionMap.Clear(frame.renderModel.blocks.size());
        nonInteractiveRegions.clear();
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

        std::function<DisplayInlineText(elmd::RenderBlock const&, float, bool)> prepare;
        prepare = [&](elmd::RenderBlock const& block, float width, bool requestEmbedded) -> DisplayInlineText
        {
            if (block.kind == elmd::RenderBlockKind::Blank)
            {
                DisplayInlineText display;
                AppendGeneratedText(display, U"\u200B", {block.id, 0, elmd::TextAffinity::Downstream}, elmd::InlineStyle::plain());
                display.displayToSource.push_back({block.id, 0, elmd::TextAffinity::Downstream});
                return display;
            }
            if (block.kind == elmd::RenderBlockKind::Code)
                return BuildCodeBlockText(block, caret, treeSitter);
            if (block.kind == elmd::RenderBlockKind::Math)
                return BuildMathBlockText(block, caret, mathJax, svgNormalizer, styleSheet.textColor, styleSheet.body.size, width, svgSupported, requestEmbedded);
            if (!block.inline_items.empty())
            {
                auto sourceEnd = InlineItemsEndPosition(block.inline_items, {block.id, block.content_span.source_range.end, elmd::TextAffinity::Downstream});
                return BuildDisplayInlineText(block.inline_items, caret, sourceEnd, mathJax, svgNormalizer, styleSheet.textColor, styleSheet.body.size, width, svgSupported, requestEmbedded);
            }

            DisplayInlineText display;
            if (block.kind == elmd::RenderBlockKind::Image)
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
            else
            {
                auto raw = elmd::utf8_to_cps(block.raw.empty() ? block.reason_text : block.raw);
                AppendSourceText(display, raw, {block.id, {0, raw.size()}}, elmd::InlineStyle::plain(), false);
            }
            if (display.text.empty()) AppendGeneratedText(display, U"\u200B", {block.id, 0, elmd::TextAffinity::Downstream}, elmd::InlineStyle::plain());
            display.displayToSource.push_back({block.id, block.source_span.source_range.end, elmd::TextAffinity::Downstream});
            return display;
        };

        auto drawSelection = [&](IDWriteTextLayout* layout, D2D1_POINT_2F origin, EditorDisplayMapping const& mapping)
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

        auto applyNestedCodeHighlights = [&](DisplayInlineText& display, elmd::RenderBlock const& parent)
        {
            auto apply = [&](auto& self, elmd::RenderBlock const& block) -> void
            {
                if (block.kind == elmd::RenderBlockKind::Code && block.language && !block.code_text.empty())
                {
                    auto highlights = treeSitter.Highlight(*block.language, elmd::cps_to_utf8(block.code_text));
                    for (auto const& highlight : highlights)
                    {
                        auto contentStart = (std::min)(static_cast<std::size_t>(highlight.start), block.code_text.size());
                        auto contentEnd = (std::min)(contentStart + static_cast<std::size_t>(highlight.length), block.code_text.size());
                        auto sourceStart = block.content_to_source.empty()
                            ? contentStart
                            : block.content_to_source[(std::min)(contentStart, block.content_to_source.size() - 1)];
                        auto sourceEnd = block.content_to_source.empty()
                            ? contentEnd
                            : block.content_to_source[(std::min)(contentEnd, block.content_to_source.size() - 1)];
                        auto start = DisplayPositionForSource(display.displayToSource, {block.id, sourceStart, elmd::TextAffinity::Downstream});
                        auto end = DisplayPositionForSource(display.displayToSource, {block.id, sourceEnd, elmd::TextAffinity::Downstream});
                        if (end <= start) continue;
                        elmd::InlineStyle style = elmd::InlineStyle::plain();
                        style.code = true;
                        display.ranges.push_back({
                            static_cast<UINT32>(start),
                            static_cast<UINT32>(end - start),
                            style,
                            false,
                            highlight.kind,
                        });
                    }
                }
                for (auto const& child : block.child_blocks) self(self, child);
            };
            for (auto const& child : parent.child_blocks) apply(apply, child);
        };

        auto drawFlowDecorations = [&](IDWriteTextLayout* layout, D2D1_POINT_2F origin, elmd::RenderBlock const& parent, DisplayInlineText const& display)
        {
            if (!layout || display.displayToSource.empty()) return;
            auto displayLimit = (std::min)(display.displayToSource.size(), elmd::utf16_len(display.text));
            auto displayRange = [&](elmd::RenderBlock const& nested) -> std::optional<std::pair<std::size_t, std::size_t>>
            {
                std::unordered_set<std::uint64_t> owners;
                auto collectOwners = [&](auto& self, elmd::RenderBlock const& block) -> void
                {
                    if (block.source_span.container_id.v != 0) owners.insert(block.source_span.container_id.v);
                    for (auto const& child : block.child_blocks) self(self, child);
                };
                collectOwners(collectOwners, nested);
                std::optional<std::size_t> first;
                std::size_t last = 0;
                for (std::size_t index = 0; index < displayLimit; ++index)
                {
                    auto const& position = display.displayToSource[index];
                    if (!owners.contains(position.container_id.v)) continue;
                    if (!first) first = index;
                    last = index + 1;
                }
                return first ? std::optional{std::pair{*first, last}} : std::nullopt;
            };
            auto contentLeftFor = [&](elmd::RenderBlock const& nested, std::pair<std::size_t, std::size_t> range, std::size_t indentColumns) -> std::optional<float>
            {
                std::optional<std::size_t> anchorStart;
                for (std::size_t index = 0; index < displayLimit; ++index)
                {
                    if (display.displayToSource[index].container_id == nested.flow_anchor_owner_id)
                    {
                        anchorStart = index;
                        break;
                    }
                }
                if (!anchorStart) return std::nullopt;
                auto contentColumn = (std::min)(*anchorStart + indentColumns, range.second);
                FLOAT x = 0.0f;
                FLOAT lineY = 0.0f;
                DWRITE_HIT_TEST_METRICS hit{};
                if (FAILED(layout->HitTestTextPosition(static_cast<UINT32>(contentColumn), FALSE, &x, &lineY, &hit))) return std::nullopt;
                return origin.x + x;
            };
            auto accumulateRange = [&](std::pair<std::size_t, std::size_t> range, float& top, float& bottom)
            {
                auto [displayStart, displayEnd] = range;
                UINT32 count = 0;
                auto result = layout->HitTestTextRange(static_cast<UINT32>(displayStart), static_cast<UINT32>(displayEnd - displayStart), origin.x, origin.y, nullptr, 0, &count);
                if (result != E_NOT_SUFFICIENT_BUFFER || count == 0) return false;
                std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
                if (FAILED(layout->HitTestTextRange(static_cast<UINT32>(displayStart), static_cast<UINT32>(displayEnd - displayStart), origin.x, origin.y, metrics.data(), count, &count))) return false;
                for (UINT32 index = 0; index < count; ++index)
                {
                    top = (std::min)(top, metrics[index].top);
                    bottom = (std::max)(bottom, metrics[index].top + metrics[index].height);
                }
                return true;
            };
            auto draw = [&](auto& self, elmd::RenderBlock const& nested, bool root, std::size_t parentIndentColumns) -> void
            {
                auto indentColumns = parentIndentColumns + nested.flow_local_indent_columns;
                auto decorated = nested.kind == elmd::RenderBlockKind::Code
                    || nested.kind == elmd::RenderBlockKind::Quote
                    || nested.kind == elmd::RenderBlockKind::Callout
                    || nested.kind == elmd::RenderBlockKind::Footnote;
                if (decorated && !(root && nested.kind == elmd::RenderBlockKind::Code))
                {
                    auto range = displayRange(nested);
                    if (range) {
                        auto contentLeft = contentLeftFor(nested, *range, indentColumns);
                        float top = (std::numeric_limits<float>::max)();
                        float bottom = (std::numeric_limits<float>::lowest)();
                        if (contentLeft && accumulateRange(*range, top, bottom)) {
                            auto left = (std::clamp)(*contentLeft, origin.x, documentRight);
                            auto rect = D2D1::RectF(left, top - 4.0f, documentRight, bottom + 4.0f);
                            auto brush = nested.kind == elmd::RenderBlockKind::Code
                                || nested.kind == elmd::RenderBlockKind::Callout
                                ? resources.panelBrush.Get()
                                : resources.nestedQuoteBrush.Get();
                            resources.d2dContext->FillRectangle(rect, brush);
                            if (nested.kind == elmd::RenderBlockKind::Quote || nested.kind == elmd::RenderBlockKind::Callout) {
                                auto borderWidth = nested.block_style.border_left ? nested.block_style.border_left->width : 3.0f;
                                auto lineX = left + borderWidth * 0.5f;
                                resources.d2dContext->DrawLine(
                                    D2D1::Point2F(lineX, rect.top + 3.0f),
                                    D2D1::Point2F(lineX, rect.bottom - 3.0f),
                                    resources.mutedBrush.Get(),
                                    borderWidth);
                            }
                        }
                    }
                }
                for (auto const& child : nested.child_blocks) self(self, child, false, indentColumns);
            };
            draw(draw, parent, true, 0);
        };

        if (frame.renderModel.blocks.empty())
        {
            constexpr wchar_t message[] = L"Open a Markdown file or start editing.";
            resources.d2dContext->DrawTextW(message, 38, resources.textFormat.Get(), D2D1::RectF(documentLeft, y, documentRight, y + 80.0f), resources.mutedBrush.Get());
        }

        auto remoteImageGeneration = renderCache.RemoteImageGeneration();
        auto rebuildAll = !preparedDocument
            || preparedDocument->modelRevision != frame.renderModel.revision
            || !SameSelection(preparedDocument->selection, frame.selection)
            || preparedDocument->documentWidth != documentWidth
            || preparedDocument->theme != theme
            || preparedDocument->blocks.size() != frame.renderModel.blocks.size();
        if (rebuildAll)
        {
            preparedDocument = std::make_unique<PreparedDocument>();
            preparedDocument->modelRevision = frame.renderModel.revision;
            preparedDocument->selection = frame.selection;
            preparedDocument->documentWidth = documentWidth;
            preparedDocument->theme = theme;
            preparedDocument->blocks.resize(frame.renderModel.blocks.size());
            preparedDocument->placements.resize(frame.renderModel.blocks.size());
        }
        auto prepareBlock = [&](elmd::RenderBlock const& block, bool requestEmbedded)
        {
            PreparedDocument::Block prepared;
            prepared.code = block.kind == elmd::RenderBlockKind::Code
                || block.kind == elmd::RenderBlockKind::Frontmatter
                || block.kind == elmd::RenderBlockKind::Unsupported;
            prepared.containsMath = RenderBlockContainsMath(block);
            prepared.containsImage = RenderBlockContainsImage(block);
            prepared.embeddedRequested = requestEmbedded;
            prepared.embeddedGeneration = embeddedGeneration;
            prepared.remoteImageGeneration = remoteImageGeneration;
            std::unordered_set<std::uint64_t> owners;
            auto addOwner = [&](elmd::NodeId id)
            {
                if (id.v != 0 && owners.insert(id.v).second) prepared.owners.push_back(id);
            };
            addOwner(block.id);
            addOwner(block.source_span.container_id);
            if (block.kind == elmd::RenderBlockKind::Table)
            {
                prepared.table = EditorTableBlockRenderer::Prepare(
                    block,
                    caret,
                    documentWidth,
                    svgSupported,
                    requestEmbedded,
                    resources,
                    styleSheet,
                    textLayoutEngine,
                    inlineImages,
                    mathJax,
                    svgNormalizer);
                if (prepared.table)
                {
                    for (auto const& span : block.table_cell_spans) addOwner(span.container_id);
                    prepared.pendingMath = prepared.table->pendingMath;
                    prepared.height = prepared.table->height;
                    prepared.valid = true;
                    return prepared;
                }
            }
            prepared.display = prepare(block, documentWidth, requestEmbedded);
            applyNestedCodeHighlights(prepared.display, block);
            prepared.pendingMath = prepared.display.pendingMath;
            auto format = textLayoutEngine.FormatFor(prepared.code, prepared.display.ranges);
            prepared.layout = textLayoutEngine.CreateFlow(
                prepared.display,
                format,
                documentWidth,
                [&](IDWriteTextLayout* candidate, DisplayInlineText const& candidateDisplay)
                {
                    prepared.images = inlineImages.Resolve(
                        candidate,
                        candidateDisplay.imageOverlays,
                        documentWidth);
                });
            prepared.mathPreviews.reserve(prepared.display.mathPreviews.size());
            for (auto const& preview : prepared.display.mathPreviews)
            {
                if (!preview.separateBlock) continue;
                auto previewDisplay = BuildMathPreviewText(preview);
                auto previewLayout = textLayoutEngine.CreateFlow(
                    previewDisplay,
                    resources.textFormat.Get(),
                    (std::max)(1.0f, documentWidth - 16.0f),
                    {});
                auto previewHeight = textLayoutEngine.MeasureHeight(
                    previewLayout.Get(),
                    styleSheet.body.lineHeight);
                prepared.mathPreviews.push_back({
                    std::move(previewDisplay),
                    std::move(previewLayout),
                    previewHeight,
                });
            }
            auto flowContainer = !block.inline_items.empty()
                && (block.kind == elmd::RenderBlockKind::Quote
                    || block.kind == elmd::RenderBlockKind::Callout
                    || block.kind == elmd::RenderBlockKind::Footnote);
            auto paddingTop = flowContainer ? 0.0f : block.block_style.padding_top;
            auto paddingBottom = flowContainer ? 0.0f : block.block_style.padding_bottom;
            prepared.textHeight = textLayoutEngine.MeasureHeight(
                prepared.layout.Get(),
                textLayoutEngine.LineHeightFor(prepared.code, prepared.display.ranges));
            auto previewHeight = 0.0f;
            for (auto const& preview : prepared.mathPreviews)
                previewHeight += preview.height + 24.0f;
            prepared.height = prepared.textHeight + previewHeight + paddingTop + paddingBottom;
            for (auto const& position : prepared.display.displayToSource) addOwner(position.container_id);
            prepared.valid = true;
            return prepared;
        };

        constexpr float viewportOverscan = 240.0f;
        constexpr float embeddedOverscanBefore = 1200.0f;
        constexpr float embeddedOverscanAfter = 800.0f;
        auto requestEmbeddedAt = [&](float documentTop)
        {
            auto screenTop = documentTop - scrollOffset;
            return screenTop < resources.surfaceHeightDip + embeddedOverscanAfter
                && screenTop > -embeddedOverscanBefore;
        };
        auto rebuildGeometry = [&]
        {
            auto documentY = styleSheet.verticalPadding;
            documentOwnerY.clear();
            for (std::size_t index = 0; index < frame.renderModel.blocks.size(); ++index)
            {
                auto const& block = frame.renderModel.blocks[index];
                auto& prepared = preparedDocument->blocks[index];
                documentY += block.block_style.margin_top;
                if (block.kind == elmd::RenderBlockKind::ThematicBreak)
                {
                    prepared = {};
                    prepared.height = 40.0f;
                    prepared.owners.push_back(block.id);
                    prepared.valid = true;
                }
                else if (!prepared.valid)
                {
                    prepared = prepareBlock(block, requestEmbeddedAt(documentY));
                }
                auto& placement = preparedDocument->placements[index];
                placement.top = documentY;
                placement.bottom = documentY + prepared.height;
                for (auto owner : prepared.owners) documentOwnerY[owner.v] = placement.top;
                documentY = placement.bottom + block.block_style.margin_bottom + styleSheet.blockGap;
            }
            preparedDocument->totalHeight = documentY + styleSheet.verticalPadding;
            preparedDocument->geometryValid = true;
        };
        if (!preparedDocument->geometryValid) rebuildGeometry();

        auto firstIntersecting = [&](float documentTop)
        {
            return static_cast<std::size_t>(std::lower_bound(
                preparedDocument->placements.begin(),
                preparedDocument->placements.end(),
                documentTop,
                [](PreparedDocument::Placement const& placement, float value)
                {
                    return placement.bottom < value;
                }) - preparedDocument->placements.begin());
        };

        auto embeddedTop = scrollOffset - embeddedOverscanBefore;
        auto embeddedBottom = scrollOffset + resources.surfaceHeightDip + embeddedOverscanAfter;
        auto geometryChanged = false;
        for (auto index = firstIntersecting(embeddedTop); index < frame.renderModel.blocks.size(); ++index)
        {
            auto const& placement = preparedDocument->placements[index];
            if (placement.top > embeddedBottom) break;
            auto const& block = frame.renderModel.blocks[index];
            if (block.kind == elmd::RenderBlockKind::ThematicBreak) continue;
            auto& prepared = preparedDocument->blocks[index];
            auto refreshForMath = prepared.pendingMath
                && prepared.embeddedRequested
                && prepared.embeddedGeneration != embeddedGeneration;
            auto refreshForImages = prepared.containsImage
                && prepared.embeddedRequested
                && prepared.remoteImageGeneration != remoteImageGeneration;
            auto enteredEmbeddedBand = !prepared.embeddedRequested
                && (prepared.containsMath || prepared.containsImage);
            if (!refreshForMath && !refreshForImages && !enteredEmbeddedBand) continue;
            auto previousHeight = prepared.height;
            prepared = prepareBlock(block, true);
            geometryChanged = geometryChanged || prepared.height != previousHeight;
        }
        if (geometryChanged)
        {
            preparedDocument->geometryValid = false;
            rebuildGeometry();
        }

        auto viewportTop = scrollOffset - viewportOverscan;
        auto viewportBottom = scrollOffset + resources.surfaceHeightDip + viewportOverscan;
        for (auto blockIndex = firstIntersecting(viewportTop); blockIndex < frame.renderModel.blocks.size(); ++blockIndex)
        {
            auto const& placement = preparedDocument->placements[blockIndex];
            if (placement.top > viewportBottom) break;
            auto const& block = frame.renderModel.blocks[blockIndex];
            auto& prepared = preparedDocument->blocks[blockIndex];
            auto top = placement.top - scrollOffset;
            auto bottom = placement.bottom - scrollOffset;
            auto documentY = placement.top;
            if (block.kind == elmd::RenderBlockKind::ThematicBreak)
            {
                auto center = (top + bottom) * 0.5f;
                resources.d2dContext->DrawLine(D2D1::Point2F(documentLeft, center), D2D1::Point2F(documentRight, center), resources.mutedBrush.Get(), 1.0f);
                EditorVisualBlock visual;
                visual.rect = D2D1::RectF(documentLeft, top, documentRight, bottom);
                visual.textOrigin = D2D1::Point2F(documentLeft, top);
                visual.textWidth = documentWidth;
                visual.sourceSpan = block.source_span;
                visual.documentY = documentY;
                visual.thematicBreak = true;
                interactionMap.blocks.push_back(visual);
                EditorVisualLine line;
                line.blockIndex = interactionMap.blocks.size() - 1;
                line.sourceSpans = {block.source_span};
                line.rect = visual.rect;
                interactionMap.lines.push_back(line);
                continue;
            }
            if (prepared.table)
            {
                EditorTableBlockRenderer::Paint(
                    block,
                    *prepared.table,
                    frame.selection,
                    documentLeft,
                    top,
                    scrollOffset,
                    resources,
                    styleSheet,
                    interactionMap,
                    inlineImages,
                    drawMath,
                    drawMathFallback);
                continue;
            }
            auto flowContainer = !block.inline_items.empty()
                && (block.kind == elmd::RenderBlockKind::Quote
                    || block.kind == elmd::RenderBlockKind::Callout
                    || block.kind == elmd::RenderBlockKind::Footnote);
            auto paddingTop = flowContainer ? 0.0f : block.block_style.padding_top;
            auto paddingLeft = flowContainer ? 0.0f : block.block_style.padding_left;
            auto origin = D2D1::Point2F(documentLeft + paddingLeft, top + paddingTop);
            auto rect = D2D1::RectF(documentLeft, top, documentRight, bottom);

            if (prepared.code || block.block_style.background) resources.d2dContext->FillRectangle(rect, resources.panelBrush.Get());
            drawFlowDecorations(prepared.layout.Get(), origin, block, prepared.display);
            drawSelection(prepared.layout.Get(), origin, prepared.display.displayToSource);
            if (prepared.layout)
                resources.d2dContext->DrawTextLayout(
                    origin,
                    prepared.layout.Get(),
                    prepared.code ? resources.codeBrush.Get() : resources.textBrush.Get(),
                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
            inlineImages.Draw(prepared.layout.Get(), origin, prepared.images);
            for (auto const& overlay : prepared.display.mathOverlays)
            {
                FLOAT x = 0.0f, lineY = 0.0f;
                DWRITE_HIT_TEST_METRICS metrics{};
                if (!prepared.layout || FAILED(prepared.layout->HitTestTextPosition(overlay.displayStart, FALSE, &x, &lineY, &metrics))) continue;
                auto mathOrigin = D2D1::Point2F(origin.x + x + overlay.leadingSpace, origin.y + metrics.top);
                if (!drawMath(overlay.fragment, mathOrigin, styleSheet.textColor)) drawMathFallback(overlay.sourceSpan, mathOrigin);
                interactionMap.mathHits.push_back({D2D1::RectF(mathOrigin.x, mathOrigin.y, mathOrigin.x + overlay.fragment.width, mathOrigin.y + overlay.fragment.height), overlay.sourceSpan, overlay.progressStart, overlay.progressEnd});
            }
            auto previewTop = origin.y + prepared.textHeight;
            for (auto const& preview : prepared.mathPreviews)
            {
                auto nonInteractiveTop = previewTop;
                previewTop += 8.0f;
                auto previewRect = D2D1::RectF(
                    documentLeft + paddingLeft,
                    previewTop,
                    documentRight - block.block_style.padding_right,
                    previewTop + preview.height + 16.0f);
                resources.d2dContext->FillRectangle(previewRect, resources.nestedQuoteBrush.Get());
                auto previewOrigin = D2D1::Point2F(previewRect.left + 8.0f, previewRect.top + 8.0f);
                if (preview.layout)
                    resources.d2dContext->DrawTextLayout(
                        previewOrigin,
                        preview.layout.Get(),
                        resources.textBrush.Get(),
                        D2D1_DRAW_TEXT_OPTIONS_CLIP);
                for (auto const& overlay : preview.display.mathOverlays)
                {
                    FLOAT x = 0.0f, lineY = 0.0f;
                    DWRITE_HIT_TEST_METRICS metrics{};
                    if (!preview.layout || FAILED(preview.layout->HitTestTextPosition(
                            overlay.displayStart, FALSE, &x, &lineY, &metrics))) continue;
                    auto mathOrigin = D2D1::Point2F(
                        previewOrigin.x + x + overlay.leadingSpace,
                        previewOrigin.y + metrics.top);
                    if (!drawMath(overlay.fragment, mathOrigin, styleSheet.textColor))
                        drawMathFallback(overlay.sourceSpan, mathOrigin);
                }
                nonInteractiveRegions.push_back(D2D1::RectF(
                    previewRect.left,
                    nonInteractiveTop,
                    previewRect.right,
                    previewRect.bottom));
                previewTop = previewRect.bottom;
            }
            EditorVisualBlock visual;
            visual.rect = rect;
            visual.textOrigin = origin;
            visual.textWidth = documentWidth;
            visual.sourceSpan = block.source_span;
            visual.documentY = documentY;
            visual.text = prepared.display.text;
            visual.displayToSource = prepared.display.displayToSource;
            visual.layout = prepared.layout;
            interactionMap.blocks.push_back(std::move(visual));
            interactionMap.AddBlockLines(interactionMap.blocks.size() - 1);
        }

        EditorTableInteraction::Paint(resources, interactionMap, pointerPosition, draggedTableAction, tableDropIndex);
        totalDocumentHeight = preparedDocument->totalHeight;
        scrollOffset = (std::min)(scrollOffset, MaximumScrollOffset());
        scrollTarget = (std::min)(scrollTarget, MaximumScrollOffset());
        if (frame.selection.is_caret())
        {
            if (auto rect = CaretBounds(caret))
                resources.d2dContext->DrawLine(D2D1::Point2F(rect->left, rect->top), D2D1::Point2F(rect->left, rect->bottom), resources.caretBrush.Get(), 1.5f);
        }
    }

    void EditorSurfaceRenderer::ScrollBy(float delta) { SetScrollOffset(scrollOffset + delta); }
    void EditorSurfaceRenderer::QueueScrollBy(float delta)
    {
        if (!std::isfinite(delta) || delta == 0.0f) return;
        auto pendingDistance = scrollTarget - scrollOffset;
        if (pendingDistance * delta < 0.0f) scrollTarget = scrollOffset;
        scrollTarget = (std::clamp)(scrollTarget + delta, 0.0f, MaximumScrollOffset());
    }

    bool EditorSurfaceRenderer::AdvanceScrollAnimation(float elapsedSeconds)
    {
        auto distance = scrollTarget - scrollOffset;
        auto elapsed = (std::max)(0.0f, elapsedSeconds);
        if (std::fabs(distance) < 0.1f)
        {
            scrollOffset = scrollTarget;
            return false;
        }
        constexpr float responseHalfLifeSeconds = 0.040f;
        auto response = 1.0f - std::exp2(-elapsed / responseHalfLifeSeconds);
        scrollOffset += distance * response;
        return true;
    }

    void EditorSurfaceRenderer::SetScrollOffset(float value)
    {
        scrollOffset = (std::clamp)(value, 0.0f, MaximumScrollOffset());
        scrollTarget = scrollOffset;
    }
    float EditorSurfaceRenderer::ScrollOffset() const { return scrollOffset; }
    float EditorSurfaceRenderer::MaximumScrollOffset() const { return (std::max)(0.0f, totalDocumentHeight - resources.surfaceHeightDip); }
    float EditorSurfaceRenderer::ViewportHeight() const { return resources.surfaceHeightDip; }
    HANDLE EditorSurfaceRenderer::FrameLatencyWaitableObject() const { return resources.frameLatencyWaitableObject; }
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
        if (auto owner = documentOwnerY.find(position.container_id.v); owner != documentOwnerY.end())
        {
            scrollOffset = (std::clamp)(
                owner->second - styleSheet.verticalPadding,
                0.0f,
                MaximumScrollOffset());
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

    std::optional<elmd::TextPosition> EditorSurfaceRenderer::HitTest(float x, float y) const
    {
        for (auto const& rect : nonInteractiveRegions)
            if (x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom)
                return std::nullopt;
        return interactionMap.HitTest(x, y);
    }
    std::optional<D2D1_RECT_F> EditorSurfaceRenderer::CaretBounds(elmd::TextPosition position) const { return interactionMap.CaretBounds(position, styleSheet.body.lineHeight); }
    std::optional<elmd::TextPosition> EditorSurfaceRenderer::MoveCaretVertically(elmd::TextPosition position, bool down, float& goalX) const { return interactionMap.MoveCaretVertically(position, down, goalX, styleSheet.body.lineHeight); }
    std::optional<elmd::TextPosition> EditorSurfaceRenderer::VisualLineStart(elmd::TextPosition position) const { return interactionMap.VisualLineStart(position); }
    std::optional<elmd::TextPosition> EditorSurfaceRenderer::VisualLineEnd(elmd::TextPosition position) const { return interactionMap.VisualLineEnd(position); }

    void EditorSurfaceRenderer::Render(detail::EditorRenderFrame const& frame)
    {
        if (!resources.Ready()) return;
        if (resizing || rendering)
        {
            // A source edit may request a frame while an asynchronous math or
            // SVG invalidation is painting. Never drop that newer frame: the
            // deferred callback obtains the latest session model after this
            // render completes.
            deferredInvalidate = true;
            return;
        }
        rendering = true;
        struct Reset { EditorSurfaceRenderer& owner; ~Reset() { owner.rendering = false; if (owner.deferredInvalidate.exchange(false)) owner.Invalidate(); } } reset{*this};
        resources.EnsureFrameResources(styleSheet);
        resources.d2dContext->BeginDraw();
        resources.d2dContext->Clear(styleSheet.canvasColor);
        DrawDocument(frame);
        auto ended = resources.d2dContext->EndDraw();
        if (ended == D2DERR_RECREATE_TARGET) { resources.ResetTargets(); return; }
        if (FAILED(ended)) return;
        auto presented = resources.swapChain->Present(0, 0);
        if (presented == DXGI_ERROR_DEVICE_REMOVED || presented == DXGI_ERROR_DEVICE_RESET) resources.ResetTargets();
    }
}
