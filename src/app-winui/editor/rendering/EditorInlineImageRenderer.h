#pragma once

#include "editor/rendering/EditorContentPreparation.h"
#include "editor/rendering/EditorRenderCache.h"
#include "editor/rendering/EditorRenderResources.h"
#include "editor/rendering/EditorStyleSheet.h"
#include "editor/rendering/EditorSvgPainter.h"
#include "media/SvgNormalizer.h"

namespace winrt::ElMd
{
    struct EditorInlineImageRenderer
    {
        struct ImageDraw
        {
            std::uint32_t displayStart = 0;
            std::optional<EditorRenderCache::RasterImage> image;
            std::optional<NormalizedSvg> svg;
            std::string source;
            std::wstring alt;
            std::wstring caption;
            ::Microsoft::WRL::ComPtr<IDWriteTextLayout> captionLayout;
            float width = 0.0f;
            float height = 0.0f;
            float imageHeight = 0.0f;
            float imageTopOffset = 0.0f;
            float captionGap = 0.0f;
            float captionHeight = 0.0f;
            float advance = 0.0f;
            bool block = false;
            bool pending = false;

            bool Loaded() const { return image.has_value() || svg.has_value(); }
        };

        EditorInlineImageRenderer(
            EditorRenderResources& resources,
            EditorRenderCache& cache,
            EditorStyleSheet const& styleSheet,
            SvgNormalizer& svgNormalizer,
            EditorSvgPainter& svgPainter,
            std::wstring const& baseDirectory,
            bool animate = true);
        std::vector<ImageDraw> Resolve(IDWriteTextLayout* layout, std::vector<DisplayInlineText::ImageOverlay> const& overlays, float availableWidth, bool loadContent = true) const;
        void ReleaseGif(std::string_view source) const;
        void Draw(IDWriteTextLayout* layout, D2D1_POINT_2F origin, std::vector<ImageDraw> const& images) const;

    private:
        EditorRenderResources& resources;
        EditorRenderCache& cache;
        EditorStyleSheet const& styleSheet;
        SvgNormalizer& svgNormalizer;
        EditorSvgPainter& svgPainter;
        std::wstring const& baseDirectory;
        bool animate = true;
    };
}
