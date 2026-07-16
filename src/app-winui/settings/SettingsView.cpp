#include "pch.h"
#include "settings/SettingsView.h"
#include "localization/Localization.h"
#include "storage/AssetPaths.h"

namespace
{
    namespace Xaml = winrt::Microsoft::UI::Xaml;
    namespace Controls = winrt::Microsoft::UI::Xaml::Controls;

    constexpr std::array<std::string_view, 3> LanguageIds{ "system", "en-US", "zh-CN" };

    std::int32_t LanguageIndex(std::string_view languageId)
    {
        auto found = std::ranges::find(LanguageIds, languageId);
        return found == LanguageIds.end()
            ? 0
            : static_cast<std::int32_t>(std::distance(LanguageIds.begin(), found));
    }

    Xaml::Media::SolidColorBrush Brush(elmd::Color color)
    {
        return Xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{ color.a, color.r, color.g, color.b });
    }

    Xaml::Media::FontFamily Font(std::string const& family)
    {
        return Xaml::Media::FontFamily(winrt::to_hstring(family));
    }

    Controls::TextBlock Text(winrt::hstring const& value, double size = 14.0)
    {
        Controls::TextBlock text;
        text.Text(value);
        text.FontSize(size);
        text.TextWrapping(Xaml::TextWrapping::Wrap);
        return text;
    }

    Controls::TextBlock PageHeading(winrt::hstring const& value)
    {
        auto text = Text(value, 24.0);
        text.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        return text;
    }

    Controls::Border Card(Xaml::UIElement const& child)
    {
        Controls::Border border;
        border.Padding(Xaml::Thickness{ 16, 14, 16, 14 });
        border.CornerRadius(Xaml::CornerRadius{ 8 });
        border.BorderThickness(Xaml::Thickness{ 1 });
        border.BorderBrush(Brush({ 128, 128, 128, 72 }));
        border.Child(child);
        return border;
    }

    winrt::Windows::Foundation::Uri FileUri(std::filesystem::path path)
    {
        path = std::filesystem::absolute(path);
        auto value = path.generic_wstring();
        return winrt::Windows::Foundation::Uri(L"file:///" + winrt::hstring(value));
    }

    Controls::NavigationViewItem NavigationItem(winrt::hstring const& label, winrt::hstring const& tag, wchar_t const* glyph)
    {
        Controls::NavigationViewItem item;
        item.Content(winrt::box_value(label));
        item.Tag(winrt::box_value(tag));
        Controls::FontIcon icon;
        icon.Glyph(glyph);
        item.Icon(icon);
        return item;
    }

    Controls::StackPanel PagePanel()
    {
        Controls::StackPanel panel;
        panel.Spacing(16);
        panel.Margin(Xaml::Thickness{ 24, 18, 24, 24 });
        return panel;
    }
}

namespace winrt::ElMd
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;

    SettingsView::SettingsView(
        AppSettings settings,
        std::shared_ptr<ThemeCatalog> catalog,
        elmd::Theme systemVariant,
        HWND owner,
        ApplySettings applySettings)
        : settings_(std::move(settings)),
          appliedSettings_(settings_),
          catalog_(std::move(catalog)),
          systemVariant_(systemVariant),
          owner_(owner),
          applySettings_(std::move(applySettings))
    {
        Build();
    }

    void SettingsView::Build()
    {
        navigation_.AlwaysShowHeader(false);
        navigation_.CompactPaneLength(0);
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
        auto themes = NavigationItem(Localize(L"Themes"), L"themes", L"\xE790");
        auto about = NavigationItem(Localize(L"About"), L"about", L"\xE946");
        navigation_.MenuItems().Append(general);
        navigation_.MenuItems().Append(themes);
        navigation_.MenuItems().Append(about);

        generalPage_ = BuildGeneralPage();
        themesPage_ = BuildThemesPage();
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

    UIElement SettingsView::BuildGeneralPage()
    {
        auto panel = PagePanel();
        panel.Children().Append(PageHeading(Localize(L"General")));
        panel.Children().Append(Text(Localize(L"GeneralDescription")));

        StackPanel mathCard;
        mathCard.Spacing(8);
        mathToggle_.Header(box_value(Localize(L"RenderMath")));
        mathToggle_.OnContent(box_value(Localize(L"MathJaxOn")));
        mathToggle_.OffContent(box_value(Localize(L"ShowMarkdownSource")));
        mathToggle_.IsOn(settings_.mathRenderingEnabled);
        mathToggle_.Toggled([this](auto const&, auto const&)
        {
            if (refreshing_) return;
            settings_.mathRenderingEnabled = mathToggle_.IsOn();
            ApplyMathSetting();
        });
        mathCard.Children().Append(mathToggle_);
        mathCard.Children().Append(Text(Localize(L"MathServiceDescription")));
        panel.Children().Append(Card(mathCard));

        StackPanel languageCard;
        languageCard.Spacing(8);
        auto languageHeading = Text(Localize(L"Language"), 18);
        languageHeading.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        languageCard.Children().Append(languageHeading);
        languageCombo_.Header(box_value(Localize(L"ApplicationLanguage")));
        languageCombo_.Items().Append(box_value(Localize(L"FollowWindowsLanguage")));
        languageCombo_.Items().Append(box_value(Localize(L"EnglishLanguage")));
        languageCombo_.Items().Append(box_value(Localize(L"SimplifiedChineseLanguage")));
        languageCombo_.SelectedIndex(LanguageIndex(settings_.languageId));
        languageCombo_.SelectionChanged([this](auto const&, SelectionChangedEventArgs const&)
        {
            if (refreshing_) return;
            auto index = languageCombo_.SelectedIndex();
            if (index < 0 || static_cast<std::size_t>(index) >= LanguageIds.size()) return;
            settings_.languageId = LanguageIds[static_cast<std::size_t>(index)];
            ApplyLanguageSetting();
        });
        languageCard.Children().Append(languageCombo_);
        languageCard.Children().Append(Text(Localize(L"LanguageRestartDescription")));
        panel.Children().Append(Card(languageCard));

        InfoBar note;
        note.IsOpen(true);
        note.IsClosable(false);
        note.Severity(InfoBarSeverity::Informational);
        note.Title(Localize(L"ChangesApplyImmediately"));
        note.Message(Localize(L"MathChangesDescription"));
        panel.Children().Append(note);
        generalStatus_.TextWrapping(TextWrapping::Wrap);
        panel.Children().Append(generalStatus_);
        ScrollViewer scroller;
        scroller.Content(panel);
        return scroller;
    }

    UIElement SettingsView::BuildThemesPage()
    {
        Grid page;
        page.Margin(Thickness{ 24, 18, 24, 24 });
        page.ColumnSpacing(20);
        ColumnDefinition listColumn;
        listColumn.Width(GridLengthHelper::FromPixels(280));
        ColumnDefinition previewColumn;
        previewColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        page.ColumnDefinitions().Append(listColumn);
        page.ColumnDefinitions().Append(previewColumn);

        Grid listPanel;
        listPanel.RowSpacing(12);
        for (auto index = 0; index < 5; ++index)
        {
            RowDefinition row;
            row.Height(index == 2
                ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star)
                : GridLengthHelper::Auto());
            listPanel.RowDefinitions().Append(row);
        }
        auto heading = PageHeading(Localize(L"Themes"));
        Grid::SetRow(heading, 0);
        listPanel.Children().Append(heading);
        auto description = Text(Localize(L"ThemesDescription"));
        Grid::SetRow(description, 1);
        listPanel.Children().Append(description);
        themeList_.SelectionMode(ListViewSelectionMode::Single);
        themeList_.MinHeight(220);
        themeList_.SelectionChanged([this](auto const&, SelectionChangedEventArgs const&)
        {
            auto index = themeList_.SelectedIndex();
            if (index < 0 || static_cast<std::size_t>(index) >= themeIds_.size()) return;
            if (refreshing_) return;
            settings_.themeId = themeIds_[static_cast<std::size_t>(index)];
            UpdateThemePreview();
        });
        Grid::SetRow(themeList_, 2);
        listPanel.Children().Append(themeList_);

        StackPanel buttons;
        buttons.Orientation(Orientation::Horizontal);
        buttons.Spacing(8);
        applyThemeButton_.Content(box_value(Localize(L"Apply")));
        applyThemeButton_.Click([this](auto const&, auto const&) { ApplyPendingTheme(); });
        if (auto accentStyle = Application::Current().Resources().TryLookup(box_value(L"AccentButtonStyle")).try_as<Style>())
            applyThemeButton_.Style(accentStyle);
        Button importButton;
        importButton.Content(box_value(Localize(L"Import")));
        importButton.Click([this](auto const&, auto const&) { ImportThemeAsync(); });
        removeThemeButton_.Content(box_value(Localize(L"Remove")));
        removeThemeButton_.Click([this](auto const&, auto const&) { RemoveSelectedTheme(); });
        buttons.Children().Append(applyThemeButton_);
        buttons.Children().Append(importButton);
        buttons.Children().Append(removeThemeButton_);
        Grid::SetRow(buttons, 3);
        listPanel.Children().Append(buttons);
        themeStatus_.TextWrapping(TextWrapping::Wrap);
        Grid::SetRow(themeStatus_, 4);
        listPanel.Children().Append(themeStatus_);
        Grid::SetColumn(listPanel, 0);
        page.Children().Append(listPanel);

        themePreview_.CornerRadius(CornerRadius{ 10 });
        themePreview_.BorderThickness(Thickness{ 1 });
        Grid::SetColumn(themePreview_, 1);
        page.Children().Append(themePreview_);
        RefreshThemeList();
        return page;
    }

    UIElement SettingsView::BuildAboutPage()
    {
        auto panel = PagePanel();
        panel.Children().Append(PageHeading(Localize(L"AboutApp")));

        Grid identity;
        identity.ColumnSpacing(16);
        ColumnDefinition iconColumn;
        iconColumn.Width(GridLengthHelper::FromPixels(88));
        ColumnDefinition copyColumn;
        copyColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        identity.ColumnDefinitions().Append(iconColumn);
        identity.ColumnDefinitions().Append(copyColumn);

        Image logo;
        logo.Width(88);
        logo.Height(88);
        logo.Stretch(Microsoft::UI::Xaml::Media::Stretch::Uniform);
        logo.Source(Microsoft::UI::Xaml::Media::Imaging::BitmapImage(
            FileUri(AssetPath(std::filesystem::path(L"branding") / L"Folia.png"))));
        identity.Children().Append(logo);

        StackPanel copy;
        copy.Spacing(8);
        auto name = Text(Localize(L"AppName"), 30);
        name.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        copy.Children().Append(name);
        copy.Children().Append(Text(LocalizeFormat(L"Version", { L"0.1.0" })));
        copy.Children().Append(Text(Localize(L"AppDescription")));
        copy.Children().Append(Text(Localize(L"LicenseSummary")));
        Grid::SetColumn(copy, 1);
        identity.Children().Append(copy);
        panel.Children().Append(Card(identity));

        StackPanel technology;
        technology.Spacing(8);
        auto heading = Text(Localize(L"BuiltForWindows"), 18);
        heading.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        technology.Children().Append(heading);
        technology.Children().Append(Text(Localize(L"TechnologyDescription")));
        panel.Children().Append(Card(technology));

        ScrollViewer scroller;
        scroller.Content(panel);
        return scroller;
    }

    void SettingsView::Navigate(hstring const& page)
    {
        if (page == L"themes") navigation_.Content(themesPage_);
        else if (page == L"about") navigation_.Content(aboutPage_);
        else navigation_.Content(generalPage_);
    }

    void SettingsView::RefreshThemeList()
    {
        refreshing_ = true;
        struct ResetRefreshing { bool& value; ~ResetRefreshing() { value = false; } } reset{ refreshing_ };
        catalog_->Refresh();
        themeIds_.clear();
        themeList_.Items().Clear();

        auto append = [&](hstring const& name, hstring const& detail, std::string id)
        {
            StackPanel content;
            content.Spacing(2);
            auto title = Text(name);
            title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            auto subtitle = Text(detail, 12);
            subtitle.Opacity(0.7);
            content.Children().Append(title);
            content.Children().Append(subtitle);
            ListViewItem item;
            item.HorizontalContentAlignment(HorizontalAlignment::Stretch);
            item.Content(content);
            themeIds_.push_back(std::move(id));
            themeList_.Items().Append(item);
        };

        append(Localize(L"FollowWindowsTheme"), Localize(L"AutomaticThemeDescription"), "system");
        for (auto const& entry : catalog_->Entries())
        {
            append(
                winrt::to_hstring(entry.profile.name),
                entry.builtIn ? Localize(L"BuiltIn") : Localize(L"Custom"),
                entry.profile.id);
        }

        auto found = std::ranges::find(themeIds_, settings_.themeId);
        if (found == themeIds_.end())
        {
            settings_.themeId = "system";
            themeList_.SelectedIndex(0);
        }
        else
        {
            themeList_.SelectedIndex(static_cast<std::int32_t>(std::distance(themeIds_.begin(), found)));
        }
        UpdateThemePreview();
    }

    void SettingsView::UpdateThemePreview()
    {
        auto resolved = catalog_->Resolve(settings_.themeId, systemVariant_);
        auto const& profile = resolved.profile;
        auto const& colors = profile.colors;

        StackPanel preview;
        preview.Padding(Thickness{ 24 });
        preview.Spacing(16);
        auto title = Text(winrt::to_hstring(profile.name), profile.typography.heading2.size);
        title.FontFamily(Font(profile.typography.heading2.family));
        title.FontWeight(winrt::Windows::UI::Text::FontWeight{ profile.typography.heading2.weight });
        title.Foreground(Brush(colors.heading_fg));
        preview.Children().Append(title);

        auto body = Text(Localize(L"ThemePreviewBody"), profile.typography.body.size);
        body.FontFamily(Font(profile.typography.body.family));
        body.Foreground(Brush(colors.fg));
        preview.Children().Append(body);

        Border code;
        code.Padding(Thickness{ 14, 10, 14, 10 });
        code.CornerRadius(CornerRadius{ 6 });
        code.Background(Brush(colors.code_block_bg));
        auto codeText = Text(L"auto answer = 42;", profile.typography.code.size);
        codeText.FontFamily(Font(profile.typography.code.family));
        codeText.Foreground(Brush(colors.code_block_fg));
        code.Child(codeText);
        preview.Children().Append(code);

        Grid quote;
        quote.ColumnSpacing(12);
        ColumnDefinition barColumn;
        barColumn.Width(GridLengthHelper::FromPixels((std::max)(3.0f, profile.layout.quote_border_width)));
        ColumnDefinition textColumn;
        textColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        quote.ColumnDefinitions().Append(barColumn);
        quote.ColumnDefinitions().Append(textColumn);
        Border bar;
        bar.Background(Brush(colors.blockquote_border));
        quote.Children().Append(bar);
        auto quoteText = Text(Localize(L"ThemePreviewQuote"), profile.typography.body.size);
        quoteText.Foreground(Brush(colors.fg));
        Grid::SetColumn(quoteText, 1);
        quote.Children().Append(quoteText);
        preview.Children().Append(quote);

        auto metadata = Text(
            profile.variant == elmd::Theme::Light ? Localize(L"LightProfile")
                : profile.variant == elmd::Theme::HighContrast ? Localize(L"HighContrastProfile")
                : Localize(L"DarkProfile"),
            12);
        metadata.Foreground(Brush(colors.muted_fg));
        preview.Children().Append(metadata);

        themePreview_.Background(Brush(colors.bg));
        themePreview_.BorderBrush(Brush(colors.shell_border));
        themePreview_.Child(preview);

        UpdateThemeActions();
        if (!resolved.loadedFromFile && !resolved.diagnostic.empty()) SetThemeStatus(resolved.diagnostic, true);
        else if (!catalog_->Diagnostics().empty()) SetThemeStatus(catalog_->Diagnostics().front(), true);
        else SetThemeStatus({});
    }

    void SettingsView::SetGeneralStatus(hstring const& message, bool error)
    {
        generalStatus_.Text(message);
        generalStatus_.Foreground(error ? Brush({ 196, 43, 28, 255 }) : nullptr);
    }

    void SettingsView::SetThemeStatus(hstring const& message, bool error)
    {
        themeStatus_.Text(message);
        themeStatus_.Foreground(error ? Brush({ 196, 43, 28, 255 }) : nullptr);
    }

    fire_and_forget SettingsView::ImportThemeAsync()
    {
        auto lifetime = shared_from_this();
        if (detached_) co_return;
        try
        {
            Windows::Storage::Pickers::FileOpenPicker picker;
            picker.SuggestedStartLocation(Windows::Storage::Pickers::PickerLocationId::DocumentsLibrary);
            picker.FileTypeFilter().Append(L".json");
            auto initialize = picker.as<::IInitializeWithWindow>();
            check_hresult(initialize->Initialize(owner_));
            auto file = co_await picker.PickSingleFileAsync();
            if (detached_ || !file) co_return;
            auto parsed = LoadThemeFile(std::filesystem::path(file.Path().c_str()));
            if (!parsed.profile)
            {
                SetThemeStatus(parsed.diagnostic, true);
                co_return;
            }
            auto id = parsed.profile->id;
            if (auto error = catalog_->Import(std::filesystem::path(file.Path().c_str())))
            {
                SetThemeStatus(*error, true);
                co_return;
            }
            settings_.themeId = std::move(id);
            RefreshThemeList();
            SetThemeStatus(Localize(L"ThemeImported"));
        }
        catch (hresult_error const& error)
        {
            if (detached_) co_return;
            SetThemeStatus(LocalizeFormat(L"ThemeImportFailed", { error.message() }), true);
        }
        catch (std::exception const& error)
        {
            if (detached_) co_return;
            SetThemeStatus(LocalizeFormat(L"ThemeImportFailed", { winrt::to_hstring(error.what()) }), true);
        }
    }

    void SettingsView::RemoveSelectedTheme()
    {
        auto index = themeList_.SelectedIndex();
        if (index <= 0 || static_cast<std::size_t>(index) >= themeIds_.size()) return;
        auto id = themeIds_[static_cast<std::size_t>(index)];
        if (id == appliedSettings_.themeId)
        {
            SetThemeStatus(Localize(L"ApplyThemeBeforeRemove"), true);
            return;
        }
        if (auto error = catalog_->Remove(id))
        {
            SetThemeStatus(*error, true);
            return;
        }
        if (settings_.themeId == id) settings_.themeId = appliedSettings_.themeId;
        RefreshThemeList();
        SetThemeStatus(Localize(L"CustomThemeRemoved"));
    }

    void SettingsView::ApplyMathSetting()
    {
        if (detached_ || !applySettings_) return;
        auto proposed = appliedSettings_;
        proposed.mathRenderingEnabled = settings_.mathRenderingEnabled;
        if (auto error = applySettings_(proposed))
        {
            SetGeneralStatus(*error, true);
            refreshing_ = true;
            settings_.mathRenderingEnabled = appliedSettings_.mathRenderingEnabled;
            mathToggle_.IsOn(settings_.mathRenderingEnabled);
            refreshing_ = false;
            return;
        }
        appliedSettings_.mathRenderingEnabled = proposed.mathRenderingEnabled;
        SetGeneralStatus({});
    }

    void SettingsView::ApplyLanguageSetting()
    {
        if (detached_ || !applySettings_ || settings_.languageId == appliedSettings_.languageId) return;
        auto proposed = appliedSettings_;
        proposed.languageId = settings_.languageId;
        if (auto error = applySettings_(proposed))
        {
            SetGeneralStatus(*error, true);
            refreshing_ = true;
            settings_.languageId = appliedSettings_.languageId;
            languageCombo_.SelectedIndex(LanguageIndex(settings_.languageId));
            refreshing_ = false;
            return;
        }
        appliedSettings_.languageId = proposed.languageId;
        SetGeneralStatus(Localize(L"LanguageSavedRestart"));
    }

    void SettingsView::ApplyPendingTheme()
    {
        if (detached_ || !applySettings_ || settings_.themeId == appliedSettings_.themeId) return;
        auto proposed = appliedSettings_;
        proposed.themeId = settings_.themeId;
        if (auto error = applySettings_(proposed))
        {
            SetThemeStatus(*error, true);
            return;
        }
        appliedSettings_.themeId = proposed.themeId;
        SetThemeStatus(Localize(L"ThemeApplied"));
        UpdateThemeActions();
    }

    void SettingsView::UpdateThemeActions()
    {
        applyThemeButton_.IsEnabled(settings_.themeId != appliedSettings_.themeId);
        auto index = themeList_.SelectedIndex();
        auto removable = index > 0
            && static_cast<std::size_t>(index) < themeIds_.size()
            && static_cast<std::size_t>(index - 1) < catalog_->Entries().size()
            && !catalog_->Entries()[static_cast<std::size_t>(index - 1)].builtIn
            && themeIds_[static_cast<std::size_t>(index)] != appliedSettings_.themeId;
        removeThemeButton_.IsEnabled(removable);
    }

    void SettingsView::Reset(AppSettings settings, elmd::Theme systemVariant)
    {
        settings_ = std::move(settings);
        appliedSettings_ = settings_;
        systemVariant_ = systemVariant;
        refreshing_ = true;
        mathToggle_.IsOn(settings_.mathRenderingEnabled);
        languageCombo_.SelectedIndex(LanguageIndex(settings_.languageId));
        refreshing_ = false;
        RefreshThemeList();
        SetGeneralStatus({});
    }

    void SettingsView::SetSystemVariant(elmd::Theme variant)
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
