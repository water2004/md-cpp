#include "pch.h"
#include "settings/LatexCommandSettingsComponents.h"
#include "settings/SettingsViewSupport.h"
#include "localization/Localization.h"

import folia.core.utf;

namespace
{
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
    using settings_ui::Text;

    FrameworkElement LatexCommandEditorForm::Build(Submit submit)
    {
        submit_ = std::move(submit);
        if (root_) return root_;

        StackPanel content;
        content.Spacing(8);
        auto heading = Text(Localize(L"CustomLatexCommand"), 18);
        heading.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        content.Children().Append(heading);
        content.Children().Append(Text(Localize(L"LatexTemplateHelp"), 12));

        Grid fields;
        fields.ColumnSpacing(10);
        for (auto width : {0.65, 1.6, 1.0})
        {
            ColumnDefinition column;
            column.Width(GridLengthHelper::FromValueAndType(width, GridUnitType::Star));
            fields.ColumnDefinitions().Append(column);
        }
        triggerBox_.Header(box_value(Localize(L"LatexCommand")));
        triggerBox_.PlaceholderText(LR"(frac)");
        fields.Children().Append(triggerBox_);
        templateBox_.Header(box_value(Localize(L"InsertionTemplate")));
        templateBox_.PlaceholderText(LR"(\frac{$1}{$2}$0)");
        templateBox_.AcceptsReturn(true);
        templateBox_.TextWrapping(TextWrapping::Wrap);
        templateBox_.MinHeight(88);
        templateBox_.MaxHeight(160);
        ScrollViewer::SetVerticalScrollBarVisibility(
            templateBox_, ScrollBarVisibility::Auto);
        Grid::SetColumn(templateBox_, 1);
        fields.Children().Append(templateBox_);
        descriptionBox_.Header(box_value(Localize(L"Description")));
        descriptionBox_.PlaceholderText(Localize(L"LatexDescriptionExample"));
        Grid::SetColumn(descriptionBox_, 2);
        fields.Children().Append(descriptionBox_);
        content.Children().Append(fields);

        StackPanel buttons;
        buttons.Orientation(Orientation::Horizontal);
        buttons.Spacing(8);
        saveButton_.Content(box_value(Localize(L"AddCommand")));
        saveButton_.Click([this](auto const&, auto const&)
        {
            if (!submit_) return;
            LatexCommandFormSubmission submission{
                .editingId = editingId_,
                .trigger = folia::utf8_to_cps(winrt::to_string(triggerBox_.Text())),
                .snippet = folia::utf8_to_cps(winrt::to_string(templateBox_.Text())),
                .description = winrt::to_string(descriptionBox_.Text()),
            };
            if (submit_(std::move(submission))) Reset();
        });
        cancelButton_.Content(box_value(Localize(L"Cancel")));
        cancelButton_.Visibility(Visibility::Collapsed);
        cancelButton_.Click([this](auto const&, auto const&) { Reset(); });
        buttons.Children().Append(saveButton_);
        buttons.Children().Append(cancelButton_);
        content.Children().Append(buttons);

        root_ = Card(content);
        return root_;
    }

    void LatexCommandEditorForm::BeginEdit(folia::LatexCommandDefinition const& command)
    {
        editingId_ = command.id;
        triggerBox_.Text(winrt::to_hstring(folia::cps_to_utf8(command.trigger)));
        templateBox_.Text(winrt::to_hstring(folia::cps_to_utf8(command.snippet)));
        descriptionBox_.Text(winrt::to_hstring(command.description));
        saveButton_.Content(box_value(Localize(L"SaveCommand")));
        cancelButton_.Visibility(Visibility::Visible);
        triggerBox_.Focus(FocusState::Programmatic);
    }

    void LatexCommandEditorForm::Reset()
    {
        editingId_.reset();
        triggerBox_.Text({});
        templateBox_.Text({});
        descriptionBox_.Text({});
        saveButton_.Content(box_value(Localize(L"AddCommand")));
        cancelButton_.Visibility(Visibility::Collapsed);
    }

    bool LatexCommandEditorForm::Editing(std::string_view id) const
    {
        return editingId_ && *editingId_ == id;
    }

    FrameworkElement BuildLatexCommandSettingsRow(
        folia::LatexCommandDefinition const& command,
        double recentScore,
        std::function<void(std::string)> edit,
        std::function<void(std::string)> remove)
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
        detail.Children().Append(Text(recentScore > 0.01
            ? LocalizeFormat(L"LatexRecentScore", {
                winrt::to_hstring(std::format("{:.1f}", recentScore))})
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
            Button editButton;
            editButton.Content(box_value(Localize(L"Edit")));
            editButton.Click([edit = std::move(edit), id = command.id](auto const&, auto const&)
            {
                if (edit) edit(id);
            });
            actions.Children().Append(editButton);
            Button removeButton;
            removeButton.Content(box_value(Localize(L"Remove")));
            removeButton.Click([remove = std::move(remove), id = command.id](auto const&, auto const&)
            {
                if (remove) remove(id);
            });
            actions.Children().Append(removeButton);
        }
        Grid::SetColumn(actions, 3);
        row.Children().Append(actions);

        Border separator;
        separator.BorderThickness(Thickness{0, 0, 0, 1});
        separator.BorderBrush(Brush({128, 128, 128, 40}));
        separator.Child(row);
        return separator;
    }
}
