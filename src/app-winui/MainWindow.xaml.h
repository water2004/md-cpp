#pragma once

#include "EditorSession.h"
#include "MainWindow.g.h"

namespace winrt::ElMd::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

    private:
        void InitializeEditorSurface();
        void ResizeEditorSurface(double width, double height);
        void RenderEditorSurface();
        float CompositionScaleX();
        float CompositionScaleY();
        void ApplySwapChainTransform();
        void RegisterCommandHandlers();
        void SetStatus(winrt::hstring const& text);
        HWND WindowHandle();
        winrt::fire_and_forget OpenDocumentAsync();
        winrt::fire_and_forget SaveDocumentAsync();
        winrt::fire_and_forget SaveDocumentAsAsync();

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
        ::Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush;
        uint32_t surfaceWidth = 0;
        uint32_t surfaceHeight = 0;
        float surfaceScaleX = 1.0f;
        float surfaceScaleY = 1.0f;
        winrt::hstring lastCommand = L"Ready";
        winrt::ElMd::EditorSession editorSession;
    };
}

namespace winrt::ElMd::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
