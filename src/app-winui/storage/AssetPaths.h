#pragma once

#include <filesystem>

namespace winrt::ElMd
{
    // FOLIA_ASSETS_DIRECTORY names the Assets directory itself. When the build
    // does not provide it, assets are resolved from ./Assets.
    std::filesystem::path AssetsDirectory();
    std::filesystem::path AssetPath(std::filesystem::path const& relative);
}
