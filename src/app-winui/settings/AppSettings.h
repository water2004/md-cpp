#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace winrt::ElMd
{
    struct AppSettings
    {
        bool mathRenderingEnabled = true;
        std::string themeId = "system";

        bool operator==(AppSettings const&) const = default;
    };

    struct SettingsLoadResult
    {
        AppSettings settings;
        bool loadedFromFile = false;
        winrt::hstring diagnostic;
    };

    SettingsLoadResult LoadAppSettings();
    std::optional<winrt::hstring> SaveAppSettings(AppSettings const& settings);
}
