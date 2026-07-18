#pragma once

#include "settings/LatexCommandStore.h"

import folia.core.latex_command_catalog;

namespace winrt::Folia
{
    class LatexCommandCatalog
    {
    public:
        LatexCommandCatalog();

        std::vector<folia::LatexCommandDefinition> const& Commands() const { return state_.Commands(); }
        std::vector<folia::LatexCommandDefinition> const& CustomCommands() const { return state_.CustomCommands(); }
        std::vector<winrt::hstring> const& Diagnostics() const { return diagnostics_; }

        std::optional<winrt::hstring> AddCustom(
            std::u32string trigger,
            std::u32string snippet,
            std::string description);
        std::optional<winrt::hstring> UpdateCustom(
            std::string_view id,
            std::u32string trigger,
            std::u32string snippet,
            std::string description);
        std::optional<winrt::hstring> RemoveCustom(std::string_view id);
        std::optional<winrt::hstring> ResetUsage();
        std::vector<folia::LatexCompletionCandidate> Query(
            std::u32string_view prefix,
            std::size_t limit = 8) const;
        void RecordUsage(std::string_view id);
        double RecentScore(std::string_view id) const;
        std::optional<winrt::hstring> Flush();

    private:
        std::optional<winrt::hstring> SaveUserState();
        std::optional<winrt::hstring> CommitMutation(
            folia::LatexCatalogMutationResult const& result);
        winrt::hstring MutationError(folia::LatexCatalogMutationError error) const;
        std::int64_t NowSeconds() const;

        LatexCommandStore store_;
        folia::LatexCommandCatalogState state_;
        std::vector<winrt::hstring> diagnostics_;
        std::size_t usageChangesSinceFlush_ = 0;
        bool dirty_ = false;
    };
}
