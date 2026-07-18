#pragma once

#include "editor/rendering/EditorRenderCache.h"
#include "editor/rendering/EditorRenderResources.h"

namespace winrt::Folia
{
    struct EditorSvgPainter
    {
        EditorSvgPainter(EditorRenderResources& resources, EditorRenderCache& cache);
        bool Draw(
            std::uint64_t renderId,
            std::string const& source,
            float width,
            float height,
            D2D1_POINT_2F origin,
            float intrinsicWidth = 0.0f,
            float intrinsicHeight = 0.0f) const;

    private:
        EditorRenderResources& resources;
        EditorRenderCache& cache;
    };
}
