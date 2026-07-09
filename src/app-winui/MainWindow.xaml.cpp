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

        const float color[] = { 0.070f, 0.086f, 0.110f, 1.0f };
        d3dContext->ClearRenderTargetView(renderTargetView.Get(), color);
        winrt::check_hresult(swapChain->Present(1, 0));
    }
}
