#include "pch.h"
#include "MainWindow.xaml.h"
#include "localization/Localization.h"

import folia.core.search;
import folia.core.utf;

namespace winrt::Folia::implementation
{
    namespace
    {
        std::u32string SearchText(winrt::hstring const& text)
        {
            return folia::utf8_to_cps(winrt::to_string(text));
        }
    }

    folia::SearchOptions MainWindow::CurrentSearchOptions()
    {
        return {
            FindRegexButton().IsChecked().GetBoolean(),
            FindCaseButton().IsChecked().GetBoolean()};
    }

    void MainWindow::ShowFindBar(bool replace)
    {
        if (settingsMode) SetSettingsMode(false);
        FindReplaceBar().Visibility(Microsoft::UI::Xaml::Visibility::Visible);
        ReplaceControls().Visibility(replace
            ? Microsoft::UI::Xaml::Visibility::Visible
            : Microsoft::UI::Xaml::Visibility::Collapsed);
        FindQueryBox().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
        FindQueryBox().SelectAll();
        RefreshSearch(true);
    }

    void MainWindow::HideFindBar()
    {
        if (FindReplaceBar().Visibility() == Microsoft::UI::Xaml::Visibility::Collapsed) return;
        FindReplaceBar().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
        editorSession.ClearSearch();
        activeSearchMatch.reset();
        searchMatchCount = 0;
        RenderEditorSurface();
        EditorSurface().Focus(Microsoft::UI::Xaml::FocusState::Programmatic);
    }

    void MainWindow::RefreshSearch(bool activateMatch)
    {
        if (updatingSearch
            || FindReplaceBar().Visibility() == Microsoft::UI::Xaml::Visibility::Collapsed) return;
        updatingSearch = true;
        auto query = SearchText(FindQueryBox().Text());
        auto summary = editorSession.Search(query, CurrentSearchOptions());
        searchMatchCount = summary.matchCount;
        if (summary.error)
        {
            FindMatchCountText().Text(LocalizeFormat(
                L"FindError", {winrt::to_hstring(*summary.error)}));
            activeSearchMatch.reset();
        }
        else if (summary.matchCount == 0)
        {
            FindMatchCountText().Text(Localize(L"FindNoResults"));
            activeSearchMatch.reset();
        }
        else
        {
            if (!activeSearchMatch || *activeSearchMatch >= summary.matchCount)
                activeSearchMatch = 0;
            if (activateMatch && editorSession.ActivateSearchMatch(*activeSearchMatch))
            {
                textInputController.NotifySelectionChanged();
                editorRenderer.ScrollToPosition(editorSession.Selection().active);
            }
            FindMatchCountText().Text(LocalizeFormat(
                L"FindResultCount",
                {winrt::to_hstring(*activeSearchMatch + 1), winrt::to_hstring(summary.matchCount)}));
        }
        FindPreviousButton().IsEnabled(summary.matchCount != 0 && !summary.error);
        FindNextButton().IsEnabled(summary.matchCount != 0 && !summary.error);
        ReplaceCurrentButton().IsEnabled(summary.matchCount != 0 && !summary.error);
        ReplaceAllButton().IsEnabled(summary.matchCount != 0 && !summary.error);
        updatingSearch = false;
        RenderEditorSurface();
    }

    void MainWindow::NavigateSearch(int direction)
    {
        if (searchMatchCount == 0) return;
        auto current = activeSearchMatch.value_or(0);
        if (direction < 0)
            current = current == 0 ? searchMatchCount - 1 : current - 1;
        else
            current = (current + 1) % searchMatchCount;
        activeSearchMatch = current;
        if (!editorSession.ActivateSearchMatch(current)) return;
        FindMatchCountText().Text(LocalizeFormat(
            L"FindResultCount",
            {winrt::to_hstring(current + 1), winrt::to_hstring(searchMatchCount)}));
        textInputController.NotifySelectionChanged();
        editorRenderer.ScrollToPosition(editorSession.Selection().active);
        RenderEditorSurface();
    }

    void MainWindow::ReplaceCurrentSearchMatch()
    {
        if (!activeSearchMatch) return;
        auto query = SearchText(FindQueryBox().Text());
        auto replacement = SearchText(ReplaceTextBox().Text());
        if (!editorSession.ReplaceSearchMatch(
                *activeSearchMatch, query, replacement, CurrentSearchOptions())) return;
        UpdateDocumentInfo();
        sidebarController.Refresh();
        textInputController.NotifyTextChanged();
        RefreshSearch(true);
    }

    void MainWindow::ReplaceAllSearchMatches()
    {
        auto query = SearchText(FindQueryBox().Text());
        auto replacement = SearchText(ReplaceTextBox().Text());
        if (!editorSession.ReplaceAllSearchMatches(
                query, replacement, CurrentSearchOptions())) return;
        UpdateDocumentInfo();
        sidebarController.Refresh();
        textInputController.NotifyTextChanged();
        activeSearchMatch = 0;
        RefreshSearch(true);
    }
}
