#include "pch.h"
#include "settings/SettingsView.h"
#include "settings/SettingsViewSupport.h"
#include "localization/Localization.h"

import folia.core.utf;

namespace
{
    namespace Xaml = winrt::Microsoft::UI::Xaml;
    namespace Controls = winrt::Microsoft::UI::Xaml::Controls;

    winrt::hstring Description(folia::LatexCommandDefinition const& command)
    {
        if (!command.built_in) return winrt::to_hstring(command.description);
        auto key = winrt::to_hstring(command.description);
        return winrt::Folia::Localize(std::wstring_view{key.c_str(), key.size()});
    }

    winrt::hstring Trigger(folia::LatexCommandDefinition const& command)
    {
        return L"\\" + winrt::to_hstring(folia::cps_to_utf8(command.trigger));
    }
}

namespace winrt::Folia
{
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using settings_ui::Brush;
    using settings_ui::Card;
    using settings_ui::PageHeading;
    using settings_ui::Text;

    UIElement SettingsView::BuildLatexPage()
    {
        Grid page;
        page.Margin(Thickness{24, 18, 24, 24});
        page.RowSpacing(12);
        for (int index = 0; index < 7; ++index)
        {
            RowDefinition row;
            row.Height(index == 3
                ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star)
                : GridLengthHelper::Auto());
            page.RowDefinitions().Append(row);
        }

        auto heading = PageHeading(Localize(L"LatexCommands"));
        Grid::SetRow(heading, 0);
        page.Children().Append(heading);
        auto description = Text(Localize(L"LatexCommandsDescription"));
        Grid::SetRow(description, 1);
        page.Children().Append(description);

        Grid overview;
        overview.ColumnDefinitions().Append(ColumnDefinition{});
        ColumnDefinition actionColumn;
        actionColumn.Width(GridLengthHelper::Auto());
        overview.ColumnDefinitions().Append(actionColumn);
        StackPanel togglePanel;
        togglePanel.Spacing(4);
        latexSuggestionsToggle_.Header(box_value(Localize(L"LatexSuggestionsToggle")));
        latexSuggestionsToggle_.OnContent(box_value(Localize(L"Enabled")));
        latexSuggestionsToggle_.OffContent(box_value(Localize(L"Disabled")));
        latexSuggestionsToggle_.IsOn(settings_.latexSuggestionsEnabled);
        latexSuggestionsToggle_.Toggled([this](auto const&, auto const&)
        {
            if (refreshing_) return;
            settings_.latexSuggestionsEnabled = latexSuggestionsToggle_.IsOn();
            ApplyLatexSetting();
        });
        togglePanel.Children().Append(latexSuggestionsToggle_);
        togglePanel.Children().Append(Text(Localize(L"LatexRankingDescription"), 12));
        overview.Children().Append(togglePanel);
        Button resetUsage;
        resetUsage.Content(box_value(Localize(L"ResetLatexUsage")));
        resetUsage.VerticalAlignment(VerticalAlignment::Center);
        resetUsage.Click([this](auto const&, auto const&) { ResetLatexUsage(); });
        Grid::SetColumn(resetUsage, 1);
        overview.Children().Append(resetUsage);
        auto overviewCard = Card(overview);
        Grid::SetRow(overviewCard, 2);
        page.Children().Append(overviewCard);

        latexCommandList_.SelectionMode(ListViewSelectionMode::None);
        latexCommandList_.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        latexCommandList_.Padding(Thickness{0, 4, 0, 4});
        Grid::SetRow(latexCommandList_, 3);
        page.Children().Append(latexCommandList_);

        StackPanel formCard;
        formCard.Spacing(8);
        auto formHeading = Text(Localize(L"CustomLatexCommand"), 18);
        formHeading.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        formCard.Children().Append(formHeading);
        formCard.Children().Append(Text(Localize(L"LatexTemplateHelp"), 12));
        Grid fields;
        fields.ColumnSpacing(10);
        for (auto width : {0.65, 1.6, 1.0})
        {
            ColumnDefinition column;
            column.Width(GridLengthHelper::FromValueAndType(width, GridUnitType::Star));
            fields.ColumnDefinitions().Append(column);
        }
        latexTriggerBox_.Header(box_value(Localize(L"LatexCommand")));
        latexTriggerBox_.PlaceholderText(LR"(frac)");
        fields.Children().Append(latexTriggerBox_);
        latexTemplateBox_.Header(box_value(Localize(L"InsertionTemplate")));
        latexTemplateBox_.PlaceholderText(LR"(\frac{$1}{$2}$0)");
        Grid::SetColumn(latexTemplateBox_, 1);
        fields.Children().Append(latexTemplateBox_);
        latexDescriptionBox_.Header(box_value(Localize(L"Description")));
        latexDescriptionBox_.PlaceholderText(Localize(L"LatexDescriptionExample"));
        Grid::SetColumn(latexDescriptionBox_, 2);
        fields.Children().Append(latexDescriptionBox_);
        formCard.Children().Append(fields);

        StackPanel buttons;
        buttons.Orientation(Orientation::Horizontal);
        buttons.Spacing(8);
        saveLatexButton_.Content(box_value(Localize(L"AddCommand")));
        saveLatexButton_.Click([this](auto const&, auto const&) { SaveLatexCommand(); });
        cancelLatexButton_.Content(box_value(Localize(L"Cancel")));
        cancelLatexButton_.Visibility(Visibility::Collapsed);
        cancelLatexButton_.Click([this](auto const&, auto const&) { ResetLatexForm(); });
        buttons.Children().Append(saveLatexButton_);
        buttons.Children().Append(cancelLatexButton_);
        formCard.Children().Append(buttons);
        auto form = Card(formCard);
        Grid::SetRow(form, 4);
        page.Children().Append(form);

        latexStatus_.TextWrapping(TextWrapping::Wrap);
        Grid::SetRow(latexStatus_, 5);
        page.Children().Append(latexStatus_);
        RefreshLatexCommandList();
        return page;
    }

    void SettingsView::RefreshLatexCommandList()
    {
        if (!latexCatalog_ || !latexCommandList_) return;
        latexCommandList_.Items().Clear();
        for (auto const& command : latexCatalog_->Commands())
        {
            Grid row;
            row.MinHeight(68);
            row.Padding(Thickness{10, 7, 8, 7});
            row.ColumnSpacing(12);
            for (auto width : {0.75, 1.15, 1.8, 0.65})
            {
                ColumnDefinition column;
                column.Width(GridLengthHelper::FromValueAndType(width, GridUnitType::Star));
                row.ColumnDefinitions().Append(column);
            }

            StackPanel identity;
            identity.Spacing(4);
            auto trigger = Text(Trigger(command), 16);
            trigger.FontFamily(Media::FontFamily(L"Cascadia Mono"));
            trigger.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            trigger.Foreground(Brush({60, 130, 246, 255}));
            identity.Children().Append(trigger);
            Border source;
            source.HorizontalAlignment(HorizontalAlignment::Left);
            source.Padding(Thickness{7, 2, 7, 2});
            source.CornerRadius(CornerRadius{9});
            source.Background(Brush(command.built_in
                ? folia::Color{90, 120, 150, 36}
                : folia::Color{60, 160, 100, 42}));
            source.Child(Text(Localize(command.built_in ? L"BuiltIn" : L"Custom"), 11));
            identity.Children().Append(source);
            row.Children().Append(identity);

            StackPanel detail;
            detail.Spacing(3);
            auto description = Text(Description(command), 14);
            description.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            detail.Children().Append(description);
            auto score = latexCatalog_->RecentScore(command.id);
            detail.Children().Append(Text(score > 0.01
                ? LocalizeFormat(L"LatexRecentScore", {winrt::to_hstring(std::format("{:.1f}", score))})
                : Localize(L"LatexNotRecentlyUsed"), 11));
            Grid::SetColumn(detail, 1);
            row.Children().Append(detail);

            auto snippet = Text(winrt::to_hstring(folia::cps_to_utf8(command.snippet)), 12);
            snippet.FontFamily(Media::FontFamily(L"Cascadia Mono"));
            snippet.TextTrimming(TextTrimming::CharacterEllipsis);
            snippet.MaxLines(2);
            snippet.VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(snippet, 2);
            row.Children().Append(snippet);

            StackPanel actions;
            actions.Orientation(Orientation::Horizontal);
            actions.Spacing(6);
            actions.HorizontalAlignment(HorizontalAlignment::Right);
            actions.VerticalAlignment(VerticalAlignment::Center);
            if (!command.built_in)
            {
                Button edit;
                edit.Content(box_value(Localize(L"Edit")));
                edit.Click([this, id = command.id](auto const&, auto const&) { EditLatexCommand(id); });
                actions.Children().Append(edit);
                Button remove;
                remove.Content(box_value(Localize(L"Remove")));
                remove.Click([this, id = command.id](auto const&, auto const&) { RemoveLatexCommand(id); });
                actions.Children().Append(remove);
            }
            Grid::SetColumn(actions, 3);
            row.Children().Append(actions);

            Border separator;
            separator.BorderThickness(Thickness{0, 0, 0, 1});
            separator.BorderBrush(Brush({128, 128, 128, 40}));
            separator.Child(row);
            latexCommandList_.Items().Append(separator);
        }
    }

    void SettingsView::EditLatexCommand(std::string id)
    {
        if (!latexCatalog_) return;
        auto found = std::ranges::find(latexCatalog_->CustomCommands(), id,
            &folia::LatexCommandDefinition::id);
        if (found == latexCatalog_->CustomCommands().end()) return;
        editingLatexCommand_ = found->id;
        latexTriggerBox_.Text(winrt::to_hstring(folia::cps_to_utf8(found->trigger)));
        latexTemplateBox_.Text(winrt::to_hstring(folia::cps_to_utf8(found->snippet)));
        latexDescriptionBox_.Text(winrt::to_hstring(found->description));
        saveLatexButton_.Content(box_value(Localize(L"SaveCommand")));
        cancelLatexButton_.Visibility(Visibility::Visible);
        latexTriggerBox_.Focus(FocusState::Programmatic);
    }

    void SettingsView::SaveLatexCommand()
    {
        if (!latexCatalog_) return;
        auto trigger = folia::utf8_to_cps(winrt::to_string(latexTriggerBox_.Text()));
        auto snippet = folia::utf8_to_cps(winrt::to_string(latexTemplateBox_.Text()));
        auto description = winrt::to_string(latexDescriptionBox_.Text());
        std::optional<hstring> error;
        if (editingLatexCommand_)
            error = latexCatalog_->UpdateCustom(*editingLatexCommand_,
                std::move(trigger), std::move(snippet), std::move(description));
        else
            error = latexCatalog_->AddCustom(
                std::move(trigger), std::move(snippet), std::move(description));
        if (error)
        {
            SetLatexStatus(*error, true);
            return;
        }
        SetLatexStatus(Localize(editingLatexCommand_ ? L"LatexCommandSaved" : L"LatexCommandAdded"));
        ResetLatexForm();
        RefreshLatexCommandList();
    }

    void SettingsView::RemoveLatexCommand(std::string id)
    {
        if (!latexCatalog_) return;
        if (auto error = latexCatalog_->RemoveCustom(id))
        {
            SetLatexStatus(*error, true);
            return;
        }
        if (editingLatexCommand_ == id) ResetLatexForm();
        SetLatexStatus(Localize(L"LatexCommandRemoved"));
        RefreshLatexCommandList();
    }

    void SettingsView::ResetLatexForm()
    {
        editingLatexCommand_.reset();
        latexTriggerBox_.Text({});
        latexTemplateBox_.Text({});
        latexDescriptionBox_.Text({});
        saveLatexButton_.Content(box_value(Localize(L"AddCommand")));
        cancelLatexButton_.Visibility(Visibility::Collapsed);
    }

    void SettingsView::ResetLatexUsage()
    {
        if (!latexCatalog_) return;
        if (auto error = latexCatalog_->ResetUsage())
        {
            SetLatexStatus(*error, true);
            return;
        }
        SetLatexStatus(Localize(L"LatexUsageReset"));
        RefreshLatexCommandList();
    }

    void SettingsView::SetLatexStatus(hstring const& message, bool error)
    {
        latexStatus_.Text(message);
        latexStatus_.Foreground(error ? Brush({196, 43, 28, 255}) : nullptr);
    }

    void SettingsView::ApplyLatexSetting()
    {
        if (detached_ || !applySettings_) return;
        auto proposed = appliedSettings_;
        proposed.latexSuggestionsEnabled = settings_.latexSuggestionsEnabled;
        if (auto error = applySettings_(proposed))
        {
            SetLatexStatus(*error, true);
            refreshing_ = true;
            settings_.latexSuggestionsEnabled = appliedSettings_.latexSuggestionsEnabled;
            latexSuggestionsToggle_.IsOn(settings_.latexSuggestionsEnabled);
            refreshing_ = false;
            return;
        }
        appliedSettings_.latexSuggestionsEnabled = proposed.latexSuggestionsEnabled;
        SetLatexStatus({});
    }
}
