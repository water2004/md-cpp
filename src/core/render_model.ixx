// elmd.core.render_model — RenderModel, RenderBlock enums and styles.
// No HtmlRenderBlock / HtmlInlineRenderItem variants (acceptance gate).
export module elmd.core.render_model;
import std;
import elmd.core.types;
import elmd.core.ids;
import elmd.core.dialect;
import elmd.core.theme;
import elmd.core.outline;
import elmd.core.diagnostics;
import elmd.core.ast; // for AST contents reused during build (children)

export namespace elmd {

enum class MathDisplayMode { Inline, Block };
enum class MarkerVisibility { Always, WhenCaretInsideNode, WhenBlockFocused, HiddenButEditable };

struct MathDiagnostic {
    enum class Sev { Info, Warning, Error };
    Sev severity = Sev::Info;
    std::string message;
};

struct MathStyle {
    float font_size = 14.0f;
    MathDisplayMode display = MathDisplayMode::Inline;
    Color color = Color(212, 212, 212);
    Color background = Color(45, 45, 50);
    static MathStyle inline_default() { MathStyle s; s.font_size = 14.0f; s.display = MathDisplayMode::Inline; return s; }
    static MathStyle block_default() { MathStyle s; s.font_size = 14.0f; s.display = MathDisplayMode::Block; return s; }
};

enum class RenderedMathKind { NativeGlyphRuns, VectorPicture, RasterImage, PlainTextFallback };

struct RenderedMath {
    LogicalSize size;
    float baseline = 0.0f;
    RenderedMathKind kind = RenderedMathKind::PlainTextFallback;
    std::string fallback_text;
    std::vector<MathDiagnostic> diagnostics;
};

struct InlineStyle {
    bool bold = false, italic = false, underline = false, strikethrough = false;
    bool code = false, link = false;
    std::optional<std::uint8_t> heading_level;
    static InlineStyle plain() { return {}; }
};

struct MarkerStyle {
    bool dimmed = false;
    std::optional<Color> color;
};

struct BorderSide { float width = 0.0f; std::optional<Color> color; };

struct BlockStyle {
    float margin_top = 0, margin_bottom = 0, margin_left = 0, margin_right = 0;
    float padding_top = 0, padding_bottom = 0, padding_left = 0, padding_right = 0;
    std::optional<Color> background;
    std::optional<BorderSide> border_left, border_right, border_top, border_bottom;

    static BlockStyle paragraph() { BlockStyle s; s.margin_bottom = 8; return s; }
    static BlockStyle heading(std::uint8_t level) {
        BlockStyle s;
        switch (level) {
            case 1: s.margin_top = 24; break; case 2: s.margin_top = 20; break;
            case 3: s.margin_top = 16; break; case 4: s.margin_top = 12; break;
            case 5: s.margin_top = 8;  break; default: s.margin_top = 4; break;
        }
        s.margin_bottom = 4;
        return s;
    }
    static BlockStyle code() {
        BlockStyle s; s.margin_top = 8; s.margin_bottom = 8;
        s.padding_top = 8; s.padding_bottom = 8; s.padding_left = 12; s.padding_right = 12;
        return s;
    }
    static BlockStyle blockquote() {
        BlockStyle s; s.margin_top = 8; s.margin_bottom = 8;
        s.padding_left = 16; s.border_left = BorderSide{3.0f, {}};
        return s;
    }
    static BlockStyle math() {
        BlockStyle s; s.margin_top = 12; s.margin_bottom = 12;
        s.padding_top = 8; s.padding_bottom = 8; s.padding_left = 12; s.padding_right = 12;
        return s;
    }
    static BlockStyle table()  { BlockStyle s; s.margin_top = 8; s.margin_bottom = 8; return s; }
    static BlockStyle image()  { BlockStyle s; s.margin_top = 12; s.margin_bottom = 12; return s; }
    static BlockStyle toc()    { BlockStyle s; s.margin_top = 12; s.margin_bottom = 12; s.padding_left = 8; s.border_left = BorderSide{2.0f, {}}; return s; }
    static BlockStyle callout(std::string_view /*kind*/) {
        BlockStyle s; s.margin_top = 8; s.margin_bottom = 8; s.padding_left = 16;
        s.padding_top = 8; s.padding_bottom = 8; s.border_left = BorderSide{4.0f, {}};
        return s;
    }
    static BlockStyle frontmatter() {
        BlockStyle s; s.margin_bottom = 12;
        s.padding_top = 8; s.padding_bottom = 8; s.padding_left = 12; s.padding_right = 12;
        return s;
    }
    static BlockStyle footnote() { BlockStyle s; s.margin_top = 4; s.margin_bottom = 4; s.padding_left = 8; return s; }
    static BlockStyle unsupported() {
        BlockStyle s; s.margin_top = 8; s.margin_bottom = 8; s.padding_left = 12;
        s.padding_top = 4; s.padding_bottom = 4;
        return s;
    }
    static BlockStyle extension() { BlockStyle s; s.margin_top = 4; s.margin_bottom = 4; return s; }
};

struct InlineRenderItem {
    enum class Kind { Text, Math, Image, Link, Marker };
    Kind kind = Kind::Text;
    TextRange<CharOffset> source_range;
    std::u32string text;
    std::optional<NodeId> id;             // Math/Image/Link
    InlineStyle style;
    MathDisplayMode display = MathDisplayMode::Inline; // Math
    std::string href, src, alt;            // Image/Link
    MarkerStyle marker_style;
    MarkerVisibility visibility = MarkerVisibility::WhenCaretInsideNode;
    std::vector<InlineRenderItem> children; // Link children

    static InlineRenderItem plain_text(std::u32string t, TextRange<CharOffset> r) {
        InlineRenderItem i; i.kind = Kind::Text; i.text = std::move(t); i.source_range = r; i.style = InlineStyle::plain();
        return i;
    }
};

enum class RenderBlockKind {
    Text, Code, Math, Table, Image, Toc, Callout, Frontmatter, Footnote, Unsupported, Extension,
};

struct RenderBlock {
    RenderBlockKind kind = RenderBlockKind::Text;
    NodeId id{};
    TextRange<CharOffset> source_range;
    BlockStyle block_style;

    // TextBlock
    std::vector<InlineRenderItem> inline_items;
    // CodeBlock
    std::optional<std::string> language;
    std::u32string code_text;
    std::size_t line_count = 0;
    // MathBlock
    std::u32string tex;
    MathDelimiter math_delim = MathDelimiter::BlockDollar;
    std::shared_ptr<RenderedMath> math_rendered;       // None in v1
    // Table
    std::vector<std::vector<InlineRenderItem>> table_cells;
    std::vector<TableAlignment> table_aligns;
    std::size_t column_count = 0, row_count = 0;
    // Image block (block-level, alt-only)
    // Callout / Footnote
    std::vector<RenderBlock> child_blocks;
    std::string callout_kind;
    std::optional<std::vector<InlineRenderItem>> callout_title;
    std::string footnote_label;
    // Frontmatter / Unsupported
    std::string raw;
    std::string reason_text;             // Unsupported reason text
    std::string extension_name;
    // Image block (block-level)
    std::string src;
    std::string alt;
};

struct RenderDiagnostic {
    enum class Sev { Info, Warning, Error };
    Sev severity = Sev::Warning;
    std::string message;
    std::optional<TextRange<CharOffset>> source_range;
};

struct RenderModel {
    std::uint64_t revision = 1;
    std::vector<RenderBlock> blocks;
    Outline outline;
    std::vector<RenderDiagnostic> diagnostics;
};

} // namespace elmd
