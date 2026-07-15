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

    std::vector<EditorInlineImageRenderer::ImageDraw> EditorInlineImageRenderer::Resolve(IDWriteTextLayout* layout, std::vector<DisplayInlineText::ImageOverlay> const& overlays, float availableWidth) const
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
            draw.image = cache.LoadRasterImage(resources, baseDirectory, overlay.source);
            draw.alt = winrt::to_hstring(overlay.alt.empty() ? std::string("image") : overlay.alt).c_str();
            if (draw.image)
            {
                draw.width = overlay.width.value_or(draw.image->width);
                draw.height = overlay.height.value_or(draw.image->height);
                if (overlay.width && !overlay.height) draw.height = draw.image->height * draw.width / draw.image->width;
                if (!overlay.width && overlay.height) draw.width = draw.image->width * draw.height / draw.image->height;
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
            ApplyInlinePlaceholder(layout, draw.displayStart, draw.advance, draw.height, draw.height);
            resolved.push_back(std::move(draw));
        }
        return resolved;
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
            auto rect = D2D1::RectF(imageLeft, origin.y + lineTop, imageLeft + image.width, origin.y + lineTop + image.height);
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
                resources.d2dContext->DrawTextW(image.alt.c_str(), static_cast<UINT32>(image.alt.size()), resources.textFormat.Get(), rect, resources.mutedBrush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
        }
    }
}
