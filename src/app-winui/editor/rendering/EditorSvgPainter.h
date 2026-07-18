#pragma once

#include "editor/rendering/EditorRenderCache.h"
#include "editor/rendering/EditorRenderResources.h"

namespace winrt::Folia
{
    struct EditorSvgPainter
    {
        EditorSvgPainter(EditorRenderResources& resources, EditorRenderCache& cache);
        bool Supported() const noexcept;
        bool Draw(
            std::uint64_t renderId,
            std::string const& source,
            float width,
            float height,
            D2D1_POINT_2F origin,
            float intrinsicWidth = 0.0f,
            float intrinsicHeight = 0.0f) const;

    private:
        bool PaintDocument(
            ID2D1SvgDocument* document,
            float width,
            float height,
            D2D1_POINT_2F origin,
            float intrinsicWidth,
            float intrinsicHeight) const;

        EditorRenderResources& resources;
        EditorRenderCache& cache;
        ::Microsoft::WRL::ComPtr<ID2D1DeviceContext5> context;
    };
}
