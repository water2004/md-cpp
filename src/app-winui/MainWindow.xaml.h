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
#include "settings/SettingsView.h"
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
        void LocalizeShell();
        winrt::hstring LocalizedDocumentName();
        void ResizeEditorSurface(double width, double height);
        void RenderEditorSurface();
        void UpdateTheme();
        void ApplyShellTheme();
        elmd::Theme CurrentThemeVariant();
        void ToggleSettingsMode();
        void SetSettingsMode(bool enabled);
        void SetDocumentCommandsVisible(bool visible);
        std::optional<winrt::hstring> ApplySettings(winrt::ElMd::AppSettings const& settings);
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
        void SetOperationProgress(
            bool active,
            std::optional<double> value = std::nullopt,
            bool cancellable = false);
        HWND WindowHandle();

        winrt::hstring lastCommand;
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
        std::shared_ptr<winrt::ElMd::ThemeCatalog> themeCatalog = std::make_shared<winrt::ElMd::ThemeCatalog>();
        std::shared_ptr<winrt::ElMd::SettingsView> settingsView;
        elmd::ThemeProfile themeProfile = elmd::default_theme_profile();
        bool updatingTheme = false;
        bool settingsMode = false;
        bool documentPaneWasOpen = true;
    };
}

namespace winrt::ElMd::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
