#pragma once

import folia.core.text_edit;
import folia.platform.editor_scroll_state;
import folia.platform.editor_viewport_plan;

#include "editor/rendering/EditorDocumentBlockPreparer.h"
#include "editor/rendering/EditorInlineImageRenderer.h"
#include "editor/rendering/EditorPreparedDocument.h"
#include "editor/rendering/EditorRenderResources.h"
#include "editor/rendering/EditorStyleSheet.h"
#include "editor/session/EditorRenderFrame.h"

namespace winrt::Folia
{
    struct EditorDocumentPreparationResult
    {
        float scrollOffset = 0.0f;
        folia::platform::editor::EditorIndexRange visible;
        bool invalidateRequested = false;
    };

    // Executes the platform preparation plan against WinUI render resources.
    // Planning stays in pure platform modules; this class owns only the cache
    // reconciliation and resource-materialization side effects.
    class EditorDocumentPreparationPass final
    {
    public:
        EditorDocumentPreparationPass(
            detail::EditorRenderFrame const& frame,
            EditorRenderResources& resources,
            EditorStyleSheet const& styleSheet,
            EditorInlineImageRenderer& inlineImages,
            EditorDocumentBlockPreparer& blockPreparer,
            folia::platform::editor::EditorScrollState& scrollState,
            std::unique_ptr<EditorPreparedDocument>& preparedDocument,
            folia::TextSelection selection,
            float documentWidth,
            std::uint64_t themeRevision,
            std::uint64_t embeddedGeneration,
            std::uint64_t remoteImageGeneration,
            float scrollOffset,
            bool printMode);

        EditorDocumentPreparationResult Prepare();

    private:
        void ReconcileCache();
        void InitializeGeometry();
        void PrepareViewport();
        void RefreshEmbeddedContent();
        void ReleaseOutsideRetention();
        bool RequestEmbeddedAt(float documentTop) const;
        std::optional<std::size_t> FindOwnerBlock(folia::NodeId owner) const;

        detail::EditorRenderFrame const& frame;
        EditorRenderResources& resources;
        EditorStyleSheet const& styleSheet;
        EditorInlineImageRenderer& inlineImages;
        EditorDocumentBlockPreparer& blockPreparer;
        folia::platform::editor::EditorScrollState& scrollState;
        std::unique_ptr<EditorPreparedDocument>& preparedDocument;
        folia::TextSelection selection;
        float documentWidth = 0.0f;
        std::uint64_t themeRevision = 0;
        std::uint64_t embeddedGeneration = 0;
        std::uint64_t remoteImageGeneration = 0;
        float scrollOffset = 0.0f;
        bool printMode = false;
        bool invalidateRequested = false;
        static constexpr folia::platform::editor::EditorViewportPolicy viewportPolicy{};
    };
}
