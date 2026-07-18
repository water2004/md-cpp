#pragma once

import folia.core.text_edit;

#include "editor/rendering/EditorDocumentBlockPreparer.h"
#include "editor/rendering/EditorInlineImageRenderer.h"
#include "editor/rendering/EditorPreparedDocument.h"
#include "editor/rendering/EditorStyleSheet.h"
#include "editor/session/EditorRenderFrame.h"

namespace winrt::Folia
{
    // Reconciles the retained render cache with a new render model and owns
    // construction of the geometry/owner indexes derived from that cache.
    // Viewport scheduling and embedded-resource work intentionally live in
    // separate preparation stages.
    class EditorPreparedDocumentCache final
    {
    public:
        EditorPreparedDocumentCache(
            detail::EditorRenderFrame const& frame,
            EditorStyleSheet const& styleSheet,
            EditorInlineImageRenderer& inlineImages,
            EditorDocumentBlockPreparer& blockPreparer,
            std::unique_ptr<EditorPreparedDocument>& preparedDocument);

        void Reconcile(
            folia::TextSelection selection,
            float documentWidth,
            std::uint64_t themeRevision);
        void EnsureGeometry();

    private:
        std::optional<std::size_t> FindOwnerBlock(folia::NodeId owner) const;
        void InitializeGeometry();

        detail::EditorRenderFrame const& frame;
        EditorStyleSheet const& styleSheet;
        EditorInlineImageRenderer& inlineImages;
        EditorDocumentBlockPreparer& blockPreparer;
        std::unique_ptr<EditorPreparedDocument>& preparedDocument;
    };
}
