#pragma once

import folia.core.render_model;
import folia.core.text_edit;
import folia.platform.editor_interaction;

#include "editor/rendering/EditorDocumentPainter.h"
#include "editor/rendering/EditorEmbeddedDraw.h"
#include "editor/rendering/EditorInlineImageRenderer.h"
#include "editor/rendering/EditorPreparedDocument.h"
#include "editor/rendering/EditorRenderResources.h"
#include "editor/rendering/EditorStyleSheet.h"
#include "editor/session/EditorRenderFrame.h"

namespace winrt::Folia
{
    using folia::platform::editor::EditorTableAction;
    using folia::platform::editor::EditorVisualBlock;
    using folia::platform::editor::EditorVisualLine;

    class EditorDocumentRenderPass final
    {
    public:
        EditorDocumentRenderPass(
            EditorRenderResources& resources,
            EditorStyleSheet const& styleSheet,
            EditorInteractionMap& interactionMap,
            std::vector<D2D1_RECT_F>& nonInteractiveRegions,
            EditorInlineImageRenderer& inlineImages,
            EditorDocumentPainter& documentPainter,
            EditorDrawMath const& drawMath,
            EditorDrawMathFallback const& drawMathFallback);

        void Paint(
            detail::EditorRenderFrame const& frame,
            EditorPreparedDocument& preparedDocument,
            folia::TextSelection selection,
            folia::TextPosition caret,
            float documentLeft,
            float documentRight,
            float documentWidth,
            float sourceGutterLeft,
            float sourceGutterWidth,
            float scrollOffset,
            std::size_t viewportBegin,
            std::size_t viewportEnd,
            bool printMode,
            std::optional<D2D1_POINT_2F> pointerPosition,
            std::optional<EditorTableAction> const& draggedTableAction,
            std::optional<std::size_t> tableDropIndex);

    private:
        EditorRenderResources& resources;
        EditorStyleSheet const& styleSheet;
        EditorInteractionMap& interactionMap;
        std::vector<D2D1_RECT_F>& nonInteractiveRegions;
        EditorInlineImageRenderer& inlineImages;
        EditorDocumentPainter& documentPainter;
        EditorDrawMath const& drawMath;
        EditorDrawMathFallback const& drawMathFallback;
    };
}
