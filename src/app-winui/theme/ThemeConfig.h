#pragma once

import elmd.core.theme;

namespace winrt::ElMd
{
    struct ThemeLoadResult
    {
        elmd::ThemeProfile profile;
        bool loadedFromFile = false;
        winrt::hstring diagnostic;
    };

    ThemeLoadResult LoadThemeProfile(elmd::Theme variant);
}
