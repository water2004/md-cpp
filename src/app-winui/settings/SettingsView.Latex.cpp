#include "pch.h"
#include "settings/SettingsView.h"
#include "settings/SettingsViewSupport.h"
#include "localization/Localization.h"

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

        auto form = latexCommandEditor_.Build(
            [this](LatexCommandFormSubmission submission)
            {
                return SaveLatexCommand(std::move(submission));
            });
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
            latexCommandList_.Items().Append(BuildLatexCommandSettingsRow(
                command,
                latexCatalog_->RecentScore(command.id),
                [this](std::string id) { EditLatexCommand(std::move(id)); },
                [this](std::string id) { RemoveLatexCommand(std::move(id)); }));
        }
    }

    void SettingsView::EditLatexCommand(std::string id)
    {
        if (!latexCatalog_) return;
        auto found = std::ranges::find(latexCatalog_->CustomCommands(), id,
            &folia::LatexCommandDefinition::id);
        if (found == latexCatalog_->CustomCommands().end()) return;
        latexCommandEditor_.BeginEdit(*found);
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
        if (latexCommandEditor_.Editing(id)) latexCommandEditor_.Reset();
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
