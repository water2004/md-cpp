#include "pch.h"
#include "editor/rendering/EditorDocumentPreparationPass.h"

namespace winrt::Folia
{
    using folia::platform::editor::BuildEditorViewportPlan;

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
          viewportMaterializer(
              valueFrame,
              valueResources,
              valueBlockPreparer,
              valueScrollState,
              valuePreparedDocument),
          embeddedContentUpdater(
              valueFrame,
              valueResources,
              valueInlineImages,
              valueMathJax,
              valueSvgNormalizer,
              valueBlockPreparer,
              valuePreparedDocument),
          retention(
              valueFrame,
              valueResources,
              valueInlineImages,
              valuePreparedDocument),
          resources(valueResources),
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

    EditorDocumentPreparationResult EditorDocumentPreparationPass::Prepare()
    {
        cache.Reconcile(selection, documentWidth, themeRevision);
        cache.EnsureGeometry();
        auto viewport = viewportMaterializer.Materialize(scrollOffset, printMode);
        scrollOffset = viewport.scrollOffset;
        invalidateRequested = invalidateRequested || viewport.invalidateRequested;
        invalidateRequested = embeddedContentUpdater.Update(
                scrollOffset,
                printMode,
                viewport.scrollingForward,
                viewport.viewportActive,
                embeddedGeneration,
                remoteImageGeneration)
            || invalidateRequested;
        retention.ReleaseOutside(scrollOffset, printMode);

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
