#pragma once

#include "EditorSession.h"
#include "EditorSurfaceRenderer.h"
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
        bool ExecuteEditorCommand(elmd::Command const& command);
        void HandleEditorCharacter(char32_t character);
        void HandleEditorKey(winrt::Windows::System::VirtualKey key);
        void HandlePointerPressed(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void HandlePointerMoved(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void HandlePointerReleased(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void SetStatus(winrt::hstring const& text);
        HWND WindowHandle();
        winrt::fire_and_forget OpenDocumentAsync();
        winrt::fire_and_forget SaveDocumentAsync();
        winrt::fire_and_forget SaveDocumentAsAsync();

        winrt::hstring lastCommand = L"Ready";
        winrt::ElMd::EditorSession editorSession;
        winrt::ElMd::EditorSurfaceRenderer editorRenderer;
        bool pointerSelecting = false;
        std::size_t pointerAnchor = 0;
    };
}

namespace winrt::ElMd::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
