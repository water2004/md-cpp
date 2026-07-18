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
#include "settings/LatexCommandCatalog.h"
#include "theme/ThemeCatalog.h"
#include "theme/ThemeConfig.h"
#include "MainWindow.g.h"

namespace winrt::Folia::implementation
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
        folia::Theme CurrentThemeVariant();
        void ToggleSettingsMode();
        void SetSettingsMode(bool enabled);
        void SetDocumentCommandsVisible(bool visible);
        std::optional<winrt::hstring> ApplySettings(winrt::Folia::AppSettings const& settings);
        void RegisterCommandHandlers();
        void ToggleSourceMode();
        void UpdateSourceModeUi();
        void UpdateDocumentInfo();
        void ShowFindBar(bool replace);
        void HideFindBar();
        void RefreshSearch(bool activateMatch = false);
        void NavigateSearch(int direction);
        void ReplaceCurrentSearchMatch();
        void ReplaceAllSearchMatches();
        folia::SearchOptions CurrentSearchOptions();
        bool ExecuteEditorCommand(folia::Command const& command);
        void ShowFootnoteFlyout(
            folia::platform::editor::EditorVisualFootnoteHit const& hit,
            winrt::Windows::Foundation::Point position);
        void NavigateToFootnote(folia::TextPosition position);
        void HandlePointerWheel(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void SetStatus(winrt::hstring const& text);
        void SetOperationProgress(
            bool active,
            std::optional<double> value = std::nullopt,
            bool cancellable = false);
        HWND WindowHandle();

        winrt::hstring lastCommand;
        winrt::Folia::EditorSession editorSession;
        winrt::Folia::EditorSurfaceRenderer editorRenderer;
        winrt::Folia::EditorScrollController scrollController;
        winrt::Folia::EditorTextInputController textInputController;
        winrt::Folia::EditorPointerController pointerController;
        winrt::Folia::EditorKeyboardController keyboardController;
        winrt::Folia::EditorDocumentController documentController;
        winrt::Folia::EditorSidebarController sidebarController;
        winrt::Microsoft::UI::Xaml::Controls::Flyout footnoteFlyout{ nullptr };
        winrt::Folia::AppSettings appSettings;
        std::shared_ptr<winrt::Folia::ThemeCatalog> themeCatalog = std::make_shared<winrt::Folia::ThemeCatalog>();
        std::shared_ptr<winrt::Folia::LatexCommandCatalog> latexCommandCatalog =
            std::make_shared<winrt::Folia::LatexCommandCatalog>();
        std::shared_ptr<winrt::Folia::SettingsView> settingsView;
        folia::ThemeProfile themeProfile = folia::default_theme_profile();
        bool updatingTheme = false;
        bool settingsMode = false;
        bool documentPaneWasOpen = true;
        std::optional<std::size_t> activeSearchMatch;
        std::size_t searchMatchCount = 0;
        bool updatingSearch = false;
    };
}

namespace winrt::Folia::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
