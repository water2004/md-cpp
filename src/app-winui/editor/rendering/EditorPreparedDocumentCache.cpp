#include "pch.h"
#include "editor/rendering/EditorPreparedDocumentCache.h"

import folia.platform.editor_preparation_plan;

namespace winrt::Folia
{
    using folia::platform::editor::BuildEditorPreparationInvalidationPlan;
    using folia::platform::editor::EditorBlockGeometryIndex;
    using folia::platform::editor::EditorPreparationCacheView;

    EditorPreparedDocumentCache::EditorPreparedDocumentCache(
        detail::EditorRenderFrame const& valueFrame,
        EditorStyleSheet const& valueStyleSheet,
        EditorInlineImageRenderer& valueInlineImages,
        EditorDocumentBlockPreparer& valueBlockPreparer,
        std::unique_ptr<EditorPreparedDocument>& valuePreparedDocument)
        : frame(valueFrame),
          styleSheet(valueStyleSheet),
          inlineImages(valueInlineImages),
          blockPreparer(valueBlockPreparer),
          preparedDocument(valuePreparedDocument)
    {
    }

    std::optional<std::size_t> EditorPreparedDocumentCache::FindOwnerBlock(
        folia::NodeId owner) const
    {
        if (!preparedDocument) return std::nullopt;
        auto found = preparedDocument->ownerBlockIndex.find(owner.v);
        if (found == preparedDocument->ownerBlockIndex.end()) return std::nullopt;
        return found->second;
    }

    void EditorPreparedDocumentCache::Reconcile(
        folia::TextSelection selection,
        float documentWidth,
        std::uint64_t themeRevision)
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

    void EditorPreparedDocumentCache::EnsureGeometry()
    {
        if (!preparedDocument->geometry.Initialized()) InitializeGeometry();
    }

    void EditorPreparedDocumentCache::InitializeGeometry()
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
}
