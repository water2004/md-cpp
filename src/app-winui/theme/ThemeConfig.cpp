#include "pch.h"
#include "theme/ThemeConfig.h"

namespace
{
    using winrt::Windows::Data::Json::JsonObject;

    std::filesystem::path ExecutableDirectory()
    {
        std::wstring path(32768, L'\0');
        auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0 || length >= path.size()) winrt::throw_last_error();
        path.resize(length);
        return std::filesystem::path(path).parent_path();
    }

    std::wstring ThemeFileName(elmd::Theme variant)
    {
        switch (variant)
        {
        case elmd::Theme::Light: return L"light.json";
        case elmd::Theme::HighContrast: return L"high-contrast.json";
        case elmd::Theme::Dark:
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

    elmd::Color ParseColor(winrt::hstring const& encoded)
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

    elmd::Theme ParseVariant(winrt::hstring const& encoded)
    {
        if (encoded == L"light") return elmd::Theme::Light;
        if (encoded == L"dark") return elmd::Theme::Dark;
        if (encoded == L"high-contrast") return elmd::Theme::HighContrast;
        throw std::runtime_error("theme variant must be light, dark, or high-contrast");
    }

    float Number(JsonObject const& object, wchar_t const* name)
    {
        auto value = static_cast<float>(object.GetNamedNumber(name));
        if (!std::isfinite(value) || value < 0.0f)
            throw std::runtime_error("theme numeric values must be finite and non-negative");
        return value;
    }

    elmd::ThemeFont ParseFont(JsonObject const& object)
    {
        elmd::ThemeFont font;
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

    elmd::ThemeTypography ParseTypography(JsonObject const& object)
    {
        elmd::ThemeTypography typography;
        typography.body = ParseFont(object.GetNamedObject(L"body"));
        typography.heading1 = ParseFont(object.GetNamedObject(L"heading1"));
        typography.heading2 = ParseFont(object.GetNamedObject(L"heading2"));
        typography.heading3 = ParseFont(object.GetNamedObject(L"heading3"));
        typography.code = ParseFont(object.GetNamedObject(L"code"));
        typography.ui = ParseFont(object.GetNamedObject(L"ui"));
        typography.ui_monospace = ParseFont(object.GetNamedObject(L"uiMonospace"));
        return typography;
    }

    elmd::ThemeColors ParseColors(JsonObject const& object)
    {
        elmd::ThemeColors colors;
        struct Field { wchar_t const* name; elmd::Color elmd::ThemeColors::* value; };
        static constexpr Field fields[] = {
            {L"background", &elmd::ThemeColors::bg}, {L"foreground", &elmd::ThemeColors::fg},
            {L"mutedForeground", &elmd::ThemeColors::muted_fg}, {L"accent", &elmd::ThemeColors::accent_fg},
            {L"heading", &elmd::ThemeColors::heading_fg}, {L"strong", &elmd::ThemeColors::strong_fg},
            {L"emphasis", &elmd::ThemeColors::emphasis_fg}, {L"inlineCodeBackground", &elmd::ThemeColors::inline_code_bg},
            {L"inlineCodeForeground", &elmd::ThemeColors::inline_code_fg}, {L"codeBlockBackground", &elmd::ThemeColors::code_block_bg},
            {L"codeBlockForeground", &elmd::ThemeColors::code_block_fg}, {L"blockquoteBorder", &elmd::ThemeColors::blockquote_border},
            {L"blockquoteBackground", &elmd::ThemeColors::blockquote_bg}, {L"nestedQuoteBackground", &elmd::ThemeColors::nested_quote_bg},
            {L"link", &elmd::ThemeColors::link_fg}, {L"imageBorder", &elmd::ThemeColors::image_border},
            {L"selectionBackground", &elmd::ThemeColors::selection_bg}, {L"selectionForeground", &elmd::ThemeColors::selection_fg},
            {L"caret", &elmd::ThemeColors::caret_fg}, {L"marker", &elmd::ThemeColors::marker_fg},
            {L"mathBackground", &elmd::ThemeColors::math_bg}, {L"mathForeground", &elmd::ThemeColors::math_fg},
            {L"calloutNoteBackground", &elmd::ThemeColors::callout_note_bg}, {L"calloutNoteBorder", &elmd::ThemeColors::callout_note_border},
            {L"calloutWarningBackground", &elmd::ThemeColors::callout_warning_bg}, {L"calloutWarningBorder", &elmd::ThemeColors::callout_warning_border},
            {L"calloutTipBackground", &elmd::ThemeColors::callout_tip_bg}, {L"calloutTipBorder", &elmd::ThemeColors::callout_tip_border},
            {L"calloutImportantBackground", &elmd::ThemeColors::callout_important_bg}, {L"calloutImportantBorder", &elmd::ThemeColors::callout_important_border},
            {L"calloutCautionBackground", &elmd::ThemeColors::callout_caution_bg}, {L"calloutCautionBorder", &elmd::ThemeColors::callout_caution_border},
            {L"frontmatterBackground", &elmd::ThemeColors::frontmatter_bg}, {L"frontmatterForeground", &elmd::ThemeColors::frontmatter_fg},
            {L"tableBorder", &elmd::ThemeColors::table_border}, {L"tableHeaderBackground", &elmd::ThemeColors::table_header_bg},
            {L"lineNumber", &elmd::ThemeColors::line_number_fg}, {L"strikethrough", &elmd::ThemeColors::strikethrough_fg},
            {L"unsupportedBackground", &elmd::ThemeColors::unsupported_bg}, {L"unsupportedForeground", &elmd::ThemeColors::unsupported_fg},
            {L"shellBackground", &elmd::ThemeColors::shell_bg}, {L"shellLayerBackground", &elmd::ThemeColors::shell_layer_bg},
            {L"shellBorder", &elmd::ThemeColors::shell_border}, {L"shellForeground", &elmd::ThemeColors::shell_fg},
            {L"shellMutedForeground", &elmd::ThemeColors::shell_muted_fg}, {L"shellAccent", &elmd::ThemeColors::shell_accent},
        };
        for (auto const& field : fields) colors.*field.value = ParseColor(object.GetNamedString(field.name));

        auto syntax = object.GetNamedArray(L"syntax");
        if (syntax.Size() != colors.syntax.size()) throw std::runtime_error("theme syntax palette must contain 11 colors");
        for (std::uint32_t index = 0; index < syntax.Size(); ++index)
            colors.syntax[index] = ParseColor(syntax.GetStringAt(index));
        return colors;
    }

    elmd::ThemeLayout ParseLayout(JsonObject const& object)
    {
        elmd::ThemeLayout layout;
        struct Field { wchar_t const* name; float elmd::ThemeLayout::* value; };
        static constexpr Field fields[] = {
            {L"documentHorizontalPadding", &elmd::ThemeLayout::document_horizontal_padding},
            {L"documentVerticalPadding", &elmd::ThemeLayout::document_vertical_padding},
            {L"blockGap", &elmd::ThemeLayout::block_gap},
            {L"paragraphMarginBottom", &elmd::ThemeLayout::paragraph_margin_bottom},
            {L"listMarginBottom", &elmd::ThemeLayout::list_margin_bottom}, {L"listPaddingLeft", &elmd::ThemeLayout::list_padding_left},
            {L"thematicBreakMargin", &elmd::ThemeLayout::thematic_break_margin}, {L"headingMarginBottom", &elmd::ThemeLayout::heading_margin_bottom},
            {L"codeMargin", &elmd::ThemeLayout::code_margin}, {L"codePaddingVertical", &elmd::ThemeLayout::code_padding_vertical},
            {L"codePaddingHorizontal", &elmd::ThemeLayout::code_padding_horizontal}, {L"quoteMargin", &elmd::ThemeLayout::quote_margin},
            {L"quotePaddingVertical", &elmd::ThemeLayout::quote_padding_vertical}, {L"quotePaddingLeft", &elmd::ThemeLayout::quote_padding_left},
            {L"quotePaddingRight", &elmd::ThemeLayout::quote_padding_right}, {L"quoteBorderWidth", &elmd::ThemeLayout::quote_border_width},
            {L"mathMargin", &elmd::ThemeLayout::math_margin}, {L"mathPaddingVertical", &elmd::ThemeLayout::math_padding_vertical},
            {L"mathPaddingHorizontal", &elmd::ThemeLayout::math_padding_horizontal}, {L"tableMargin", &elmd::ThemeLayout::table_margin},
            {L"imageMargin", &elmd::ThemeLayout::image_margin}, {L"tocMargin", &elmd::ThemeLayout::toc_margin},
            {L"tocPaddingLeft", &elmd::ThemeLayout::toc_padding_left}, {L"tocBorderWidth", &elmd::ThemeLayout::toc_border_width},
            {L"calloutMargin", &elmd::ThemeLayout::callout_margin}, {L"calloutPaddingVertical", &elmd::ThemeLayout::callout_padding_vertical},
            {L"calloutPaddingLeft", &elmd::ThemeLayout::callout_padding_left}, {L"calloutBorderWidth", &elmd::ThemeLayout::callout_border_width},
            {L"frontmatterMarginBottom", &elmd::ThemeLayout::frontmatter_margin_bottom},
            {L"frontmatterPaddingVertical", &elmd::ThemeLayout::frontmatter_padding_vertical},
            {L"frontmatterPaddingHorizontal", &elmd::ThemeLayout::frontmatter_padding_horizontal},
            {L"footnoteMargin", &elmd::ThemeLayout::footnote_margin}, {L"footnotePaddingVertical", &elmd::ThemeLayout::footnote_padding_vertical},
            {L"footnotePaddingHorizontal", &elmd::ThemeLayout::footnote_padding_horizontal},
            {L"unsupportedMargin", &elmd::ThemeLayout::unsupported_margin},
            {L"unsupportedPaddingVertical", &elmd::ThemeLayout::unsupported_padding_vertical},
            {L"unsupportedPaddingLeft", &elmd::ThemeLayout::unsupported_padding_left},
            {L"extensionMargin", &elmd::ThemeLayout::extension_margin}, {L"titleBarHeight", &elmd::ThemeLayout::title_bar_height},
            {L"navigationOpenWidth", &elmd::ThemeLayout::navigation_open_width},
            {L"scrollbarWidth", &elmd::ThemeLayout::scrollbar_width}, {L"statusBarMinHeight", &elmd::ThemeLayout::status_bar_min_height},
            {L"footnotePreviewWidth", &elmd::ThemeLayout::footnote_preview_width},
            {L"footnotePreviewSpacing", &elmd::ThemeLayout::footnote_preview_spacing},
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

    elmd::ThemeProfile ParseProfile(std::string const& source)
    {
        auto root = JsonObject::Parse(winrt::to_hstring(source));
        elmd::ThemeProfile profile;
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

namespace winrt::ElMd
{
    std::filesystem::path BuiltinThemeDirectory()
    {
        return ExecutableDirectory() / L"Assets" / L"themes";
    }

    ThemeFileLoadResult LoadThemeFile(std::filesystem::path const& path)
    {
        try
        {
            return { ParseProfile(ReadUtf8(path)), {} };
        }
        catch (winrt::hresult_error const& error)
        {
            return { std::nullopt, L"Unable to load theme " + winrt::hstring(path.filename().c_str()) + L": " + error.message() };
        }
        catch (std::exception const& error)
        {
            return { std::nullopt, L"Unable to load theme " + winrt::hstring(path.filename().c_str()) + L": " + winrt::to_hstring(error.what()) };
        }
    }

    ThemeLoadResult LoadThemeProfile(elmd::Theme variant)
    {
        auto loaded = LoadThemeFile(BuiltinThemeDirectory() / ThemeFileName(variant));
        if (loaded.profile && loaded.profile->variant == variant)
        {
            return { std::move(*loaded.profile), true, {} };
        }
        auto diagnostic = loaded.diagnostic.empty()
            ? winrt::hstring(L"Theme fallback: built-in theme variant does not match its file")
            : L"Theme fallback: " + loaded.diagnostic;
        return { elmd::default_theme_profile(variant), false, std::move(diagnostic) };
    }
}
