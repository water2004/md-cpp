#pragma once

import folia.platform.editor_viewport_plan;

#include "editor/rendering/EditorInlineImageRenderer.h"
#include "editor/rendering/EditorPreparedDocument.h"
#include "editor/rendering/EditorRenderResources.h"
#include "editor/session/EditorRenderFrame.h"

namespace winrt::Folia
{
    // Keeps heavyweight visual resources close to the viewport while leaving
    // measured geometry and stable block metadata intact.
    class EditorPreparedDocumentRetention final
    {
    public:
        EditorPreparedDocumentRetention(
            detail::EditorRenderFrame const& frame,
            EditorRenderResources& resources,
            EditorInlineImageRenderer& inlineImages,
            std::unique_ptr<EditorPreparedDocument>& preparedDocument);

        void ReleaseOutside(float scrollOffset, bool printMode);

    private:
        detail::EditorRenderFrame const& frame;
        EditorRenderResources& resources;
        EditorInlineImageRenderer& inlineImages;
        std::unique_ptr<EditorPreparedDocument>& preparedDocument;
        static constexpr folia::platform::editor::EditorViewportPolicy viewportPolicy{};
    };
}
