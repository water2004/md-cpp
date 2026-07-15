#include "pch.h"
#include "EditorRenderResources.h"

namespace winrt::ElMd
{
    EditorRenderResources::~EditorRenderResources()
    {
        if (frameLatencyWaitableObject)
        {
            CloseHandle(frameLatencyWaitableObject);
            frameLatencyWaitableObject = nullptr;
        }
    }

    float EditorRenderResources::CompositionScaleX(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel) const
    {
        return (std::max)(1.0f, panel.CompositionScaleX());
    }

    float EditorRenderResources::CompositionScaleY(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel) const
    {
        return (std::max)(1.0f, panel.CompositionScaleY());
    }

    bool EditorRenderResources::Ready() const
    {
        return swapChain && d3dDevice && d3dContext;
    }

    void EditorRenderResources::ApplySwapChainTransform()
    {
        if (!swapChain) return;
        ::Microsoft::WRL::ComPtr<IDXGISwapChain2> swapChain2;
        if (SUCCEEDED(swapChain.As(&swapChain2)))
        {
            DXGI_MATRIX_3X2_F matrix{};
            matrix._11 = 1.0f / surfaceScaleX;
            matrix._22 = 1.0f / surfaceScaleY;
            winrt::check_hresult(swapChain2->SetMatrixTransform(&matrix));
        }
    }

    void EditorRenderResources::ResetBrushes()
    {
        textBrush = nullptr;
        mutedBrush = nullptr;
        accentBrush = nullptr;
        codeBrush = nullptr;
        panelBrush = nullptr;
        canvasBrush = nullptr;
        nestedQuoteBrush = nullptr;
        calloutNoteBackgroundBrush = nullptr;
        calloutNoteBorderBrush = nullptr;
        calloutTipBackgroundBrush = nullptr;
        calloutTipBorderBrush = nullptr;
        calloutWarningBackgroundBrush = nullptr;
        calloutWarningBorderBrush = nullptr;
        selectionBrush = nullptr;
        caretBrush = nullptr;
        for (auto& brush : syntaxBrushes) brush = nullptr;
    }

    void EditorRenderResources::ResetTargets()
    {
        if (d2dContext)
        {
            d2dContext->SetTarget(nullptr);
            d2dContext->Flush();
        }
        if (d3dContext)
        {
            d3dContext->OMSetRenderTargets(0, nullptr, nullptr);
            d3dContext->Flush();
        }
        renderTargetView = nullptr;
        d2dTarget = nullptr;
        ResetBrushes();
    }

    void EditorRenderResources::RebuildTextFormats(EditorStyleSheet const& styleSheet)
    {
        if (!dwriteFactory) return;
        auto createFormat = [&](EditorFontStyle const& font, ::Microsoft::WRL::ComPtr<IDWriteTextFormat>& target)
        {
            target = nullptr;
            winrt::check_hresult(dwriteFactory->CreateTextFormat(
                font.family.c_str(), nullptr, font.weight, font.style, DWRITE_FONT_STRETCH_NORMAL,
                font.size, L"en-us", target.GetAddressOf()));
            winrt::check_hresult(target->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP));
            winrt::check_hresult(target->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, font.lineHeight, font.size * 1.2f));
        };
        createFormat(styleSheet.body, textFormat);
        createFormat(styleSheet.heading1, heading1Format);
        createFormat(styleSheet.heading2, heading2Format);
        createFormat(styleSheet.heading3, heading3Format);
        createFormat(styleSheet.code, codeFormat);
    }

    void EditorRenderResources::Initialize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel, EditorStyleSheet const& styleSheet)
    {
        if (swapChain) return;
        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
        };
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        winrt::check_hresult(D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, featureLevels,
            static_cast<UINT>(sizeof(featureLevels) / sizeof(featureLevels[0])), D3D11_SDK_VERSION,
            d3dDevice.GetAddressOf(), nullptr, d3dContext.GetAddressOf()));

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
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wicFactory.GetAddressOf()))))
            CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wicFactory.GetAddressOf()));
        RebuildTextFormats(styleSheet);

        ::Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        winrt::check_hresult(dxgiDevice->GetAdapter(adapter.GetAddressOf()));
        ::Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
        winrt::check_hresult(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(factory.GetAddressOf())));
        surfaceScaleX = CompositionScaleX(panel);
        surfaceScaleY = CompositionScaleY(panel);
        surfaceWidthDip = static_cast<float>((std::max)(1.0, panel.ActualWidth()));
        surfaceHeightDip = static_cast<float>((std::max)(1.0, panel.ActualHeight()));
        auto width = (std::max)(uint32_t{ 1 }, static_cast<uint32_t>(std::ceil(panel.ActualWidth() * surfaceScaleX)));
        auto height = (std::max)(uint32_t{ 1 }, static_cast<uint32_t>(std::ceil(panel.ActualHeight() * surfaceScaleY)));
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        winrt::check_hresult(factory->CreateSwapChainForComposition(d3dDevice.Get(), &desc, nullptr, swapChain.GetAddressOf()));
        ::Microsoft::WRL::ComPtr<IDXGISwapChain2> swapChain2;
        winrt::check_hresult(swapChain.As(&swapChain2));
        winrt::check_hresult(swapChain2->SetMaximumFrameLatency(1));
        frameLatencyWaitableObject = swapChain2->GetFrameLatencyWaitableObject();
        if (!frameLatencyWaitableObject) winrt::throw_hresult(E_FAIL);
        surfaceWidth = width;
        surfaceHeight = height;
        ApplySwapChainTransform();
        auto panelNative = panel.as<ISwapChainPanelNative>();
        winrt::check_hresult(panelNative->SetSwapChain(swapChain.Get()));
    }

    EditorResizeResult EditorRenderResources::Resize(winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& panel, double width, double height)
    {
        if (!swapChain || !std::isfinite(width) || !std::isfinite(height) || width <= 0.0 || height <= 0.0) return {};
        auto newScaleX = CompositionScaleX(panel);
        auto newScaleY = CompositionScaleY(panel);
        auto newWidthDip = static_cast<float>((std::max)(1.0, width));
        auto newHeightDip = static_cast<float>((std::max)(1.0, height));
        auto newWidth = (std::max)(uint32_t{ 1 }, static_cast<uint32_t>(std::ceil(width * newScaleX)));
        auto newHeight = (std::max)(uint32_t{ 1 }, static_cast<uint32_t>(std::ceil(height * newScaleY)));
        if (newWidth == surfaceWidth && newHeight == surfaceHeight && newWidthDip == surfaceWidthDip
            && newHeightDip == surfaceHeightDip && newScaleX == surfaceScaleX && newScaleY == surfaceScaleY) return {};
        EditorResizeResult result{ true, newWidthDip != surfaceWidthDip };
        ResetTargets();
        if (FAILED(swapChain->ResizeBuffers(
                0,
                newWidth,
                newHeight,
                DXGI_FORMAT_UNKNOWN,
                DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))) return {};
        surfaceWidth = newWidth;
        surfaceHeight = newHeight;
        surfaceWidthDip = newWidthDip;
        surfaceHeightDip = newHeightDip;
        surfaceScaleX = newScaleX;
        surfaceScaleY = newScaleY;
        ApplySwapChainTransform();
        return result;
    }

    void EditorRenderResources::EnsureFrameResources(EditorStyleSheet const& styleSheet)
    {
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
            properties.pixelFormat = { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED };
            properties.dpiX = 96.0f * surfaceScaleX;
            properties.dpiY = 96.0f * surfaceScaleY;
            properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
            winrt::check_hresult(d2dContext->CreateBitmapFromDxgiSurface(surface.Get(), &properties, d2dTarget.GetAddressOf()));
            d2dContext->SetTarget(d2dTarget.Get());
            d2dContext->SetDpi(properties.dpiX, properties.dpiY);
        }
        if (!textBrush)
        {
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.textColor, textBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.mutedColor, mutedBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.accentColor, accentBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.codeTextColor, codeBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.panelColor, panelBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.canvasColor, canvasBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.nestedQuoteColor, nestedQuoteBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.calloutNoteBackgroundColor, calloutNoteBackgroundBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.calloutNoteBorderColor, calloutNoteBorderBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.calloutTipBackgroundColor, calloutTipBackgroundBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.calloutTipBorderColor, calloutTipBorderBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.calloutWarningBackgroundColor, calloutWarningBackgroundBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.calloutWarningBorderColor, calloutWarningBorderBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.selectionColor, selectionBrush.GetAddressOf()));
            winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.caretColor, caretBrush.GetAddressOf()));
            for (std::size_t index = 0; index < syntaxBrushes.size(); ++index)
                winrt::check_hresult(d2dContext->CreateSolidColorBrush(styleSheet.syntaxColors[index], syntaxBrushes[index].GetAddressOf()));
        }
    }
}
