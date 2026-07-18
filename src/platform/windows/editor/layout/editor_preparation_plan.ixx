// folia.platform.editor_preparation_plan — pure prepared-cache invalidation policy.
export module folia.platform.editor_preparation_plan;
import std;
import folia.core.text_edit;

export namespace folia::platform::editor
{
    struct EditorPreparationCacheView
    {
        bool available = false;
        std::size_t blockCount = 0;
        std::uint64_t modelRevision = 0;
        float documentWidth = 0.0f;
        std::uint64_t themeRevision = 0;
        folia::TextPosition active{};
    };

    struct EditorPreparationInvalidationPlan
    {
        bool rebuildAll = false;
        bool modelChanged = false;
        bool activePositionChanged = false;
        std::vector<std::size_t> invalidatedBlocks;
    };

    inline EditorPreparationInvalidationPlan BuildEditorPreparationInvalidationPlan(
        EditorPreparationCacheView const& cache,
        std::size_t blockCount,
        std::uint64_t modelRevision,
        float documentWidth,
        std::uint64_t themeRevision,
        folia::TextPosition active,
        bool incrementalUpdate,
        bool sourceMode,
        std::span<std::size_t const> changedBlockIndices,
        std::optional<std::size_t> previousActiveBlock,
        std::optional<std::size_t> activeBlock)
    {
        EditorPreparationInvalidationPlan plan;
        plan.modelChanged = cache.available && cache.modelRevision != modelRevision;
        plan.activePositionChanged = cache.available && cache.active != active;
        plan.rebuildAll = !cache.available
            || cache.documentWidth != documentWidth
            || cache.themeRevision != themeRevision
            || cache.blockCount != blockCount
            || (plan.modelChanged && !incrementalUpdate);
        if (plan.rebuildAll) return plan;

        plan.invalidatedBlocks.reserve(
            (plan.modelChanged ? changedBlockIndices.size() : 0)
            + (plan.activePositionChanged && !sourceMode ? 2 : 0));
        if (plan.modelChanged)
        {
            for (auto index : changedBlockIndices)
                if (index < blockCount) plan.invalidatedBlocks.push_back(index);
        }
        if (plan.activePositionChanged && !sourceMode)
        {
            if (previousActiveBlock && *previousActiveBlock < blockCount)
                plan.invalidatedBlocks.push_back(*previousActiveBlock);
            if (activeBlock && *activeBlock < blockCount)
                plan.invalidatedBlocks.push_back(*activeBlock);
        }
        std::ranges::sort(plan.invalidatedBlocks);
        auto uniqueEnd = std::ranges::unique(plan.invalidatedBlocks).begin();
        plan.invalidatedBlocks.erase(uniqueEnd, plan.invalidatedBlocks.end());
        return plan;
    }
}
