#include "pch.h"

import folia.core.render_model;
import folia.core.types;

#include "editor/rendering/EditorInlineImageRenderer.h"

namespace winrt::Folia
{
    EditorInlineImageRenderer::EditorInlineImageRenderer(
        EditorRenderResources& resources,
        EditorRenderCache& cache,
        EditorStyleSheet const& styleSheet,
        SvgNormalizer& svgNormalizer,
        EditorSvgPainter& svgPainter,
        std::wstring const& baseDirectory,
        bool animate)
        : resources(resources),
          cache(cache),
          styleSheet(styleSheet),
          svgNormalizer(svgNormalizer),
          svgPainter(svgPainter),
          baseDirectory(baseDirectory),
          animate(animate)
    {
    }

    std::vector<EditorInlineImageRenderer::ImageDraw> EditorInlineImageRenderer::Resolve(IDWriteTextLayout* layout, std::vector<DisplayInlineText::ImageOverlay> const& overlays, float availableWidth, bool loadContent) const
    {
        std::vector<ImageDraw> resolved;
        if (!layout) return resolved;
        if (!overlays.empty()) layout->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_DEFAULT, 0.0f, 0.0f);
        resolved.reserve(overlays.size());
        for (auto const& overlay : overlays)
        {
            ImageDraw draw;
            draw.displayStart = overlay.displayStart;
            draw.block = overlay.block;
            draw.source = overlay.source;
            auto svgSource = cache.LoadSvgSource(baseDirectory, overlay.source, loadContent);
            if (svgSource.candidate)
            {
                if (svgSource.source)
                {
                    auto normalized = svgNormalizer.GetOrQueue(
                        *svgSource.source,
                        styleSheet.body.size,
                        loadContent);
                    if (normalized)
                    {
                        if (*normalized) draw.svg = std::move(*normalized);
                    }
                    else if (loadContent)
                    {
                        draw.pending = true;
                    }
                }
                else
                {
                    draw.pending = svgSource.pending;
                }
            }
            else if (loadContent)
            {
                draw.image = cache.LoadRasterImage(resources, baseDirectory, overlay.source);
            }
            draw.alt = winrt::to_hstring(overlay.alt.empty() ? std::string("image") : overlay.alt).c_str();
            auto dimensions = draw.svg
                ? std::optional(EditorRenderCache::ImageDimensions{draw.svg->width, draw.svg->height})
                : draw.image
                    ? std::optional(EditorRenderCache::ImageDimensions{draw.image->width, draw.image->height})
                    : cache.ProbeImageDimensions(resources, baseDirectory, overlay.source);
            auto requestedWidth = ResolveImageDimension(overlay.width, availableWidth);
            // Percentage heights require a definite containing-block height.
            // Editor text flow has no such height, matching HTML's automatic
            // height behavior for replaced elements in an indefinite block.
            auto requestedHeight = ResolveImageDimension(overlay.height);
            if (dimensions)
            {
                draw.width = requestedWidth.value_or(dimensions->width);
                draw.height = requestedHeight.value_or(dimensions->height);
                if (requestedWidth && !requestedHeight) draw.height = dimensions->height * draw.width / dimensions->width;
                if (!requestedWidth && requestedHeight) draw.width = dimensions->width * draw.height / dimensions->height;
                auto maximumWidth = requestedWidth
                    ? (std::max)(48.0f, availableWidth)
                    : (std::max)(48.0f, availableWidth * 0.75f);
                auto scale = (std::min)(1.0f, (std::min)(maximumWidth / draw.width, 240.0f / draw.height));
                draw.width = (std::max)(1.0f, draw.width * scale);
                draw.height = (std::max)(1.0f, draw.height * scale);
            }
            else
            {
                draw.width = requestedWidth.value_or((std::min)((std::max)(48.0f, static_cast<float>(draw.alt.size()) * styleSheet.body.size * 0.56f), (std::max)(48.0f, availableWidth)));
                draw.height = requestedHeight.value_or(styleSheet.body.lineHeight);
            }
            draw.advance = draw.width;
            if (draw.block)
            {
                float pointX = 0.0f;
                float pointY = 0.0f;
                DWRITE_HIT_TEST_METRICS hit{};
                if (SUCCEEDED(layout->HitTestTextPosition(
                        draw.displayStart,
                        FALSE,
                        &pointX,
                        &pointY,
                        &hit)))
                {
                    draw.advance = (std::max)(draw.width, availableWidth - pointX);
                }
            }
            // The DirectWrite inline-object placeholder must be at least one
            // body line tall, but that line box is not the image's draw rect.
            // Keeping the two dimensions separate prevents short inline
            // images such as badges from being stretched to the line height.
            draw.imageHeight = draw.height;
            draw.height = (std::max)(styleSheet.body.lineHeight, draw.imageHeight);
            draw.imageTopOffset = draw.height - draw.imageHeight;
            if (draw.block && !overlay.alt.empty())
            {
                draw.caption = winrt::to_hstring(overlay.alt).c_str();
                draw.captionGap = 4.0f;
                if (resources.dwriteFactory && resources.textFormat
                    && SUCCEEDED(resources.dwriteFactory->CreateTextLayout(
                        draw.caption.c_str(),
                        static_cast<UINT32>(draw.caption.size()),
                        resources.textFormat.Get(),
                        draw.advance,
                        4096.0f,
                        draw.captionLayout.GetAddressOf()))
                    && draw.captionLayout)
                {
                    draw.captionLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    draw.captionLayout->SetFontSize(
                        styleSheet.body.size * 0.88f,
                        DWRITE_TEXT_RANGE{0, static_cast<UINT32>(draw.caption.size())});
                    DWRITE_TEXT_METRICS captionMetrics{};
                    if (SUCCEEDED(draw.captionLayout->GetMetrics(&captionMetrics)))
                        draw.captionHeight = (std::max)(styleSheet.body.lineHeight, captionMetrics.height);
                }
                if (draw.captionHeight == 0.0f) draw.captionHeight = styleSheet.body.lineHeight;
                draw.height += draw.captionGap + draw.captionHeight;
            }
            ApplyInlinePlaceholder(layout, draw.displayStart, draw.advance, draw.height, draw.height);
            resolved.push_back(std::move(draw));
        }
        return resolved;
    }

    void EditorInlineImageRenderer::ReleaseGif(std::string_view source) const
    {
        cache.ReleaseGifImage(baseDirectory, source);
    }

    void EditorInlineImageRenderer::Draw(IDWriteTextLayout* layout, D2D1_POINT_2F origin, std::vector<ImageDraw> const& images) const
    {
        if (!layout) return;
        UINT32 lineCount = 0;
        if (layout->GetLineMetrics(nullptr, 0, &lineCount) != E_NOT_SUFFICIENT_BUFFER || lineCount == 0) return;
        std::vector<DWRITE_LINE_METRICS> lineMetrics(lineCount);
        if (FAILED(layout->GetLineMetrics(lineMetrics.data(), lineCount, &lineCount))) return;
        for (auto const& image : images)
        {
            float pointX = 0.0f;
            float ignoredY = 0.0f;
            DWRITE_HIT_TEST_METRICS hit{};
            if (FAILED(layout->HitTestTextPosition(image.displayStart, FALSE, &pointX, &ignoredY, &hit))) continue;
            UINT32 lineStart = 0;
            float lineTop = 0.0f;
            for (UINT32 line = 0; line < lineCount; ++line)
            {
                auto lineEnd = lineStart + lineMetrics[line].length;
                if (image.displayStart < lineEnd || line + 1 == lineCount) break;
                lineStart = lineEnd;
                lineTop += lineMetrics[line].height;
            }
            auto imageLeft = origin.x + pointX;
            if (image.block && image.advance > image.width)
                imageLeft += (image.advance - image.width) * 0.5f;
            auto imageTop = origin.y + lineTop + image.imageTopOffset;
            auto rect = D2D1::RectF(imageLeft, imageTop, imageLeft + image.width, imageTop + image.imageHeight);
            if (image.svg)
            {
                if (!svgPainter.Draw(
                        image.svg->id,
                        *image.svg->svg,
                        image.width,
                        image.imageHeight,
                        D2D1::Point2F(rect.left, rect.top),
                        image.svg->width,
                        image.svg->height))
                    resources.d2dContext->DrawTextW(
                        image.alt.c_str(),
                        static_cast<UINT32>(image.alt.size()),
                        resources.textFormat.Get(),
                        rect,
                        resources.mutedBrush.Get(),
                        D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            else if (image.image)
            {
                std::chrono::milliseconds untilNextFrame{0};
                auto bitmap = cache.CurrentBitmap(*image.image, untilNextFrame);
                if (bitmap)
                    resources.d2dContext->DrawBitmap(bitmap, rect, 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
                if (animate && untilNextFrame.count() > 0)
                    cache.RequestAnimationFrame(untilNextFrame);
            }
            else
            {
                if (image.caption.empty())
                    resources.d2dContext->DrawTextW(image.alt.c_str(), static_cast<UINT32>(image.alt.size()), resources.textFormat.Get(), rect, resources.mutedBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                else
                    resources.d2dContext->FillRectangle(rect, resources.panelBrush.Get());
            }
            if (!image.caption.empty())
            {
                auto captionOrigin = D2D1::Point2F(
                    origin.x + pointX,
                    imageTop + image.imageHeight + image.captionGap);
                if (image.captionLayout)
                    resources.d2dContext->DrawTextLayout(
                        captionOrigin,
                        image.captionLayout.Get(),
                        resources.mutedBrush.Get(),
                        D2D1_DRAW_TEXT_OPTIONS_CLIP);
                else
                    resources.d2dContext->DrawTextW(
                        image.caption.c_str(),
                        static_cast<UINT32>(image.caption.size()),
                        resources.textFormat.Get(),
                        D2D1::RectF(
                            captionOrigin.x,
                            captionOrigin.y,
                            captionOrigin.x + image.advance,
                            captionOrigin.y + image.captionHeight),
                        resources.mutedBrush.Get(),
                        D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
    }
}
