#include "pch.h"
#include "settings/SettingsView.h"
#include "localization/Localization.h"
#include "storage/AssetPaths.h"

import folia.core.utf;

namespace
{
    namespace Xaml = winrt::Microsoft::UI::Xaml;
    namespace Controls = winrt::Microsoft::UI::Xaml::Controls;

    constexpr std::array<std::string_view, 3> LanguageIds{ "system", "en-US", "zh-CN" };
    using ShortcutBinding = folia::platform::editor::EditorShortcutBinding;
    using ShortcutScope = folia::platform::editor::EditorShortcutScope;
    using EditorKey = folia::platform::editor::EditorKey;
    using EditorKeyGesture = folia::platform::editor::EditorKeyGesture;

    std::int32_t LanguageIndex(std::string_view languageId)
    {
        auto found = std::ranges::find(LanguageIds, languageId);
        return found == LanguageIds.end()
            ? 0
            : static_cast<std::int32_t>(std::distance(LanguageIds.begin(), found));
    }

    std::int32_t ScopeIndex(ShortcutScope scope)
    {
        return static_cast<std::int32_t>(scope);
    }

    ShortcutScope ScopeFromIndex(std::int32_t index)
    {
        if (index == 1) return ShortcutScope::Code;
        if (index == 2) return ShortcutScope::Math;
        return ShortcutScope::Global;
    }

    winrt::hstring ActionLabel(ShortcutBinding const& binding)
    {
        if (!binding.custom_name.empty()) return winrt::to_hstring(binding.custom_name);
        constexpr std::array<std::pair<std::string_view, std::wstring_view>, 49> Labels{{
            {"file.open", L"Open"}, {"file.save", L"Save"},
            {"file.save_as", L"ShortcutSaveAs"}, {"file.export_pdf", L"Pdf"},
            {"search.find", L"Find"}, {"search.replace", L"Replace"},
            {"edit.copy", L"Copy"}, {"edit.cut", L"Cut"},
            {"edit.paste", L"Paste"}, {"edit.select_all", L"SelectAll"},
            {"history.undo", L"ShortcutUndo"}, {"history.redo", L"ShortcutRedo"},
            {"format.strong", L"Bold"}, {"format.emphasis", L"Italic"},
            {"format.strikethrough", L"Strikethrough"},
            {"format.inline_code", L"InlineCode"},
            {"block.quote", L"Quote"}, {"block.table", L"Table"},
            {"block.code", L"CodeBlock"}, {"math.inline", L"InlineMath"},
            {"math.block", L"MathBlock"}, {"insert.link", L"Link"},
            {"insert.image", L"Image"}, {"insert.footnote", L"Footnote"},
            {"insert.toc", L"TableOfContents"}, {"callout.note", L"Note"},
            {"callout.tip", L"Tip"}, {"callout.warning", L"Warning"},
            {"view.source_mode", L"SourceMode"},
            {"block.heading1", L"Heading1"}, {"block.heading2", L"Heading2"},
            {"block.ordered_list", L"NumberedList"},
            {"block.unordered_list", L"BulletedList"},
            {"block.task_list", L"TaskList"},
            {"nav.document_start", L"ShortcutDocumentStart"},
            {"nav.document_start_select", L"ShortcutSelectDocumentStart"},
            {"nav.document_end", L"ShortcutDocumentEnd"},
            {"nav.document_end_select", L"ShortcutSelectDocumentEnd"},
            {"table.row_above", L"ShortcutTableRowAbove"},
            {"table.row_below", L"ShortcutTableRowBelow"},
            {"table.column_left", L"ShortcutTableColumnLeft"},
            {"table.column_right", L"ShortcutTableColumnRight"},
            {"table.row_move_up", L"ShortcutTableMoveRowUp"},
            {"table.row_move_down", L"ShortcutTableMoveRowDown"},
            {"table.column_move_left", L"ShortcutTableMoveColumnLeft"},
            {"table.column_move_right", L"ShortcutTableMoveColumnRight"},
            {"table.row_delete", L"ShortcutTableDeleteRow"},
            {"table.column_delete", L"ShortcutTableDeleteColumn"},
            {"", L""},
        }};
        for (auto const& [action, resource] : Labels)
            if (binding.action_id == action) return winrt::Folia::Localize(resource);
        return winrt::to_hstring(binding.action_id);
    }

    winrt::hstring KeyLabel(EditorKey key)
    {
        auto value = static_cast<std::uint32_t>(key);
        if (value >= static_cast<std::uint32_t>(EditorKey::A)
            && value <= static_cast<std::uint32_t>(EditorKey::Z))
            return winrt::hstring(std::wstring(1, static_cast<wchar_t>(value)));
        if (value >= static_cast<std::uint32_t>(EditorKey::Number0)
            && value <= static_cast<std::uint32_t>(EditorKey::Number9))
            return winrt::hstring(std::wstring(1, static_cast<wchar_t>(value)));
        if (value >= static_cast<std::uint32_t>(EditorKey::F1)
            && value <= static_cast<std::uint32_t>(EditorKey::F12))
            return L"F" + winrt::to_hstring(value - static_cast<std::uint32_t>(EditorKey::F1) + 1);
        switch (key)
        {
            case EditorKey::Back: return L"Backspace";
            case EditorKey::Tab: return L"Tab";
            case EditorKey::Enter: return L"Enter";
            case EditorKey::Escape: return L"Esc";
            case EditorKey::Space: return L"Space";
            case EditorKey::PageUp: return L"Page Up";
            case EditorKey::PageDown: return L"Page Down";
            case EditorKey::End: return L"End";
            case EditorKey::Home: return L"Home";
            case EditorKey::Left: return L"Left";
            case EditorKey::Up: return L"Up";
            case EditorKey::Right: return L"Right";
            case EditorKey::Down: return L"Down";
            case EditorKey::DeleteKey: return L"Delete";
            default: return winrt::to_hstring(value);
        }
    }

    winrt::hstring GestureLabel(std::optional<EditorKeyGesture> const& gesture)
    {
        if (!gesture) return winrt::Folia::Localize(L"ShortcutUnassigned");
        winrt::hstring result;
        if (gesture->control) result = L"Ctrl+";
        if (gesture->shift) result = result + L"Shift+";
        if (gesture->alt) result = result + L"Alt+";
        return result + KeyLabel(gesture->key);
    }

    bool ModifierDown(winrt::Windows::System::VirtualKey key)
    {
        auto state = winrt::Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(key);
        return (static_cast<std::uint32_t>(state) & 0x1u) != 0;
    }

    bool IsModifierKey(winrt::Windows::System::VirtualKey key)
    {
        using winrt::Windows::System::VirtualKey;
        return key == VirtualKey::Control || key == VirtualKey::LeftControl || key == VirtualKey::RightControl
            || key == VirtualKey::Shift || key == VirtualKey::LeftShift || key == VirtualKey::RightShift
            || key == VirtualKey::Menu || key == VirtualKey::LeftMenu || key == VirtualKey::RightMenu;
    }

    Xaml::Media::SolidColorBrush Brush(folia::Color color)
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

    winrt::Windows::Foundation::Collections::IObservableVector<winrt::Windows::Foundation::IInspectable>
        ReadUtf8Lines(std::filesystem::path const& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("cannot open text asset");
        std::string source(
            std::istreambuf_iterator<char>{ stream },
            std::istreambuf_iterator<char>{});
        if (source.starts_with("\xEF\xBB\xBF")) source.erase(0, 3);

        auto lines = winrt::single_threaded_observable_vector<winrt::Windows::Foundation::IInspectable>();
        std::size_t start = 0;
        while (start <= source.size())
        {
            auto end = source.find('\n', start);
            if (end == std::string::npos) end = source.size();
            auto length = end - start;
            if (length != 0 && source[start + length - 1] == '\r') --length;
            lines.Append(winrt::box_value(winrt::to_hstring(
                std::string_view{ source }.substr(start, length))));
            if (end == source.size()) break;
            start = end + 1;
        }
        return lines;
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

namespace winrt::Folia
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;

    SettingsView::SettingsView(
        AppSettings settings,
        std::shared_ptr<ThemeCatalog> catalog,
        folia::Theme systemVariant,
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
        // Keep the standard icon column even though this pane cannot collapse.
        // A zero compact width hides the icons and leaves labels against the
        // selection indicator instead of using WinUI's normal navigation inset.
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
        auto themes = NavigationItem(Localize(L"Themes"), L"themes", L"\xE790");
        auto licenses = NavigationItem(Localize(L"Licenses"), L"licenses", L"\xE8A5");
        auto about = NavigationItem(Localize(L"About"), L"about", L"\xE946");
        navigation_.MenuItems().Append(general);
        navigation_.MenuItems().Append(shortcuts);
        navigation_.MenuItems().Append(themes);
        navigation_.MenuItems().Append(licenses);
        navigation_.MenuItems().Append(about);

        generalPage_ = BuildGeneralPage();
        shortcutsPage_ = BuildShortcutsPage();
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

    UIElement SettingsView::BuildShortcutsPage()
    {
        Grid page;
        page.Margin(Thickness{24, 18, 24, 24});
        page.RowSpacing(12);
        for (auto index = 0; index < 6; ++index)
        {
            RowDefinition row;
            row.Height(index == 3
                ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star)
                : GridLengthHelper::Auto());
            page.RowDefinitions().Append(row);
        }

        auto heading = PageHeading(Localize(L"Shortcuts"));
        Grid::SetRow(heading, 0);
        page.Children().Append(heading);
        auto description = Text(Localize(L"ShortcutsDescription"));
        Grid::SetRow(description, 1);
        page.Children().Append(description);

        Grid tableHeader;
        tableHeader.ColumnSpacing(12);
        for (auto width : {2.0, 1.0, 1.2, 0.8})
        {
            ColumnDefinition column;
            column.Width(GridLengthHelper::FromValueAndType(width, GridUnitType::Star));
            tableHeader.ColumnDefinitions().Append(column);
        }
        auto appendHeader = [&](hstring const& label, int column)
        {
            auto text = Text(label, 12);
            text.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            Grid::SetColumn(text, column);
            tableHeader.Children().Append(text);
        };
        appendHeader(Localize(L"ShortcutAction"), 0);
        appendHeader(Localize(L"ShortcutScope"), 1);
        appendHeader(Localize(L"ShortcutKeys"), 2);
        appendHeader(Localize(L"ShortcutOperations"), 3);
        Grid::SetRow(tableHeader, 2);
        page.Children().Append(tableHeader);

        shortcutList_.SelectionMode(ListViewSelectionMode::None);
        shortcutList_.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        Grid::SetRow(shortcutList_, 3);
        page.Children().Append(shortcutList_);

        StackPanel snippetCard;
        snippetCard.Spacing(8);
        auto snippetHeading = Text(Localize(L"CustomInsertionAction"), 18);
        snippetHeading.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        snippetCard.Children().Append(snippetHeading);
        snippetCard.Children().Append(Text(Localize(L"SnippetPlaceholderDescription")));
        Grid form;
        form.ColumnSpacing(8);
        for (auto width : {1.0, 2.0, 0.8})
        {
            ColumnDefinition column;
            column.Width(GridLengthHelper::FromValueAndType(width, GridUnitType::Star));
            form.ColumnDefinitions().Append(column);
        }
        snippetNameBox_.Header(box_value(Localize(L"ActionName")));
        snippetNameBox_.PlaceholderText(Localize(L"ActionNameExample"));
        form.Children().Append(snippetNameBox_);
        snippetTemplateBox_.Header(box_value(Localize(L"InsertionTemplate")));
        snippetTemplateBox_.PlaceholderText(LR"(\frac{$1}{$2}$0)");
        Grid::SetColumn(snippetTemplateBox_, 1);
        form.Children().Append(snippetTemplateBox_);
        snippetScopeBox_.Header(box_value(Localize(L"ShortcutScope")));
        snippetScopeBox_.Items().Append(box_value(Localize(L"ShortcutScopeGlobal")));
        snippetScopeBox_.Items().Append(box_value(Localize(L"ShortcutScopeCode")));
        snippetScopeBox_.Items().Append(box_value(Localize(L"ShortcutScopeMath")));
        snippetScopeBox_.SelectedIndex(0);
        Grid::SetColumn(snippetScopeBox_, 2);
        form.Children().Append(snippetScopeBox_);
        snippetCard.Children().Append(form);

        StackPanel snippetButtons;
        snippetButtons.Orientation(Orientation::Horizontal);
        snippetButtons.Spacing(8);
        saveSnippetButton_.Content(box_value(Localize(L"AddInsertionAction")));
        saveSnippetButton_.Click([this](auto const&, auto const&) { SaveSnippet(); });
        cancelSnippetButton_.Content(box_value(Localize(L"Cancel")));
        cancelSnippetButton_.Visibility(Visibility::Collapsed);
        cancelSnippetButton_.Click([this](auto const&, auto const&) { ResetSnippetForm(); });
        snippetButtons.Children().Append(saveSnippetButton_);
        snippetButtons.Children().Append(cancelSnippetButton_);
        snippetCard.Children().Append(snippetButtons);
        auto card = Card(snippetCard);
        Grid::SetRow(card, 4);
        page.Children().Append(card);

        shortcutStatus_.TextWrapping(TextWrapping::Wrap);
        Grid::SetRow(shortcutStatus_, 5);
        page.Children().Append(shortcutStatus_);
        RefreshShortcutList();
        return page;
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

    UIElement SettingsView::BuildLicensesPage()
    {
        Grid page;
        page.Margin(Thickness{ 24, 18, 24, 24 });
        page.RowSpacing(12);
        for (auto index = 0; index < 5; ++index)
        {
            RowDefinition row;
            row.Height(index == 3
                ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star)
                : GridLengthHelper::Auto());
            page.RowDefinitions().Append(row);
        }

        auto heading = PageHeading(Localize(L"Licenses"));
        Grid::SetRow(heading, 0);
        page.Children().Append(heading);

        auto description = Text(Localize(L"LicensesDescription"));
        Grid::SetRow(description, 1);
        page.Children().Append(description);

        licenseSelector_.Header(box_value(Localize(L"LicenseDocument")));
        licenseSelector_.Items().Append(box_value(Localize(L"FoliaLicense")));
        licenseSelector_.Items().Append(box_value(Localize(L"ThirdPartyNotices")));
        licenseSelector_.SelectedIndex(0);
        licenseSelector_.SelectionChanged([this](auto const&, SelectionChangedEventArgs const&)
        {
            LoadSelectedLicense();
        });
        Grid::SetRow(licenseSelector_, 2);
        page.Children().Append(licenseSelector_);

        auto itemTemplate = Markup::XamlReader::Load(LR"(
            <DataTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
                <TextBlock
                    Text="{Binding}"
                    TextWrapping="Wrap"
                    FontFamily="Consolas"
                    FontSize="14"
                    MinHeight="20"
                    Padding="4,0,4,0"/>
            </DataTemplate>)").as<DataTemplate>();
        auto itemContainerStyle = Markup::XamlReader::Load(LR"(
            <Style
                xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
                TargetType="ListViewItem">
                <Setter Property="HorizontalContentAlignment" Value="Stretch"/>
                <Setter Property="Padding" Value="0"/>
                <Setter Property="MinHeight" Value="20"/>
                <Setter Property="IsTabStop" Value="False"/>
            </Style>)").as<Style>();

        licenseList_.ItemTemplate(itemTemplate);
        licenseList_.ItemContainerStyle(itemContainerStyle);
        licenseList_.SelectionMode(ListViewSelectionMode::None);
        licenseList_.IsItemClickEnabled(false);
        licenseList_.CanDragItems(false);
        licenseList_.CanReorderItems(false);
        licenseList_.HorizontalAlignment(HorizontalAlignment::Stretch);
        licenseList_.VerticalAlignment(VerticalAlignment::Stretch);
        ScrollViewer::SetHorizontalScrollMode(licenseList_, ScrollMode::Disabled);
        ScrollViewer::SetHorizontalScrollBarVisibility(licenseList_, ScrollBarVisibility::Disabled);
        ScrollViewer::SetVerticalScrollMode(licenseList_, ScrollMode::Enabled);
        ScrollViewer::SetVerticalScrollBarVisibility(licenseList_, ScrollBarVisibility::Auto);

        auto licenseCard = Card(licenseList_);
        Grid::SetRow(licenseCard, 3);
        page.Children().Append(licenseCard);

        licenseStatus_.TextWrapping(TextWrapping::Wrap);
        Grid::SetRow(licenseStatus_, 4);
        page.Children().Append(licenseStatus_);
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

    void SettingsView::LoadSelectedLicense()
    {
        auto const index = licenseSelector_.SelectedIndex();
        if (index < 0 || index > 1 || index == loadedLicenseIndex_) return;

        try
        {
            auto const file = index == 0 ? L"LICENSE.txt" : L"THIRD-PARTY-NOTICES.txt";
            auto const path = AssetPath(std::filesystem::path(L"licenses") / file);
            licenseLines_ = ReadUtf8Lines(path);
            licenseList_.ItemsSource(licenseLines_);
            licenseStatus_.Text({});
            loadedLicenseIndex_ = index;
        }
        catch (std::exception const& error)
        {
            licenseLines_ = nullptr;
            licenseList_.ItemsSource(nullptr);
            licenseStatus_.Text(LocalizeFormat(
                L"UnableLoadLicense",
                { winrt::to_hstring(error.what()) }));
        }
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
            profile.variant == folia::Theme::Light ? Localize(L"LightProfile")
                : profile.variant == folia::Theme::HighContrast ? Localize(L"HighContrastProfile")
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

    void SettingsView::SetShortcutStatus(hstring const& message, bool error)
    {
        shortcutStatus_.Text(message);
        shortcutStatus_.Foreground(error ? Brush({196, 43, 28, 255}) : nullptr);
    }

    void SettingsView::RefreshShortcutList()
    {
        refreshing_ = true;
        struct ResetRefreshing { bool& value; ~ResetRefreshing() { value = false; } } reset{refreshing_};
        capturingShortcut_.reset();
        shortcutButtons_.clear();
        shortcutList_.Items().Clear();
        shortcutButtons_.reserve(settings_.shortcutBindings.size());

        for (std::size_t index = 0; index < settings_.shortcutBindings.size(); ++index)
        {
            auto const& binding = settings_.shortcutBindings[index];
            Grid row;
            row.Padding(Thickness{4, 4, 4, 4});
            row.ColumnSpacing(12);
            for (auto width : {2.0, 1.0, 1.2, 0.8})
            {
                ColumnDefinition column;
                column.Width(GridLengthHelper::FromValueAndType(width, GridUnitType::Star));
                row.ColumnDefinitions().Append(column);
            }

            auto action = Text(ActionLabel(binding));
            action.VerticalAlignment(VerticalAlignment::Center);
            row.Children().Append(action);

            ComboBox scope;
            scope.Items().Append(box_value(Localize(L"ShortcutScopeGlobal")));
            scope.Items().Append(box_value(Localize(L"ShortcutScopeCode")));
            scope.Items().Append(box_value(Localize(L"ShortcutScopeMath")));
            scope.SelectedIndex(ScopeIndex(binding.scope));
            scope.SelectionChanged([this, index, scope](auto const&, auto const&)
            {
                if (!refreshing_) ChangeShortcutScope(index, scope.SelectedIndex());
            });
            Grid::SetColumn(scope, 1);
            row.Children().Append(scope);

            Button capture;
            capture.HorizontalAlignment(HorizontalAlignment::Stretch);
            capture.HorizontalContentAlignment(HorizontalAlignment::Center);
            capture.Content(box_value(GestureLabel(binding.gesture)));
            capture.Click([this, index](auto const&, auto const&) { BeginShortcutCapture(index); });
            capture.KeyDown([this, index](
                auto const&,
                Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
            {
                CaptureShortcut(index, args);
            });
            Grid::SetColumn(capture, 2);
            row.Children().Append(capture);
            shortcutButtons_.push_back(capture);

            StackPanel operations;
            operations.Orientation(Orientation::Horizontal);
            operations.Spacing(6);
            Button clear;
            clear.Content(box_value(Localize(L"Clear")));
            clear.Click([this, index](auto const&, auto const&) { ClearShortcut(index); });
            operations.Children().Append(clear);
            if (binding.action_kind == folia::platform::editor::EditorShortcutActionKind::InsertSnippet)
            {
                Button edit;
                edit.Content(box_value(Localize(L"Edit")));
                edit.Click([this, index](auto const&, auto const&) { EditSnippet(index); });
                operations.Children().Append(edit);
                Button remove;
                remove.Content(box_value(Localize(L"Remove")));
                remove.Click([this, index](auto const&, auto const&) { RemoveSnippet(index); });
                operations.Children().Append(remove);
            }
            Grid::SetColumn(operations, 3);
            row.Children().Append(operations);

            ListViewItem item;
            item.HorizontalContentAlignment(HorizontalAlignment::Stretch);
            item.Content(row);
            shortcutList_.Items().Append(item);
        }
    }

    void SettingsView::BeginShortcutCapture(std::size_t index)
    {
        if (index >= shortcutButtons_.size()) return;
        if (capturingShortcut_ && *capturingShortcut_ < shortcutButtons_.size())
            shortcutButtons_[*capturingShortcut_].Content(box_value(
                GestureLabel(settings_.shortcutBindings[*capturingShortcut_].gesture)));
        capturingShortcut_ = index;
        shortcutButtons_[index].Content(box_value(Localize(L"PressShortcut")));
        shortcutButtons_[index].Focus(FocusState::Programmatic);
        SetShortcutStatus(Localize(L"ShortcutCaptureHint"));
    }

    void SettingsView::CaptureShortcut(
        std::size_t index,
        Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        if (!capturingShortcut_ || *capturingShortcut_ != index
            || index >= settings_.shortcutBindings.size()) return;
        auto key = args.Key();
        if (key == Windows::System::VirtualKey::Escape)
        {
            capturingShortcut_.reset();
            shortcutButtons_[index].Content(box_value(
                GestureLabel(settings_.shortcutBindings[index].gesture)));
            SetShortcutStatus({});
            args.Handled(true);
            return;
        }
        if (IsModifierKey(key))
        {
            args.Handled(true);
            return;
        }
        auto gesture = EditorKeyGesture{
            .key = static_cast<EditorKey>(static_cast<std::uint32_t>(key)),
            .control = ModifierDown(Windows::System::VirtualKey::Control)
                || ModifierDown(Windows::System::VirtualKey::LeftControl)
                || ModifierDown(Windows::System::VirtualKey::RightControl),
            .shift = ModifierDown(Windows::System::VirtualKey::Shift)
                || ModifierDown(Windows::System::VirtualKey::LeftShift)
                || ModifierDown(Windows::System::VirtualKey::RightShift),
            .alt = ModifierDown(Windows::System::VirtualKey::Menu)
                || ModifierDown(Windows::System::VirtualKey::LeftMenu)
                || ModifierDown(Windows::System::VirtualKey::RightMenu),
        };
        if ((key == Windows::System::VirtualKey::Back
                || key == Windows::System::VirtualKey::Delete)
            && !gesture.control && !gesture.shift && !gesture.alt)
        {
            ClearShortcut(index);
            args.Handled(true);
            return;
        }
        auto const scope = settings_.shortcutBindings[index].scope;
        if (auto conflict = folia::platform::editor::find_editor_shortcut_conflict(
            settings_.shortcutBindings, gesture, scope, index))
        {
            SetShortcutStatus(LocalizeFormat(
                L"ShortcutConflict",
                {GestureLabel(gesture), ActionLabel(settings_.shortcutBindings[*conflict])}), true);
            args.Handled(true);
            return;
        }
        settings_.shortcutBindings[index].gesture = gesture;
        capturingShortcut_.reset();
        if (ApplyShortcutSettings()) RefreshShortcutList();
        args.Handled(true);
    }

    void SettingsView::ClearShortcut(std::size_t index)
    {
        if (index >= settings_.shortcutBindings.size()) return;
        settings_.shortcutBindings[index].gesture.reset();
        capturingShortcut_.reset();
        if (ApplyShortcutSettings()) RefreshShortcutList();
    }

    void SettingsView::ChangeShortcutScope(std::size_t index, std::int32_t selectedIndex)
    {
        if (index >= settings_.shortcutBindings.size()) return;
        auto& binding = settings_.shortcutBindings[index];
        auto proposed = ScopeFromIndex(selectedIndex);
        if (binding.scope == proposed) return;
        if (binding.gesture)
        {
            if (auto conflict = folia::platform::editor::find_editor_shortcut_conflict(
                settings_.shortcutBindings, *binding.gesture, proposed, index))
            {
                SetShortcutStatus(LocalizeFormat(
                    L"ShortcutConflict",
                    {GestureLabel(binding.gesture), ActionLabel(settings_.shortcutBindings[*conflict])}), true);
                RefreshShortcutList();
                return;
            }
        }
        binding.scope = proposed;
        if (ApplyShortcutSettings()) RefreshShortcutList();
    }

    bool SettingsView::ApplyShortcutSettings()
    {
        if (detached_ || !applySettings_) return false;
        for (std::size_t index = 0; index < settings_.shortcutBindings.size(); ++index)
        {
            auto const& binding = settings_.shortcutBindings[index];
            if (!binding.gesture) continue;
            if (auto conflict = folia::platform::editor::find_editor_shortcut_conflict(
                settings_.shortcutBindings, *binding.gesture, binding.scope, index))
            {
                SetShortcutStatus(LocalizeFormat(
                    L"ShortcutConflict",
                    {GestureLabel(binding.gesture), ActionLabel(settings_.shortcutBindings[*conflict])}), true);
                return false;
            }
        }
        auto proposed = appliedSettings_;
        proposed.shortcutBindings = settings_.shortcutBindings;
        if (auto error = applySettings_(proposed))
        {
            settings_.shortcutBindings = appliedSettings_.shortcutBindings;
            SetShortcutStatus(*error, true);
            RefreshShortcutList();
            return false;
        }
        appliedSettings_.shortcutBindings = proposed.shortcutBindings;
        SetShortcutStatus(Localize(L"ShortcutsSaved"));
        return true;
    }

    void SettingsView::EditSnippet(std::size_t index)
    {
        if (index >= settings_.shortcutBindings.size()) return;
        auto const& binding = settings_.shortcutBindings[index];
        if (binding.action_kind != folia::platform::editor::EditorShortcutActionKind::InsertSnippet)
            return;
        editingSnippet_ = index;
        snippetNameBox_.Text(winrt::to_hstring(binding.custom_name));
        snippetTemplateBox_.Text(winrt::to_hstring(folia::cps_to_utf8(binding.snippet)));
        snippetScopeBox_.SelectedIndex(ScopeIndex(binding.scope));
        saveSnippetButton_.Content(box_value(Localize(L"SaveInsertionAction")));
        cancelSnippetButton_.Visibility(Visibility::Visible);
    }

    void SettingsView::SaveSnippet()
    {
        auto name = winrt::to_string(snippetNameBox_.Text());
        auto source = winrt::to_string(snippetTemplateBox_.Text());
        if (name.empty() || source.empty())
        {
            SetShortcutStatus(Localize(L"SnippetFieldsRequired"), true);
            return;
        }
        if (editingSnippet_ && *editingSnippet_ < settings_.shortcutBindings.size())
        {
            auto& binding = settings_.shortcutBindings[*editingSnippet_];
            auto proposedScope = ScopeFromIndex(snippetScopeBox_.SelectedIndex());
            if (binding.gesture)
            {
                if (auto conflict = folia::platform::editor::find_editor_shortcut_conflict(
                    settings_.shortcutBindings, *binding.gesture, proposedScope, *editingSnippet_))
                {
                    SetShortcutStatus(LocalizeFormat(
                        L"ShortcutConflict",
                        {GestureLabel(binding.gesture), ActionLabel(settings_.shortcutBindings[*conflict])}), true);
                    return;
                }
            }
            binding.custom_name = std::move(name);
            binding.snippet = folia::utf8_to_cps(source);
            binding.scope = proposedScope;
        }
        else
        {
            auto id = std::string("snippet.") + std::to_string(GetTickCount64());
            while (std::ranges::find(settings_.shortcutBindings, id, &ShortcutBinding::id)
                != settings_.shortcutBindings.end()) id.push_back('x');
            settings_.shortcutBindings.push_back({
                .id = id,
                .action_id = id,
                .custom_name = std::move(name),
                .action_kind = folia::platform::editor::EditorShortcutActionKind::InsertSnippet,
                .scope = ScopeFromIndex(snippetScopeBox_.SelectedIndex()),
                .gesture = std::nullopt,
                .snippet = folia::utf8_to_cps(source),
            });
        }
        if (ApplyShortcutSettings())
        {
            ResetSnippetForm();
            RefreshShortcutList();
        }
    }

    void SettingsView::RemoveSnippet(std::size_t index)
    {
        if (index >= settings_.shortcutBindings.size()
            || settings_.shortcutBindings[index].action_kind
                != folia::platform::editor::EditorShortcutActionKind::InsertSnippet) return;
        settings_.shortcutBindings.erase(settings_.shortcutBindings.begin() + index);
        if (editingSnippet_ == index) ResetSnippetForm();
        else editingSnippet_.reset();
        if (ApplyShortcutSettings()) RefreshShortcutList();
    }

    void SettingsView::ResetSnippetForm()
    {
        editingSnippet_.reset();
        snippetNameBox_.Text({});
        snippetTemplateBox_.Text({});
        snippetScopeBox_.SelectedIndex(0);
        saveSnippetButton_.Content(box_value(Localize(L"AddInsertionAction")));
        cancelSnippetButton_.Visibility(Visibility::Collapsed);
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

    void SettingsView::Reset(AppSettings settings, folia::Theme systemVariant)
    {
        settings_ = std::move(settings);
        appliedSettings_ = settings_;
        systemVariant_ = systemVariant;
        refreshing_ = true;
        mathToggle_.IsOn(settings_.mathRenderingEnabled);
        languageCombo_.SelectedIndex(LanguageIndex(settings_.languageId));
        refreshing_ = false;
        ResetSnippetForm();
        RefreshShortcutList();
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
