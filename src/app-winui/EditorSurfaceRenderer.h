#pragma once

#include "EditorRenderFrame.h"
#include "MathJaxRenderer.h"
#include "MermaidRenderer.h"
#include "SvgNormalizer.h"
#include "TreeSitterHighlighter.h"
#include "EditorStyleSheet.h"
#include "EditorInteractionMap.h"
#include "EditorRenderCache.h"
#include "EditorRenderResources.h"
#include "EditorTableInteraction.h"

namespace winrt::ElMd
{
    struct EditorSurfaceRenderer
    {
        EditorSurfaceRenderer();
        ~EditorSurfaceRenderer();

        enum class Theme
        {
            Light,
            Dark,
        };

        enum class PdfExportResult
        {
            WaitingForAssets,
            Completed,
        };

        void Initialize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel);
        void Resize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel, double width, double height);
        void Render(detail::EditorRenderFrame const& frame);
        PdfExportResult ExportPdf(
            std::filesystem::path const& path,
            std::wstring const& title,
            detail::EditorRenderFrame const& frame);
        void SetTheme(Theme value);
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
        Theme theme = Theme::Dark;
        EditorStyleSheet styleSheet = CreateEditorStyleSheet(true);
        EditorInteractionMap interactionMap;
        std::vector<D2D1_RECT_F> nonInteractiveRegions;
        std::unique_ptr<PreparedDocument> preparedDocument;
        std::unordered_map<std::uint64_t, float> documentOwnerY;
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
