#pragma once

#include "editor/rendering/EditorRenderCache.h"
#include "editor/rendering/EditorRenderResources.h"

namespace winrt::Folia
{
    struct EditorSvgPainter
    {
        EditorSvgPainter(EditorRenderResources& resources, EditorRenderCache& cache);
        bool Supported() const noexcept;
        bool Prepared(std::uint64_t renderId) const;
        bool Prepare(
            std::uint64_t renderId,
            std::string const& source,
            float intrinsicWidth,
            float intrinsicHeight) const;
        bool DrawCached(
            std::uint64_t renderId,
            float width,
            float height,
            D2D1_POINT_2F origin,
            float intrinsicWidth = 0.0f,
            float intrinsicHeight = 0.0f) const;
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
