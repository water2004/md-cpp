#pragma once

import folia.platform.editor_scroll_state;
import folia.platform.editor_viewport_plan;

#include "editor/rendering/EditorDocumentBlockPreparer.h"
#include "editor/rendering/EditorPreparedDocument.h"
#include "editor/rendering/EditorRenderResources.h"
#include "editor/session/EditorRenderFrame.h"

namespace winrt::Folia
{
    struct EditorViewportMaterializationResult
    {
        float scrollOffset = 0.0f;
        bool scrollingForward = true;
        bool viewportActive = false;
        bool invalidateRequested = false;
    };

    // Materializes visible/prefetch blocks under a frame budget. It owns the
    // scroll-anchor correction caused by newly measured block heights, but it
    // does not refresh completed media or evict retained resources.
    class EditorViewportMaterializer final
    {
    public:
        EditorViewportMaterializer(
            detail::EditorRenderFrame const& frame,
            EditorRenderResources& resources,
            EditorDocumentBlockPreparer& blockPreparer,
            folia::platform::editor::EditorScrollState& scrollState,
            std::unique_ptr<EditorPreparedDocument>& preparedDocument);

        EditorViewportMaterializationResult Materialize(
            float scrollOffset,
            bool printMode);

    private:
        bool RequestEmbeddedAt(
            float documentTop,
            float scrollOffset,
            bool printMode) const;

        detail::EditorRenderFrame const& frame;
        EditorRenderResources& resources;
        EditorDocumentBlockPreparer& blockPreparer;
        folia::platform::editor::EditorScrollState& scrollState;
        std::unique_ptr<EditorPreparedDocument>& preparedDocument;
        static constexpr folia::platform::editor::EditorViewportPolicy viewportPolicy{};
    };
}
