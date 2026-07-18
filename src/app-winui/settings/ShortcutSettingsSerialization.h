#pragma once

#include <vector>
#include <winrt/Windows.Data.Json.h>

import folia.platform.editor_shortcuts;

namespace winrt::Folia
{
    Windows::Data::Json::JsonArray SerializeShortcutSettings(
        std::vector<folia::platform::editor::EditorShortcutBinding> const& bindings);

    std::vector<folia::platform::editor::EditorShortcutBinding> ParseShortcutSettings(
        Windows::Data::Json::JsonArray const& values);
}
