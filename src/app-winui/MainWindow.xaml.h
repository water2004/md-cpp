#pragma once

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
        void RegisterCommandHandlers();
        void SetStatus(winrt::hstring const& text);

        ::Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice;
        ::Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext;
        ::Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
        ::Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTargetView;
        uint32_t surfaceWidth = 0;
        uint32_t surfaceHeight = 0;
        winrt::hstring lastCommand = L"Ready";
    };
}

namespace winrt::ElMd::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
