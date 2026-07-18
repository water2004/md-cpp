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
    using settings_ui::LanguageIds;
    using settings_ui::LanguageIndex;
    using settings_ui::PageHeading;
    using settings_ui::PagePanel;
    using settings_ui::Text;

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

    void SettingsView::SetGeneralStatus(hstring const& message, bool error)
    {
        generalStatus_.Text(message);
        generalStatus_.Foreground(error ? Brush({196, 43, 28, 255}) : nullptr);
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
        if (detached_ || !applySettings_ || settings_.languageId == appliedSettings_.languageId)
            return;
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
}
