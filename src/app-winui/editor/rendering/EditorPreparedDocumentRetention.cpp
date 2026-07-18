#include "pch.h"
#include "editor/rendering/EditorPreparedDocumentRetention.h"
#include "editor/rendering/EditorDocumentBlockPreparer.h"

namespace winrt::Folia
{
    using folia::platform::editor::BuildEditorViewportPlan;

    EditorPreparedDocumentRetention::EditorPreparedDocumentRetention(
        detail::EditorRenderFrame const& valueFrame,
        EditorRenderResources& valueResources,
        EditorInlineImageRenderer& valueInlineImages,
        std::unique_ptr<EditorPreparedDocument>& valuePreparedDocument)
        : frame(valueFrame),
          resources(valueResources),
          inlineImages(valueInlineImages),
          preparedDocument(valuePreparedDocument)
    {
    }

    void EditorPreparedDocumentRetention::ReleaseOutside(
        float scrollOffset,
        bool printMode)
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
}
