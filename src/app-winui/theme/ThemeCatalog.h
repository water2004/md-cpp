#pragma once

#include "settings/AppSettings.h"
#include "theme/ThemeConfig.h"

#include <string_view>
#include <vector>

namespace winrt::Folia
{
    struct ThemeEntry
    {
        folia::ThemeProfile profile;
        std::filesystem::path path;
        bool builtIn = false;
    };

    class ThemeCatalog
    {
    public:
        void Refresh();
        std::vector<ThemeEntry> const& Entries() const { return entries_; }
        std::vector<winrt::hstring> const& Diagnostics() const { return diagnostics_; }
        ThemeLoadResult Resolve(std::string_view id, folia::Theme systemVariant) const;
        std::optional<winrt::hstring> Import(std::filesystem::path const& source);
        std::optional<winrt::hstring> Remove(std::string_view id);

        static std::filesystem::path CustomThemeDirectory();

    private:
        std::vector<ThemeEntry> entries_;
        std::vector<winrt::hstring> diagnostics_;
    };
}
