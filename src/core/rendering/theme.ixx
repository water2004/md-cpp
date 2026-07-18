// folia.core.theme — platform-independent theme model and built-in fail-safe profiles.
export module folia.core.theme;
import std;

export namespace folia {

enum class Theme { Light, Dark, HighContrast };

struct Color {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
    std::uint8_t a{255};

    constexpr Color() = default;
    constexpr Color(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha = 255)
        : r(red), g(green), b(blue), a(alpha) {}

    friend constexpr bool operator==(Color const&, Color const&) = default;
};

struct ThemeFont {
    std::string family;
    float size = 0.0f;
    float line_height = 0.0f;
    std::uint16_t weight = 400;
    bool italic = false;
};

struct ThemeTypography {
    ThemeFont body;
    ThemeFont heading1;
    ThemeFont heading2;
    ThemeFont heading3;
    ThemeFont code;
    ThemeFont ui;
    ThemeFont ui_monospace;
};

struct ThemeColors {
    Color bg, fg, muted_fg, accent_fg, heading_fg, strong_fg, emphasis_fg;
    Color inline_code_bg, inline_code_fg, code_block_bg, code_block_fg;
    Color blockquote_border, blockquote_bg, nested_quote_bg, link_fg, image_border;
    Color selection_bg, selection_fg, caret_fg, marker_fg;
    Color math_bg, math_fg;
    Color callout_note_bg, callout_note_border;
    Color callout_warning_bg, callout_warning_border;
    Color callout_tip_bg, callout_tip_border;
    Color callout_important_bg, callout_important_border;
    Color callout_caution_bg, callout_caution_border;
    Color frontmatter_bg, frontmatter_fg;
    Color table_border, table_header_bg, line_number_fg, line_number_bg;
    Color strikethrough_fg, unsupported_bg, unsupported_fg;
    Color shell_bg, shell_layer_bg, shell_border, shell_fg, shell_muted_fg, shell_accent;
    std::array<Color, 11> syntax{};

    std::pair<Color, Color> callout_for(std::string_view kind) const {
        if (kind == "note") return {callout_note_bg, callout_note_border};
        if (kind == "warning") return {callout_warning_bg, callout_warning_border};
        if (kind == "tip") return {callout_tip_bg, callout_tip_border};
        if (kind == "important") return {callout_important_bg, callout_important_border};
        if (kind == "caution") return {callout_caution_bg, callout_caution_border};
        return {callout_note_bg, callout_note_border};
    }
};

struct ThemeLayout {
    float document_horizontal_padding = 48.0f;
    float document_vertical_padding = 40.0f;
    float block_gap = 6.0f;

    float paragraph_margin_bottom = 8.0f;
    float list_margin_bottom = 8.0f;
    float list_padding_left = 20.0f;
    float thematic_break_margin = 8.0f;
    std::array<float, 6> heading_margin_top{24.0f, 20.0f, 16.0f, 12.0f, 8.0f, 4.0f};
    float heading_margin_bottom = 4.0f;

    float code_margin = 8.0f;
    float code_padding_vertical = 8.0f;
    float code_padding_horizontal = 12.0f;
    float quote_margin = 8.0f;
    float quote_padding_vertical = 6.0f;
    float quote_padding_left = 18.0f;
    float quote_padding_right = 8.0f;
    float quote_border_width = 3.0f;
    float math_margin = 12.0f;
    float math_padding_vertical = 8.0f;
    float math_padding_horizontal = 12.0f;
    float table_margin = 8.0f;
    float image_margin = 12.0f;
    float toc_margin = 12.0f;
    float toc_padding_left = 8.0f;
    float toc_border_width = 2.0f;
    float callout_margin = 8.0f;
    float callout_padding_vertical = 8.0f;
    float callout_padding_left = 16.0f;
    float callout_border_width = 4.0f;
    float frontmatter_margin_bottom = 12.0f;
    float frontmatter_padding_vertical = 8.0f;
    float frontmatter_padding_horizontal = 12.0f;
    float footnote_margin = 8.0f;
    float footnote_padding_vertical = 8.0f;
    float footnote_padding_horizontal = 12.0f;
    float unsupported_margin = 8.0f;
    float unsupported_padding_vertical = 4.0f;
    float unsupported_padding_left = 12.0f;
    float extension_margin = 4.0f;

    float title_bar_height = 40.0f;
    float navigation_open_width = 280.0f;
    float scrollbar_width = 16.0f;
    float status_bar_min_height = 32.0f;
    float footnote_preview_width = 360.0f;
    float footnote_preview_spacing = 8.0f;
};

struct ThemeProfile {
    std::uint32_t schema_version = 1;
    std::string id;
    std::string name;
    Theme variant = Theme::Dark;
    ThemeTypography typography;
    ThemeColors colors;
    ThemeLayout layout;
};

namespace detail {

inline ThemeTypography default_typography() {
    ThemeTypography typography;
    typography.body = {"Microsoft YaHei UI", 18.0f, 29.0f, 400, false};
    typography.heading1 = {"Microsoft YaHei UI", 38.0f, 46.0f, 600, false};
    typography.heading2 = {"Microsoft YaHei UI", 30.0f, 37.0f, 600, false};
    typography.heading3 = {"Microsoft YaHei UI", 24.0f, 30.0f, 600, false};
    typography.code = {"Cascadia Code", 15.0f, 24.0f, 400, false};
    typography.ui = {"Segoe UI Variable Text", 14.0f, 20.0f, 400, false};
    typography.ui_monospace = {"Cascadia Mono", 14.0f, 20.0f, 400, false};
    return typography;
}

inline ThemeProfile dark_profile() {
    ThemeProfile profile;
    profile.id = "folia.dark";
    profile.name = "Folia Dark";
    profile.variant = Theme::Dark;
    profile.typography = default_typography();
    auto& t = profile.colors;
    t.bg = Color(18, 20, 25); t.fg = Color(228, 232, 240); t.muted_fg = Color(139, 149, 170);
    t.accent_fg = Color(122, 162, 247); t.heading_fg = t.fg; t.strong_fg = t.fg; t.emphasis_fg = Color(224, 175, 104);
    t.inline_code_bg = Color(31, 35, 43); t.inline_code_fg = Color(224, 175, 104);
    t.code_block_bg = Color(26, 29, 36); t.code_block_fg = Color(223, 228, 236);
    t.blockquote_border = Color(108, 182, 255); t.blockquote_bg = Color(26, 29, 36); t.nested_quote_bg = Color(22, 24, 30);
    t.link_fg = t.accent_fg; t.image_border = Color(74, 82, 100);
    t.selection_bg = Color(65, 99, 179, 112); t.selection_fg = Color(255, 255, 255);
    t.caret_fg = Color(246, 249, 255); t.marker_fg = t.muted_fg;
    t.math_bg = t.code_block_bg; t.math_fg = t.fg;
    t.callout_note_bg = Color(22, 34, 56); t.callout_note_border = Color(108, 182, 255);
    t.callout_tip_bg = Color(18, 45, 36); t.callout_tip_border = Color(52, 211, 153);
    t.callout_warning_bg = Color(53, 42, 18); t.callout_warning_border = Color(251, 191, 36);
    t.callout_important_bg = Color(49, 30, 64); t.callout_important_border = Color(192, 132, 252);
    t.callout_caution_bg = Color(58, 27, 31); t.callout_caution_border = Color(248, 113, 113);
    t.frontmatter_bg = t.code_block_bg; t.frontmatter_fg = t.muted_fg;
    t.table_border = Color(74, 82, 100); t.table_header_bg = Color(31, 35, 43); t.line_number_fg = t.muted_fg;
    t.line_number_bg = Color(25, 28, 35);
    t.strikethrough_fg = t.muted_fg; t.unsupported_bg = Color(55, 43, 14); t.unsupported_fg = Color(251, 191, 36);
    t.shell_bg = t.bg; t.shell_layer_bg = Color(25, 28, 35); t.shell_border = Color(52, 58, 70);
    t.shell_fg = t.fg; t.shell_muted_fg = t.muted_fg; t.shell_accent = t.accent_fg;
    t.syntax = {t.code_block_fg, Color(197, 134, 192), Color(78, 201, 176), Color(220, 220, 170),
        Color(206, 145, 120), Color(181, 206, 168), Color(106, 153, 85), Color(212, 212, 212),
        Color(197, 134, 192), Color(156, 220, 254), Color(79, 193, 255)};
    return profile;
}

inline ThemeProfile light_profile() {
    ThemeProfile profile;
    profile.id = "folia.light";
    profile.name = "Folia Light";
    profile.variant = Theme::Light;
    profile.typography = default_typography();
    auto& t = profile.colors;
    t.bg = Color(250, 251, 252); t.fg = Color(32, 35, 41); t.muted_fg = Color(107, 116, 133);
    t.accent_fg = Color(37, 99, 235); t.heading_fg = t.fg; t.strong_fg = t.fg; t.emphasis_fg = Color(161, 74, 45);
    t.inline_code_bg = Color(240, 241, 244); t.inline_code_fg = Color(163, 21, 21);
    t.code_block_bg = Color(240, 241, 244); t.code_block_fg = Color(46, 52, 64);
    t.blockquote_border = Color(79, 143, 232); t.blockquote_bg = Color(245, 247, 250); t.nested_quote_bg = Color(244, 245, 247);
    t.link_fg = t.accent_fg; t.image_border = Color(174, 181, 193);
    t.selection_bg = Color(94, 145, 245, 77); t.selection_fg = Color(0, 0, 0);
    t.caret_fg = Color(17, 19, 23); t.marker_fg = t.muted_fg;
    t.math_bg = t.code_block_bg; t.math_fg = t.fg;
    t.callout_note_bg = Color(234, 243, 255); t.callout_note_border = Color(79, 143, 232);
    t.callout_tip_bg = Color(234, 248, 240); t.callout_tip_border = Color(40, 163, 106);
    t.callout_warning_bg = Color(255, 246, 221); t.callout_warning_border = Color(212, 154, 22);
    t.callout_important_bg = Color(247, 237, 255); t.callout_important_border = Color(147, 51, 234);
    t.callout_caution_bg = Color(255, 235, 235); t.callout_caution_border = Color(220, 38, 38);
    t.frontmatter_bg = t.code_block_bg; t.frontmatter_fg = t.muted_fg;
    t.table_border = Color(174, 181, 193); t.table_header_bg = Color(235, 237, 241); t.line_number_fg = t.muted_fg;
    t.line_number_bg = Color(245, 246, 248);
    t.strikethrough_fg = t.muted_fg; t.unsupported_bg = Color(255, 246, 221); t.unsupported_fg = Color(150, 100, 20);
    t.shell_bg = t.bg; t.shell_layer_bg = Color(245, 246, 248); t.shell_border = Color(215, 219, 226);
    t.shell_fg = t.fg; t.shell_muted_fg = t.muted_fg; t.shell_accent = t.accent_fg;
    t.syntax = {t.code_block_fg, Color(175, 0, 219), Color(38, 127, 153), Color(121, 94, 38),
        Color(163, 21, 21), Color(9, 134, 88), Color(0, 128, 0), Color(48, 48, 48),
        Color(128, 0, 128), Color(0, 16, 128), Color(0, 112, 193)};
    return profile;
}

inline ThemeProfile high_contrast_profile() {
    auto profile = dark_profile();
    profile.id = "folia.high-contrast";
    profile.name = "Folia High Contrast";
    profile.variant = Theme::HighContrast;
    auto& t = profile.colors;
    t.bg = Color(0, 0, 0); t.fg = Color(255, 255, 255); t.muted_fg = Color(220, 220, 220);
    t.accent_fg = Color(0, 255, 255); t.heading_fg = Color(255, 255, 0); t.strong_fg = t.fg; t.emphasis_fg = Color(255, 165, 0);
    t.inline_code_bg = Color(40, 40, 40); t.inline_code_fg = Color(255, 255, 0);
    t.code_block_bg = Color(20, 20, 20); t.code_block_fg = t.fg;
    t.blockquote_border = t.accent_fg; t.blockquote_bg = Color(20, 30, 40); t.nested_quote_bg = Color(10, 15, 20);
    t.link_fg = t.accent_fg; t.image_border = t.fg;
    t.selection_bg = Color(255, 255, 0); t.selection_fg = Color(0, 0, 0); t.caret_fg = t.fg; t.marker_fg = t.muted_fg;
    t.line_number_fg = t.muted_fg;
    t.math_bg = Color(30, 30, 30); t.math_fg = t.fg;
    t.table_border = t.fg; t.table_header_bg = Color(60, 60, 60); t.line_number_bg = Color(20, 20, 20);
    t.shell_bg = t.bg; t.shell_layer_bg = Color(20, 20, 20); t.shell_border = t.fg;
    t.shell_fg = t.fg; t.shell_muted_fg = t.muted_fg; t.shell_accent = t.accent_fg;
    return profile;
}

} // namespace detail

inline ThemeProfile const& default_theme_profile(Theme variant = Theme::Dark) {
    static const ThemeProfile dark = detail::dark_profile();
    static const ThemeProfile light = detail::light_profile();
    static const ThemeProfile high_contrast = detail::high_contrast_profile();
    switch (variant) {
        case Theme::Light: return light;
        case Theme::HighContrast: return high_contrast;
        case Theme::Dark:
        default: return dark;
    }
}

inline ThemeColors const& theme_colors_for(Theme variant) {
    return default_theme_profile(variant).colors;
}

inline Theme default_theme() { return Theme::Dark; }

} // namespace folia
