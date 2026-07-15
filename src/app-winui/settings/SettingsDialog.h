#pragma once

#include "settings/AppSettings.h"
#include "theme/ThemeCatalog.h"

namespace winrt::ElMd
{
    class SettingsDialog
    {
    public:
        SettingsDialog(AppSettings settings, ThemeCatalog& catalog, elmd::Theme systemVariant, HWND owner);

        winrt::Windows::Foundation::IAsyncOperation<bool> ShowAsync(
            winrt::Microsoft::UI::Xaml::XamlRoot const& xamlRoot);
        AppSettings const& Settings() const { return staged_; }

    private:
        void Build();
        winrt::Microsoft::UI::Xaml::UIElement BuildGeneralPage();
        winrt::Microsoft::UI::Xaml::UIElement BuildThemesPage();
        winrt::Microsoft::UI::Xaml::UIElement BuildAboutPage();
        void Navigate(winrt::hstring const& page);
        void RefreshThemeList();
        void UpdateThemePreview();
        void SetThemeStatus(winrt::hstring const& message, bool error = false);
        winrt::fire_and_forget ImportThemeAsync();
        void RemoveSelectedTheme();

        AppSettings staged_;
        ThemeCatalog& catalog_;
        elmd::Theme systemVariant_ = elmd::Theme::Dark;
        HWND owner_ = nullptr;
        winrt::Microsoft::UI::Xaml::Controls::ContentDialog dialog_;
        winrt::Microsoft::UI::Xaml::Controls::NavigationView navigation_;
        winrt::Microsoft::UI::Xaml::UIElement generalPage_{ nullptr };
        winrt::Microsoft::UI::Xaml::UIElement themesPage_{ nullptr };
        winrt::Microsoft::UI::Xaml::UIElement aboutPage_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch mathToggle_;
        winrt::Microsoft::UI::Xaml::Controls::ListView themeList_;
        winrt::Microsoft::UI::Xaml::Controls::Border themePreview_;
        winrt::Microsoft::UI::Xaml::Controls::Button removeThemeButton_;
        winrt::Microsoft::UI::Xaml::Controls::TextBlock themeStatus_;
        std::vector<std::string> themeIds_;
    };
}
