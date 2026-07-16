#include "pch.h"
#include "theme/ThemeCatalog.h"
#include "localization/Localization.h"
#include "storage/AssetPaths.h"

namespace
{
    void ScanThemeDirectory(
        std::filesystem::path const& directory,
        bool builtIn,
        std::unordered_set<std::string>& ids,
        std::vector<winrt::ElMd::ThemeEntry>& entries,
        std::vector<winrt::hstring>& diagnostics)
    {
        std::error_code error;
        if (!std::filesystem::exists(directory, error)) return;
        for (auto const& item : std::filesystem::directory_iterator(directory, error))
        {
            if (error) break;
            if (!item.is_regular_file() || item.path().extension() != L".json") continue;
            auto loaded = winrt::ElMd::LoadThemeFile(item.path());
            if (!loaded.profile)
            {
                diagnostics.push_back(std::move(loaded.diagnostic));
                continue;
            }
            if (!ids.insert(loaded.profile->id).second)
            {
                diagnostics.push_back(winrt::ElMd::LocalizeFormat(
                    L"IgnoredDuplicateTheme", { winrt::to_hstring(loaded.profile->id) }));
                continue;
            }
            entries.push_back({ std::move(*loaded.profile), item.path(), builtIn });
        }
        if (error) diagnostics.push_back(winrt::ElMd::LocalizeFormat(
            L"UnableEnumerateThemeDirectory", { winrt::hstring(directory.c_str()) }));
    }
}

namespace winrt::ElMd
{
    std::filesystem::path ThemeCatalog::CustomThemeDirectory()
    {
        return AssetPath(std::filesystem::path(L"themes") / L"custom");
    }

    void ThemeCatalog::Refresh()
    {
        entries_.clear();
        diagnostics_.clear();
        std::unordered_set<std::string> ids;
        ScanThemeDirectory(BuiltinThemeDirectory(), true, ids, entries_, diagnostics_);
        ScanThemeDirectory(CustomThemeDirectory(), false, ids, entries_, diagnostics_);
        std::ranges::sort(entries_, [](ThemeEntry const& left, ThemeEntry const& right)
        {
            if (left.builtIn != right.builtIn) return left.builtIn > right.builtIn;
            return left.profile.name < right.profile.name;
        });
    }

    ThemeLoadResult ThemeCatalog::Resolve(std::string_view id, elmd::Theme systemVariant) const
    {
        if (id.empty() || id == "system") return LoadThemeProfile(systemVariant);
        auto found = std::ranges::find(entries_, id, [](ThemeEntry const& entry) { return std::string_view(entry.profile.id); });
        if (found != entries_.end()) return { found->profile, true, {} };
        auto fallback = LoadThemeProfile(systemVariant);
        fallback.loadedFromFile = false;
        fallback.diagnostic = Localize(L"SelectedThemeUnavailable");
        return fallback;
    }

    std::optional<winrt::hstring> ThemeCatalog::Import(std::filesystem::path const& source)
    {
        auto loaded = LoadThemeFile(source);
        if (!loaded.profile) return loaded.diagnostic;
        auto builtIn = std::ranges::find_if(entries_, [&](ThemeEntry const& entry)
        {
            return entry.builtIn && entry.profile.id == loaded.profile->id;
        });
        if (builtIn != entries_.end()) return Localize(L"BuiltInThemeIdConflict");
        try
        {
            auto directory = CustomThemeDirectory();
            std::filesystem::create_directories(directory);
            auto destination = directory / (winrt::to_hstring(loaded.profile->id) + L".json").c_str();
            std::error_code equivalentError;
            if (!std::filesystem::equivalent(source, destination, equivalentError))
                std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing);
            Refresh();
            return std::nullopt;
        }
        catch (std::exception const& error)
        {
            return LocalizeFormat(L"UnableImportTheme", { winrt::to_hstring(error.what()) });
        }
    }

    std::optional<winrt::hstring> ThemeCatalog::Remove(std::string_view id)
    {
        auto found = std::ranges::find(entries_, id, [](ThemeEntry const& entry) { return std::string_view(entry.profile.id); });
        if (found == entries_.end()) return Localize(L"ThemeNotFound");
        if (found->builtIn) return Localize(L"BuiltInThemeCannotRemove");
        try
        {
            auto parent = std::filesystem::weakly_canonical(found->path.parent_path());
            auto allowed = std::filesystem::weakly_canonical(CustomThemeDirectory());
            if (parent != allowed) return Localize(L"ThemeOutsideManagedDirectory");
            std::filesystem::remove(found->path);
            Refresh();
            return std::nullopt;
        }
        catch (std::exception const& error)
        {
            return LocalizeFormat(L"UnableRemoveTheme", { winrt::to_hstring(error.what()) });
        }
    }
}
