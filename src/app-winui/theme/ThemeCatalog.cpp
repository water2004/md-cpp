#include "pch.h"
#include "theme/ThemeCatalog.h"

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
                diagnostics.push_back(L"Ignored duplicate theme id: " + winrt::to_hstring(loaded.profile->id));
                continue;
            }
            entries.push_back({ std::move(*loaded.profile), item.path(), builtIn });
        }
        if (error) diagnostics.push_back(L"Unable to enumerate theme directory: " + winrt::hstring(directory.c_str()));
    }
}

namespace winrt::ElMd
{
    std::filesystem::path ThemeCatalog::CustomThemeDirectory()
    {
        return AppDataDirectory() / L"themes";
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
        fallback.diagnostic = L"Selected theme is unavailable; using the Windows theme";
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
        if (builtIn != entries_.end()) return L"A built-in theme already uses this id";
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
            return L"Unable to import theme: " + winrt::to_hstring(error.what());
        }
    }

    std::optional<winrt::hstring> ThemeCatalog::Remove(std::string_view id)
    {
        auto found = std::ranges::find(entries_, id, [](ThemeEntry const& entry) { return std::string_view(entry.profile.id); });
        if (found == entries_.end()) return L"Theme was not found";
        if (found->builtIn) return L"Built-in themes cannot be removed";
        try
        {
            auto parent = std::filesystem::weakly_canonical(found->path.parent_path());
            auto allowed = std::filesystem::weakly_canonical(CustomThemeDirectory());
            if (parent != allowed) return L"Theme is outside the managed theme directory";
            std::filesystem::remove(found->path);
            Refresh();
            return std::nullopt;
        }
        catch (std::exception const& error)
        {
            return L"Unable to remove theme: " + winrt::to_hstring(error.what());
        }
    }
}
