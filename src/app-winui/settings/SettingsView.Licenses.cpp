#include "pch.h"
#include "settings/SettingsView.h"
#include "settings/SettingsViewSupport.h"
#include "localization/Localization.h"
#include "storage/AssetPaths.h"

namespace winrt::Folia
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using settings_ui::Card;
    using settings_ui::FileUri;
    using settings_ui::PageHeading;
    using settings_ui::PagePanel;
    using settings_ui::ReadUtf8Lines;
    using settings_ui::Text;

    UIElement SettingsView::BuildLicensesPage()
    {
        Grid page;
        page.Margin(Thickness{24, 18, 24, 24});
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
                <TextBlock Text="{Binding}" TextWrapping="Wrap" FontFamily="Consolas"
                    FontSize="14" MinHeight="20" Padding="4,0,4,0"/>
            </DataTemplate>)").as<DataTemplate>();
        auto itemContainerStyle = Markup::XamlReader::Load(LR"(
            <Style xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
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
        ScrollViewer::SetHorizontalScrollBarVisibility(
            licenseList_, ScrollBarVisibility::Disabled);
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
        logo.Stretch(Media::Stretch::Uniform);
        logo.Source(Media::Imaging::BitmapImage(
            FileUri(AssetPath(std::filesystem::path(L"branding") / L"Folia.png"))));
        identity.Children().Append(logo);

        StackPanel copy;
        copy.Spacing(8);
        auto name = Text(Localize(L"AppName"), 30);
        name.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        copy.Children().Append(name);
        copy.Children().Append(Text(LocalizeFormat(L"Version", {L"0.1.0"})));
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
                L"UnableLoadLicense", {winrt::to_hstring(error.what())}));
        }
    }
}
