#pragma once

namespace winrt::ElMd
{
    namespace detail
    {
        struct EditorSessionCore;
    }

    struct EditorSurfaceRenderer
    {
        void Initialize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel);
        void Resize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel, double width, double height);
        void Render(detail::EditorSessionCore const& sessionCore);
        std::optional<std::size_t> HitTest(float x, float y) const;
        std::optional<D2D1_RECT_F> CaretBounds(std::size_t sourceOffset) const;
        void ScrollBy(float delta);
        void ScrollToSourceOffset(std::size_t sourceOffset);

    private:
        float CompositionScaleX(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel) const;
        float CompositionScaleY(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel) const;
        void ApplySwapChainTransform();
        void ResetTargets();
        void DrawDocument(detail::EditorSessionCore const& sessionCore);

        struct VisualBlock
        {
            D2D1_RECT_F rect{};
            D2D1_POINT_2F textOrigin{};
            float textWidth = 0.0f;
            std::size_t sourceStart = 0;
            std::size_t sourceEnd = 0;
            float documentY = 0.0f;
            std::u32string text;
            ::Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        };

        ::Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice;
        ::Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext;
        ::Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
        ::Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTargetView;
        ::Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory;
        ::Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice;
        ::Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext;
        ::Microsoft::WRL::ComPtr<ID2D1Bitmap1> d2dTarget;
        ::Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory;
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
        ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selectionBrush;
        ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> caretBrush;
        std::vector<VisualBlock> visualBlocks;
        float scrollOffset = 0.0f;
        float totalDocumentHeight = 0.0f;
        uint32_t surfaceWidth = 0;
        uint32_t surfaceHeight = 0;
        float surfaceWidthDip = 1.0f;
        float surfaceHeightDip = 1.0f;
        float surfaceScaleX = 1.0f;
        float surfaceScaleY = 1.0f;
    };
}
