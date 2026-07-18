#pragma once

import folia.platform.editor_viewport_plan;

#include "editor/rendering/EditorDocumentBlockPreparer.h"
#include "editor/rendering/EditorInlineImageRenderer.h"
#include "editor/rendering/EditorPreparedDocument.h"
#include "editor/rendering/EditorRenderResources.h"
#include "editor/session/EditorRenderFrame.h"
#include "media/MathJaxRenderer.h"
#include "media/SvgNormalizer.h"

namespace winrt::Folia
{
    // Refreshes completed asynchronous math/image work near the viewport and
    // unloads embedded resources after they leave the retention band.
    class EditorEmbeddedContentUpdater final
    {
    public:
        EditorEmbeddedContentUpdater(
            detail::EditorRenderFrame const& frame,
            EditorRenderResources& resources,
            EditorInlineImageRenderer& inlineImages,
            MathJaxRenderer& mathJax,
            SvgNormalizer& svgNormalizer,
            EditorDocumentBlockPreparer& blockPreparer,
            std::unique_ptr<EditorPreparedDocument>& preparedDocument);

        bool Update(
            float scrollOffset,
            bool printMode,
            bool scrollingForward,
            bool viewportActive,
            std::uint64_t embeddedGeneration,
            std::uint64_t remoteImageGeneration);

    private:
        detail::EditorRenderFrame const& frame;
        EditorRenderResources& resources;
        EditorInlineImageRenderer& inlineImages;
        MathJaxRenderer& mathJax;
        SvgNormalizer& svgNormalizer;
        EditorDocumentBlockPreparer& blockPreparer;
        std::unique_ptr<EditorPreparedDocument>& preparedDocument;
        static constexpr folia::platform::editor::EditorViewportPolicy viewportPolicy{};
    };
}
