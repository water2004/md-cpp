#include "pch.h"
#include "editor/rendering/EditorSvgPainter.h"

namespace winrt::ElMd
{
    EditorSvgPainter::EditorSvgPainter(EditorRenderResources& resources, EditorRenderCache& cache)
        : resources(resources), cache(cache)
    {
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
        if (renderId == 0 || source.empty() || width <= 0.0f || height <= 0.0f) return false;
        if (origin.x + width < 0.0f || origin.y + height < 0.0f || origin.x > resources.surfaceWidthDip || origin.y > resources.surfaceHeightDip) return true;
        if (intrinsicWidth <= 0.0f) intrinsicWidth = width;
        if (intrinsicHeight <= 0.0f) intrinsicHeight = height;
        ::Microsoft::WRL::ComPtr<ID2D1DeviceContext5> context;
        if (FAILED(resources.d2dContext.As(&context)) || !context) return false;
        auto document = cache.FindOrCreateSvgDocument(
            context.Get(),
            renderId,
            source,
            intrinsicWidth,
            intrinsicHeight);
        if (!document) return false;
        document->SetViewportSize(D2D1::SizeF(intrinsicWidth, intrinsicHeight));
        D2D1_MATRIX_3X2_F transform{};
        context->GetTransform(&transform);
        auto scale = D2D1::Matrix3x2F::Scale(
            width / intrinsicWidth,
            height / intrinsicHeight);
        context->SetTransform(
            scale
            * D2D1::Matrix3x2F::Translation(origin.x, origin.y)
            * transform);
        context->DrawSvgDocument(document.Get());
        context->SetTransform(transform);
        return true;
    }
}
