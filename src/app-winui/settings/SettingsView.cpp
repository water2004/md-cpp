#include "pch.h"
#include "settings/SettingsView.h"
#include "settings/SettingsViewSupport.h"
#include "localization/Localization.h"

namespace winrt::Folia
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using settings_ui::LanguageIndex;
    using settings_ui::NavigationItem;

    SettingsView::SettingsView(
        AppSettings settings,
        std::shared_ptr<ThemeCatalog> catalog,
        std::shared_ptr<LatexCommandCatalog> latexCatalog,
        folia::Theme systemVariant,
        HWND owner,
        ApplySettings applySettings)
        : settings_(std::move(settings)),
          appliedSettings_(settings_),
          catalog_(std::move(catalog)),
          latexCatalog_(std::move(latexCatalog)),
          systemVariant_(systemVariant),
          owner_(owner),
          applySettings_(std::move(applySettings))
    {
        shortcutModel_.Reset(settings_.shortcutBindings);
        Build();
    }

    void SettingsView::Build()
    {
        navigation_.AlwaysShowHeader(false);
        navigation_.CompactPaneLength(48);
        navigation_.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
        navigation_.IsPaneOpen(true);
        navigation_.IsPaneToggleButtonVisible(false);
        navigation_.IsSettingsVisible(false);
        navigation_.OpenPaneLength(280);
        navigation_.PaneTitle(Localize(L"Settings"));
        navigation_.PaneDisplayMode(NavigationViewPaneDisplayMode::Left);
        navigation_.HorizontalAlignment(HorizontalAlignment::Stretch);
        navigation_.VerticalAlignment(VerticalAlignment::Stretch);

        auto general = NavigationItem(Localize(L"General"), L"general", L"\xE713");
        auto shortcuts = NavigationItem(Localize(L"Shortcuts"), L"shortcuts", L"\xE765");
        auto latex = NavigationItem(Localize(L"LatexCommands"), L"latex", L"\xE943");
        auto themes = NavigationItem(Localize(L"Themes"), L"themes", L"\xE790");
        auto licenses = NavigationItem(Localize(L"Licenses"), L"licenses", L"\xE8A5");
        auto about = NavigationItem(Localize(L"About"), L"about", L"\xE946");
        navigation_.MenuItems().Append(general);
        navigation_.MenuItems().Append(shortcuts);
        navigation_.MenuItems().Append(latex);
        navigation_.MenuItems().Append(themes);
        navigation_.MenuItems().Append(licenses);
        navigation_.MenuItems().Append(about);

        generalPage_ = BuildGeneralPage();
        shortcutsPage_ = BuildShortcutsPage();
        latexPage_ = BuildLatexPage();
        themesPage_ = BuildThemesPage();
        licensesPage_ = BuildLicensesPage();
        aboutPage_ = BuildAboutPage();
        navigation_.SelectionChanged([this](auto const&, NavigationViewSelectionChangedEventArgs const& args)
        {
            auto item = args.SelectedItem().try_as<NavigationViewItem>();
            if (!item || !item.Tag()) return;
            Navigate(unbox_value<hstring>(item.Tag()));
        });
        navigation_.SelectedItem(general);
        Navigate(L"general");
    }

    void SettingsView::Navigate(hstring const& page)
    {
        if (page == L"themes") navigation_.Content(themesPage_);
        else if (page == L"latex")
        {
            navigation_.Content(latexPage_);
            RefreshLatexCommandList();
        }
        else if (page == L"shortcuts")
        {
            navigation_.Content(shortcutsPage_);
            RefreshShortcutList();
        }
        else if (page == L"licenses")
        {
            navigation_.Content(licensesPage_);
            LoadSelectedLicense();
        }
        else if (page == L"about") navigation_.Content(aboutPage_);
        else navigation_.Content(generalPage_);
    }

    void SettingsView::Reset(AppSettings settings, folia::Theme systemVariant)
    {
        settings_ = std::move(settings);
        appliedSettings_ = settings_;
        shortcutModel_.Reset(settings_.shortcutBindings);
        systemVariant_ = systemVariant;
        refreshing_ = true;
        mathToggle_.IsOn(settings_.mathRenderingEnabled);
        latexSuggestionsToggle_.IsOn(settings_.latexSuggestionsEnabled);
        languageCombo_.SelectedIndex(LanguageIndex(settings_.languageId));
        refreshing_ = false;
        RefreshShortcutList();
        RefreshLatexCommandList();
        RefreshThemeList();
        SetGeneralStatus({});
    }

    void SettingsView::SetSystemVariant(folia::Theme variant)
    {
        if (systemVariant_ == variant) return;
        systemVariant_ = variant;
        if (settings_.themeId == "system") UpdateThemePreview();
    }

    void SettingsView::Detach()
    {
        detached_ = true;
        applySettings_ = {};
        owner_ = nullptr;
    }
}
