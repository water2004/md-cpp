#include "pch.h"
#include "editor/rendering/EditorSurfaceRenderer.h"
#include "editor/rendering/EditorPreparedDocument.h"
#include "localization/Localization.h"

import elmd.core.render_model;
import elmd.core.utf;
import elmd.platform.editor_viewport_plan;

#include "editor/rendering/EditorContentPreparation.h"
#include "editor/rendering/EditorDocumentPainter.h"
#include "editor/rendering/EditorInlineImageRenderer.h"
#include "editor/rendering/EditorSvgPainter.h"
#include "editor/rendering/EditorTableBlockRenderer.h"
#include "editor/rendering/EditorTextLayoutEngine.h"

namespace winrt::ElMd
{
    using elmd::platform::editor::BuildEditorViewportPlan;
    using elmd::platform::editor::EditorViewportPolicy;

    namespace
    {
        bool InlineItemsContain(
            std::vector<elmd::InlineRenderItem> const& items,
            elmd::InlineRenderItem::Kind kind)
        {
            for (auto const& item : items)
            {
                if (item.kind == kind
                    || InlineItemsContain(item.special().semantic().children, kind)) return true;
            }
            return false;
        }

        bool RenderBlockContainsMath(elmd::RenderBlock const& block)
        {
            if (block.kind == elmd::RenderBlockKind::Math
                || InlineItemsContain(block.inline_items, elmd::InlineRenderItem::Kind::Math)) return true;
            for (auto const& cell : block.special().table_cells)
                if (InlineItemsContain(cell, elmd::InlineRenderItem::Kind::Math)) return true;
            for (auto const& child : block.child_blocks)
                if (RenderBlockContainsMath(child)) return true;
            return false;
        }

        bool RenderBlockContainsImage(elmd::RenderBlock const& block)
        {
            if (block.kind == elmd::RenderBlockKind::Image
                || InlineItemsContain(block.inline_items, elmd::InlineRenderItem::Kind::Image)) return true;
            for (auto const& cell : block.special().table_cells)
                if (InlineItemsContain(cell, elmd::InlineRenderItem::Kind::Image)) return true;
            for (auto const& child : block.child_blocks)
                if (RenderBlockContainsImage(child)) return true;
            return false;
        }

        void CollectInlineOwners(
            std::vector<elmd::InlineRenderItem> const& items,
            std::unordered_set<std::uint64_t>& seen,
            std::vector<elmd::NodeId>& owners)
        {
            for (auto const& item : items)
            {
                auto owner = item.source_span.container_id;
                if (owner.v != 0 && seen.insert(owner.v).second) owners.push_back(owner);
                CollectInlineOwners(item.special().semantic().children, seen, owners);
            }
        }

        void CollectRenderOwners(
            elmd::RenderBlock const& block,
            std::unordered_set<std::uint64_t>& seen,
            std::vector<elmd::NodeId>& owners)
        {
            auto add = [&](elmd::NodeId owner)
            {
                if (owner.v != 0 && seen.insert(owner.v).second) owners.push_back(owner);
            };
            add(block.id);
            add(block.source_span.container_id);
            add(block.content_span.container_id);
            CollectInlineOwners(block.inline_items, seen, owners);
            for (auto const& cell : block.special().table_cells) CollectInlineOwners(cell, seen, owners);
            for (auto const& span : block.special().table_cell_spans) add(span.container_id);
            for (auto const& child : block.child_blocks) CollectRenderOwners(child, seen, owners);
        }
    }

    void EditorSurfaceRenderer::DrawDocument(detail::EditorRenderFrame const& frame)
    {
        interactionMap.Clear();
        nonInteractiveRegions.clear();
        auto padding = (std::min)(styleSheet.horizontalPadding, (std::max)(12.0f, resources.surfaceWidthDip * 0.06f));
        auto sourceDocument = !frame.renderModel.blocks.empty()
            && frame.renderModel.blocks.front().source_mode;
        auto sourceLineCount = sourceDocument
            ? (std::max)(std::uint32_t{1}, frame.renderModel.blocks.back().source_line_number)
            : std::uint32_t{0};
        auto sourceLineDigits = sourceDocument
            ? std::to_wstring(sourceLineCount).size()
            : std::size_t{0};
        auto desiredSourceGutterWidth = sourceDocument
            ? (std::max)(24.0f, styleSheet.code.size * 0.64f * static_cast<float>(sourceLineDigits) + 12.0f)
            : 0.0f;
        auto availableSourceGutterWidth = (std::max)(
            0.0f,
            resources.surfaceWidthDip - padding - 14.0f - 80.0f - 8.0f);
        auto sourceGutterWidth = (std::min)(desiredSourceGutterWidth, availableSourceGutterWidth);
        if (sourceGutterWidth < 20.0f) sourceGutterWidth = 0.0f;
        auto sourceGutterLeft = 4.0f;
        auto documentLeft = sourceGutterWidth > 0.0f ? sourceGutterWidth + 8.0f : padding;
        auto documentRight = (std::max)(documentLeft + 1.0f, resources.surfaceWidthDip - padding - 14.0f);
        auto documentWidth = documentRight - documentLeft;
        auto scrollOffset = scrollState.Offset();
        auto y = styleSheet.verticalPadding - scrollOffset;
        auto selection = printMode ? elmd::TextSelection{} : frame.selection;
        auto caret = selection.active;

        ::Microsoft::WRL::ComPtr<ID2D1DeviceContext5> svgContext;
        auto svgSupported = SUCCEEDED(resources.d2dContext.As(&svgContext)) && svgContext;
        auto mathSvgSupported = svgSupported && mathJax.Enabled();
        EditorSvgPainter svgPainter(resources, renderCache);
        EditorTextLayoutEngine textLayoutEngine(resources, styleSheet);
        EditorInlineImageRenderer inlineImages(
            resources,
            renderCache,
            styleSheet,
            svgNormalizer,
            svgPainter,
            frame.baseDirectory,
            !printMode);
        auto drawMath = [&](MathJaxSvgFragment const& fragment, D2D1_POINT_2F origin, D2D1_COLOR_F) {
            return fragment.svg && svgPainter.Draw(fragment.renderId, *fragment.svg, fragment.width, fragment.height, origin);
        };
        auto drawMathFallback = [&](elmd::TextSpan, D2D1_POINT_2F origin) {
            auto label = Localize(L"Formula");
            resources.d2dContext->DrawTextW(
                label.c_str(),
                static_cast<std::uint32_t>(label.size()),
                resources.codeFormat.Get(),
                D2D1::RectF(origin.x, origin.y, documentRight, origin.y + styleSheet.code.lineHeight),
                resources.textBrush.Get());
        };
        EditorDocumentPainter documentPainter(
            resources,
            styleSheet,
            interactionMap,
            treeSitter,
            pointerPosition,
            printMode,
            documentRight,
            selection,
            printMode ? std::span<const detail::EditorSearchHighlight>{} : frame.searchHighlights,
            frame.renderModel.editable_index);

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
                return BuildMathBlockText(block, caret, mathJax, svgNormalizer, styleSheet.textColor, styleSheet.body.size, width, mathSvgSupported, requestEmbedded);
            if (!block.inline_items.empty())
            {
                auto sourceEnd = InlineItemsEndPosition(block.inline_items, {block.id, block.content_span.source_range.end, elmd::TextAffinity::Downstream});
                return BuildDisplayInlineText(block.inline_items, caret, sourceEnd, mathJax, svgNormalizer, styleSheet.textColor, styleSheet.body.size, width, mathSvgSupported, requestEmbedded);
            }

            DisplayInlineText display;
            if (block.kind == elmd::RenderBlockKind::Image)
            {
                auto const& special = block.special();
                auto position = elmd::TextPosition{block.id, 0, elmd::TextAffinity::Downstream};
                auto start = static_cast<std::uint32_t>(display.displayToSource.size());
                AppendGeneratedText(display, U"\uFFFC", position, elmd::InlineStyle::plain());
                display.imageOverlays.push_back({start, block.source_span, special.src, special.alt, special.image_width, special.image_height, true});
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
                auto const& special = block.special();
                auto raw = elmd::utf8_to_cps(special.raw.empty() ? special.reason_text : special.raw);
                AppendSourceText(display, raw, {block.id, {0, raw.size()}}, elmd::InlineStyle::plain(), false);
            }
            if (display.text.empty()) AppendGeneratedText(display, U"\u200B", {block.id, 0, elmd::TextAffinity::Downstream}, elmd::InlineStyle::plain());
            display.displayToSource.push_back({block.id, block.source_span.source_range.end, elmd::TextAffinity::Downstream});
            return display;
        };

        if (frame.renderModel.blocks.empty() && !printMode)
        {
            auto message = Localize(L"EmptyDocumentHint");
            resources.d2dContext->DrawTextW(
                message.c_str(),
                static_cast<std::uint32_t>(message.size()),
                resources.textFormat.Get(),
                D2D1::RectF(documentLeft, y, documentRight, y + 80.0f),
                resources.mutedBrush.Get());
        }

        auto remoteImageGeneration = renderCache.RemoteImageGeneration();
        std::unordered_map<void const*, std::vector<SyntaxHighlightRange>> sourceCodeHighlights;
        auto initializePreparedMetadata = [&](PreparedDocument::Block& prepared, elmd::RenderBlock const& block)
        {
            prepared.sourceId = block.id;
            prepared.presentationKey = block.presentation_key;
            prepared.sourceMode = block.source_mode;
            prepared.code = block.kind == elmd::RenderBlockKind::Code
                || block.kind == elmd::RenderBlockKind::Frontmatter
                || block.kind == elmd::RenderBlockKind::Unsupported;
        };
        auto initializePreparedContentMetadata = [&](PreparedDocument::Block& prepared, elmd::RenderBlock const& block)
        {
            prepared.containsMath = RenderBlockContainsMath(block);
            prepared.containsImage = RenderBlockContainsImage(block);
            std::unordered_set<std::uint64_t> owners;
            CollectRenderOwners(block, owners, prepared.owners);
        };
        auto estimateBlockHeight = [&](elmd::RenderBlock const& block)
        {
            auto const& special = block.special();
            auto flowContainer = !block.inline_items.empty()
                && (block.kind == elmd::RenderBlockKind::Quote
                    || block.kind == elmd::RenderBlockKind::Callout);
            auto paddingTop = flowContainer ? 0.0f : block.block_style.padding_top;
            auto paddingBottom = flowContainer ? 0.0f : block.block_style.padding_bottom;
            auto paddingLeft = flowContainer ? 0.0f : block.block_style.padding_left;
            auto paddingRight = flowContainer ? 0.0f : block.block_style.padding_right;
            auto contentWidth = (std::max)(1.0f, documentWidth - paddingLeft - paddingRight);
            if (block.kind == elmd::RenderBlockKind::ThematicBreak) return 40.0f;
            if (block.kind == elmd::RenderBlockKind::Blank)
                return styleSheet.body.lineHeight + paddingTop + paddingBottom;
            if (block.source_code)
                return styleSheet.code.lineHeight + paddingTop + paddingBottom;
            if (block.kind == elmd::RenderBlockKind::Code
                || block.kind == elmd::RenderBlockKind::Frontmatter
                || block.kind == elmd::RenderBlockKind::Unsupported)
            {
                auto lines = (std::max)(std::size_t{1}, special.line_count);
                if (special.line_count == 0)
                {
                    auto const& source = special.code_text.empty() ? special.raw_source : special.code_text;
                    lines = 1 + static_cast<std::size_t>(std::ranges::count(source, U'\n'));
                }
                return static_cast<float>(lines) * styleSheet.code.lineHeight + paddingTop + paddingBottom;
            }
            if (block.kind == elmd::RenderBlockKind::Math)
                return styleSheet.body.lineHeight * 2.0f + paddingTop + paddingBottom;
            if (block.kind == elmd::RenderBlockKind::Table)
                return static_cast<float>((std::max)(
                    std::size_t{1},
                    special.row_count != 0
                        ? special.row_count
                        : static_cast<std::size_t>(block.estimated_line_breaks)))
                    * (styleSheet.body.lineHeight + 16.0f) + paddingTop + paddingBottom;
            if (block.kind == elmd::RenderBlockKind::Image)
            {
                auto requestedWidth = ResolveImageDimension(special.image_width, contentWidth);
                auto requestedHeight = ResolveImageDimension(special.image_height);
                auto width = requestedWidth.value_or(0.0f);
                auto height = requestedHeight.value_or(0.0f);
                if ((width <= 0.0f || height <= 0.0f) && !special.src.empty())
                {
                    if (auto dimensions = renderCache.ProbeImageDimensions(resources, frame.baseDirectory, special.src))
                    {
                        width = requestedWidth.value_or(dimensions->width);
                        height = requestedHeight.value_or(dimensions->height);
                        if (requestedWidth && !requestedHeight)
                            height = dimensions->height * width / dimensions->width;
                        if (!requestedWidth && requestedHeight)
                            width = dimensions->width * height / dimensions->height;
                    }
                }
                if (width <= 0.0f) width = contentWidth * 0.5f;
                if (height <= 0.0f) height = styleSheet.body.lineHeight;
                auto scale = (std::min)(1.0f, (std::min)(
                    (std::max)(48.0f, contentWidth * 0.75f) / width,
                    240.0f / height));
                auto caption = special.alt.empty() ? 0.0f : styleSheet.body.lineHeight + 4.0f;
                return (std::max)(styleSheet.body.lineHeight, height * scale)
                    + caption + paddingTop + paddingBottom;
            }

            auto level = block.text_heading_level;
            auto font = level == 0 ? styleSheet.body
                : level <= 1 ? styleSheet.heading1
                : level == 2 ? styleSheet.heading2
                : styleSheet.heading3;
            for (auto const& source : special.inline_image_sources)
                renderCache.ProbeImageDimensions(resources, frame.baseDirectory, source);
            auto averageAdvance = (std::max)(1.0f, font.size * 0.72f);
            auto charactersPerLine = (std::max)(
                std::size_t{1},
                static_cast<std::size_t>(contentWidth / averageAdvance));
            auto wrappedLines = (static_cast<std::size_t>(block.estimated_characters)
                + charactersPerLine - 1) / charactersPerLine;
            auto lines = (std::max)(
                std::size_t{1} + static_cast<std::size_t>(block.estimated_line_breaks),
                (std::max)(std::size_t{1}, wrappedLines));
            return static_cast<float>(lines) * font.lineHeight + paddingTop + paddingBottom;
        };
        auto activePositionChanged = preparedDocument
            && preparedDocument->selection.active != selection.active;
        auto modelChanged = preparedDocument
            && preparedDocument->modelRevision != frame.renderModel.revision;
        auto rebuildAll = !preparedDocument
            || preparedDocument->documentWidth != documentWidth
            || preparedDocument->themeRevision != themeRevision
            || preparedDocument->blocks.size() != frame.renderModel.blocks.size()
            || (modelChanged && !frame.renderModel.incremental_update);
        if (rebuildAll)
        {
            auto previous = std::move(preparedDocument);
            preparedDocument = std::make_unique<PreparedDocument>();
            preparedDocument->modelRevision = frame.renderModel.revision;
            preparedDocument->selection = selection;
            preparedDocument->documentWidth = documentWidth;
            preparedDocument->themeRevision = themeRevision;
            preparedDocument->blocks.resize(frame.renderModel.blocks.size());
            if (previous
                && previous->documentWidth == documentWidth
                && previous->themeRevision == themeRevision)
            {
                std::unordered_map<std::uint64_t, std::size_t> previousById;
                previousById.reserve(previous->blocks.size());
                for (std::size_t index = 0; index < previous->blocks.size(); ++index)
                {
                    auto const& block = previous->blocks[index];
                    if (block.sourceId.v != 0) previousById.emplace(block.sourceId.v, index);
                }
                auto owns = [](PreparedDocument::Block const& block, elmd::NodeId owner)
                {
                    return owner.v != 0 && std::ranges::any_of(
                        block.owners,
                        [&](auto candidate) { return candidate == owner; });
                };
                for (std::size_t index = 0; index < frame.renderModel.blocks.size(); ++index)
                {
                    auto const& source = frame.renderModel.blocks[index];
                    auto found = previousById.find(source.id.v);
                    if (found == previousById.end()) continue;
                    auto& candidate = previous->blocks[found->second];
                    if (candidate.sourceMode != source.source_mode
                        || candidate.presentationKey != source.presentation_key) continue;
                    // The active source position controls which markers and
                    // editable previews are exposed. Rebuild only the old and
                    // new caret-owning blocks; selection painting itself is a
                    // draw-time operation and does not invalidate text layout.
                    if (!source.source_mode && activePositionChanged
                        && (owns(candidate, previous->selection.active.container_id)
                            || owns(candidate, selection.active.container_id))) continue;
                    preparedDocument->blocks[index] = std::move(candidate);
                    if (preparedDocument->blocks[index].valid
                        && source.kind != elmd::RenderBlockKind::ThematicBreak)
                        preparedDocument->layoutBlocks.insert(index);
                }
            }
        }
        else
        {
            std::unordered_set<std::size_t> invalidated;
            if (modelChanged)
            {
                for (auto index : frame.renderModel.changed_block_indices)
                    if (index < preparedDocument->blocks.size()) invalidated.insert(index);
            }
            if (activePositionChanged
                && !frame.renderModel.blocks.empty()
                && !frame.renderModel.blocks.front().source_mode)
            {
                auto addOwner = [&](elmd::NodeId owner)
                {
                    auto found = preparedDocument->ownerBlockIndex.find(owner.v);
                    if (found != preparedDocument->ownerBlockIndex.end())
                        invalidated.insert(found->second);
                };
                addOwner(preparedDocument->selection.active.container_id);
                addOwner(selection.active.container_id);
            }
            auto releaseImages = [&](PreparedDocument::Block const& prepared)
            {
                std::vector<std::string> sources;
                sources.reserve(prepared.images.size());
                for (auto const& image : prepared.images)
                    if (!image.source.empty()) sources.push_back(image.source);
                if (prepared.table)
                {
                    for (auto const& cellImages : prepared.table->imageDraws)
                        for (auto const& image : cellImages)
                            if (!image.source.empty()) sources.push_back(image.source);
                }
                return sources;
            };
            for (auto index : invalidated)
            {
                auto const& source = frame.renderModel.blocks[index];
                auto& prepared = preparedDocument->blocks[index];
                auto imageSources = releaseImages(prepared);
                auto previousHeight = prepared.height > 0.0f
                    ? prepared.height
                    : estimateBlockHeight(source);
                for (auto owner : prepared.owners)
                {
                    auto found = preparedDocument->ownerBlockIndex.find(owner.v);
                    if (found != preparedDocument->ownerBlockIndex.end() && found->second == index)
                        preparedDocument->ownerBlockIndex.erase(found);
                }
                prepared = {};
                initializePreparedMetadata(prepared, source);
                if (source.kind == elmd::RenderBlockKind::ThematicBreak)
                {
                    prepared.height = 40.0f;
                    prepared.valid = true;
                }
                else
                {
                    prepared.height = previousHeight;
                }
                if (preparedDocument->geometry.Initialized())
                {
                    preparedDocument->geometry.Update(index, {
                        source.block_style.margin_top,
                        prepared.height,
                        source.block_style.margin_bottom
                            + (source.source_mode ? 0.0f : styleSheet.blockGap),
                    });
                }
                for (auto owner : prepared.owners)
                    preparedDocument->ownerBlockIndex[owner.v] = index;
                preparedDocument->layoutBlocks.erase(index);
                preparedDocument->embeddedBlocks.erase(index);
                for (auto const& imageSource : imageSources) inlineImages.ReleaseGif(imageSource);
            }
            if (modelChanged) preparedDocument->modelRevision = frame.renderModel.revision;
            if (preparedDocument->geometry.Initialized())
                preparedDocument->totalHeight = preparedDocument->geometry.TotalHeight();
        }
        preparedDocument->selection = selection;
        auto prepareBlock = [&](elmd::RenderBlock const& block, bool requestEmbedded)
        {
            PreparedDocument::Block prepared;
            initializePreparedMetadata(prepared, block);
            initializePreparedContentMetadata(prepared, block);
            prepared.embeddedRequested = requestEmbedded;
            prepared.embeddedGeneration = embeddedGeneration;
            prepared.remoteImageGeneration = remoteImageGeneration;
            std::unordered_set<std::uint64_t> owners;
            for (auto owner : prepared.owners) owners.insert(owner.v);
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
                    mathSvgSupported,
                    requestEmbedded,
                    resources,
                    styleSheet,
                    textLayoutEngine,
                    inlineImages,
                    mathJax,
                    svgNormalizer);
                if (prepared.table)
                {
                    for (auto const& span : block.special().table_cell_spans) addOwner(span.container_id);
                    prepared.pendingMath = prepared.table->pendingMath;
                    prepared.pendingImage = prepared.table->pendingImage;
                    prepared.height = prepared.table->height;
                    prepared.valid = true;
                    return prepared;
                }
            }
            auto flowContainer = !block.inline_items.empty()
                && (block.kind == elmd::RenderBlockKind::Quote
                    || block.kind == elmd::RenderBlockKind::Callout);
            auto paddingTop = flowContainer ? 0.0f : block.block_style.padding_top;
            auto paddingBottom = flowContainer ? 0.0f : block.block_style.padding_bottom;
            auto paddingLeft = flowContainer ? 0.0f : block.block_style.padding_left;
            auto paddingRight = flowContainer ? 0.0f : block.block_style.padding_right;
            auto contentWidth = (std::max)(1.0f, documentWidth - paddingLeft - paddingRight);
            prepared.display = prepare(block, contentWidth, requestEmbedded);
            if (block.source_mode && block.source_code && block.special().language && !prepared.display.text.empty())
            {
                auto const* contextKey = block.source_code_context
                    ? static_cast<void const*>(block.source_code_context.get())
                    : static_cast<void const*>(&block);
                auto [found, inserted] = sourceCodeHighlights.try_emplace(contextKey);
                if (inserted)
                {
                    auto const& context = block.source_code_context
                        ? *block.source_code_context
                        : prepared.display.text;
                    found->second = treeSitter.Highlight(*block.special().language, elmd::cps_to_utf8(context));
                }
                auto lineStart = block.source_code_context ? block.source_code_context_offset : 0;
                auto lineEnd = lineStart + prepared.display.text.size();
                auto const& highlights = found->second;
                for (auto const& highlight : highlights)
                {
                    auto highlightStart = static_cast<std::size_t>(highlight.start);
                    auto highlightEnd = highlightStart + static_cast<std::size_t>(highlight.length);
                    if (highlightEnd <= lineStart || highlightStart >= lineEnd) continue;
                    auto start = (std::max)(highlightStart, lineStart) - lineStart;
                    auto end = (std::min)(highlightEnd, lineEnd) - lineStart;
                    auto displayStart = elmd::char_index_to_utf16(prepared.display.text, start);
                    auto displayEnd = elmd::char_index_to_utf16(prepared.display.text, end);
                    if (displayEnd <= displayStart) continue;
                    prepared.display.ranges.push_back({
                        static_cast<UINT32>(displayStart),
                        static_cast<UINT32>(displayEnd - displayStart),
                        elmd::InlineStyle::plain(),
                        false,
                        highlight.kind,
                    });
                }
            }
            documentPainter.ApplyNestedCodeHighlights(prepared.display, block);
            prepared.pendingMath = prepared.display.pendingMath;
            auto format = textLayoutEngine.FormatFor(prepared.code || prepared.sourceMode, prepared.display.ranges);
            prepared.layout = textLayoutEngine.CreateFlow(
                prepared.display,
                format,
                contentWidth,
                [&](IDWriteTextLayout* candidate, DisplayInlineText const& candidateDisplay)
                {
                    prepared.images = inlineImages.Resolve(
                        candidate,
                        candidateDisplay.imageOverlays,
                        contentWidth,
                        requestEmbedded);
                    prepared.pendingImage = std::ranges::any_of(
                        prepared.images,
                        [](auto const& image) { return image.pending; });
                });
            if (prepared.layout && !block.source_mode && block.block_style.text_alignment)
            {
                auto alignment = DWRITE_TEXT_ALIGNMENT_LEADING;
                switch (*block.block_style.text_alignment)
                {
                    case elmd::TextAlignment::Start:
                        alignment = DWRITE_TEXT_ALIGNMENT_LEADING;
                        break;
                    case elmd::TextAlignment::Center:
                        alignment = DWRITE_TEXT_ALIGNMENT_CENTER;
                        break;
                    case elmd::TextAlignment::End:
                        alignment = DWRITE_TEXT_ALIGNMENT_TRAILING;
                        break;
                    case elmd::TextAlignment::Justify:
                        alignment = DWRITE_TEXT_ALIGNMENT_JUSTIFIED;
                        break;
                }
                prepared.layout->SetTextAlignment(alignment);
            }
            prepared.mathPreviews.reserve(prepared.display.mathPreviews.size());
            for (auto const& preview : prepared.display.mathPreviews)
            {
                auto previewDisplay = BuildMathPreviewText(preview);
                auto previewLayout = textLayoutEngine.CreateFlow(
                    previewDisplay,
                    resources.textFormat.Get(),
                    (std::max)(1.0f, contentWidth - 16.0f),
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
            prepared.textHeight = textLayoutEngine.MeasureHeight(
                prepared.layout.Get(),
                textLayoutEngine.LineHeightFor(prepared.code || prepared.sourceMode, prepared.display.ranges));
            auto previewHeight = 0.0f;
            for (auto const& preview : prepared.mathPreviews)
                previewHeight += preview.height + 24.0f;
            prepared.height = prepared.textHeight + previewHeight + paddingTop + paddingBottom;
            for (auto const& position : prepared.display.displayToSource) addOwner(position.container_id);
            prepared.valid = true;
            return prepared;
        };

        constexpr EditorViewportPolicy viewportPolicy{};
        auto requestEmbeddedAt = [&](float documentTop)
        {
            if (printMode) return true;
            auto screenTop = documentTop - scrollOffset;
            return screenTop < resources.surfaceHeightDip + viewportPolicy.embeddedAfter
                && screenTop > -viewportPolicy.embeddedBefore;
        };
        auto initializeGeometry = [&]
        {
            preparedDocument->ownerBlockIndex.clear();
            preparedDocument->embeddedBlocks.clear();
            std::vector<EditorBlockGeometryIndex::Entry> entries;
            entries.reserve(frame.renderModel.blocks.size());
            for (std::size_t index = 0; index < frame.renderModel.blocks.size(); ++index)
            {
                auto const& block = frame.renderModel.blocks[index];
                auto& prepared = preparedDocument->blocks[index];
                if (block.kind == elmd::RenderBlockKind::ThematicBreak)
                {
                    prepared = {};
                    initializePreparedMetadata(prepared, block);
                    prepared.height = 40.0f;
                    prepared.valid = true;
                }
                else if (!prepared.valid && prepared.height <= 0.0f)
                {
                    initializePreparedMetadata(prepared, block);
                    prepared.height = estimateBlockHeight(block);
                }
                else if (prepared.sourceId.v == 0)
                {
                    initializePreparedMetadata(prepared, block);
                }
                entries.push_back({
                    block.block_style.margin_top,
                    prepared.height,
                    block.block_style.margin_bottom
                        + (block.source_mode ? 0.0f : styleSheet.blockGap),
                });
                if (prepared.embeddedRequested && (prepared.containsMath || prepared.containsImage))
                    preparedDocument->embeddedBlocks.insert(index);
                for (auto owner : prepared.owners)
                    preparedDocument->ownerBlockIndex[owner.v] = index;
            }
            std::unordered_map<std::uint64_t, std::size_t> topLevelBlockIndex;
            topLevelBlockIndex.reserve(frame.renderModel.blocks.size());
            for (std::size_t index = 0; index < frame.renderModel.blocks.size(); ++index)
                topLevelBlockIndex[frame.renderModel.blocks[index].id.v] = index;
            preparedDocument->ownerBlockIndex.reserve(frame.renderModel.editable_top_level.size());
            for (auto const& [owner, topLevel] : frame.renderModel.editable_top_level)
            {
                auto found = topLevelBlockIndex.find(topLevel.v);
                if (found == topLevelBlockIndex.end()
                    || found->second >= entries.size()) continue;
                preparedDocument->ownerBlockIndex[owner] = found->second;
            }
            preparedDocument->geometry.Reset(std::move(entries), styleSheet.verticalPadding);
            preparedDocument->totalHeight = preparedDocument->geometry.TotalHeight();
        };
        if (!preparedDocument->geometry.Initialized()) initializeGeometry();

        // Refine estimated blocks incrementally. DirectWrite layout and render
        // model materialization both run on the UI thread, so a fixed amount of
        // first-visit work is admitted per frame. The viewport is prepared
        // before a short directional look-ahead band; another frame continues
        // the work when the budget is exhausted or corrected heights expose a
        // different set of blocks.
        if (!frame.renderModel.blocks.empty())
        {
            auto anchorIndex = (std::min)(
                preparedDocument->geometry.FirstIntersecting(scrollOffset),
                frame.renderModel.blocks.size() - 1);
            auto anchorTop = preparedDocument->geometry.At(anchorIndex).top;
            auto geometryChanged = false;
            auto needsAnotherFrame = false;
            auto preparedThisFrame = std::size_t{0};
            auto deadline = std::chrono::steady_clock::now()
                + (printMode ? std::chrono::hours{24} : std::chrono::milliseconds{2});

            auto prepareIndex = [&](std::size_t index)
            {
                auto placement = preparedDocument->geometry.At(index);
                auto const& block = frame.renderModel.blocks[index];
                auto& prepared = preparedDocument->blocks[index];
                if (block.kind == elmd::RenderBlockKind::ThematicBreak || prepared.valid) return;
                auto previousHeight = prepared.height;
                prepared = prepareBlock(block, requestEmbeddedAt(placement.top));
                preparedDocument->layoutBlocks.insert(index);
                if (prepared.embeddedRequested && (prepared.containsMath || prepared.containsImage))
                    preparedDocument->embeddedBlocks.insert(index);
                else
                    preparedDocument->embeddedBlocks.erase(index);
                for (auto owner : prepared.owners)
                    preparedDocument->ownerBlockIndex[owner.v] = index;
                if (prepared.height != previousHeight)
                {
                    preparedDocument->geometry.UpdateHeight(index, prepared.height);
                    geometryChanged = true;
                }
                ++preparedThisFrame;
            };
            auto withinBudget = [&]
            {
                return printMode || preparedThisFrame == 0
                    || std::chrono::steady_clock::now() < deadline;
            };
            auto prepareForward = [&](std::size_t begin, std::size_t end)
            {
                constexpr std::size_t materializationBatch = 16;
                for (auto cursor = begin; cursor < end;)
                {
                    if (!withinBudget()) { needsAnotherFrame = true; break; }
                    auto batchEnd = (std::min)(end, cursor + materializationBatch);
                    if (frame.materializeBlocks) frame.materializeBlocks(cursor, batchEnd);
                    for (; cursor < batchEnd; ++cursor)
                    {
                        prepareIndex(cursor);
                        if (!withinBudget() && cursor + 1 < end)
                        {
                            ++cursor;
                            needsAnotherFrame = true;
                            break;
                        }
                    }
                    if (needsAnotherFrame) break;
                }
            };
            auto prepareBackward = [&](std::size_t begin, std::size_t end)
            {
                constexpr std::size_t materializationBatch = 16;
                auto cursor = end;
                while (cursor > begin)
                {
                    if (!withinBudget()) { needsAnotherFrame = true; break; }
                    auto batchBegin = cursor > begin + materializationBatch
                        ? cursor - materializationBatch
                        : begin;
                    if (frame.materializeBlocks) frame.materializeBlocks(batchBegin, cursor);
                    while (cursor > batchBegin)
                    {
                        --cursor;
                        prepareIndex(cursor);
                        if (!withinBudget() && cursor > begin)
                        {
                            needsAnotherFrame = true;
                            break;
                        }
                    }
                    if (needsAnotherFrame) break;
                }
            };

            // PDF export is a streaming viewport. Screen rendering prepares
            // the visible band first, then one directional look-ahead band.
            // The deterministic planner is shared with platform tests; this
            // method only enforces the per-frame preparation budget.
            auto scrollingForward = !preparedDocument->hasLastViewportOffset
                || scrollOffset >= preparedDocument->lastViewportOffset;
            auto viewportPlan = BuildEditorViewportPlan(
                preparedDocument->geometry,
                scrollOffset,
                resources.surfaceHeightDip,
                printMode,
                scrollingForward,
                viewportPolicy);
            prepareForward(viewportPlan.visible.begin, viewportPlan.visible.end);
            if (!printMode && !needsAnotherFrame && !viewportPlan.prefetch.Empty())
            {
                if (scrollingForward)
                    prepareForward(viewportPlan.prefetch.begin, viewportPlan.prefetch.end);
                else
                    prepareBackward(viewportPlan.prefetch.begin, viewportPlan.prefetch.end);
            }
            if (!printMode)
            {
                preparedDocument->lastViewportOffset = scrollOffset;
                preparedDocument->hasLastViewportOffset = true;
            }

            if (geometryChanged)
            {
                preparedDocument->totalHeight = preparedDocument->geometry.TotalHeight();
                if (!printMode && anchorIndex < preparedDocument->geometry.Size())
                {
                    auto shift = preparedDocument->geometry.At(anchorIndex).top - anchorTop;
                    if (shift != 0.0f)
                    {
                        scrollState.Shift(
                            shift,
                            (std::numeric_limits<float>::max)());
                        scrollOffset = scrollState.Offset();
                    }
                }
                needsAnotherFrame = !printMode;
            }
            if (needsAnotherFrame) Invalidate();
        }

        auto embeddedPlan = BuildEditorViewportPlan(
            preparedDocument->geometry,
            scrollOffset,
            resources.surfaceHeightDip,
            printMode,
            true,
            viewportPolicy);
        auto embeddedBegin = embeddedPlan.embedded.begin;
        auto embeddedEnd = embeddedPlan.embedded.end;
        auto geometryChanged = false;
        for (auto index = embeddedBegin; index < embeddedEnd; ++index)
        {
            auto const& block = frame.renderModel.blocks[index];
            if (block.kind == elmd::RenderBlockKind::ThematicBreak) continue;
            auto& prepared = preparedDocument->blocks[index];
            if (!prepared.valid) continue;
            auto refreshForMath = prepared.pendingMath
                && prepared.embeddedRequested
                && prepared.embeddedGeneration != embeddedGeneration;
            auto refreshForImages = prepared.containsImage
                && prepared.embeddedRequested
                && (prepared.remoteImageGeneration != remoteImageGeneration
                    || (prepared.pendingImage && prepared.embeddedGeneration != embeddedGeneration));
            auto enteredEmbeddedBand = !prepared.embeddedRequested
                && (prepared.containsMath || prepared.containsImage);
            if (!refreshForMath && !refreshForImages && !enteredEmbeddedBand) continue;
            auto previousHeight = prepared.height;
            prepared = prepareBlock(block, true);
            preparedDocument->layoutBlocks.insert(index);
            preparedDocument->embeddedBlocks.insert(index);
            if (prepared.height != previousHeight)
            {
                preparedDocument->geometry.UpdateHeight(index, prepared.height);
                geometryChanged = true;
            }
        }

        auto unloadTop = embeddedPlan.embeddedKeepTop;
        auto unloadBottom = embeddedPlan.embeddedKeepBottom;
        auto activeEmbedded = std::vector<std::size_t>(
            preparedDocument->embeddedBlocks.begin(),
            preparedDocument->embeddedBlocks.end());
        for (auto index : activeEmbedded)
        {
            if (printMode) break;
            if (index >= frame.renderModel.blocks.size()) continue;
            auto placement = preparedDocument->geometry.At(index);
            if (placement.bottom >= unloadTop && placement.top <= unloadBottom) continue;
            auto const& block = frame.renderModel.blocks[index];
            auto& prepared = preparedDocument->blocks[index];
            std::vector<std::string> imageSources;
            imageSources.reserve(prepared.images.size());
            for (auto const& image : prepared.images)
                if (!image.source.empty()) imageSources.push_back(image.source);
            if (prepared.table)
            {
                for (auto const& cellImages : prepared.table->imageDraws)
                    for (auto const& image : cellImages)
                        if (!image.source.empty()) imageSources.push_back(image.source);
            }
            auto previousHeight = prepared.height;
            prepared = prepareBlock(block, false);
            preparedDocument->layoutBlocks.insert(index);
            preparedDocument->embeddedBlocks.erase(index);
            for (auto const& source : imageSources) inlineImages.ReleaseGif(source);
            if (prepared.height != previousHeight)
            {
                preparedDocument->geometry.UpdateHeight(index, prepared.height);
                geometryChanged = true;
            }
        }
        if (geometryChanged)
            preparedDocument->totalHeight = preparedDocument->geometry.TotalHeight();

        {
            // Screen rendering keeps a multi-viewport cache for responsive
            // reverse scrolling. Printing keeps only its page-local band.
            auto retentionPlan = BuildEditorViewportPlan(
                preparedDocument->geometry,
                scrollOffset,
                resources.surfaceHeightDip,
                printMode,
                true,
                viewportPolicy);
            auto retentionBegin = retentionPlan.retention.begin;
            auto retentionEnd = retentionPlan.retention.end;
            auto activeLayouts = std::vector<std::size_t>(
                preparedDocument->layoutBlocks.begin(),
                preparedDocument->layoutBlocks.end());
            for (auto index : activeLayouts)
            {
                if (index >= frame.renderModel.blocks.size()) continue;
                if (retentionPlan.retention.Contains(index)) continue;
                auto& prepared = preparedDocument->blocks[index];
                std::vector<std::string> imageSources;
                for (auto const& image : prepared.images)
                    if (!image.source.empty()) imageSources.push_back(image.source);
                if (prepared.table)
                {
                    for (auto const& cellImages : prepared.table->imageDraws)
                        for (auto const& image : cellImages)
                            if (!image.source.empty()) imageSources.push_back(image.source);
                }
                prepared.ReleaseVisualContent();
                for (auto const& source : imageSources) inlineImages.ReleaseGif(source);
                preparedDocument->embeddedBlocks.erase(index);
                preparedDocument->layoutBlocks.erase(index);
            }
            if (!printMode && frame.releaseBlocksOutside)
                frame.releaseBlocksOutside(retentionBegin, retentionEnd);
        }

        auto paintPlan = BuildEditorViewportPlan(
            preparedDocument->geometry,
            scrollOffset,
            resources.surfaceHeightDip,
            printMode,
            true,
            viewportPolicy);
        auto viewportBegin = paintPlan.visible.begin;
        auto viewportEnd = paintPlan.visible.end;
        if (sourceGutterWidth > 0.0f && resources.lineNumberBackgroundBrush)
        {
            resources.d2dContext->FillRectangle(
                D2D1::RectF(0.0f, 0.0f, sourceGutterWidth, resources.surfaceHeightDip),
                resources.lineNumberBackgroundBrush.Get());
        }
        for (auto blockIndex = viewportBegin; blockIndex < viewportEnd; ++blockIndex)
        {
            auto placement = preparedDocument->geometry.At(blockIndex);
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
                    selection,
                    documentLeft,
                    top,
                    scrollOffset,
                    resources,
                    styleSheet,
                    interactionMap,
                    inlineImages,
                    drawMath,
                    drawMathFallback,
                    nonInteractiveRegions);
                continue;
            }
            auto flowContainer = !block.inline_items.empty()
                && (block.kind == elmd::RenderBlockKind::Quote
                    || block.kind == elmd::RenderBlockKind::Callout);
            auto paddingTop = flowContainer ? 0.0f : block.block_style.padding_top;
            auto paddingLeft = flowContainer ? 0.0f : block.block_style.padding_left;
            auto paddingRight = flowContainer ? 0.0f : block.block_style.padding_right;
            auto contentWidth = (std::max)(1.0f, documentWidth - paddingLeft - paddingRight);
            auto origin = D2D1::Point2F(documentLeft + paddingLeft, top + paddingTop);
            auto rect = D2D1::RectF(documentLeft, top, documentRight, bottom);

            if (sourceGutterWidth > 0.0f
                && block.source_mode
                && block.source_line_number != 0
                && resources.lineNumberFormat
                && resources.lineNumberBrush)
            {
                auto number = std::to_wstring(block.source_line_number);
                resources.d2dContext->DrawTextW(
                    number.c_str(),
                    static_cast<UINT32>(number.size()),
                    resources.lineNumberFormat.Get(),
                    D2D1::RectF(
                        sourceGutterLeft,
                        origin.y,
                        sourceGutterWidth - 5.0f,
                        origin.y + styleSheet.code.lineHeight),
                    resources.lineNumberBrush.Get(),
                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            if (prepared.code || block.block_style.background) resources.d2dContext->FillRectangle(rect, resources.panelBrush.Get());
            documentPainter.DrawFlowDecorations(prepared.layout.Get(), origin, block, prepared.display);
            DrawInlinePresentationBackgrounds(
                resources,
                styleSheet,
                prepared.layout.Get(),
                origin,
                prepared.display.ranges);
            documentPainter.DrawSelection(prepared.layout.Get(), origin, prepared.display.displayToSource);
            documentPainter.DrawSearchHighlights(
                prepared.layout.Get(), origin, prepared.display.displayToSource);
            if (prepared.layout)
                resources.d2dContext->DrawTextLayout(
                    origin,
                    prepared.layout.Get(),
                    prepared.code ? resources.codeBrush.Get() : resources.textBrush.Get(),
                    D2D1_DRAW_TEXT_OPTIONS_CLIP);
            documentPainter.DrawTaskCheckboxes(prepared.layout.Get(), origin, prepared.display.taskCheckboxOverlays);
            documentPainter.RegisterFootnotes(prepared.layout.Get(), origin, prepared.display.footnoteOverlays);
            inlineImages.Draw(prepared.layout.Get(), origin, prepared.images);
            for (auto const& positioned : EditorDocumentPainter::PositionMath(
                    prepared.layout.Get(),
                    prepared.display.mathOverlays,
                    contentWidth))
            {
                auto const& overlay = *positioned.overlay;
                auto mathOrigin = D2D1::Point2F(
                    origin.x + positioned.localX,
                    origin.y + positioned.localTop);
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
                    documentLeft + paddingLeft + contentWidth,
                    previewTop + preview.height + 16.0f);
                resources.d2dContext->FillRectangle(previewRect, resources.nestedQuoteBrush.Get());
                auto previewOrigin = D2D1::Point2F(previewRect.left + 8.0f, previewRect.top + 8.0f);
                if (preview.layout)
                    resources.d2dContext->DrawTextLayout(
                        previewOrigin,
                        preview.layout.Get(),
                        resources.textBrush.Get(),
                        D2D1_DRAW_TEXT_OPTIONS_CLIP);
                auto previewContentWidth = (std::max)(1.0f, previewRect.right - previewRect.left - 16.0f);
                for (auto const& positioned : EditorDocumentPainter::PositionMath(
                        preview.layout.Get(),
                        preview.display.mathOverlays,
                        previewContentWidth))
                {
                    auto const& overlay = *positioned.overlay;
                    auto mathOrigin = D2D1::Point2F(
                        previewOrigin.x + positioned.localX,
                        previewOrigin.y + positioned.localTop);
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
            visual.textWidth = contentWidth;
            visual.sourceSpan = block.source_span;
            visual.documentY = documentY;
            visual.text = prepared.display.text;
            visual.displayToSource = prepared.display.displayToSource;
            visual.layout = prepared.layout;
            interactionMap.blocks.push_back(std::move(visual));
            interactionMap.AddBlockLines(interactionMap.blocks.size() - 1);
        }

        EditorTableInteraction::Paint(resources, interactionMap, printMode ? std::nullopt : pointerPosition, draggedTableAction, tableDropIndex);
        totalDocumentHeight = preparedDocument->totalHeight;
        scrollState.Clamp(MaximumScrollOffset());
        if (!printMode && selection.is_caret())
        {
            if (auto rect = CaretBounds(caret))
                resources.d2dContext->DrawLine(D2D1::Point2F(rect->left, rect->top), D2D1::Point2F(rect->left, rect->bottom), resources.caretBrush.Get(), 1.5f);
        }
    }

}
