#include "pch.h"

import elmd.core.render_model;
import elmd.core.types;

#include "EditorInlineImageRenderer.h"

namespace winrt::ElMd
{
    EditorInlineImageRenderer::EditorInlineImageRenderer(EditorRenderResources& resources, EditorRenderCache& cache, EditorStyleSheet const& styleSheet, std::wstring const& baseDirectory)
        : resources(resources), cache(cache), styleSheet(styleSheet), baseDirectory(baseDirectory)
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
            if (loadContent)
                draw.image = cache.LoadRasterImage(resources, baseDirectory, overlay.source);
            draw.alt = winrt::to_hstring(overlay.alt.empty() ? std::string("image") : overlay.alt).c_str();
            auto dimensions = draw.image
                ? std::optional(EditorRenderCache::ImageDimensions{draw.image->width, draw.image->height})
                : cache.ProbeGifDimensions(baseDirectory, overlay.source);
            if (dimensions)
            {
                draw.width = overlay.width.value_or(dimensions->width);
                draw.height = overlay.height.value_or(dimensions->height);
                if (overlay.width && !overlay.height) draw.height = dimensions->height * draw.width / dimensions->width;
                if (!overlay.width && overlay.height) draw.width = dimensions->width * draw.height / dimensions->height;
                auto scale = (std::min)(1.0f, (std::min)((std::max)(48.0f, availableWidth * 0.75f) / draw.width, 240.0f / draw.height));
                draw.width = (std::max)(1.0f, draw.width * scale);
                draw.height = (std::max)(styleSheet.body.lineHeight, draw.height * scale);
            }
            else
            {
                draw.width = overlay.width.value_or((std::min)((std::max)(48.0f, static_cast<float>(draw.alt.size()) * styleSheet.body.size * 0.56f), (std::max)(48.0f, availableWidth)));
                draw.height = overlay.height.value_or(styleSheet.body.lineHeight);
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
            draw.imageHeight = draw.height;
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
            auto rect = D2D1::RectF(imageLeft, origin.y + lineTop, imageLeft + image.width, origin.y + lineTop + image.imageHeight);
            if (image.image)
            {
                std::chrono::milliseconds untilNextFrame{0};
                auto bitmap = cache.CurrentBitmap(*image.image, untilNextFrame);
                if (bitmap)
                    resources.d2dContext->DrawBitmap(bitmap, rect, 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
                if (untilNextFrame.count() > 0) cache.RequestAnimationFrame(untilNextFrame);
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
                    origin.y + lineTop + image.imageHeight + image.captionGap);
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
