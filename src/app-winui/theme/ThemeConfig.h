#pragma once

import folia.core.theme;

namespace winrt::Folia
{
    struct ThemeFileLoadResult
    {
        std::optional<folia::ThemeProfile> profile;
        winrt::hstring diagnostic;
    };

    struct ThemeLoadResult
    {
        folia::ThemeProfile profile;
        bool loadedFromFile = false;
        winrt::hstring diagnostic;
    };

    std::filesystem::path BuiltinThemeDirectory();
    ThemeFileLoadResult LoadThemeFile(std::filesystem::path const& path);
    ThemeLoadResult LoadThemeProfile(folia::Theme variant);
}
