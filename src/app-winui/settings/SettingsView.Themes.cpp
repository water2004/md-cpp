#include "pch.h"
#include "settings/SettingsView.h"
#include "settings/SettingsViewSupport.h"
#include "localization/Localization.h"

namespace winrt::Folia
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using settings_ui::Brush;
    using settings_ui::Font;
    using settings_ui::PageHeading;
    using settings_ui::Text;

    UIElement SettingsView::BuildThemesPage()
    {
        Grid page;
        page.Margin(Thickness{24, 18, 24, 24});
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
        if (auto accentStyle = Application::Current().Resources().TryLookup(
            box_value(L"AccentButtonStyle")).try_as<Style>())
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

        themePreview_.CornerRadius(CornerRadius{10});
        themePreview_.BorderThickness(Thickness{1});
        Grid::SetColumn(themePreview_, 1);
        page.Children().Append(themePreview_);
        RefreshThemeList();
        return page;
    }

    void SettingsView::RefreshThemeList()
    {
        refreshing_ = true;
        struct ResetRefreshing { bool& value; ~ResetRefreshing() { value = false; } } reset{refreshing_};
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
            themeList_.SelectedIndex(
                static_cast<std::int32_t>(std::distance(themeIds_.begin(), found)));
        }
        UpdateThemePreview();
    }

    void SettingsView::UpdateThemePreview()
    {
        auto resolved = catalog_->Resolve(settings_.themeId, systemVariant_);
        auto const& profile = resolved.profile;
        auto const& colors = profile.colors;

        StackPanel preview;
        preview.Padding(Thickness{24});
        preview.Spacing(16);
        auto title = Text(winrt::to_hstring(profile.name), profile.typography.heading2.size);
        title.FontFamily(Font(profile.typography.heading2.family));
        title.FontWeight(winrt::Windows::UI::Text::FontWeight{profile.typography.heading2.weight});
        title.Foreground(Brush(colors.heading_fg));
        preview.Children().Append(title);

        auto body = Text(Localize(L"ThemePreviewBody"), profile.typography.body.size);
        body.FontFamily(Font(profile.typography.body.family));
        body.Foreground(Brush(colors.fg));
        preview.Children().Append(body);

        Border code;
        code.Padding(Thickness{14, 10, 14, 10});
        code.CornerRadius(CornerRadius{6});
        code.Background(Brush(colors.code_block_bg));
        auto codeText = Text(L"auto answer = 42;", profile.typography.code.size);
        codeText.FontFamily(Font(profile.typography.code.family));
        codeText.Foreground(Brush(colors.code_block_fg));
        code.Child(codeText);
        preview.Children().Append(code);

        Grid quote;
        quote.ColumnSpacing(12);
        ColumnDefinition barColumn;
        barColumn.Width(GridLengthHelper::FromPixels(
            (std::max)(3.0f, profile.layout.quote_border_width)));
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
            profile.variant == folia::Theme::Light ? Localize(L"LightProfile")
                : profile.variant == folia::Theme::HighContrast
                    ? Localize(L"HighContrastProfile")
                    : Localize(L"DarkProfile"),
            12);
        metadata.Foreground(Brush(colors.muted_fg));
        preview.Children().Append(metadata);

        themePreview_.Background(Brush(colors.bg));
        themePreview_.BorderBrush(Brush(colors.shell_border));
        themePreview_.Child(preview);

        UpdateThemeActions();
        if (!resolved.loadedFromFile && !resolved.diagnostic.empty())
            SetThemeStatus(resolved.diagnostic, true);
        else if (!catalog_->Diagnostics().empty())
            SetThemeStatus(catalog_->Diagnostics().front(), true);
        else
            SetThemeStatus({});
    }

    void SettingsView::SetThemeStatus(hstring const& message, bool error)
    {
        themeStatus_.Text(message);
        themeStatus_.Foreground(error ? Brush({196, 43, 28, 255}) : nullptr);
    }

    fire_and_forget SettingsView::ImportThemeAsync()
    {
        auto lifetime = shared_from_this();
        if (detached_) co_return;
        try
        {
            Windows::Storage::Pickers::FileOpenPicker picker;
            picker.SuggestedStartLocation(
                Windows::Storage::Pickers::PickerLocationId::DocumentsLibrary);
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
            SetThemeStatus(LocalizeFormat(L"ThemeImportFailed", {error.message()}), true);
        }
        catch (std::exception const& error)
        {
            if (detached_) co_return;
            SetThemeStatus(LocalizeFormat(
                L"ThemeImportFailed", {winrt::to_hstring(error.what())}), true);
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

    void SettingsView::ApplyPendingTheme()
    {
        if (detached_ || !applySettings_ || settings_.themeId == appliedSettings_.themeId)
            return;
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
}
