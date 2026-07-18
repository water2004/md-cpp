#pragma once

import folia.core.latex_completion;

namespace winrt::Folia
{
    class LatexCommandCatalog
    {
    public:
        LatexCommandCatalog();

        std::vector<folia::LatexCommandDefinition> const& Commands() const { return commands_; }
        std::vector<folia::LatexCommandDefinition> const& CustomCommands() const { return custom_; }
        std::unordered_map<std::string, folia::LatexCommandUsage> const& Usage() const { return usage_; }
        std::vector<winrt::hstring> const& Diagnostics() const { return diagnostics_; }
        std::uint64_t Revision() const { return revision_; }

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
        void RecordUsage(std::string_view id);
        double RecentScore(std::string_view id) const;
        std::optional<winrt::hstring> Flush();

    private:
        void LoadBuiltIns();
        void LoadUserState();
        void Rebuild();
        std::optional<winrt::hstring> SaveUserState();
        std::int64_t NowSeconds() const;

        std::vector<folia::LatexCommandDefinition> builtIn_;
        std::vector<folia::LatexCommandDefinition> custom_;
        std::vector<folia::LatexCommandDefinition> commands_;
        std::unordered_map<std::string, folia::LatexCommandUsage> usage_;
        std::vector<winrt::hstring> diagnostics_;
        std::uint64_t revision_ = 1;
        std::size_t usageChangesSinceFlush_ = 0;
        bool dirty_ = false;
    };
}
