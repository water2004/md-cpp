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

        EditorSurface().CompositionScaleChanged([this](auto const&, auto const&)
        {
            ResizeEditorSurface(EditorSurface().ActualWidth(), EditorSurface().ActualHeight());
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

            auto text = co_await winrt::Windows::Storage::FileIO::ReadTextAsync(file);
            editorSession.Open(file, text);
            Title(L"el-md - " + editorSession.DisplayName());
            SetStatus(editorSession.Path() + L" | " + winrt::to_hstring(editorSession.Text().size()) + L" chars | rev " + winrt::to_hstring(editorSession.Revision()));
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
            if (!editorSession.HasFile())
            {
                SaveDocumentAsAsync();
                co_return;
            }

            co_await winrt::Windows::Storage::FileIO::WriteTextAsync(editorSession.File(), editorSession.Text());
            SetStatus(L"Saved " + editorSession.Path() + L" | " + winrt::to_hstring(editorSession.Text().size()) + L" chars | rev " + winrt::to_hstring(editorSession.Revision()));
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

            co_await winrt::Windows::Storage::FileIO::WriteTextAsync(file, editorSession.Text());
            editorSession.SaveAs(file);
            Title(L"el-md - " + editorSession.DisplayName());
            SetStatus(L"Saved " + editorSession.Path() + L" | " + winrt::to_hstring(editorSession.Text().size()) + L" chars | rev " + winrt::to_hstring(editorSession.Revision()));
        }
        catch (winrt::hresult_error const& error)
        {
            SetStatus(L"Save failed: " + error.message());
        }
    }

    void MainWindow::InitializeEditorSurface()
    {
        editorRenderer.Initialize(EditorSurface());
        RenderEditorSurface();
    }

    void MainWindow::ResizeEditorSurface(double width, double height)
    {
        editorRenderer.Resize(EditorSurface(), width, height);
        RenderEditorSurface();
    }

    void MainWindow::RenderEditorSurface()
    {
        editorRenderer.Render(editorSession.Text());
    }
}
