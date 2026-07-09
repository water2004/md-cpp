#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

namespace winrt::ElMd::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();
        RegisterCommandHandlers();

        EditorSurface().Loaded([this](auto const&, auto const&)
        {
            InitializeEditorSurface();
        });

        EditorSurface().SizeChanged([this](auto const&, Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
        {
            ResizeEditorSurface(args.NewSize().Width, args.NewSize().Height);
        });
    }

    void MainWindow::RegisterCommandHandlers()
    {
        OpenButton().Click([this](auto const&, auto const&)
        {
            OpenDocumentAsync();
        });

        SaveButton().Click([this](auto const&, auto const&)
        {
            SaveDocumentAsync();
        });

        BoldButton().Click([this](auto const&, auto const&)
        {
            SetStatus(L"Bold command pending editor-core bridge");
        });

        ItalicButton().Click([this](auto const&, auto const&)
        {
            SetStatus(L"Italic command pending editor-core bridge");
        });
    }

    void MainWindow::SetStatus(winrt::hstring const& text)
    {
        lastCommand = text;
        StatusText().Text(text);
        RenderEditorSurface();
    }

    HWND MainWindow::WindowHandle()
    {
        HWND hwnd{};
        auto windowNative = get_strong().as<IWindowNative>();
        winrt::check_hresult(windowNative->get_WindowHandle(&hwnd));
        return hwnd;
    }

    winrt::fire_and_forget MainWindow::OpenDocumentAsync()
    {
        auto lifetime = get_strong();

        try
        {
            auto picker = winrt::Windows::Storage::Pickers::FileOpenPicker();
            picker.FileTypeFilter().Append(L".md");
            picker.FileTypeFilter().Append(L".markdown");
            picker.FileTypeFilter().Append(L".txt");

            auto initializeWithWindow = picker.as<IInitializeWithWindow>();
            winrt::check_hresult(initializeWithWindow->Initialize(WindowHandle()));

            auto file = co_await picker.PickSingleFileAsync();
            if (!file)
            {
                SetStatus(L"Open cancelled");
                co_return;
            }

            currentText = co_await winrt::Windows::Storage::FileIO::ReadTextAsync(file);
            currentFile = file;
            Title(L"el-md - " + file.Name());
            SetStatus(file.Path() + L" | " + winrt::to_hstring(currentText.size()) + L" chars");
        }
        catch (winrt::hresult_error const& error)
        {
            SetStatus(L"Open failed: " + error.message());
        }
    }

    winrt::fire_and_forget MainWindow::SaveDocumentAsync()
    {
        auto lifetime = get_strong();

        try
        {
            if (!currentFile)
            {
                SaveDocumentAsAsync();
                co_return;
            }

            co_await winrt::Windows::Storage::FileIO::WriteTextAsync(currentFile, currentText);
            SetStatus(L"Saved " + currentFile.Path() + L" | " + winrt::to_hstring(currentText.size()) + L" chars");
        }
        catch (winrt::hresult_error const& error)
        {
            SetStatus(L"Save failed: " + error.message());
        }
    }

    winrt::fire_and_forget MainWindow::SaveDocumentAsAsync()
    {
        auto lifetime = get_strong();

        try
        {
            auto picker = winrt::Windows::Storage::Pickers::FileSavePicker();
            picker.DefaultFileExtension(L".md");
            picker.SuggestedFileName(L"Untitled.md");
            picker.FileTypeChoices().Insert(L"Markdown", winrt::single_threaded_vector<winrt::hstring>({ L".md" }));
            picker.FileTypeChoices().Insert(L"Text", winrt::single_threaded_vector<winrt::hstring>({ L".txt" }));

            auto initializeWithWindow = picker.as<IInitializeWithWindow>();
            winrt::check_hresult(initializeWithWindow->Initialize(WindowHandle()));

            auto file = co_await picker.PickSaveFileAsync();
            if (!file)
            {
                SetStatus(L"Save cancelled");
                co_return;
            }

            co_await winrt::Windows::Storage::FileIO::WriteTextAsync(file, currentText);
            currentFile = file;
            Title(L"el-md - " + file.Name());
            SetStatus(L"Saved " + file.Path() + L" | " + winrt::to_hstring(currentText.size()) + L" chars");
        }
        catch (winrt::hresult_error const& error)
        {
            SetStatus(L"Save failed: " + error.message());
        }
    }

    void MainWindow::InitializeEditorSurface()
    {
        if (swapChain)
        {
            RenderEditorSurface();
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
            15.0f,
            L"en-us",
            textFormat.GetAddressOf()));
        winrt::check_hresult(textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP));

        ::Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        winrt::check_hresult(dxgiDevice->GetAdapter(adapter.GetAddressOf()));

        ::Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
        winrt::check_hresult(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(factory.GetAddressOf())));

        auto width = std::max<uint32_t>(1, static_cast<uint32_t>(EditorSurface().ActualWidth()));
        auto height = std::max<uint32_t>(1, static_cast<uint32_t>(EditorSurface().ActualHeight()));

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

        auto panelNative = EditorSurface().as<ISwapChainPanelNative>();
        winrt::check_hresult(panelNative->SetSwapChain(swapChain.Get()));

        surfaceWidth = width;
        surfaceHeight = height;
        RenderEditorSurface();
    }

    void MainWindow::ResizeEditorSurface(double width, double height)
    {
        if (!swapChain)
        {
            return;
        }

        auto newWidth = std::max<uint32_t>(1, static_cast<uint32_t>(width));
        auto newHeight = std::max<uint32_t>(1, static_cast<uint32_t>(height));
        if (newWidth == surfaceWidth && newHeight == surfaceHeight)
        {
            return;
        }

        renderTargetView = nullptr;
        d2dTarget = nullptr;
        textBrush = nullptr;
        winrt::check_hresult(swapChain->ResizeBuffers(0, newWidth, newHeight, DXGI_FORMAT_UNKNOWN, 0));
        surfaceWidth = newWidth;
        surfaceHeight = newHeight;
        RenderEditorSurface();
    }

    void MainWindow::RenderEditorSurface()
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

        auto text = currentText.empty() ? winrt::hstring(L"Open a Markdown file to preview it here.") : currentText;
        auto rect = D2D1::RectF(28.0f, 24.0f, static_cast<float>(surfaceWidth) - 28.0f, static_cast<float>(surfaceHeight) - 24.0f);

        d2dContext->BeginDraw();
        d2dContext->Clear(D2D1::ColorF(0.070f, 0.086f, 0.110f, 1.0f));
        d2dContext->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), textFormat.Get(), rect, textBrush.Get());
        winrt::check_hresult(d2dContext->EndDraw());

        winrt::check_hresult(swapChain->Present(1, 0));
    }
}
