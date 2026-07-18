#include "pch.h"
#include "theme/ThemeConfig.h"
#include "localization/Localization.h"
#include "storage/AssetPaths.h"

namespace
{
    using winrt::Windows::Data::Json::JsonObject;

    std::wstring ThemeFileName(folia::Theme variant)
    {
        switch (variant)
        {
        case folia::Theme::Light: return L"light.json";
        case folia::Theme::HighContrast: return L"high-contrast.json";
        case folia::Theme::Dark:
        default: return L"dark.json";
        }
    }

    std::string ReadUtf8(std::filesystem::path const& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("cannot open " + path.string());
        return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    }

    unsigned HexDigit(wchar_t value)
    {
        if (value >= L'0' && value <= L'9') return static_cast<unsigned>(value - L'0');
        if (value >= L'a' && value <= L'f') return 10u + static_cast<unsigned>(value - L'a');
        if (value >= L'A' && value <= L'F') return 10u + static_cast<unsigned>(value - L'A');
        throw std::runtime_error("theme color contains a non-hexadecimal digit");
    }

    std::uint8_t HexByte(std::wstring_view value, std::size_t offset)
    {
        return static_cast<std::uint8_t>((HexDigit(value[offset]) << 4u) | HexDigit(value[offset + 1]));
    }

    folia::Color ParseColor(winrt::hstring const& encoded)
    {
        std::wstring_view value(encoded);
        if ((value.size() != 7 && value.size() != 9) || value.front() != L'#')
            throw std::runtime_error("theme colors must use #RRGGBB or #RRGGBBAA");
        return {
            HexByte(value, 1),
            HexByte(value, 3),
            HexByte(value, 5),
            value.size() == 9 ? HexByte(value, 7) : std::uint8_t{255},
        };
    }

    folia::Theme ParseVariant(winrt::hstring const& encoded)
    {
        if (encoded == L"light") return folia::Theme::Light;
        if (encoded == L"dark") return folia::Theme::Dark;
        if (encoded == L"high-contrast") return folia::Theme::HighContrast;
        throw std::runtime_error("theme variant must be light, dark, or high-contrast");
    }

    float Number(JsonObject const& object, wchar_t const* name)
    {
        auto value = static_cast<float>(object.GetNamedNumber(name));
        if (!std::isfinite(value) || value < 0.0f)
            throw std::runtime_error("theme numeric values must be finite and non-negative");
        return value;
    }

    folia::ThemeFont ParseFont(JsonObject const& object)
    {
        folia::ThemeFont font;
        font.family = winrt::to_string(object.GetNamedString(L"family"));
        font.size = Number(object, L"size");
        font.line_height = Number(object, L"lineHeight");
        auto weight = object.GetNamedNumber(L"weight");
        font.italic = object.GetNamedBoolean(L"italic");
        if (font.family.empty() || font.size <= 0.0f || font.line_height <= 0.0f || weight < 100.0 || weight > 999.0)
            throw std::runtime_error("theme font is invalid");
        font.weight = static_cast<std::uint16_t>(weight);
        return font;
    }

    folia::ThemeTypography ParseTypography(JsonObject const& object)
    {
        folia::ThemeTypography typography;
        typography.body = ParseFont(object.GetNamedObject(L"body"));
        typography.heading1 = ParseFont(object.GetNamedObject(L"heading1"));
        typography.heading2 = ParseFont(object.GetNamedObject(L"heading2"));
        typography.heading3 = ParseFont(object.GetNamedObject(L"heading3"));
        typography.code = ParseFont(object.GetNamedObject(L"code"));
        typography.ui = ParseFont(object.GetNamedObject(L"ui"));
        typography.ui_monospace = ParseFont(object.GetNamedObject(L"uiMonospace"));
        return typography;
    }

    folia::ThemeColors ParseColors(JsonObject const& object)
    {
        folia::ThemeColors colors;
        struct Field { wchar_t const* name; folia::Color folia::ThemeColors::* value; };
        static constexpr Field fields[] = {
            {L"background", &folia::ThemeColors::bg}, {L"foreground", &folia::ThemeColors::fg},
            {L"mutedForeground", &folia::ThemeColors::muted_fg}, {L"accent", &folia::ThemeColors::accent_fg},
            {L"heading", &folia::ThemeColors::heading_fg}, {L"strong", &folia::ThemeColors::strong_fg},
            {L"emphasis", &folia::ThemeColors::emphasis_fg}, {L"inlineCodeBackground", &folia::ThemeColors::inline_code_bg},
            {L"inlineCodeForeground", &folia::ThemeColors::inline_code_fg}, {L"codeBlockBackground", &folia::ThemeColors::code_block_bg},
            {L"codeBlockForeground", &folia::ThemeColors::code_block_fg}, {L"blockquoteBorder", &folia::ThemeColors::blockquote_border},
            {L"blockquoteBackground", &folia::ThemeColors::blockquote_bg}, {L"nestedQuoteBackground", &folia::ThemeColors::nested_quote_bg},
            {L"link", &folia::ThemeColors::link_fg}, {L"imageBorder", &folia::ThemeColors::image_border},
            {L"selectionBackground", &folia::ThemeColors::selection_bg}, {L"selectionForeground", &folia::ThemeColors::selection_fg},
            {L"caret", &folia::ThemeColors::caret_fg}, {L"marker", &folia::ThemeColors::marker_fg},
            {L"mathBackground", &folia::ThemeColors::math_bg}, {L"mathForeground", &folia::ThemeColors::math_fg},
            {L"calloutNoteBackground", &folia::ThemeColors::callout_note_bg}, {L"calloutNoteBorder", &folia::ThemeColors::callout_note_border},
            {L"calloutWarningBackground", &folia::ThemeColors::callout_warning_bg}, {L"calloutWarningBorder", &folia::ThemeColors::callout_warning_border},
            {L"calloutTipBackground", &folia::ThemeColors::callout_tip_bg}, {L"calloutTipBorder", &folia::ThemeColors::callout_tip_border},
            {L"calloutImportantBackground", &folia::ThemeColors::callout_important_bg}, {L"calloutImportantBorder", &folia::ThemeColors::callout_important_border},
            {L"calloutCautionBackground", &folia::ThemeColors::callout_caution_bg}, {L"calloutCautionBorder", &folia::ThemeColors::callout_caution_border},
            {L"frontmatterBackground", &folia::ThemeColors::frontmatter_bg}, {L"frontmatterForeground", &folia::ThemeColors::frontmatter_fg},
            {L"tableBorder", &folia::ThemeColors::table_border}, {L"tableHeaderBackground", &folia::ThemeColors::table_header_bg},
            {L"lineNumber", &folia::ThemeColors::line_number_fg}, {L"strikethrough", &folia::ThemeColors::strikethrough_fg},
            {L"unsupportedBackground", &folia::ThemeColors::unsupported_bg}, {L"unsupportedForeground", &folia::ThemeColors::unsupported_fg},
            {L"shellBackground", &folia::ThemeColors::shell_bg}, {L"shellLayerBackground", &folia::ThemeColors::shell_layer_bg},
            {L"shellBorder", &folia::ThemeColors::shell_border}, {L"shellForeground", &folia::ThemeColors::shell_fg},
            {L"shellMutedForeground", &folia::ThemeColors::shell_muted_fg}, {L"shellAccent", &folia::ThemeColors::shell_accent},
        };
        for (auto const& field : fields) colors.*field.value = ParseColor(object.GetNamedString(field.name));
        colors.line_number_bg = object.HasKey(L"lineNumberBackground")
            ? ParseColor(object.GetNamedString(L"lineNumberBackground"))
            : colors.shell_layer_bg;

        auto syntax = object.GetNamedArray(L"syntax");
        if (syntax.Size() != colors.syntax.size()) throw std::runtime_error("theme syntax palette must contain 11 colors");
        for (std::uint32_t index = 0; index < syntax.Size(); ++index)
            colors.syntax[index] = ParseColor(syntax.GetStringAt(index));
        return colors;
    }

    folia::ThemeLayout ParseLayout(JsonObject const& object)
    {
        folia::ThemeLayout layout;
        struct Field { wchar_t const* name; float folia::ThemeLayout::* value; };
        static constexpr Field fields[] = {
            {L"documentHorizontalPadding", &folia::ThemeLayout::document_horizontal_padding},
            {L"documentVerticalPadding", &folia::ThemeLayout::document_vertical_padding},
            {L"blockGap", &folia::ThemeLayout::block_gap},
            {L"paragraphMarginBottom", &folia::ThemeLayout::paragraph_margin_bottom},
            {L"listMarginBottom", &folia::ThemeLayout::list_margin_bottom}, {L"listPaddingLeft", &folia::ThemeLayout::list_padding_left},
            {L"thematicBreakMargin", &folia::ThemeLayout::thematic_break_margin}, {L"headingMarginBottom", &folia::ThemeLayout::heading_margin_bottom},
            {L"codeMargin", &folia::ThemeLayout::code_margin}, {L"codePaddingVertical", &folia::ThemeLayout::code_padding_vertical},
            {L"codePaddingHorizontal", &folia::ThemeLayout::code_padding_horizontal}, {L"quoteMargin", &folia::ThemeLayout::quote_margin},
            {L"quotePaddingVertical", &folia::ThemeLayout::quote_padding_vertical}, {L"quotePaddingLeft", &folia::ThemeLayout::quote_padding_left},
            {L"quotePaddingRight", &folia::ThemeLayout::quote_padding_right}, {L"quoteBorderWidth", &folia::ThemeLayout::quote_border_width},
            {L"mathMargin", &folia::ThemeLayout::math_margin}, {L"mathPaddingVertical", &folia::ThemeLayout::math_padding_vertical},
            {L"mathPaddingHorizontal", &folia::ThemeLayout::math_padding_horizontal}, {L"tableMargin", &folia::ThemeLayout::table_margin},
            {L"imageMargin", &folia::ThemeLayout::image_margin}, {L"tocMargin", &folia::ThemeLayout::toc_margin},
            {L"tocPaddingLeft", &folia::ThemeLayout::toc_padding_left}, {L"tocBorderWidth", &folia::ThemeLayout::toc_border_width},
            {L"calloutMargin", &folia::ThemeLayout::callout_margin}, {L"calloutPaddingVertical", &folia::ThemeLayout::callout_padding_vertical},
            {L"calloutPaddingLeft", &folia::ThemeLayout::callout_padding_left}, {L"calloutBorderWidth", &folia::ThemeLayout::callout_border_width},
            {L"frontmatterMarginBottom", &folia::ThemeLayout::frontmatter_margin_bottom},
            {L"frontmatterPaddingVertical", &folia::ThemeLayout::frontmatter_padding_vertical},
            {L"frontmatterPaddingHorizontal", &folia::ThemeLayout::frontmatter_padding_horizontal},
            {L"footnoteMargin", &folia::ThemeLayout::footnote_margin}, {L"footnotePaddingVertical", &folia::ThemeLayout::footnote_padding_vertical},
            {L"footnotePaddingHorizontal", &folia::ThemeLayout::footnote_padding_horizontal},
            {L"unsupportedMargin", &folia::ThemeLayout::unsupported_margin},
            {L"unsupportedPaddingVertical", &folia::ThemeLayout::unsupported_padding_vertical},
            {L"unsupportedPaddingLeft", &folia::ThemeLayout::unsupported_padding_left},
            {L"extensionMargin", &folia::ThemeLayout::extension_margin}, {L"titleBarHeight", &folia::ThemeLayout::title_bar_height},
            {L"navigationOpenWidth", &folia::ThemeLayout::navigation_open_width},
            {L"scrollbarWidth", &folia::ThemeLayout::scrollbar_width}, {L"statusBarMinHeight", &folia::ThemeLayout::status_bar_min_height},
            {L"footnotePreviewWidth", &folia::ThemeLayout::footnote_preview_width},
            {L"footnotePreviewSpacing", &folia::ThemeLayout::footnote_preview_spacing},
        };
        for (auto const& field : fields) layout.*field.value = Number(object, field.name);

        auto headingMargins = object.GetNamedArray(L"headingMarginTop");
        if (headingMargins.Size() != layout.heading_margin_top.size())
            throw std::runtime_error("theme headingMarginTop must contain six values");
        for (std::uint32_t index = 0; index < headingMargins.Size(); ++index)
        {
            auto value = static_cast<float>(headingMargins.GetNumberAt(index));
            if (!std::isfinite(value) || value < 0.0f) throw std::runtime_error("theme heading margins are invalid");
            layout.heading_margin_top[index] = value;
        }
        return layout;
    }

    bool ValidThemeId(std::string_view value)
    {
        if (value.empty() || value.size() > 96) return false;
        return std::ranges::all_of(value, [](unsigned char character)
        {
            return std::isalnum(character) || character == '.' || character == '_' || character == '-';
        });
    }

    folia::ThemeProfile ParseProfile(std::string const& source)
    {
        auto root = JsonObject::Parse(winrt::to_hstring(source));
        folia::ThemeProfile profile;
        auto schema = root.GetNamedNumber(L"schemaVersion");
        if (schema != 1.0) throw std::runtime_error("unsupported theme schema version");
        profile.schema_version = 1;
        profile.id = winrt::to_string(root.GetNamedString(L"id"));
        profile.name = winrt::to_string(root.GetNamedString(L"name"));
        profile.variant = ParseVariant(root.GetNamedString(L"variant"));
        if (!ValidThemeId(profile.id) || profile.name.empty())
            throw std::runtime_error("theme identity is invalid");
        profile.typography = ParseTypography(root.GetNamedObject(L"typography"));
        profile.colors = ParseColors(root.GetNamedObject(L"colors"));
        profile.layout = ParseLayout(root.GetNamedObject(L"layout"));
        return profile;
    }
}

namespace winrt::Folia
{
    std::filesystem::path BuiltinThemeDirectory()
    {
        return AssetPath(L"themes");
    }

    ThemeFileLoadResult LoadThemeFile(std::filesystem::path const& path)
    {
        try
        {
            return { ParseProfile(ReadUtf8(path)), {} };
        }
        catch (winrt::hresult_error const& error)
        {
            return { std::nullopt, LocalizeFormat(
                L"UnableLoadTheme", { winrt::hstring(path.filename().c_str()), error.message() }) };
        }
        catch (std::exception const& error)
        {
            return { std::nullopt, LocalizeFormat(
                L"UnableLoadTheme",
                { winrt::hstring(path.filename().c_str()), winrt::to_hstring(error.what()) }) };
        }
    }

    ThemeLoadResult LoadThemeProfile(folia::Theme variant)
    {
        auto loaded = LoadThemeFile(BuiltinThemeDirectory() / ThemeFileName(variant));
        if (loaded.profile && loaded.profile->variant == variant)
        {
            return { std::move(*loaded.profile), true, {} };
        }
        auto reason = loaded.diagnostic.empty()
            ? Localize(L"ThemeVariantMismatch")
            : loaded.diagnostic;
        auto diagnostic = LocalizeFormat(L"ThemeFallback", { reason });
        return { folia::default_theme_profile(variant), false, std::move(diagnostic) };
    }
}
