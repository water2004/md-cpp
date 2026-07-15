#pragma once

#include "editor/session/EditorSession.h"
#include "editor/rendering/EditorSurfaceRenderer.h"
#include "editor/interaction/EditorScrollController.h"
#include "editor/interaction/EditorTextInputController.h"
#include "editor/interaction/EditorPointerController.h"
#include "editor/interaction/EditorKeyboardController.h"
#include "editor/interaction/EditorDocumentController.h"
#include "editor/interaction/EditorSidebarController.h"
#include "settings/AppSettings.h"
#include "theme/ThemeCatalog.h"
#include "theme/ThemeConfig.h"
#include "MainWindow.g.h"

namespace winrt::ElMd::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

    private:
        void AttachControllers();
        void RegisterWindowHandlers();
        void InitializeEditorSurface();
        void InitializeTextInput();
        void ResizeEditorSurface(double width, double height);
        void RenderEditorSurface();
        void UpdateTheme();
        void ApplyShellTheme();
        elmd::Theme CurrentThemeVariant();
        winrt::fire_and_forget ShowSettingsAsync();
        void RegisterCommandHandlers();
        void ToggleSourceMode();
        void UpdateSourceModeUi();
        void UpdateDocumentInfo();
        bool ExecuteEditorCommand(elmd::Command const& command);
        void ShowFootnoteFlyout(
            winrt::ElMd::EditorSurfaceRenderer::FootnoteHit const& hit,
            winrt::Windows::Foundation::Point position);
        void NavigateToFootnote(elmd::TextPosition position);
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
        winrt::Microsoft::UI::Xaml::Controls::Flyout footnoteFlyout{ nullptr };
        winrt::ElMd::AppSettings appSettings;
        winrt::ElMd::ThemeCatalog themeCatalog;
        elmd::ThemeProfile themeProfile = elmd::default_theme_profile();
        bool updatingTheme = false;
        bool settingsOpen = false;
    };
}

namespace winrt::ElMd::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
