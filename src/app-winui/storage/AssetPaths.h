#pragma once

#include <filesystem>

namespace winrt::Folia
{
    // FOLIA_ASSETS_DIRECTORY names the Assets directory itself. It can use
    // {LocalAppData} or Windows %ENVIRONMENT_VARIABLE% references. When the
    // build does not provide it, assets are resolved from ./Assets.
    std::filesystem::path AssetsDirectory();
    std::filesystem::path AssetPath(std::filesystem::path const& relative);
}
