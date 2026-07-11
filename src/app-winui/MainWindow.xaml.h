#pragma once

#include "EditorSession.h"
#include "EditorSurfaceRenderer.h"
#include "EditorScrollController.h"
#include "EditorTextInputController.h"
#include "EditorPointerController.h"
#include "EditorKeyboardController.h"
#include "EditorDocumentController.h"
#include "EditorSidebarController.h"
#include "MainWindow.g.h"

namespace winrt::ElMd::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

    private:
        void InitializeEditorSurface();
        void InitializeTextInput();
        void ResizeEditorSurface(double width, double height);
        void RenderEditorSurface();
        void SetSidebarExpanded(bool expanded);
        void UpdateTheme();
        winrt::ElMd::EditorSurfaceRenderer::Theme CurrentRendererTheme();
        void RegisterCommandHandlers();
        bool ExecuteEditorCommand(elmd::Command const& command);
        void HandlePointerWheel(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void SetStatus(winrt::hstring const& text);
        HWND WindowHandle();

        winrt::hstring lastCommand = L"Ready";
        winrt::ElMd::EditorSession editorSession;
        winrt::ElMd::EditorSurfaceRenderer editorRenderer;
        winrt::ElMd::EditorScrollController scrollController;
        winrt::ElMd::EditorTextInputController textInputController;
        winrt::ElMd::EditorPointerController pointerController;
        winrt::ElMd::EditorKeyboardController keyboardController;
        winrt::ElMd::EditorDocumentController documentController;
        winrt::ElMd::EditorSidebarController sidebarController;
    };
}

namespace winrt::ElMd::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
