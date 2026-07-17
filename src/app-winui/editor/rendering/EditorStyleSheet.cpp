#include "pch.h"
#include "editor/rendering/EditorStyleSheet.h"

namespace
{
    D2D1_COLOR_F ToD2D(elmd::Color color)
    {
        constexpr auto scale = 1.0f / 255.0f;
        return D2D1::ColorF(color.r * scale, color.g * scale, color.b * scale, color.a * scale);
    }

    std::wstring ToWide(std::string const& text)
    {
        auto value = winrt::to_hstring(text);
        return { value.c_str(), value.size() };
    }

    winrt::ElMd::EditorFontStyle ConvertFont(elmd::ThemeFont const& font)
    {
        return {
            ToWide(font.family),
            font.size,
            font.line_height,
            static_cast<DWRITE_FONT_WEIGHT>(font.weight),
            font.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
        };
    }
}

namespace winrt::ElMd
{
    EditorStyleSheet CreateEditorStyleSheet(elmd::ThemeProfile const& theme)
    {
        EditorStyleSheet sheet;
        sheet.body = ConvertFont(theme.typography.body);
        sheet.heading1 = ConvertFont(theme.typography.heading1);
        sheet.heading2 = ConvertFont(theme.typography.heading2);
        sheet.heading3 = ConvertFont(theme.typography.heading3);
        sheet.code = ConvertFont(theme.typography.code);

        auto const& colors = theme.colors;
        sheet.canvasColor = ToD2D(colors.bg);
        sheet.textColor = ToD2D(colors.fg);
        sheet.mutedColor = ToD2D(colors.muted_fg);
        sheet.lineNumberColor = ToD2D(colors.line_number_fg);
        sheet.accentColor = ToD2D(colors.accent_fg);
        sheet.codeTextColor = ToD2D(colors.code_block_fg);
        sheet.panelColor = ToD2D(colors.code_block_bg);
        sheet.nestedQuoteColor = ToD2D(colors.nested_quote_bg);
        sheet.calloutNoteBackgroundColor = ToD2D(colors.callout_note_bg);
        sheet.calloutNoteBorderColor = ToD2D(colors.callout_note_border);
        sheet.calloutTipBackgroundColor = ToD2D(colors.callout_tip_bg);
        sheet.calloutTipBorderColor = ToD2D(colors.callout_tip_border);
        sheet.calloutWarningBackgroundColor = ToD2D(colors.callout_warning_bg);
        sheet.calloutWarningBorderColor = ToD2D(colors.callout_warning_border);
        sheet.selectionColor = ToD2D(colors.selection_bg);
        sheet.caretColor = ToD2D(colors.caret_fg);
        for (std::size_t index = 0; index < sheet.syntaxColors.size(); ++index)
            sheet.syntaxColors[index] = ToD2D(colors.syntax[index]);

        sheet.horizontalPadding = theme.layout.document_horizontal_padding;
        sheet.verticalPadding = theme.layout.document_vertical_padding;
        sheet.blockGap = theme.layout.block_gap;
        return sheet;
    }
}
