#pragma once

import folia.platform.editor_interaction;
import folia.platform.editor_scroll_state;

#include "editor/session/EditorRenderFrame.h"
#include "media/MathJaxRenderer.h"
#include "media/MermaidRenderer.h"
#include "media/SvgNormalizer.h"
#include "media/TreeSitterHighlighter.h"
#include "editor/rendering/EditorStyleSheet.h"
#include "editor/rendering/EditorRenderCache.h"
#include "editor/rendering/EditorRenderResources.h"
#include "editor/interaction/EditorTableInteraction.h"

namespace winrt::Folia
{
    using folia::platform::editor::EditorInteractionMap;
    using folia::platform::editor::EditorScrollState;
    using folia::platform::editor::EditorFootnoteControlKind;
    using folia::platform::editor::EditorVisualBlock;
    using folia::platform::editor::EditorVisualFootnoteHit;
    using folia::platform::editor::EditorVisualLine;
    using folia::platform::editor::EditorVisualMathHit;
    using folia::platform::editor::EditorVisualTable;
    using folia::platform::editor::EditorVisualTableCell;

    struct EditorSurfaceRenderer
    {
        EditorSurfaceRenderer();
        ~EditorSurfaceRenderer();

        enum class PdfExportResult
        {
            WaitingForAssets,
            InProgress,
            Completed,
        };

        struct PdfExportProgress
        {
            PdfExportResult result = PdfExportResult::WaitingForAssets;
            std::size_t completedPages = 0;
            std::size_t totalPages = 0;
        };

        void Initialize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel);
        void InitializeForPdf();
        void Resize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel, double width, double height);
        void Render(detail::EditorRenderFrame const& frame);
        PdfExportProgress ExportPdfStep(
            std::filesystem::path const& path,
            std::wstring const& title,
            detail::EditorRenderFrame const& frame);
        void CancelPdfExport();
        void SetTheme(folia::ThemeProfile const& value);
        folia::ThemeProfile Theme() const;
        void SetMathRenderingEnabled(bool enabled);
        bool MathRenderingEnabled() const;
        void ResetDocumentCaches();
        void SetInvalidateCallback(std::function<void()> callback);

        using TableActionKind = EditorTableActionKind;
        using TableAction = EditorTableAction;
        using FootnoteHit = EditorVisualFootnoteHit;

        std::optional<folia::TextPosition> HitTest(float x, float y) const;
        std::optional<folia::TextPosition> TaskCheckboxAt(float x, float y) const;
        std::optional<FootnoteHit> FootnoteAt(float x, float y) const;
        std::optional<D2D1_RECT_F> CaretBounds(folia::TextPosition position) const;
        std::optional<folia::TextPosition> MoveCaretVertically(folia::TextPosition position, bool down, float& goalX) const;
        std::optional<folia::TextPosition> VisualLineStart(folia::TextPosition position) const;
        std::optional<folia::TextPosition> VisualLineEnd(folia::TextPosition position) const;
        void UpdatePointer(float x, float y);
        void ClearPointer();
        std::optional<TableAction> TableActionAt(float x, float y) const;
        std::optional<std::size_t> TableDropIndexAt(float x, float y, bool rows) const;
        void SetTableDrag(std::optional<TableAction> action, std::optional<std::size_t> dropIndex);
        void ScrollBy(float delta);
        void QueueScrollBy(float delta);
        bool AdvanceScrollAnimation(float elapsedSeconds);
        void SetScrollOffset(float value);
        float ScrollOffset() const;
        float MaximumScrollOffset() const;
        float ViewportHeight() const;
        HANDLE FrameLatencyWaitableObject() const;
        bool ScrollToPosition(folia::TextPosition position);

    private:
        struct PreparedDocument;
        struct PdfExportState;

        void DrawDocument(detail::EditorRenderFrame const& frame);
        void ClearPreparedDocument();

        void Invalidate();

        struct InvalidationState
        {
            std::mutex mutex;
            std::function<void()> callback;
            bool active = true;
        };

        using VisualBlock = EditorVisualBlock;
        using VisualLine = EditorVisualLine;
        using VisualTableCell = EditorVisualTableCell;
        using VisualTable = EditorVisualTable;
        using VisualMathHit = EditorVisualMathHit;

        EditorRenderResources resources;
        EditorRenderCache renderCache;
        MathJaxRenderer mathJax;
        MermaidRenderer mermaid;
        SvgNormalizer svgNormalizer;
        TreeSitterHighlighter treeSitter;
        std::shared_ptr<InvalidationState> invalidationState = std::make_shared<InvalidationState>();
        std::atomic_bool mathInvalidationQueued = false;
        folia::ThemeProfile themeProfile = folia::default_theme_profile();
        std::uint64_t themeRevision = 1;
        EditorStyleSheet styleSheet = CreateEditorStyleSheet(themeProfile);
        EditorInteractionMap interactionMap;
        std::vector<D2D1_RECT_F> nonInteractiveRegions;
        std::unique_ptr<PreparedDocument> preparedDocument;
        std::shared_ptr<PdfExportState> pdfExportState;
        std::uint64_t embeddedGeneration = 0;
        std::optional<D2D1_POINT_2F> pointerPosition;
        std::optional<TableAction> draggedTableAction;
        std::optional<std::size_t> tableDropIndex;
        EditorScrollState scrollState;
        float totalDocumentHeight = 0.0f;
        bool resizing = false;
        bool rendering = false;
        bool printMode = false;
        bool exporting = false;
        std::atomic_bool deferredInvalidate = false;
    };
}
