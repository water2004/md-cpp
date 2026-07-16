#pragma once

#include "editor/session/EditorRenderFrame.h"
#include "media/MathJaxRenderer.h"
#include "media/MermaidRenderer.h"
#include "media/SvgNormalizer.h"
#include "media/TreeSitterHighlighter.h"
#include "editor/rendering/EditorStyleSheet.h"
#include "editor/interaction/EditorInteractionMap.h"
#include "editor/rendering/EditorRenderCache.h"
#include "editor/rendering/EditorRenderResources.h"
#include "editor/interaction/EditorTableInteraction.h"

namespace winrt::ElMd
{
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
        void Resize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel, double width, double height);
        void Render(detail::EditorRenderFrame const& frame);
        PdfExportProgress ExportPdfStep(
            std::filesystem::path const& path,
            std::wstring const& title,
            detail::EditorRenderFrame const& frame);
        void CancelPdfExport();
        void SetTheme(elmd::ThemeProfile const& value);
        void SetMathRenderingEnabled(bool enabled);
        bool MathRenderingEnabled() const;
        void ResetDocumentCaches();
        void SetInvalidateCallback(std::function<void()> callback);

        using TableActionKind = EditorTableActionKind;
        using TableAction = EditorTableAction;
        using FootnoteHit = EditorVisualFootnoteHit;

        std::optional<elmd::TextPosition> HitTest(float x, float y) const;
        std::optional<elmd::TextPosition> TaskCheckboxAt(float x, float y) const;
        std::optional<FootnoteHit> FootnoteAt(float x, float y) const;
        std::optional<D2D1_RECT_F> CaretBounds(elmd::TextPosition position) const;
        std::optional<elmd::TextPosition> MoveCaretVertically(elmd::TextPosition position, bool down, float& goalX) const;
        std::optional<elmd::TextPosition> VisualLineStart(elmd::TextPosition position) const;
        std::optional<elmd::TextPosition> VisualLineEnd(elmd::TextPosition position) const;
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
        bool ScrollToPosition(elmd::TextPosition position);

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
        elmd::ThemeProfile themeProfile = elmd::default_theme_profile();
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
        float scrollOffset = 0.0f;
        float scrollTarget = 0.0f;
        float totalDocumentHeight = 0.0f;
        bool resizing = false;
        bool rendering = false;
        bool printMode = false;
        bool exporting = false;
        std::atomic_bool deferredInvalidate = false;
    };
}
