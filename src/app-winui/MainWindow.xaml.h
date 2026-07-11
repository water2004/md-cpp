#pragma once

#include "EditorSession.h"
#include "EditorSurfaceRenderer.h"
#include "EditorScrollController.h"
#include "EditorTextInputController.h"
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
        void UpdateOutlinePanel();
        void UpdateDiagnosticsPanel();
        void HandleOutlineSelection(winrt::Windows::Foundation::IInspectable const& selectedItem);
        void HandleDiagnosticsSelection(winrt::Windows::Foundation::IInspectable const& selectedItem);
        void RegisterCommandHandlers();
        bool ExecuteEditorCommand(elmd::Command const& command);
        bool InsertEditorNewline();
        bool HandleEditorCharacter(char32_t character);
        bool HandleEditorKey(winrt::Windows::System::VirtualKey key);
        void HandlePointerPressed(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void HandlePointerMoved(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void HandlePointerReleased(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void HandlePointerWheel(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void HandleEditorDoubleTapped(winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args);
        bool SelectWordAt(std::size_t offset);
        bool TryToggleTaskCheckboxAt(std::size_t offset);
        std::optional<std::string> LinkAtSourceOffset(std::size_t offset) const;
        std::optional<std::string> TooltipAtSourceOffset(std::size_t offset) const;
        winrt::fire_and_forget OpenLinkAsync(std::string href);
        void CopySelectionToClipboard();
        void CutSelectionToClipboard();
        winrt::fire_and_forget PasteClipboardAsync();
        winrt::fire_and_forget InsertImageAsync();
        void SetStatus(winrt::hstring const& text);
        HWND WindowHandle();
        winrt::fire_and_forget OpenDocumentAsync();
        winrt::fire_and_forget SaveDocumentAsync();
        winrt::fire_and_forget SaveDocumentAsAsync();

        winrt::hstring lastCommand = L"Ready";
        winrt::ElMd::EditorSession editorSession;
        winrt::ElMd::EditorSurfaceRenderer editorRenderer;
        winrt::ElMd::EditorScrollController scrollController;
        winrt::ElMd::EditorTextInputController textInputController;
        bool pointerSelecting = false;
        std::optional<std::string> hoverTooltip;
        std::size_t pointerAnchor = 0;
        std::optional<winrt::ElMd::EditorSurfaceRenderer::TableAction> tableDrag;
        std::optional<std::size_t> tableDropIndex;
        float caretGoalX = -1.0f;
        bool MoveCaretVerticalStep(bool down, bool extend);
        std::vector<std::size_t> outlineOffsets;
        std::vector<std::size_t> diagnosticOffsets;
        std::vector<std::wstring> outlineLabels;
        std::vector<std::wstring> diagnosticLabels;
    };
}

namespace winrt::ElMd::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
