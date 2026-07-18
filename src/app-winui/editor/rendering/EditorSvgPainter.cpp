#include "pch.h"
#include "editor/rendering/EditorSvgPainter.h"

namespace winrt::Folia
{
    EditorSvgPainter::EditorSvgPainter(EditorRenderResources& resources, EditorRenderCache& cache)
        : resources(resources), cache(cache)
    {
        resources.d2dContext.As(&context);
    }

    bool EditorSvgPainter::Supported() const noexcept
    {
        return static_cast<bool>(context);
    }

    bool EditorSvgPainter::Prepared(std::uint64_t renderId) const
    {
        return renderId != 0 && static_cast<bool>(cache.FindSvgDocument(renderId));
    }

    bool EditorSvgPainter::Prepare(
        std::uint64_t renderId,
        std::string const& source,
        float intrinsicWidth,
        float intrinsicHeight) const
    {
        if (!context || renderId == 0 || source.empty()
            || intrinsicWidth <= 0.0f || intrinsicHeight <= 0.0f) return false;
        return static_cast<bool>(cache.FindOrCreateSvgDocument(
            context.Get(),
            renderId,
            source,
            intrinsicWidth,
            intrinsicHeight,
            true));
    }

    bool EditorSvgPainter::PaintDocument(
        ID2D1SvgDocument* document,
        float width,
        float height,
        D2D1_POINT_2F origin,
        float intrinsicWidth,
        float intrinsicHeight) const
    {
        if (!context || !document || width <= 0.0f || height <= 0.0f) return false;
        if (origin.x + width < 0.0f || origin.y + height < 0.0f
            || origin.x > resources.surfaceWidthDip || origin.y > resources.surfaceHeightDip) return true;
        if (intrinsicWidth <= 0.0f) intrinsicWidth = width;
        if (intrinsicHeight <= 0.0f) intrinsicHeight = height;
        D2D1_MATRIX_3X2_F transform{};
        context->GetTransform(&transform);
        auto scale = D2D1::Matrix3x2F::Scale(
            width / intrinsicWidth,
            height / intrinsicHeight);
        context->SetTransform(
            scale
            * D2D1::Matrix3x2F::Translation(origin.x, origin.y)
            * transform);
        context->DrawSvgDocument(document);
        context->SetTransform(transform);
        return true;
    }

    bool EditorSvgPainter::PaintCommands(
        ID2D1CommandList* commands,
        float width,
        float height,
        D2D1_POINT_2F origin,
        float intrinsicWidth,
        float intrinsicHeight) const
    {
        if (!context || !commands || width <= 0.0f || height <= 0.0f) return false;
        if (origin.x + width < 0.0f || origin.y + height < 0.0f
            || origin.x > resources.surfaceWidthDip
            || origin.y > resources.surfaceHeightDip) return true;
        if (intrinsicWidth <= 0.0f) intrinsicWidth = width;
        if (intrinsicHeight <= 0.0f) intrinsicHeight = height;
        D2D1_MATRIX_3X2_F transform{};
        context->GetTransform(&transform);
        auto scale = D2D1::Matrix3x2F::Scale(
            width / intrinsicWidth,
            height / intrinsicHeight);
        context->SetTransform(
            scale
            * D2D1::Matrix3x2F::Translation(origin.x, origin.y)
            * transform);
        context->DrawImage(commands);
        context->SetTransform(transform);
        return true;
    }

    bool EditorSvgPainter::DrawCached(
        std::uint64_t renderId,
        float width,
        float height,
        D2D1_POINT_2F origin,
        float intrinsicWidth,
        float intrinsicHeight) const
    {
        if (intrinsicWidth <= 0.0f) intrinsicWidth = width;
        if (intrinsicHeight <= 0.0f) intrinsicHeight = height;
        if (auto commands = cache.FindSvgCommands(renderId))
            return PaintCommands(
                commands.Get(),
                width,
                height,
                origin,
                intrinsicWidth,
                intrinsicHeight);
        auto document = cache.FindSvgDocument(renderId);
        return PaintDocument(
            document.Get(),
            width,
            height,
            origin,
            intrinsicWidth,
            intrinsicHeight);
    }

    bool EditorSvgPainter::Draw(
        std::uint64_t renderId,
        std::string const& source,
        float width,
        float height,
        D2D1_POINT_2F origin,
        float intrinsicWidth,
        float intrinsicHeight) const
    {
        if (!context || renderId == 0 || source.empty() || width <= 0.0f || height <= 0.0f) return false;
        if (intrinsicWidth <= 0.0f) intrinsicWidth = width;
        if (intrinsicHeight <= 0.0f) intrinsicHeight = height;
        auto document = cache.FindOrCreateSvgDocument(
            context.Get(),
            renderId,
            source,
            intrinsicWidth,
            intrinsicHeight);
        if (auto commands = cache.FindSvgCommands(renderId))
            return PaintCommands(
                commands.Get(),
                width,
                height,
                origin,
                intrinsicWidth,
                intrinsicHeight);
        return PaintDocument(
            document.Get(), width, height, origin, intrinsicWidth, intrinsicHeight);
    }
}
