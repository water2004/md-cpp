// elmd.core.settings — editor settings (defaults per Rust shared::settings).
export module elmd.core.settings;
import std;
import elmd.core.theme;

export namespace elmd {

enum class MarkerVisibilityMode { AlwaysVisible, Dim, DimWhenFocused, HideWhenFocused };

struct EditorBehavior {
    int tab_size = 4;
    bool use_spaces = true;
    bool word_wrap = true;
    int word_wrap_column = 80;
    bool show_line_numbers = true;
    bool highlight_current_line = true;
    bool blink_caret = true;
    int caret_blink_interval_ms = 530;
    MarkerVisibilityMode marker_visibility = MarkerVisibilityMode::DimWhenFocused;
    float scroll_sensitivity = 1.0f;
    int debounce_parse_ms = 50;
};

enum class MathRenderBackendSetting { Native, SvgRasterized, PlainTextFallback };

struct MathSettings {
    MathRenderBackendSetting render_backend = MathRenderBackendSetting::PlainTextFallback;
    std::string native_font_family = "Latin Modern Math";
    float native_font_size = 14.0f;
};

enum class AssetExportPolicy { CopyRelative, EmbedBase64, Ignore };
enum class ExportRawHtmlPolicy { EscapeAsText, Drop };

struct ExportSettings {
    bool expand_toc = false;
    bool render_math = false;
    bool include_frontmatter = true;
    AssetExportPolicy asset_policy = AssetExportPolicy::CopyRelative;
    ExportRawHtmlPolicy raw_html_policy = ExportRawHtmlPolicy::EscapeAsText;
};

struct AutosaveSettings {
    bool enabled = true;
    int interval_seconds = 30;
    int keep_revisions = 10;
};

struct EditorSettings {
    ThemeProfile theme = default_theme_profile();
    EditorBehavior editor;
    MathSettings math;
    ExportSettings export_opts;
    AutosaveSettings autosave;
};

} // namespace elmd
