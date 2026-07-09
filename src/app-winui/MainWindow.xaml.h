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
        void SetStatus(winrt::hstring const& text);
        HWND WindowHandle();
        winrt::fire_and_forget OpenDocumentAsync();
        winrt::fire_and_forget SaveDocumentAsync();
        winrt::fire_and_forget SaveDocumentAsAsync();

        winrt::hstring lastCommand = L"Ready";
        winrt::ElMd::EditorSession editorSession;
        winrt::ElMd::EditorSurfaceRenderer editorRenderer;
    };
}

namespace winrt::ElMd::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
