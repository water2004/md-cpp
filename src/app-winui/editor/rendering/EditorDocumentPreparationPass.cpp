#include "pch.h"
#include "editor/rendering/EditorDocumentPreparationPass.h"

import folia.platform.editor_preparation_plan;

namespace winrt::Folia
{
    using folia::platform::editor::BuildEditorPreparationInvalidationPlan;
    using folia::platform::editor::BuildEditorPrioritizedTraversal;
    using folia::platform::editor::BuildEditorViewportPlan;
    using folia::platform::editor::EditorViewportMoved;
    using folia::platform::editor::EditorBlockGeometryIndex;
    using folia::platform::editor::EditorPreparationCacheView;

    EditorDocumentPreparationPass::EditorDocumentPreparationPass(
        detail::EditorRenderFrame const& valueFrame,
        EditorRenderResources& valueResources,
        EditorStyleSheet const& valueStyleSheet,
        EditorInlineImageRenderer& valueInlineImages,
        EditorSvgPainter& valueSvgPainter,
        EditorDocumentBlockPreparer& valueBlockPreparer,
        folia::platform::editor::EditorScrollState& valueScrollState,
        std::unique_ptr<EditorPreparedDocument>& valuePreparedDocument,
        folia::TextSelection valueSelection,
        float valueDocumentWidth,
        std::uint64_t valueThemeRevision,
        std::uint64_t valueEmbeddedGeneration,
        std::uint64_t valueRemoteImageGeneration,
        float valueScrollOffset,
        bool valuePrintMode)
        : frame(valueFrame),
          resources(valueResources),
          styleSheet(valueStyleSheet),
          inlineImages(valueInlineImages),
          svgPainter(valueSvgPainter),
          blockPreparer(valueBlockPreparer),
          scrollState(valueScrollState),
          preparedDocument(valuePreparedDocument),
          selection(valueSelection),
          documentWidth(valueDocumentWidth),
          themeRevision(valueThemeRevision),
          embeddedGeneration(valueEmbeddedGeneration),
          remoteImageGeneration(valueRemoteImageGeneration),
          scrollOffset(valueScrollOffset),
          printMode(valuePrintMode)
    {
    }

    std::optional<std::size_t> EditorDocumentPreparationPass::FindOwnerBlock(
        folia::NodeId owner) const
    {
        if (!preparedDocument) return std::nullopt;
        auto found = preparedDocument->ownerBlockIndex.find(owner.v);
        if (found == preparedDocument->ownerBlockIndex.end()) return std::nullopt;
        return found->second;
    }

    void EditorDocumentPreparationPass::ReconcileCache()
    {
        EditorPreparationCacheView cache;
        std::optional<std::size_t> previousActiveBlock;
        std::optional<std::size_t> activeBlock;
        if (preparedDocument)
        {
            cache = {
                .available = true,
                .blockCount = preparedDocument->blocks.size(),
                .modelRevision = preparedDocument->modelRevision,
                .documentWidth = preparedDocument->documentWidth,
                .themeRevision = preparedDocument->themeRevision,
                .active = preparedDocument->selection.active,
            };
            previousActiveBlock = FindOwnerBlock(
                preparedDocument->selection.active.container_id);
            activeBlock = FindOwnerBlock(selection.active.container_id);
        }
        auto sourceMode = !frame.renderModel.blocks.empty()
            && frame.renderModel.blocks.front().source_mode;
        auto invalidationPlan = BuildEditorPreparationInvalidationPlan(
            cache,
            frame.renderModel.blocks.size(),
            frame.renderModel.revision,
            documentWidth,
            themeRevision,
            selection.active,
            frame.renderModel.incremental_update,
            sourceMode,
            frame.renderModel.changed_block_indices,
            previousActiveBlock,
            activeBlock);

        if (invalidationPlan.rebuildAll)
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
                    if (!source.source_mode
                        && invalidationPlan.activePositionChanged
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
                    if (found != preparedDocument->ownerBlockIndex.end()
                        && found->second == index)
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
                for (auto const& imageSource : imageSources)
                    inlineImages.ReleaseGif(imageSource);
            }
            if (invalidationPlan.modelChanged)
                preparedDocument->modelRevision = frame.renderModel.revision;
            if (preparedDocument->geometry.Initialized())
                preparedDocument->totalHeight = preparedDocument->geometry.TotalHeight();
        }
        preparedDocument->selection = selection;
    }

    bool EditorDocumentPreparationPass::RequestEmbeddedAt(float documentTop) const
    {
        if (printMode) return true;
        auto screenTop = documentTop - scrollOffset;
        return screenTop < resources.surfaceHeightDip + viewportPolicy.embeddedAfter
            && screenTop > -viewportPolicy.embeddedBefore;
    }

    void EditorDocumentPreparationPass::InitializeGeometry()
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
            if (found == topLevelBlockIndex.end() || found->second >= entries.size()) continue;
            preparedDocument->ownerBlockIndex[owner] = found->second;
        }
        preparedDocument->geometry.Reset(std::move(entries), styleSheet.verticalPadding);
        preparedDocument->totalHeight = preparedDocument->geometry.TotalHeight();
    }

    void EditorDocumentPreparationPass::PrepareViewport()
    {
        if (frame.renderModel.blocks.empty()) return;

        auto anchorIndex = (std::min)(
            preparedDocument->geometry.FirstIntersecting(scrollOffset),
            frame.renderModel.blocks.size() - 1);
        auto anchorTop = preparedDocument->geometry.At(anchorIndex).top;
        auto geometryChanged = false;
        auto needsAnotherFrame = false;
        auto preparedThisFrame = std::size_t{0};
        auto deadline = std::chrono::steady_clock::now()
            + (printMode ? std::chrono::hours{24} : std::chrono::milliseconds{2});

        auto prepareIndex = [&](std::size_t index, bool highPriority)
        {
            auto placement = preparedDocument->geometry.At(index);
            auto const& block = frame.renderModel.blocks[index];
            auto& prepared = preparedDocument->blocks[index];
            if (block.kind == folia::RenderBlockKind::ThematicBreak || prepared.valid) return;
            auto previousHeight = prepared.height;
            prepared = blockPreparer.Prepare(
                block,
                RequestEmbeddedAt(placement.top),
                highPriority);
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
        auto prepareForward = [&](std::size_t begin, std::size_t end, bool highPriority)
        {
            constexpr std::size_t materializationBatch = 16;
            for (auto cursor = begin; cursor < end;)
            {
                if (!withinBudget()) { needsAnotherFrame = true; break; }
                auto batchEnd = (std::min)(end, cursor + materializationBatch);
                if (frame.materializeBlocks) frame.materializeBlocks(cursor, batchEnd);
                for (; cursor < batchEnd; ++cursor)
                {
                    prepareIndex(cursor, highPriority);
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
        auto prepareBackward = [&](std::size_t begin, std::size_t end, bool highPriority)
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
                    prepareIndex(cursor, highPriority);
                    if (!withinBudget() && cursor > begin)
                    {
                        needsAnotherFrame = true;
                        break;
                    }
                }
                if (needsAnotherFrame) break;
            }
        };

        viewportMoved = EditorViewportMoved(
            preparedDocument->hasLastViewportOffset,
            preparedDocument->lastViewportOffset,
            scrollOffset);
        scrollingForward = !preparedDocument->hasLastViewportOffset
            || scrollOffset >= preparedDocument->lastViewportOffset;
        auto viewportPlan = BuildEditorViewportPlan(
            preparedDocument->geometry,
            scrollOffset,
            resources.surfaceHeightDip,
            printMode,
            scrollingForward,
            viewportPolicy);
        prepareForward(viewportPlan.visible.begin, viewportPlan.visible.end, true);
        // Moving frames spend their budget on the new visible range only.
        // A stable follow-up frame fills the speculative band; otherwise a
        // fast scroll can continuously feed obsolete MathJax/SVG work even
        // though visible requests are correctly prioritized.
        if (!printMode
            && !viewportMoved
            && !needsAnotherFrame
            && !viewportPlan.prefetch.Empty())
        {
            if (scrollingForward)
                prepareForward(viewportPlan.prefetch.begin, viewportPlan.prefetch.end, false);
            else
                prepareBackward(viewportPlan.prefetch.begin, viewportPlan.prefetch.end, false);
        }
        if (!printMode)
        {
            preparedDocument->lastViewportOffset = scrollOffset;
            preparedDocument->hasLastViewportOffset = true;
            // Direct scrollbar/touchpad movement may not have an animation
            // tick after its final offset. Schedule one stable frame so
            // deferred embedded completions and prefetch cannot be stranded.
            needsAnotherFrame = needsAnotherFrame || viewportMoved;
        }

        if (geometryChanged)
        {
            preparedDocument->totalHeight = preparedDocument->geometry.TotalHeight();
            if (!printMode && anchorIndex < preparedDocument->geometry.Size())
            {
                auto shift = preparedDocument->geometry.At(anchorIndex).top - anchorTop;
                if (shift != 0.0f)
                {
                    scrollState.Shift(shift, (std::numeric_limits<float>::max)());
                    scrollOffset = scrollState.Offset();
                }
            }
            needsAnotherFrame = !printMode;
        }
        invalidateRequested = invalidateRequested || needsAnotherFrame;
    }

    void EditorDocumentPreparationPass::RefreshEmbeddedContent()
    {
        auto embeddedPlan = BuildEditorViewportPlan(
            preparedDocument->geometry,
            scrollOffset,
            resources.surfaceHeightDip,
            printMode,
            true,
            viewportPolicy);
        auto geometryChanged = false;
        auto deadline = std::chrono::steady_clock::now()
            + (printMode ? std::chrono::hours{24} : std::chrono::milliseconds{1});
        constexpr std::size_t maximumRefreshes = 4;
        auto refreshes = std::size_t{0};
        auto workRemaining = false;
        auto withinBudget = [&]
        {
            return printMode || refreshes == 0
                || (refreshes < maximumRefreshes
                    && std::chrono::steady_clock::now() < deadline);
        };
        auto refreshIndex = [&](std::size_t index)
        {
            auto const& block = frame.renderModel.blocks[index];
            if (block.kind == folia::RenderBlockKind::ThematicBreak) return true;
            auto& prepared = preparedDocument->blocks[index];
            if (!prepared.valid) return true;
            auto refreshForMath = prepared.pendingMath
                && prepared.embeddedRequested
                && prepared.embeddedGeneration != embeddedGeneration;
            auto refreshForImages = prepared.containsImage
                && prepared.embeddedRequested
                && (prepared.remoteImageGeneration != remoteImageGeneration
                    || (prepared.pendingImage
                        && prepared.embeddedGeneration != embeddedGeneration));
            auto enteredEmbeddedBand = !prepared.embeddedRequested
                && (prepared.containsMath || prepared.containsImage);
            if (!refreshForMath && !refreshForImages && !enteredEmbeddedBand) return true;
            if (!withinBudget())
            {
                workRemaining = true;
                return false;
            }
            auto previousHeight = prepared.height;
            auto highPriority = index >= embeddedPlan.visible.begin
                && index < embeddedPlan.visible.end;
            prepared = blockPreparer.Prepare(block, true, highPriority);
            ++refreshes;
            preparedDocument->layoutBlocks.insert(index);
            preparedDocument->embeddedBlocks.insert(index);
            if (prepared.height != previousHeight)
            {
                preparedDocument->geometry.UpdateHeight(index, prepared.height);
                geometryChanged = true;
            }
            return true;
        };
        auto traversal = BuildEditorPrioritizedTraversal(
            embeddedPlan.visible,
            embeddedPlan.embedded,
            scrollingForward);
        for (std::size_t segmentIndex = 0;
             segmentIndex < traversal.count && !workRemaining;
             ++segmentIndex)
        {
            auto const& segment = traversal.segments[segmentIndex];
            if (segment.reverse)
            {
                for (auto index = segment.range.end; index > segment.range.begin;)
                {
                    --index;
                    if (!refreshIndex(index)) break;
                }
            }
            else
            {
                for (auto index = segment.range.begin; index < segment.range.end; ++index)
                    if (!refreshIndex(index)) break;
            }
        }

        auto activeEmbedded = std::vector<std::size_t>(
            preparedDocument->embeddedBlocks.begin(),
            preparedDocument->embeddedBlocks.end());
        for (auto index : activeEmbedded)
        {
            if (printMode) break;
            if (index >= frame.renderModel.blocks.size()) continue;
            auto placement = preparedDocument->geometry.At(index);
            if (placement.bottom >= embeddedPlan.embeddedKeepTop
                && placement.top <= embeddedPlan.embeddedKeepBottom) continue;
            auto const& block = frame.renderModel.blocks[index];
            auto& prepared = preparedDocument->blocks[index];
            auto imageSources = EditorDocumentBlockPreparer::ImageSources(prepared);
            auto previousHeight = prepared.height;
            prepared = blockPreparer.Prepare(block, false, false);
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
        invalidateRequested = invalidateRequested || workRemaining;
    }

    void EditorDocumentPreparationPass::MaterializeVisibleMathDocuments()
    {
        if (printMode || !svgPainter.Supported()) return;

        // MathJax and SVG normalization are background work. ID2D1SvgDocument
        // creation is device-context work and therefore remains on the UI
        // thread, but never in the paint loop and never while the viewport is
        // moving. A bounded stable-viewport slice makes formulas appear
        // progressively without turning a dense math screen into one long
        // presentation frame.
        auto plan = BuildEditorViewportPlan(
            preparedDocument->geometry,
            scrollOffset,
            resources.surfaceHeightDip,
            false,
            scrollingForward,
            viewportPolicy);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{1};
        // Small MathJax line-break fragments are cheap individually. The time
        // budget is the primary frame guard; a larger count cap avoids making
        // a many-fragment formula wait through several otherwise idle frames.
        constexpr std::size_t maximumCreations = 16;
        auto creations = std::size_t{0};
        auto workRemaining = false;

        auto materializeDisplay = [&](DisplayInlineText const& display)
        {
            for (auto const& overlay : display.mathOverlays)
            {
                auto const& fragment = overlay.fragment;
                if (fragment.renderId == 0 || !fragment.svg || fragment.svg->empty()
                    || fragment.width <= 0.0f || fragment.height <= 0.0f) continue;
                if (svgPainter.Prepared(fragment.renderId)) continue;
                if (viewportMoved
                    || creations >= maximumCreations
                    || (creations != 0 && std::chrono::steady_clock::now() >= deadline))
                {
                    workRemaining = true;
                    return false;
                }
                svgPainter.Prepare(
                    fragment.renderId,
                    *fragment.svg,
                    fragment.width,
                    fragment.height);
                ++creations;
            }
            return true;
        };
        auto materializeBlock = [&](EditorPreparedDocument::Block const& block)
        {
            if (!materializeDisplay(block.display)) return false;
            for (auto const& preview : block.mathPreviews)
                if (!materializeDisplay(preview.display)) return false;
            if (!block.table) return true;
            for (auto const& display : block.table->displays)
                if (!materializeDisplay(display)) return false;
            for (auto const& cellPreviews : block.table->mathPreviews)
                for (auto const& preview : cellPreviews)
                    if (!materializeDisplay(preview.display)) return false;
            return true;
        };

        for (auto index = plan.visible.begin; index < plan.visible.end; ++index)
        {
            if (index >= preparedDocument->blocks.size()) break;
            auto const& block = preparedDocument->blocks[index];
            if (!block.valid || !block.containsMath) continue;
            if (!materializeBlock(block)) break;
        }
        invalidateRequested = invalidateRequested || workRemaining;
    }

    void EditorDocumentPreparationPass::ReleaseOutsideRetention()
    {
        auto retentionPlan = BuildEditorViewportPlan(
            preparedDocument->geometry,
            scrollOffset,
            resources.surfaceHeightDip,
            printMode,
            true,
            viewportPolicy);
        auto activeLayouts = std::vector<std::size_t>(
            preparedDocument->layoutBlocks.begin(),
            preparedDocument->layoutBlocks.end());
        for (auto index : activeLayouts)
        {
            if (index >= frame.renderModel.blocks.size()) continue;
            if (retentionPlan.retention.Contains(index)) continue;
            auto& prepared = preparedDocument->blocks[index];
            auto imageSources = EditorDocumentBlockPreparer::ImageSources(prepared);
            prepared.ReleaseVisualContent();
            for (auto const& source : imageSources) inlineImages.ReleaseGif(source);
            preparedDocument->embeddedBlocks.erase(index);
            preparedDocument->layoutBlocks.erase(index);
        }
        if (!printMode && frame.releaseBlocksOutside)
            frame.releaseBlocksOutside(
                retentionPlan.retention.begin,
                retentionPlan.retention.end);
    }

    EditorDocumentPreparationResult EditorDocumentPreparationPass::Prepare()
    {
        ReconcileCache();
        if (!preparedDocument->geometry.Initialized()) InitializeGeometry();
        PrepareViewport();
        RefreshEmbeddedContent();
        MaterializeVisibleMathDocuments();
        ReleaseOutsideRetention();

        auto paintPlan = BuildEditorViewportPlan(
            preparedDocument->geometry,
            scrollOffset,
            resources.surfaceHeightDip,
            printMode,
            true,
            viewportPolicy);
        return {
            .scrollOffset = scrollOffset,
            .visible = paintPlan.visible,
            .invalidateRequested = invalidateRequested,
        };
    }
}
