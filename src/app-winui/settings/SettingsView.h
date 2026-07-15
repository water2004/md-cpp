#pragma once

#include "settings/AppSettings.h"
#include "theme/ThemeCatalog.h"

namespace winrt::ElMd
{
    class SettingsView : public std::enable_shared_from_this<SettingsView>
    {
    public:
        using ApplySettings = std::function<std::optional<winrt::hstring>(AppSettings const&)>;

        SettingsView(
            AppSettings settings,
            std::shared_ptr<ThemeCatalog> catalog,
            elmd::Theme systemVariant,
            HWND owner,
            ApplySettings applySettings);

        winrt::Microsoft::UI::Xaml::Controls::NavigationView Root() const { return navigation_; }
        void SetSystemVariant(elmd::Theme variant);
        void SetNavigationWidth(double width) { navigation_.OpenPaneLength(width); }
        void Detach();

    private:
        void Build();
        winrt::Microsoft::UI::Xaml::UIElement BuildGeneralPage();
        winrt::Microsoft::UI::Xaml::UIElement BuildThemesPage();
        winrt::Microsoft::UI::Xaml::UIElement BuildAboutPage();
        void Navigate(winrt::hstring const& page);
        void RefreshThemeList();
        void UpdateThemePreview();
        void ApplyChangedSettings();
        void SetGeneralStatus(winrt::hstring const& message, bool error = false);
        void SetThemeStatus(winrt::hstring const& message, bool error = false);
        winrt::fire_and_forget ImportThemeAsync();
        void RemoveSelectedTheme();

        AppSettings settings_;
        std::shared_ptr<ThemeCatalog> catalog_;
        elmd::Theme systemVariant_ = elmd::Theme::Dark;
        HWND owner_ = nullptr;
        ApplySettings applySettings_;
        winrt::Microsoft::UI::Xaml::Controls::NavigationView navigation_;
        winrt::Microsoft::UI::Xaml::UIElement generalPage_{ nullptr };
        winrt::Microsoft::UI::Xaml::UIElement themesPage_{ nullptr };
        winrt::Microsoft::UI::Xaml::UIElement aboutPage_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch mathToggle_;
        winrt::Microsoft::UI::Xaml::Controls::ListView themeList_;
        winrt::Microsoft::UI::Xaml::Controls::Border themePreview_;
        winrt::Microsoft::UI::Xaml::Controls::Button removeThemeButton_;
        winrt::Microsoft::UI::Xaml::Controls::TextBlock generalStatus_;
        winrt::Microsoft::UI::Xaml::Controls::TextBlock themeStatus_;
        std::vector<std::string> themeIds_;
        bool refreshing_ = false;
        bool detached_ = false;
    };
}
