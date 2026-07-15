#pragma once

#include "editor/rendering/EditorRenderResources.h"
#include "editor/rendering/EditorStyleSheet.h"

namespace winrt::ElMd
{
    struct InlineStyleRange;
    struct DisplayInlineText;

    struct EditorTextLayoutEngine
    {
        EditorTextLayoutEngine(EditorRenderResources& resources, EditorStyleSheet const& styleSheet);

        ::Microsoft::WRL::ComPtr<IDWriteTextLayout> Create(std::wstring const& text, IDWriteTextFormat* format, float width) const;
        ::Microsoft::WRL::ComPtr<IDWriteTextLayout> CreateFlow(
            DisplayInlineText& display,
            IDWriteTextFormat* format,
            float width,
            std::function<void(IDWriteTextLayout*, DisplayInlineText const&)> const& configure) const;
        IDWriteTextFormat* FormatFor(bool code, std::vector<InlineStyleRange> const& ranges) const;
        float LineHeightFor(bool code, std::vector<InlineStyleRange> const& ranges) const;
        void ApplyStyles(IDWriteTextLayout* layout, std::vector<InlineStyleRange> const& ranges) const;
        float MeasureHeight(IDWriteTextLayout* layout, float fallbackHeight) const;

    private:
        EditorRenderResources& resources;
        EditorStyleSheet const& styleSheet;
    };
}
