#pragma once

#include "EditorStyleSheet.h"

namespace elmd
{
    struct RenderBlock;
}

namespace winrt::ElMd
{
    struct EditorBlockLayoutCache
    {
        void Clear();
        void Trim(std::size_t limit);
        std::uint64_t Key(elmd::RenderBlock const& block, float contentWidth, std::uint64_t themeToken) const;
        float Estimate(elmd::RenderBlock const& block, float contentWidth, std::uint64_t themeToken, EditorStyleSheet const& styleSheet) const;
        bool Record(std::uint64_t key, float measured);

    private:
        std::unordered_map<std::uint64_t, float> heights;
    };
}
