#include "pch.h"
#include "EditorSurfaceRenderer.h"

namespace winrt::ElMd
{
    constexpr float PreviewFontSizeDip = 28.0f;
    constexpr float PreviewLineHeightDip = 38.0f;
    constexpr float PreviewBaselineDip = 30.0f;
    constexpr float PreviewHorizontalPaddingDip = 36.0f;
    constexpr float PreviewVerticalPaddingDip = 32.0f;

    float EditorSurfaceRenderer::CompositionScaleX(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel) const
    {
        return (std::max)(1.0f, panel.CompositionScaleX());
    }

    float EditorSurfaceRenderer::CompositionScaleY(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel) const
    {
        return (std::max)(1.0f, panel.CompositionScaleY());
    }

    void EditorSurfaceRenderer::ApplySwapChainTransform()
    {
        if (!swapChain)
        {
            return;
        }

        ::Microsoft::WRL::ComPtr<IDXGISwapChain2> swapChain2;
        if (SUCCEEDED(swapChain.As(&swapChain2)))
        {
            DXGI_MATRIX_3X2_F matrix{};
            matrix._11 = 1.0f / surfaceScaleX;
            matrix._22 = 1.0f / surfaceScaleY;
            winrt::check_hresult(swapChain2->SetMatrixTransform(&matrix));
        }
    }

    void EditorSurfaceRenderer::ResetTargets()
    {
        renderTargetView = nullptr;
        d2dTarget = nullptr;
        textBrush = nullptr;
    }

    void EditorSurfaceRenderer::Initialize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel)
    {
        if (swapChain)
        {
            Render({});
            return;
        }

        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        winrt::check_hresult(D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            featureLevels,
            static_cast<UINT>(sizeof(featureLevels) / sizeof(featureLevels[0])),
            D3D11_SDK_VERSION,
            d3dDevice.GetAddressOf(),
            nullptr,
            d3dContext.GetAddressOf()));

        ::Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        winrt::check_hresult(d3dDevice.As(&dxgiDevice));

        D2D1_FACTORY_OPTIONS d2dOptions{};
#if defined(_DEBUG)
        d2dOptions.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
        winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dOptions, d2dFactory.GetAddressOf()));
        winrt::check_hresult(d2dFactory->CreateDevice(dxgiDevice.Get(), d2dDevice.GetAddressOf()));
        winrt::check_hresult(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dContext.GetAddressOf()));

        winrt::check_hresult(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwriteFactory.GetAddressOf())));
        winrt::check_hresult(dwriteFactory->CreateTextFormat(
            L"Cascadia Mono",
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            PreviewFontSizeDip,
            L"en-us",
            textFormat.GetAddressOf()));
        winrt::check_hresult(textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP));
        winrt::check_hresult(textFormat->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, PreviewLineHeightDip, PreviewBaselineDip));

        ::Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        winrt::check_hresult(dxgiDevice->GetAdapter(adapter.GetAddressOf()));

        ::Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
        winrt::check_hresult(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(factory.GetAddressOf())));

        surfaceScaleX = CompositionScaleX(panel);
        surfaceScaleY = CompositionScaleY(panel);
        auto width = (std::max)(uint32_t{ 1 }, static_cast<uint32_t>(std::ceil(panel.ActualWidth() * surfaceScaleX)));
        auto height = (std::max)(uint32_t{ 1 }, static_cast<uint32_t>(std::ceil(panel.ActualHeight() * surfaceScaleY)));

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.Stereo = false;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        desc.Flags = 0;

        winrt::check_hresult(factory->CreateSwapChainForComposition(d3dDevice.Get(), &desc, nullptr, swapChain.GetAddressOf()));
        ApplySwapChainTransform();

        auto panelNative = panel.as<ISwapChainPanelNative>();
        winrt::check_hresult(panelNative->SetSwapChain(swapChain.Get()));

        surfaceWidth = width;
        surfaceHeight = height;
    }

    void EditorSurfaceRenderer::Resize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel, double width, double height)
    {
        if (!swapChain)
        {
            return;
        }

        auto newScaleX = CompositionScaleX(panel);
        auto newScaleY = CompositionScaleY(panel);
        auto newWidth = (std::max)(uint32_t{ 1 }, static_cast<uint32_t>(std::ceil(width * newScaleX)));
        auto newHeight = (std::max)(uint32_t{ 1 }, static_cast<uint32_t>(std::ceil(height * newScaleY)));
        if (newWidth == surfaceWidth && newHeight == surfaceHeight && newScaleX == surfaceScaleX && newScaleY == surfaceScaleY)
        {
            return;
        }

        ResetTargets();
        winrt::check_hresult(swapChain->ResizeBuffers(0, newWidth, newHeight, DXGI_FORMAT_UNKNOWN, 0));
        surfaceWidth = newWidth;
        surfaceHeight = newHeight;
        surfaceScaleX = newScaleX;
        surfaceScaleY = newScaleY;
        ApplySwapChainTransform();
    }

    void EditorSurfaceRenderer::Render(winrt::hstring const& text)
    {
        if (!swapChain || !d3dDevice || !d3dContext)
        {
            return;
        }

        if (!renderTargetView)
        {
            ::Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
            winrt::check_hresult(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuffer.GetAddressOf())));
            winrt::check_hresult(d3dDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, renderTargetView.GetAddressOf()));
        }

        if (!d2dTarget)
        {
            ::Microsoft::WRL::ComPtr<IDXGISurface> surface;
            winrt::check_hresult(swapChain->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(surface.GetAddressOf())));

            D2D1_BITMAP_PROPERTIES1 properties{};
            properties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
            properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
            properties.dpiX = 96.0f;
            properties.dpiY = 96.0f;
            properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

            winrt::check_hresult(d2dContext->CreateBitmapFromDxgiSurface(surface.Get(), &properties, d2dTarget.GetAddressOf()));
            d2dContext->SetTarget(d2dTarget.Get());
        }

        if (!textBrush)
        {
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.86f, 0.90f, 0.96f, 1.0f), textBrush.GetAddressOf()));
        }

        auto displayText = text.empty() ? winrt::hstring(L"Open a Markdown file to preview it here.") : text;
        auto rect = D2D1::RectF(
            PreviewHorizontalPaddingDip,
            PreviewVerticalPaddingDip,
            static_cast<float>(surfaceWidth) - PreviewHorizontalPaddingDip,
            static_cast<float>(surfaceHeight) - PreviewVerticalPaddingDip);

        d2dContext->BeginDraw();
        d2dContext->Clear(D2D1::ColorF(0.070f, 0.086f, 0.110f, 1.0f));
        d2dContext->DrawTextW(displayText.c_str(), static_cast<UINT32>(displayText.size()), textFormat.Get(), rect, textBrush.Get());
        winrt::check_hresult(d2dContext->EndDraw());

        winrt::check_hresult(swapChain->Present(1, 0));
    }
}
