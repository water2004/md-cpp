#pragma once

import elmd.core.theme;

namespace winrt::ElMd
{
    struct ThemeFileLoadResult
    {
        std::optional<elmd::ThemeProfile> profile;
        winrt::hstring diagnostic;
    };

    struct ThemeLoadResult
    {
        elmd::ThemeProfile profile;
        bool loadedFromFile = false;
        winrt::hstring diagnostic;
    };

    std::filesystem::path BuiltinThemeDirectory();
    ThemeFileLoadResult LoadThemeFile(std::filesystem::path const& path);
    ThemeLoadResult LoadThemeProfile(elmd::Theme variant);
}
