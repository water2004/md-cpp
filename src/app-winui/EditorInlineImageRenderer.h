#pragma once

#include "EditorContentPreparation.h"
#include "EditorRenderCache.h"
#include "EditorRenderResources.h"
#include "EditorStyleSheet.h"

namespace winrt::ElMd
{
    struct EditorInlineImageRenderer
    {
        struct ImageDraw
        {
            std::uint32_t displayStart = 0;
            std::optional<EditorRenderCache::RasterImage> image;
            std::wstring alt;
            float width = 0.0f;
            float height = 0.0f;
        };

        EditorInlineImageRenderer(EditorRenderResources& resources, EditorRenderCache& cache, EditorStyleSheet const& styleSheet, std::wstring const& baseDirectory);
        std::vector<ImageDraw> Resolve(IDWriteTextLayout* layout, std::vector<DisplayInlineText::ImageOverlay> const& overlays, float availableWidth) const;
        void Draw(IDWriteTextLayout* layout, D2D1_POINT_2F origin, std::vector<ImageDraw> const& images) const;

    private:
        EditorRenderResources& resources;
        EditorRenderCache& cache;
        EditorStyleSheet const& styleSheet;
        std::wstring const& baseDirectory;
    };
}
