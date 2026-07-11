#pragma once

#include "MathJaxRenderer.h"
#include "MermaidRenderer.h"
#include "SvgNormalizer.h"
#include "TreeSitterHighlighter.h"

namespace winrt::ElMd
{
    namespace detail
    {
        struct EditorSessionCore;
    }

    struct EditorSurfaceRenderer
    {
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
        void InitializeMermaid(winrt::Microsoft::UI::Xaml::Controls::WebView2 const& webView);

        struct CaretMove
        {
            std::size_t offset = 0;
            bool upstream = false;
        };

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

        std::optional<CachedRasterImage> LoadRasterImage(std::wstring const& baseDirectory, std::string_view source);

        struct VisualBlock
        {
            D2D1_RECT_F rect{};
            D2D1_POINT_2F textOrigin{};
            float textWidth = 0.0f;
            std::size_t sourceStart = 0;
            std::size_t sourceEnd = 0;
            float documentY = 0.0f;
            std::u32string text;
            std::vector<std::size_t> displayToSource;
            ::Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            bool thematicBreak = false;
        };

        struct VisualLine
        {
            std::size_t blockIndex = 0;
            std::size_t tableIndex = (std::numeric_limits<std::size_t>::max)();
            std::size_t cellIndex = (std::numeric_limits<std::size_t>::max)();
            std::size_t sourceStart = 0;
            std::size_t sourceEnd = 0;
            std::uint32_t displayStart = 0;
            std::uint32_t displayEnd = 0;
            bool wrapContinuation = false;
            D2D1_RECT_F rect{};
        };

        struct VisualTableCell
        {
            D2D1_RECT_F rect{};
            D2D1_POINT_2F textOrigin{};
            float textWidth = 0.0f;
            float textHeight = 0.0f;
            std::size_t sourceStart = 0;
            std::size_t sourceEnd = 0;
            std::size_t row = 0;
            std::size_t column = 0;
            std::u32string text;
            std::vector<std::size_t> displayToSource;
            ::Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        };

        struct VisualTable
        {
            D2D1_RECT_F rect{};
            std::size_t sourceStart = 0;
            std::size_t sourceEnd = 0;
            std::size_t rowCount = 0;
            std::size_t columnCount = 0;
            std::vector<float> rowBoundaries;
            std::vector<float> columnBoundaries;
            std::vector<VisualTableCell> cells;
        };

        struct VisualMathHit
        {
            D2D1_RECT_F rect{};
            std::size_t sourceStart = 0;
            std::size_t sourceEnd = 0;
            float progressStart = 0.0f;
            float progressEnd = 1.0f;
        };

        std::optional<std::size_t> LineIndexFor(std::size_t sourceOffset, bool upstream) const;
        std::optional<D2D1_RECT_F> CaretRectOnLine(VisualLine const& line, std::size_t sourceOffset, bool upstream) const;

        struct FontStyle
        {
            std::wstring family;
            float size = 0.0f;
            float lineHeight = 0.0f;
            DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
            DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
        };

        struct EditorStyleSheet
        {
            FontStyle body;
            FontStyle heading1;
            FontStyle heading2;
            FontStyle heading3;
            FontStyle code;
            D2D1_COLOR_F canvasColor{};
            D2D1_COLOR_F textColor{};
            D2D1_COLOR_F mutedColor{};
            D2D1_COLOR_F accentColor{};
            D2D1_COLOR_F codeTextColor{};
            D2D1_COLOR_F panelColor{};
            D2D1_COLOR_F nestedQuoteColor{};
            D2D1_COLOR_F selectionColor{};
            D2D1_COLOR_F caretColor{};
            std::array<D2D1_COLOR_F, 11> syntaxColors{};
            float documentWidth = 900.0f;
            float horizontalPadding = 48.0f;
            float verticalPadding = 40.0f;
            float blockGap = 6.0f;
        };

        void RebuildTextFormats();
        void ResetBrushes();
        static EditorStyleSheet CreateStyleSheet(Theme value);

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
        std::function<void()> invalidateCallback;
        std::atomic_bool mathInvalidationQueued = false;
        Theme theme = Theme::Dark;
        EditorStyleSheet styleSheet = CreateStyleSheet(Theme::Dark);
        std::vector<VisualBlock> visualBlocks;
        std::vector<VisualLine> visualLines;
        std::vector<VisualTable> visualTables;
        std::vector<VisualMathHit> visualMathHits;
        std::unordered_map<std::uint64_t, float> blockHeightCache;
        std::unordered_map<std::wstring, CachedRasterImage> rasterImageCache;
        std::deque<std::wstring> rasterImageCacheOrder;
        std::size_t rasterImageCacheBytes = 0;
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
