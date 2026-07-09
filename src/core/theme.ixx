// elmd.core.theme — theme + color palette (Dark default, per HANDOFF).
export module elmd.core.theme;
import std;

export namespace elmd {

enum class Theme { Light, Dark, HighContrast };

struct Color {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
    std::uint8_t a{255};
    constexpr Color() = default;
    constexpr Color(std::uint8_t r_, std::uint8_t g_, std::uint8_t b_, std::uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}
};

struct ThemeColors {
    Color bg, fg, heading_fg, strong_fg, emphasis_fg;
    Color inline_code_bg, inline_code_fg, code_block_bg, code_block_fg;
    Color blockquote_border, blockquote_bg, link_fg, image_border;
    Color selection_bg, selection_fg, caret_fg, marker_fg;
    Color math_bg, math_fg;
    Color callout_note_bg, callout_note_border;
    Color callout_warning_bg, callout_warning_border;
    Color callout_tip_bg, callout_tip_border;
    Color callout_important_bg, callout_important_border;
    Color callout_caution_bg, callout_caution_border;
    Color frontmatter_bg, frontmatter_fg;
    Color table_border, table_header_bg, line_number_fg;
    Color strikethrough_fg, unsupported_bg, unsupported_fg;

    // Lookup callout bg/border by lowercased kind.
    std::pair<Color, Color> callout_for(std::string_view kind) const {
        if (kind == "note") return {callout_note_bg, callout_note_border};
        if (kind == "warning" || kind == "caution") return {kind == "warning" ? callout_warning_bg : callout_caution_bg,
                                                           kind == "warning" ? callout_warning_border : callout_caution_border};
        if (kind == "tip") return {callout_tip_bg, callout_tip_border};
        if (kind == "important") return {callout_important_bg, callout_important_border};
        return {callout_note_bg, callout_note_border};
    }
};

inline ThemeColors dark_palette() {
    ThemeColors t;
    t.bg = Color(30, 30, 30);
    t.fg = Color(212, 212, 212);
    t.heading_fg = Color(86, 156, 214);
    t.strong_fg = Color(212, 212, 212);
    t.emphasis_fg = Color(206, 145, 120);
    t.inline_code_bg = Color(55, 55, 55);
    t.inline_code_fg = Color(226, 140, 115);
    t.code_block_bg = Color(40, 40, 40);
    t.code_block_fg = Color(212, 212, 212);
    t.blockquote_border = Color(86, 156, 214);
    t.blockquote_bg = Color(35, 35, 35);
    t.link_fg = Color(78, 201, 176);
    t.image_border = Color(80, 80, 80);
    t.selection_bg = Color(38, 79, 120);
    t.selection_fg = Color(255, 255, 255);
    t.caret_fg = Color(220, 220, 220);
    t.marker_fg = Color(100, 100, 100);
    t.math_bg = Color(45, 45, 50);
    t.math_fg = Color(181, 206, 168);
    t.callout_note_bg = Color(30, 40, 60);        t.callout_note_border = Color(86, 156, 214);
    t.callout_warning_bg = Color(60, 40, 30);    t.callout_warning_border = Color(214, 157, 86);
    t.callout_tip_bg = Color(30, 60, 40);        t.callout_tip_border = Color(78, 201, 176);
    t.callout_important_bg = Color(50, 30, 60);  t.callout_important_border = Color(197, 134, 192);
    t.callout_caution_bg = Color(60, 30, 30);    t.callout_caution_border = Color(214, 86, 86);
    t.frontmatter_bg = Color(35, 35, 40);
    t.frontmatter_fg = Color(180, 180, 180);
    t.table_border = Color(80, 80, 80);
    t.table_header_bg = Color(50, 50, 55);
    t.line_number_fg = Color(100, 100, 100);
    t.strikethrough_fg = Color(130, 130, 130);
    t.unsupported_bg = Color(50, 30, 0);
    t.unsupported_fg = Color(200, 160, 80);
    return t;
}

inline ThemeColors light_palette() {
    ThemeColors t;
    t.bg = Color(255, 255, 255);
    t.fg = Color(30, 30, 30);
    t.heading_fg = Color(24, 80, 160);
    t.strong_fg = Color(30, 30, 30);
    t.emphasis_fg = Color(140, 60, 30);
    t.inline_code_bg = Color(240, 240, 240);
    t.inline_code_fg = Color(180, 60, 40);
    t.code_block_bg = Color(245, 245, 245);
    t.code_block_fg = Color(30, 30, 30);
    t.blockquote_border = Color(120, 160, 220);
    t.blockquote_bg = Color(245, 248, 252);
    t.link_fg = Color(40, 130, 110);
    t.image_border = Color(140, 140, 140);
    t.selection_bg = Color(180, 210, 240);
    t.selection_fg = Color(0, 0, 0);
    t.caret_fg = Color(40, 40, 40);
    t.marker_fg = Color(160, 160, 160);
    t.math_bg = Color(245, 248, 250);
    t.math_fg = Color(40, 90, 60);
    t.callout_note_bg = Color(230, 240, 255);   t.callout_note_border = Color(80, 130, 220);
    t.callout_warning_bg = Color(255, 240, 225); t.callout_warning_border = Color(200, 110, 40);
    t.callout_tip_bg = Color(225, 245, 230);    t.callout_tip_border = Color(40, 150, 100);
    t.callout_important_bg = Color(245, 230, 250); t.callout_important_border = Color(150, 80, 170);
    t.callout_caution_bg = Color(255, 230, 225); t.callout_caution_border = Color(200, 50, 50);
    t.frontmatter_bg = Color(245, 245, 250);
    t.frontmatter_fg = Color(90, 90, 90);
    t.table_border = Color(140, 140, 140);
    t.table_header_bg = Color(235, 235, 240);
    t.line_number_fg = Color(160, 160, 160);
    t.strikethrough_fg = Color(140, 140, 140);
    t.unsupported_bg = Color(255, 245, 225);
    t.unsupported_fg = Color(150, 100, 20);
    return t;
}

inline ThemeColors high_contrast_palette() {
    ThemeColors t;
    t.bg = Color(0, 0, 0);   t.fg = Color(255, 255, 255);
    t.heading_fg = Color(255, 255, 0); t.strong_fg = Color(255, 255, 255); t.emphasis_fg = Color(255, 165, 0);
    t.inline_code_bg = Color(40, 40, 40); t.inline_code_fg = Color(255, 255, 0);
    t.code_block_bg = Color(20, 20, 20); t.code_block_fg = Color(255, 255, 255);
    t.blockquote_border = Color(0, 255, 255); t.blockquote_bg = Color(20, 30, 40);
    t.link_fg = Color(0, 255, 255); t.image_border = Color(255, 255, 255);
    t.selection_bg = Color(255, 255, 0); t.selection_fg = Color(0, 0, 0);
    t.caret_fg = Color(255, 255, 255); t.marker_fg = Color(180, 180, 180);
    t.math_bg = Color(30, 30, 30); t.math_fg = Color(255, 255, 255);
    t.callout_note_bg = Color(0, 0, 60); t.callout_note_border = Color(0, 255, 255);
    t.callout_warning_bg = Color(60, 30, 0); t.callout_warning_border = Color(255, 165, 0);
    t.callout_tip_bg = Color(0, 40, 0); t.callout_tip_border = Color(0, 255, 0);
    t.callout_important_bg = Color(40, 0, 60); t.callout_important_border = Color(255, 0, 255);
    t.callout_caution_bg = Color(60, 0, 0); t.callout_caution_border = Color(255, 0, 0);
    t.frontmatter_bg = Color(20, 20, 30); t.frontmatter_fg = Color(255, 255, 255);
    t.table_border = Color(255, 255, 255); t.table_header_bg = Color(60, 60, 60);
    t.line_number_fg = Color(180, 180, 180); t.strikethrough_fg = Color(255, 255, 255);
    t.unsupported_bg = Color(60, 40, 0); t.unsupported_fg = Color(255, 200, 0);
    return t;
}

inline ThemeColors theme_colors_for(Theme t) {
    switch (t) {
        case Theme::Light:        return light_palette();
        case Theme::HighContrast: return high_contrast_palette();
        case Theme::Dark:
        default:                  return dark_palette();
    }
}

inline Theme default_theme() { return Theme::Dark; }

} // namespace elmd