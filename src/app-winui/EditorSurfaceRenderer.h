#pragma once

#include "MathJaxRenderer.h"
#include "MermaidRenderer.h"
#include "SvgNormalizer.h"
#include "TreeSitterHighlighter.h"
#include "EditorStyleSheet.h"
#include "EditorInteractionMap.h"

namespace winrt::ElMd
{
    namespace detail
    {
        struct EditorSessionCore;
    }

    struct EditorSurfaceRenderer
    {
        ~EditorSurfaceRenderer();

        enum class Theme
        {
            Light,
            Dark,
        };

        void Initialize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel);
        void Resize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel, double width, double height);
        void Render(detail::EditorSessionCore const& sessionCore);
        void SetTheme(Theme value);
        void SetInvalidateCallback(std::function<void()> callback);

        using CaretMove = EditorCaretMove;

        enum class TableActionKind
        {
            InsertRow,
            InsertColumn,
            DeleteRow,
            DeleteColumn,
            DragRow,
            DragColumn,
        };

        struct TableAction
        {
            TableActionKind kind = TableActionKind::InsertRow;
            std::size_t sourceOffset = 0;
            std::size_t index = 0;
        };

        std::optional<std::size_t> HitTest(float x, float y, bool* outUpstream = nullptr) const;
        std::optional<D2D1_RECT_F> CaretBounds(std::size_t sourceOffset, bool upstream = false) const;
        std::optional<CaretMove> MoveCaretVertically(std::size_t sourceOffset, bool upstream, bool down, float& goalX) const;
        std::optional<std::size_t> VisualLineStart(std::size_t sourceOffset, bool upstream) const;
        std::optional<std::size_t> VisualLineEnd(std::size_t sourceOffset, bool upstream) const;
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
        bool ScrollToSourceOffset(std::size_t sourceOffset);

    private:
        float CompositionScaleX(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel) const;
        float CompositionScaleY(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel) const;
        void ApplySwapChainTransform();
        void ResetTargets();
        void DrawDocument(detail::EditorSessionCore const& sessionCore);

        struct CachedRasterImage
        {
            ::Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
            float width = 0.0f;
            float height = 0.0f;
            std::size_t bytes = 0;
        };

        struct CachedSvgDocument
        {
            ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> document;
            std::size_t bytes = 0;
        };

        struct CachedTextLayout
        {
            ::Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            std::size_t bytes = 0;
        };

        std::optional<CachedRasterImage> LoadRasterImage(std::wstring const& baseDirectory, std::string_view source);
        void QueueRemoteImage(std::string source);
        void Invalidate();

        struct InvalidationState
        {
            std::mutex mutex;
            std::function<void()> callback;
            bool active = true;
        };

        struct RemoteImageState
        {
            std::mutex mutex;
            std::unordered_map<std::string, std::vector<std::uint8_t>> data;
            std::unordered_set<std::string> pending;
            std::unordered_set<std::string> failed;
            std::deque<std::string> order;
            std::size_t bytes = 0;
            std::atomic_uint64_t generation = 0;
        };

        using VisualBlock = EditorVisualBlock;
        using VisualLine = EditorVisualLine;
        using VisualTableCell = EditorVisualTableCell;
        using VisualTable = EditorVisualTable;
        using VisualMathHit = EditorVisualMathHit;

        void RebuildTextFormats();
        void ResetBrushes();

        ::Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice;
        ::Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext;
        ::Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
        ::Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTargetView;
        ::Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory;
        ::Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice;
        ::Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext;
        ::Microsoft::WRL::ComPtr<ID2D1Bitmap1> d2dTarget;
        ::Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory;
        ::Microsoft::WRL::ComPtr<IWICImagingFactory> wicFactory;
        ::Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat;
        ::Microsoft::WRL::ComPtr<IDWriteTextFormat> heading1Format;
        ::Microsoft::WRL::ComPtr<IDWriteTextFormat> heading2Format;
        ::Microsoft::WRL::ComPtr<IDWriteTextFormat> heading3Format;
        ::Microsoft::WRL::ComPtr<IDWriteTextFormat> codeFormat;
        ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush;
        ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> mutedBrush;
        ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accentBrush;
        ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> codeBrush;
        ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> panelBrush;
        ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> canvasBrush;
        ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> nestedQuoteBrush;
        ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selectionBrush;
        ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> caretBrush;
        std::array<::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>, 11> syntaxBrushes;
        MathJaxRenderer mathJax;
        MermaidRenderer mermaid;
        SvgNormalizer svgNormalizer;
        TreeSitterHighlighter treeSitter;
        std::shared_ptr<InvalidationState> invalidationState = std::make_shared<InvalidationState>();
        std::atomic_bool mathInvalidationQueued = false;
        Theme theme = Theme::Dark;
        EditorStyleSheet styleSheet = CreateEditorStyleSheet(true);
        EditorInteractionMap interactionMap;
        std::unordered_map<std::uint64_t, float> blockHeightCache;
        std::unordered_map<std::uint64_t, CachedTextLayout> textLayoutCache;
        std::deque<std::uint64_t> textLayoutCacheOrder;
        std::size_t textLayoutCacheBytes = 0;
        std::unordered_map<std::wstring, CachedRasterImage> rasterImageCache;
        std::unordered_set<std::wstring> rasterImageFailed;
        std::deque<std::wstring> rasterImageCacheOrder;
        std::size_t rasterImageCacheBytes = 0;
        std::shared_ptr<RemoteImageState> remoteImages = std::make_shared<RemoteImageState>();
        std::uint64_t observedRemoteImageGeneration = 0;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue renderDispatcher{ nullptr };
        std::unordered_map<std::uint64_t, CachedSvgDocument> svgDocumentCache;
        std::deque<std::uint64_t> svgDocumentCacheOrder;
        std::size_t svgDocumentCacheBytes = 0;
        std::optional<D2D1_POINT_2F> pointerPosition;
        std::optional<TableAction> draggedTableAction;
        std::optional<std::size_t> tableDropIndex;
        float scrollOffset = 0.0f;
        float scrollTarget = 0.0f;
        float totalDocumentHeight = 0.0f;
        uint32_t surfaceWidth = 0;
        uint32_t surfaceHeight = 0;
        float surfaceWidthDip = 1.0f;
        float surfaceHeightDip = 1.0f;
        float surfaceScaleX = 1.0f;
        float surfaceScaleY = 1.0f;
        bool resizing = false;
        bool rendering = false;
    };
}
