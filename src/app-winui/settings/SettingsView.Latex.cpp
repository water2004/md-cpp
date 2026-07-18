#include "pch.h"
#include "settings/SettingsView.h"
#include "settings/SettingsViewSupport.h"
#include "localization/Localization.h"

import folia.core.snippet_template;
import folia.core.utf;

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
        StackPanel page = settings_ui::PagePanel();

        auto heading = PageHeading(Localize(L"LatexCommands"));
        page.Children().Append(heading);
        auto description = Text(Localize(L"LatexCommandsDescription"));
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
        Button overviewMenu;
        FontIcon overviewIcon;
        overviewIcon.Glyph(L"\xE712");
        overviewMenu.Content(overviewIcon);
        overviewMenu.Padding(Thickness{8, 4, 8, 4});
        overviewMenu.VerticalAlignment(VerticalAlignment::Center);
        ToolTipService::SetToolTip(
            overviewMenu, box_value(Localize(L"MoreActions")));
        MenuFlyout overviewFlyout;
        MenuFlyoutItem resetUsage;
        resetUsage.Text(Localize(L"ResetLatexUsage"));
        resetUsage.Click([this](auto const&, auto const&) { ResetLatexUsage(); });
        overviewFlyout.Items().Append(resetUsage);
        overviewMenu.Flyout(overviewFlyout);
        Grid::SetColumn(overviewMenu, 1);
        overview.Children().Append(overviewMenu);
        auto overviewCard = Card(overview);
        page.Children().Append(overviewCard);

        Grid listHeader;
        listHeader.ColumnDefinitions().Append(ColumnDefinition{});
        ColumnDefinition addColumn;
        addColumn.Width(GridLengthHelper::Auto());
        listHeader.ColumnDefinitions().Append(addColumn);
        auto listTitle = Text(Localize(L"LatexCommandList"), 18);
        listTitle.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        listTitle.VerticalAlignment(VerticalAlignment::Center);
        listHeader.Children().Append(listTitle);
        Button addCommand;
        addCommand.Content(box_value(Localize(L"AddCommand")));
        addCommand.Click([this](auto const&, auto const&)
        {
            ShowLatexCommandEditorAsync(std::nullopt);
        });
        Grid::SetColumn(addCommand, 1);
        listHeader.Children().Append(addCommand);
        page.Children().Append(listHeader);

        latexCommandList_.Spacing(0);
        page.Children().Append(latexCommandList_);

        latexStatus_.TextWrapping(TextWrapping::Wrap);
        page.Children().Append(latexStatus_);
        RefreshLatexCommandList();

        ScrollViewer scroll;
        scroll.HorizontalScrollMode(ScrollMode::Disabled);
        scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        scroll.VerticalScrollMode(ScrollMode::Enabled);
        scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        scroll.Content(page);
        return scroll;
    }

    void SettingsView::RefreshLatexCommandList()
    {
        if (!latexCatalog_ || !latexCommandList_) return;
        latexCommandList_.Children().Clear();
        for (auto const& command : latexCatalog_->Commands())
        {
            latexCommandList_.Children().Append(BuildLatexCommandSettingsRow(
                command,
                latexCatalog_->RecentScore(command.id),
                [this](std::string id)
                {
                    ShowLatexCommandEditorAsync(std::move(id));
                },
                [this](std::string id) { RemoveLatexCommand(std::move(id)); }));
        }
    }

    fire_and_forget SettingsView::ShowLatexCommandEditorAsync(
        std::optional<std::string> id)
    {
        auto lifetime = shared_from_this();
        if (!latexCatalog_ || editorDialogOpen_) co_return;
        std::optional<folia::LatexCommandDefinition> command;
        if (id)
        {
            auto found = std::ranges::find(
                latexCatalog_->CustomCommands(), *id,
                &folia::LatexCommandDefinition::id);
            if (found == latexCatalog_->CustomCommands().end()) co_return;
            command = *found;
        }

        editorDialogOpen_ = true;
        struct DialogGuard
        {
            bool& open;
            ~DialogGuard() { open = false; }
        } guard{editorDialogOpen_};

        ContentDialog dialog;
        dialog.XamlRoot(navigation_.XamlRoot());
        dialog.Title(box_value(Localize(command ? L"EditLatexCommand" : L"CustomLatexCommand")));
        dialog.PrimaryButtonText(Localize(command ? L"SaveCommand" : L"AddCommand"));
        dialog.CloseButtonText(Localize(L"Cancel"));
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel fields;
        fields.Spacing(10);
        fields.MinWidth(560);
        fields.Children().Append(Text(Localize(L"LatexTemplateHelp"), 12));
        TextBox trigger;
        trigger.Header(box_value(Localize(L"LatexCommand")));
        trigger.PlaceholderText(L"begin{matrix}");
        if (command)
            trigger.Text(winrt::to_hstring(folia::cps_to_utf8(command->trigger)));
        fields.Children().Append(trigger);
        TextBox snippet;
        snippet.Header(box_value(Localize(L"InsertionTemplate")));
        snippet.PlaceholderText(LR"(\\frac{${1}}{${2}}$0)");
        snippet.AcceptsReturn(false);
        snippet.TextWrapping(TextWrapping::NoWrap);
        if (command)
        {
            auto literal = folia::encode_snippet_literal(command->snippet);
            snippet.Text(winrt::to_hstring(folia::cps_to_utf8(literal)));
        }
        fields.Children().Append(snippet);
        TextBox description;
        description.Header(box_value(Localize(L"Description")));
        description.PlaceholderText(Localize(L"LatexDescriptionExample"));
        if (command) description.Text(winrt::to_hstring(command->description));
        fields.Children().Append(description);
        TextBlock error;
        error.TextWrapping(TextWrapping::Wrap);
        error.Foreground(Brush({196, 43, 28, 255}));
        fields.Children().Append(error);
        dialog.Content(fields);

        dialog.PrimaryButtonClick([this, &trigger, &snippet, &description, &error, id](
            auto const&, ContentDialogButtonClickEventArgs const& args)
        {
            auto literal = folia::utf8_to_cps(winrt::to_string(snippet.Text()));
            auto decoded = folia::decode_snippet_literal(literal);
            if (!decoded)
            {
                error.Text(LocalizeFormat(L"SnippetLiteralError", {
                    winrt::to_hstring(decoded.error_offset.value_or(0) + 1)}));
                args.Cancel(true);
                return;
            }
            LatexCommandFormSubmission submission{
                .editingId = id,
                .trigger = folia::utf8_to_cps(winrt::to_string(trigger.Text())),
                .snippet = std::move(decoded.value),
                .description = winrt::to_string(description.Text()),
            };
            if (!SaveLatexCommand(std::move(submission)))
            {
                error.Text(latexStatus_.Text());
                args.Cancel(true);
            }
        });

        co_await dialog.ShowAsync();
    }

    bool SettingsView::SaveLatexCommand(LatexCommandFormSubmission submission)
    {
        if (!latexCatalog_) return false;
        auto editing = submission.editingId.has_value();
        std::optional<hstring> error;
        if (editing)
            error = latexCatalog_->UpdateCustom(*submission.editingId,
                std::move(submission.trigger),
                std::move(submission.snippet),
                std::move(submission.description));
        else
            error = latexCatalog_->AddCustom(
                std::move(submission.trigger),
                std::move(submission.snippet),
                std::move(submission.description));
        if (error)
        {
            SetLatexStatus(*error, true);
            return false;
        }
        SetLatexStatus(Localize(editing ? L"LatexCommandSaved" : L"LatexCommandAdded"));
        RefreshLatexCommandList();
        return true;
    }

    void SettingsView::RemoveLatexCommand(std::string id)
    {
        if (!latexCatalog_) return;
        if (auto error = latexCatalog_->RemoveCustom(id))
        {
            SetLatexStatus(*error, true);
            return;
        }
        SetLatexStatus(Localize(L"LatexCommandRemoved"));
        RefreshLatexCommandList();
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
