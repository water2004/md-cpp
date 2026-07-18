#include "pch.h"
#include "editor/rendering/EditorSurfaceRenderer.h"
#include "editor/rendering/EditorPreparedDocument.h"
#include "localization/Localization.h"

import folia.core.render_model;
import folia.platform.editor_preparation_plan;
import folia.platform.editor_viewport_plan;

#include "editor/rendering/EditorContentPreparation.h"
#include "editor/rendering/EditorDocumentBlockPreparer.h"
#include "editor/rendering/EditorDocumentPainter.h"
#include "editor/rendering/EditorDocumentRenderPass.h"
#include "editor/rendering/EditorInlineImageRenderer.h"
#include "editor/rendering/EditorSvgPainter.h"
#include "editor/rendering/EditorTableBlockRenderer.h"
#include "editor/rendering/EditorTextLayoutEngine.h"

namespace winrt::Folia
{
    using folia::platform::editor::BuildEditorViewportPlan;
    using folia::platform::editor::EditorViewportPolicy;

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
        auto selection = printMode ? folia::TextSelection{} : frame.selection;
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
        auto drawMathFallback = [&](folia::TextSpan, D2D1_POINT_2F origin) {
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
        folia::platform::editor::EditorPreparationCacheView preparationCache;
        std::optional<std::size_t> previousActiveBlock;
        std::optional<std::size_t> activeBlock;
        if (preparedDocument)
        {
            preparationCache = {
                .available = true,
                .blockCount = preparedDocument->blocks.size(),
                .modelRevision = preparedDocument->modelRevision,
                .documentWidth = preparedDocument->documentWidth,
                .themeRevision = preparedDocument->themeRevision,
                .active = preparedDocument->selection.active,
            };
            auto findOwnerBlock = [&](folia::NodeId owner) -> std::optional<std::size_t>
            {
                auto found = preparedDocument->ownerBlockIndex.find(owner.v);
                if (found == preparedDocument->ownerBlockIndex.end()) return std::nullopt;
                return found->second;
            };
            previousActiveBlock = findOwnerBlock(preparedDocument->selection.active.container_id);
            activeBlock = findOwnerBlock(selection.active.container_id);
        }
        auto invalidationPlan = folia::platform::editor::BuildEditorPreparationInvalidationPlan(
            preparationCache,
            frame.renderModel.blocks.size(),
            frame.renderModel.revision,
            documentWidth,
            themeRevision,
            selection.active,
            frame.renderModel.incremental_update,
            sourceDocument,
            frame.renderModel.changed_block_indices,
            previousActiveBlock,
            activeBlock);
        EditorDocumentBlockPreparer blockPreparer(
            frame,
            resources,
            renderCache,
            styleSheet,
            textLayoutEngine,
            inlineImages,
            documentPainter,
            mathJax,
            svgNormalizer,
            treeSitter,
            caret,
            documentWidth,
            mathSvgSupported,
            embeddedGeneration,
            remoteImageGeneration);
        auto activePositionChanged = invalidationPlan.activePositionChanged;
        auto modelChanged = invalidationPlan.modelChanged;
        auto rebuildAll = invalidationPlan.rebuildAll;
        if (rebuildAll)
        {
            auto previous = std::move(preparedDocument);
            preparedDocument = std::make_unique<EditorPreparedDocument>();
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
                auto owns = [](EditorPreparedDocument::Block const& block, folia::NodeId owner)
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
                        && source.kind != folia::RenderBlockKind::ThematicBreak)
                        preparedDocument->layoutBlocks.insert(index);
                }
            }
        }
        else
        {
            for (auto index : invalidationPlan.invalidatedBlocks)
            {
                auto const& source = frame.renderModel.blocks[index];
                auto& prepared = preparedDocument->blocks[index];
                auto imageSources = EditorDocumentBlockPreparer::ImageSources(prepared);
                auto previousHeight = prepared.height > 0.0f
                    ? prepared.height
                    : blockPreparer.EstimateHeight(source);
                for (auto owner : prepared.owners)
                {
                    auto found = preparedDocument->ownerBlockIndex.find(owner.v);
                    if (found != preparedDocument->ownerBlockIndex.end() && found->second == index)
                        preparedDocument->ownerBlockIndex.erase(found);
                }
                prepared = {};
                blockPreparer.InitializeMetadata(prepared, source);
                if (source.kind == folia::RenderBlockKind::ThematicBreak)
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
                if (block.kind == folia::RenderBlockKind::ThematicBreak)
                {
                    prepared = {};
                    blockPreparer.InitializeMetadata(prepared, block);
                    prepared.height = 40.0f;
                    prepared.valid = true;
                }
                else if (!prepared.valid && prepared.height <= 0.0f)
                {
                    blockPreparer.InitializeMetadata(prepared, block);
                    prepared.height = blockPreparer.EstimateHeight(block);
                }
                else if (prepared.sourceId.v == 0)
                {
                    blockPreparer.InitializeMetadata(prepared, block);
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
                if (block.kind == folia::RenderBlockKind::ThematicBreak || prepared.valid) return;
                auto previousHeight = prepared.height;
                prepared = blockPreparer.Prepare(block, requestEmbeddedAt(placement.top));
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
            if (block.kind == folia::RenderBlockKind::ThematicBreak) continue;
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
            prepared = blockPreparer.Prepare(block, true);
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
            prepared = blockPreparer.Prepare(block, false);
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
        EditorDocumentRenderPass renderPass(
            resources,
            styleSheet,
            interactionMap,
            nonInteractiveRegions,
            inlineImages,
            documentPainter,
            drawMath,
            drawMathFallback);
        renderPass.Paint(
            frame,
            *preparedDocument,
            selection,
            caret,
            documentLeft,
            documentRight,
            documentWidth,
            sourceGutterLeft,
            sourceGutterWidth,
            scrollOffset,
            viewportBegin,
            viewportEnd,
            printMode,
            pointerPosition,
            draggedTableAction,
            tableDropIndex);
        totalDocumentHeight = preparedDocument->totalHeight;
        scrollState.Clamp(MaximumScrollOffset());
    }

}
