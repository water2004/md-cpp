#pragma once

#include <array>
#include <string>
#include <d2d1.h>
#include <dwrite.h>

import elmd.core.theme;

namespace winrt::ElMd
{
    struct EditorFontStyle
    {
        std::wstring family;
        float size = 0.0f;
        float lineHeight = 0.0f;
        DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
        DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
    };

    struct EditorStyleSheet
    {
        EditorFontStyle body;
        EditorFontStyle heading1;
        EditorFontStyle heading2;
        EditorFontStyle heading3;
        EditorFontStyle code;
        D2D1_COLOR_F canvasColor{};
        D2D1_COLOR_F textColor{};
        D2D1_COLOR_F mutedColor{};
        D2D1_COLOR_F accentColor{};
        D2D1_COLOR_F codeTextColor{};
        D2D1_COLOR_F panelColor{};
        D2D1_COLOR_F nestedQuoteColor{};
        D2D1_COLOR_F calloutNoteBackgroundColor{};
        D2D1_COLOR_F calloutNoteBorderColor{};
        D2D1_COLOR_F calloutTipBackgroundColor{};
        D2D1_COLOR_F calloutTipBorderColor{};
        D2D1_COLOR_F calloutWarningBackgroundColor{};
        D2D1_COLOR_F calloutWarningBorderColor{};
        D2D1_COLOR_F selectionColor{};
        D2D1_COLOR_F caretColor{};
        std::array<D2D1_COLOR_F, 11> syntaxColors{};
        float horizontalPadding = 48.0f;
        float verticalPadding = 40.0f;
        float blockGap = 6.0f;
    };

    EditorStyleSheet CreateEditorStyleSheet(elmd::ThemeProfile const& theme);
}
