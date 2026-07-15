#include "pch.h"
#include "editor/rendering/EditorStyleSheet.h"

namespace
{
    D2D1_COLOR_F Rgba(float red, float green, float blue, float alpha = 1.0f)
    {
        return D2D1::ColorF(red, green, blue, alpha);
    }

    D2D1_COLOR_F Rgb(std::uint32_t value)
    {
        return Rgba(static_cast<float>((value >> 16) & 0xff) / 255.0f, static_cast<float>((value >> 8) & 0xff) / 255.0f, static_cast<float>(value & 0xff) / 255.0f);
    }
}

namespace winrt::ElMd
{
    EditorStyleSheet CreateEditorStyleSheet(bool dark)
    {
        EditorStyleSheet sheet;
        sheet.body = EditorFontStyle{ L"Microsoft YaHei UI", 18.0f, 29.0f, DWRITE_FONT_WEIGHT_NORMAL };
        sheet.heading1 = EditorFontStyle{ L"Microsoft YaHei UI", 38.0f, 46.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD };
        sheet.heading2 = EditorFontStyle{ L"Microsoft YaHei UI", 30.0f, 37.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD };
        sheet.heading3 = EditorFontStyle{ L"Microsoft YaHei UI", 24.0f, 30.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD };
        sheet.code = EditorFontStyle{ L"Cascadia Code", 15.0f, 24.0f, DWRITE_FONT_WEIGHT_NORMAL };

        if (!dark)
        {
            sheet.canvasColor = Rgba(0.982f, 0.984f, 0.988f);
            sheet.textColor = Rgba(0.125f, 0.137f, 0.160f);
            sheet.mutedColor = Rgba(0.420f, 0.455f, 0.520f);
            sheet.accentColor = Rgba(0.145f, 0.388f, 0.922f);
            sheet.codeTextColor = Rgba(0.180f, 0.205f, 0.250f);
            sheet.panelColor = Rgba(0.940f, 0.945f, 0.955f);
            sheet.nestedQuoteColor = Rgba(0.958f, 0.961f, 0.968f);
            sheet.calloutNoteBackgroundColor = Rgb(0xEAF3FF);
            sheet.calloutNoteBorderColor = Rgb(0x4F8FE8);
            sheet.calloutTipBackgroundColor = Rgb(0xEAF8F0);
            sheet.calloutTipBorderColor = Rgb(0x28A36A);
            sheet.calloutWarningBackgroundColor = Rgb(0xFFF6DD);
            sheet.calloutWarningBorderColor = Rgb(0xD49A16);
            sheet.selectionColor = Rgba(0.370f, 0.570f, 0.960f, 0.30f);
            sheet.caretColor = Rgba(0.065f, 0.075f, 0.090f);
            sheet.syntaxColors = { sheet.codeTextColor, Rgb(0xAF00DB), Rgb(0x267F99), Rgb(0x795E26), Rgb(0xA31515), Rgb(0x098658), Rgb(0x008000), Rgb(0x303030), Rgb(0x800080), Rgb(0x001080), Rgb(0x0070C1) };
            return sheet;
        }

        sheet.canvasColor = Rgba(0.070f, 0.078f, 0.098f);
        sheet.textColor = Rgba(0.895f, 0.910f, 0.940f);
        sheet.mutedColor = Rgba(0.545f, 0.585f, 0.665f);
        sheet.accentColor = Rgba(0.480f, 0.635f, 0.970f);
        sheet.codeTextColor = Rgba(0.875f, 0.895f, 0.925f);
        sheet.panelColor = Rgba(0.100f, 0.113f, 0.140f);
        sheet.nestedQuoteColor = Rgba(0.085f, 0.096f, 0.119f);
        sheet.calloutNoteBackgroundColor = Rgb(0x162238);
        sheet.calloutNoteBorderColor = Rgb(0x6CB6FF);
        sheet.calloutTipBackgroundColor = Rgb(0x122D24);
        sheet.calloutTipBorderColor = Rgb(0x34D399);
        sheet.calloutWarningBackgroundColor = Rgb(0x352A12);
        sheet.calloutWarningBorderColor = Rgb(0xFBBF24);
        sheet.selectionColor = Rgba(0.255f, 0.390f, 0.700f, 0.44f);
        sheet.caretColor = Rgba(0.965f, 0.975f, 1.000f);
        sheet.syntaxColors = { sheet.codeTextColor, Rgb(0xC586C0), Rgb(0x4EC9B0), Rgb(0xDCDCAA), Rgb(0xCE9178), Rgb(0xB5CEA8), Rgb(0x6A9955), Rgb(0xD4D4D4), Rgb(0xC586C0), Rgb(0x9CDCFE), Rgb(0x4FC1FF) };
        return sheet;
    }
}
