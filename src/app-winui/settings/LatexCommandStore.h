#pragma once

import folia.core.latex_command_catalog;

namespace winrt::Folia
{
    struct LatexCommandStoredState
    {
        std::vector<folia::LatexCommandDefinition> builtIn;
        std::vector<folia::LatexCommandDefinition> custom;
        std::unordered_map<std::string, folia::LatexCommandUsage> usage;
        std::vector<winrt::hstring> diagnostics;
    };

    class LatexCommandStore
    {
    public:
        LatexCommandStoredState Load() const;
        std::optional<winrt::hstring> Save(
            std::span<folia::LatexCommandDefinition const> custom,
            std::unordered_map<std::string, folia::LatexCommandUsage> const& usage) const;

    private:
        std::vector<folia::LatexCommandDefinition> LoadBuiltIns() const;
        void LoadUserState(
            std::vector<folia::LatexCommandDefinition>& custom,
            std::unordered_map<std::string, folia::LatexCommandUsage>& usage) const;
    };
}
