#pragma once

#include "EditorStyleSheet.h"

namespace winrt::ElMd
{
    struct EditorResizeResult
    {
        bool resized = false;
        bool widthChanged = false;
    };

    struct EditorRenderResources
    {
        ~EditorRenderResources();

        void Initialize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel, EditorStyleSheet const& styleSheet);
        EditorResizeResult Resize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel, double width, double height);
        void RebuildTextFormats(EditorStyleSheet const& styleSheet);
        void ResetBrushes();
        void ResetTargets();
        void EnsureFrameResources(EditorStyleSheet const& styleSheet);
        bool Ready() const;

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
        ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> calloutNoteBackgroundBrush;
        ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> calloutNoteBorderBrush;
        ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> calloutTipBackgroundBrush;
        ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> calloutTipBorderBrush;
        ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> calloutWarningBackgroundBrush;
        ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> calloutWarningBorderBrush;
        ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selectionBrush;
        ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> caretBrush;
        std::array<::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>, 11> syntaxBrushes;
        uint32_t surfaceWidth = 0;
        uint32_t surfaceHeight = 0;
        float surfaceWidthDip = 1.0f;
        float surfaceHeightDip = 1.0f;
        float surfaceScaleX = 1.0f;
        float surfaceScaleY = 1.0f;
        HANDLE frameLatencyWaitableObject = nullptr;

    private:
        float CompositionScaleX(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel) const;
        float CompositionScaleY(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel) const;
        void ApplySwapChainTransform();
    };
}
