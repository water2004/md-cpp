#pragma once

#include "EditorRenderResources.h"
#include "EditorStyleSheet.h"

namespace winrt::ElMd
{
    struct InlineStyleRange;

    struct EditorTextLayoutEngine
    {
        EditorTextLayoutEngine(EditorRenderResources& resources, EditorStyleSheet const& styleSheet);

        ::Microsoft::WRL::ComPtr<IDWriteTextLayout> Create(std::wstring const& text, IDWriteTextFormat* format, float width) const;
        void ApplyStyles(IDWriteTextLayout* layout, std::vector<InlineStyleRange> const& ranges) const;
        float MeasureHeight(IDWriteTextLayout* layout, float fallbackHeight) const;

    private:
        EditorRenderResources& resources;
        EditorStyleSheet const& styleSheet;
    };
}
