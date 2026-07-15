#include "pch.h"
#include "MainWindow.xaml.h"
#include "settings/SettingsDialog.h"

namespace winrt::ElMd::implementation
{
    winrt::fire_and_forget MainWindow::ShowSettingsAsync()
    {
        auto lifetime = get_strong();
        if (settingsOpen) co_return;
        settingsOpen = true;
        SettingsButton().IsEnabled(false);
        struct ResetOpenState
        {
            MainWindow& owner;
            ~ResetOpenState()
            {
                owner.settingsOpen = false;
                owner.SettingsButton().IsEnabled(true);
            }
        } resetOpenState{ *this };

        try
        {
            auto dialog = std::make_shared<winrt::ElMd::SettingsDialog>(
                appSettings,
                themeCatalog,
                CurrentThemeVariant(),
                WindowHandle());
            auto accepted = co_await dialog->ShowAsync(Root().XamlRoot());

            themeCatalog.Refresh();
            if (accepted)
            {
                appSettings = dialog->Settings();
            }
            else if (appSettings.themeId != "system")
            {
                auto selected = std::ranges::find(themeCatalog.Entries(), appSettings.themeId,
                    [](winrt::ElMd::ThemeEntry const& entry) { return entry.profile.id; });
                if (selected == themeCatalog.Entries().end()) appSettings.themeId = "system";
                else co_return;
            }
            else
            {
                co_return;
            }

            if (auto error = winrt::ElMd::SaveAppSettings(appSettings)) SetStatus(*error);
            editorRenderer.SetMathRenderingEnabled(appSettings.mathRenderingEnabled);
            UpdateTheme();
            RenderEditorSurface();
        }
        catch (winrt::hresult_error const& error)
        {
            SetStatus(L"Unable to open settings: " + error.message());
        }
        catch (std::exception const& error)
        {
            SetStatus(L"Unable to open settings: " + winrt::to_hstring(error.what()));
        }
    }
}
