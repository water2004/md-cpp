#include "pch.h"
#include "MainWindow.xaml.h"

namespace winrt::ElMd::implementation
{
    void MainWindow::SetDocumentCommandsVisible(bool visible)
    {
        auto value = visible
            ? Microsoft::UI::Xaml::Visibility::Visible
            : Microsoft::UI::Xaml::Visibility::Collapsed;
        SidebarToggleButton().Visibility(value);
        OpenButton().Visibility(value);
        SaveButton().Visibility(value);
        ExportPdfButton().Visibility(value);
        FileFormatSeparator().Visibility(value);
        BoldButton().Visibility(value);
        ItalicButton().Visibility(value);
        StrikeButton().Visibility(value);
        InlineCodeButton().Visibility(value);
        InlineBlockSeparator().Visibility(value);
        BlocksButton().Visibility(value);
        InsertButton().Visibility(value);
        SettingsSeparator().Visibility(value);
    }

    std::optional<winrt::hstring> MainWindow::ApplySettings(winrt::ElMd::AppSettings const& settings)
    {
        if (auto saveError = winrt::ElMd::SaveAppSettings(settings))
        {
            SetStatus(*saveError);
            return saveError;
        }
        auto mathChanged = appSettings.mathRenderingEnabled != settings.mathRenderingEnabled;
        auto themeChanged = appSettings.themeId != settings.themeId;
        appSettings = settings;
        if (mathChanged) editorRenderer.SetMathRenderingEnabled(appSettings.mathRenderingEnabled);
        if (themeChanged) UpdateTheme();
        else if (mathChanged) RenderEditorSurface();
        return std::nullopt;
    }

    void MainWindow::ToggleSettingsMode()
    {
        SetSettingsMode(!settingsMode);
    }

    void MainWindow::SetSettingsMode(bool enabled)
    {
        if (settingsMode == enabled)
        {
            SettingsButton().IsChecked(enabled);
            return;
        }

        settingsMode = enabled;
        SettingsButton().IsChecked(enabled);
        SetDocumentCommandsVisible(!enabled);
        if (enabled)
        {
            documentPaneWasOpen = DocumentNavigation().IsPaneOpen();
            if (!settingsView)
            {
                settingsView = std::make_shared<winrt::ElMd::SettingsView>(
                    appSettings,
                    themeCatalog,
                    CurrentThemeVariant(),
                    WindowHandle(),
                    [this](winrt::ElMd::AppSettings const& changed) { return ApplySettings(changed); });
                SettingsViewHost().Children().Append(settingsView->Root());
            }
            else
            {
                settingsView->Reset(appSettings, CurrentThemeVariant());
            }
            settingsView->SetNavigationWidth(themeProfile.layout.navigation_open_width);

            ShellCommandBar().IsOpen(false);
            DocumentNavigation().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
            SettingsViewHost().Visibility(Microsoft::UI::Xaml::Visibility::Visible);
            StatusBar().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
            TitleDocumentText().Text(L"Settings");
            Title(L"el-md - Settings");
            return;
        }

        SettingsViewHost().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
        if (settingsView) settingsView->Reset(appSettings, CurrentThemeVariant());
        DocumentNavigation().Visibility(Microsoft::UI::Xaml::Visibility::Visible);
        DocumentNavigation().IsPaneOpen(documentPaneWasOpen);
        StatusBar().Visibility(Microsoft::UI::Xaml::Visibility::Visible);
        Title(L"el-md - " + editorSession.DisplayName());
        UpdateDocumentInfo();
        RenderEditorSurface();
        EditorSurface().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
    }
}
