#include "pch.h"

import elmd.core.render_model;
import elmd.core.types;

#include "EditorContentPreparation.h"
#include "EditorTextLayoutEngine.h"

namespace winrt::ElMd
{
    EditorTextLayoutEngine::EditorTextLayoutEngine(EditorRenderResources& resources, EditorStyleSheet const& styleSheet)
        : resources(resources), styleSheet(styleSheet)
    {
    }

    ::Microsoft::WRL::ComPtr<IDWriteTextLayout> EditorTextLayoutEngine::Create(std::wstring const& text, IDWriteTextFormat* format, float width) const
    {
        ::Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        auto result = resources.dwriteFactory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, width, 100000.0f, layout.GetAddressOf());
        if (FAILED(result)) return {};
        return layout;
    }

    void EditorTextLayoutEngine::ApplyStyles(IDWriteTextLayout* layout, std::vector<InlineStyleRange> const& ranges) const
    {
        if (!layout) return;
        for (auto const& range : ranges)
        {
            DWRITE_TEXT_RANGE textRange{ range.start, range.length };
            if (range.style.bold) layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, textRange);
            if (range.style.italic) layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, textRange);
            if (range.style.link) layout->SetUnderline(true, textRange);
            if (range.style.strikethrough) layout->SetStrikethrough(true, textRange);
            if (range.style.code)
            {
                layout->SetFontFamilyName(styleSheet.code.family.c_str(), textRange);
                layout->SetFontSize(styleSheet.code.size, textRange);
            }
            if (range.style.heading_level)
            {
                auto level = *range.style.heading_level;
                auto size = level == 1 ? styleSheet.heading1.size
                    : level == 2 ? styleSheet.heading2.size
                    : level == 3 ? styleSheet.heading3.size
                    : level == 4 ? styleSheet.body.size * 1.15f
                    : styleSheet.body.size;
                layout->SetFontSize(size, textRange);
                layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, textRange);
            }
            if (range.marker && !range.style.heading_level) layout->SetFontSize(styleSheet.body.size * 0.82f, textRange);
            auto syntaxIndex = static_cast<std::size_t>(range.syntax);
            if (range.syntax != SyntaxHighlightKind::None && syntaxIndex < resources.syntaxBrushes.size() && resources.syntaxBrushes[syntaxIndex])
            {
                layout->SetDrawingEffect(resources.syntaxBrushes[syntaxIndex].Get(), textRange);
            }
        }
    }

    float EditorTextLayoutEngine::MeasureHeight(IDWriteTextLayout* layout, float fallbackHeight) const
    {
        if (!layout) return fallbackHeight;
        DWRITE_TEXT_METRICS metrics{};
        if (FAILED(layout->GetMetrics(&metrics))) return fallbackHeight;
        return (std::max)(fallbackHeight, metrics.height);
    }
}
