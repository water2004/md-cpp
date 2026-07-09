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
        void UpdateOutlinePanel();
        void UpdateDiagnosticsPanel();
        void HandleOutlineSelection(winrt::Windows::Foundation::IInspectable const& selectedItem);
        void HandleDiagnosticsSelection(winrt::Windows::Foundation::IInspectable const& selectedItem);
        void RegisterCommandHandlers();
        bool ExecuteEditorCommand(elmd::Command const& command);
        void HandleEditorCharacter(char32_t character);
        bool HandleEditorKey(winrt::Windows::System::VirtualKey key);
        void HandlePointerPressed(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void HandlePointerMoved(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void HandlePointerReleased(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void HandlePointerWheel(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void HandleEditorDoubleTapped(winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args);
        bool SelectWordAt(std::size_t offset);
        bool TryToggleTaskCheckboxAt(std::size_t offset);
        void CopySelectionToClipboard();
        void CutSelectionToClipboard();
        winrt::fire_and_forget PasteClipboardAsync();
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
        std::vector<std::size_t> outlineOffsets;
        std::vector<std::size_t> diagnosticOffsets;
    };
}

namespace winrt::ElMd::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
