#include "pch.h"

import elmd.core.render_model;

#include "EditorBlockLayoutCache.h"

namespace winrt::ElMd
{
    void EditorBlockLayoutCache::Clear()
    {
        heights.clear();
    }

    void EditorBlockLayoutCache::Trim(std::size_t limit)
    {
        if (heights.size() > limit) heights.clear();
    }

    std::uint64_t EditorBlockLayoutCache::Key(elmd::RenderBlock const& block, float contentWidth, std::uint64_t themeToken) const
    {
        auto value = block.source_fingerprint;
        value ^= static_cast<std::uint64_t>(block.kind) * 0x9e3779b97f4a7c15ull;
        value ^= static_cast<std::uint64_t>(std::llround(contentWidth * 16.0f)) << 17;
        value ^= themeToken << 61;
        return value;
    }

    float EditorBlockLayoutCache::Estimate(elmd::RenderBlock const& block, float contentWidth, std::uint64_t themeToken, EditorStyleSheet const& styleSheet) const
    {
        if (auto found = heights.find(Key(block, contentWidth, themeToken)); found != heights.end()) return found->second;
        switch (block.kind)
        {
            case elmd::RenderBlockKind::Blank:
                return styleSheet.body.lineHeight;
            case elmd::RenderBlockKind::Code:
                return (std::max)(64.0f, static_cast<float>((std::max)(std::size_t{1}, block.line_count)) * styleSheet.code.lineHeight + 32.0f);
            case elmd::RenderBlockKind::Math:
                return 96.0f;
            case elmd::RenderBlockKind::Table:
                return static_cast<float>((std::max)(std::size_t{2}, block.row_count)) * (styleSheet.body.lineHeight + 16.0f);
            case elmd::RenderBlockKind::Image:
                return 160.0f;
            case elmd::RenderBlockKind::ThematicBreak:
                return 48.0f;
            default:
            {
                auto length = block.content_range.end.v > block.content_range.start.v ? block.content_range.end.v - block.content_range.start.v : std::size_t{1};
                auto charactersPerLine = (std::max)(std::size_t{24}, static_cast<std::size_t>(contentWidth / (styleSheet.body.size * 0.56f)));
                auto lines = (std::max)(std::size_t{1}, (length + charactersPerLine - 1) / charactersPerLine);
                return static_cast<float>(lines) * styleSheet.body.lineHeight + 8.0f;
            }
        }
    }

    bool EditorBlockLayoutCache::Record(std::uint64_t key, float measured)
    {
        auto found = heights.find(key);
        auto changed = found == heights.end() || std::abs(found->second - measured) > 0.5f;
        heights[key] = measured;
        return changed;
    }
}
