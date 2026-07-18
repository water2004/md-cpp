#include "pch.h"
#include "editor/rendering/EditorDocumentPreparationPass.h"

namespace winrt::Folia
{
    using folia::platform::editor::BuildEditorPrioritizedTraversal;
    using folia::platform::editor::BuildEditorViewportPlan;
    using folia::platform::editor::EditorViewportMoved;

    EditorDocumentPreparationPass::EditorDocumentPreparationPass(
        detail::EditorRenderFrame const& valueFrame,
        EditorRenderResources& valueResources,
        EditorStyleSheet const& valueStyleSheet,
        EditorInlineImageRenderer& valueInlineImages,
        MathJaxRenderer& valueMathJax,
        SvgNormalizer& valueSvgNormalizer,
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
        : cache(
              valueFrame,
              valueStyleSheet,
              valueInlineImages,
              valueBlockPreparer,
              valuePreparedDocument),
          frame(valueFrame),
          resources(valueResources),
          inlineImages(valueInlineImages),
          mathJax(valueMathJax),
          svgNormalizer(valueSvgNormalizer),
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

    bool EditorDocumentPreparationPass::RequestEmbeddedAt(float documentTop) const
    {
        if (printMode) return true;
        auto screenTop = documentTop - scrollOffset;
        return screenTop < resources.surfaceHeightDip + viewportPolicy.embeddedAfter
            && screenTop > -viewportPolicy.embeddedBefore;
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
        viewportActive = viewportMoved || scrollState.Animating();
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
            && !viewportActive
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
            auto refreshForMath = false;
            if (prepared.pendingMath
                && prepared.embeddedRequested
                && prepared.dependencyCheckGeneration != embeddedGeneration)
            {
                refreshForMath = mathJax.AnyCompletedAfter(
                        prepared.pendingMathJaxDependencies)
                    || svgNormalizer.AnyGroupCompletedAfter(
                        prepared.pendingSvgDependencyGroups);
                // A completion elsewhere in the document must not make this
                // block reacquire both worker locks on every animation frame.
                // If none of its own dependencies completed, this generation
                // has been fully observed and can be skipped until the next
                // coalesced completion callback advances it again.
                if (!refreshForMath)
                    prepared.dependencyCheckGeneration = embeddedGeneration;
            }
            auto refreshForImages = prepared.containsImage
                && prepared.embeddedRequested
                && (prepared.remoteImageGeneration != remoteImageGeneration
                    || (prepared.pendingImage
                        && prepared.embeddedGeneration != embeddedGeneration));
            auto highPriority = index >= embeddedPlan.visible.begin
                && index < embeddedPlan.visible.end;
            auto enteredEmbeddedBand = !prepared.embeddedRequested
                && (prepared.containsMath || prepared.containsImage)
                && (!viewportActive || highPriority);
            if (!refreshForMath && !refreshForImages && !enteredEmbeddedBand) return true;
            if (!withinBudget())
            {
                workRemaining = true;
                return false;
            }
            auto previousHeight = prepared.height;
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
        cache.Reconcile(selection, documentWidth, themeRevision);
        cache.EnsureGeometry();
        PrepareViewport();
        RefreshEmbeddedContent();
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
