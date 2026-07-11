#pragma once

#include "EditorRenderCache.h"
#include "EditorRenderResources.h"

namespace winrt::ElMd
{
    struct EditorSvgPainter
    {
        EditorSvgPainter(EditorRenderResources& resources, EditorRenderCache& cache);
        bool Draw(std::uint64_t renderId, std::string const& source, float width, float height, D2D1_POINT_2F origin) const;

    private:
        EditorRenderResources& resources;
        EditorRenderCache& cache;
    };
}
