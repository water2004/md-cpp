#pragma once

#include <filesystem>
#include <optional>
#include <string>

import folia.platform.editor_shortcuts;

namespace winrt::Folia
{
    struct AppSettings
    {
        bool mathRenderingEnabled = true;
        std::string themeId = "system";
        std::string languageId = "system";
        std::vector<folia::platform::editor::EditorShortcutBinding> shortcutBindings =
            folia::platform::editor::default_editor_shortcuts();

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
