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
import elmd.core.selection;
import elmd.core.text_edit;
import elmd.core.source_style;

export namespace elmd {

enum class MathDisplayMode { Inline, Block };
enum class MarkerVisibility { Always, WhenCaretInsideNode, WhenBlockFocused, HiddenButEditable };
enum class MarkerRole {
    Syntax,
    Heading,
    ListBullet,
    ListNumber,
    TaskCheckbox,
    FootnoteLabel,
    Structural,
};

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
    static BlockStyle list() { BlockStyle s; s.margin_bottom = 8; s.padding_left = 20; return s; }
    static BlockStyle thematic_break() { BlockStyle s; s.margin_top = 8; s.margin_bottom = 8; return s; }
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
        s.padding_top = 6; s.padding_bottom = 6; s.padding_left = 18; s.padding_right = 8;
        s.border_left = BorderSide{3.0f, {}};
        return s;
    }
    static BlockStyle math() {
        BlockStyle s; s.margin_top = 12; s.margin_bottom = 12;
        s.padding_top = 8; s.padding_bottom = 8; s.padding_left = 12; s.padding_right = 12;
        // Display math is an atomic editable block. Keep its container
        // visible while the source delimiters are revealed for editing.
        s.background = Color{};
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
    static BlockStyle footnote() {
        BlockStyle s;
        s.margin_top = 8;
        s.margin_bottom = 8;
        s.padding_top = 8;
        s.padding_bottom = 8;
        s.padding_left = 12;
        s.padding_right = 12;
        // A definition is one list item container whose children may contain
        // paragraphs, lists, quotes, code, and other block content.
        s.background = Color{};
        return s;
    }
    static BlockStyle unsupported() {
        BlockStyle s; s.margin_top = 8; s.margin_bottom = 8; s.padding_left = 12;
        s.padding_top = 4; s.padding_bottom = 4;
        return s;
    }
    static BlockStyle extension() { BlockStyle s; s.margin_top = 4; s.margin_bottom = 4; return s; }
};

struct InlineRenderItem {
    enum class Kind { Text, Math, Image, Link, FootnoteReference, Marker };
    Kind kind = Kind::Text;
    TextSpan source_span;
    std::u32string source_text;          // exact source spelling for this span
    std::u32string text;
    std::u32string display_text;
    std::optional<NodeId> id;             // Math/Image/Link
    std::optional<NodeId> marker_owner;
    InlineStyle style;
    MathDisplayMode display = MathDisplayMode::Inline; // Math
    MathDelimiter math_delim = MathDelimiter::InlineDollar;
    std::string href, src, alt;            // Image/Link
    std::string footnote_label;            // FootnoteReference / generated footnote controls
    std::optional<std::string> title;
    std::optional<float> image_width;
    std::optional<float> image_height;
    MarkerStyle marker_style;
    SourceSyntaxKind source_syntax = SourceSyntaxKind::None;
    MarkerVisibility visibility = MarkerVisibility::WhenCaretInsideNode;
    MarkerRole marker_role = MarkerRole::Syntax;
    bool task_checked = false;           // TaskCheckbox marker semantic state
    // Generated block markers have an empty source span. This records which
    // visual edge of that marker is the actual source boundary: after a
    // prefix (Downstream), before a suffix (Upstream).
    std::optional<TextAffinity> generated_boundary_affinity;
    std::vector<InlineRenderItem> children; // Link children

    static InlineRenderItem plain_text(std::u32string t, TextSpan span) {
        InlineRenderItem i; i.kind = Kind::Text; i.text = std::move(t); i.source_span = span; i.style = InlineStyle::plain();
        return i;
    }
};

enum class RenderBlockKind {
    Text, Blank, Quote, Code, Math, Table, Image, ThematicBreak, Toc, Callout, Frontmatter, Footnote, Unsupported, Extension,
};

struct RenderBlock {
    RenderBlockKind kind = RenderBlockKind::Text;
    NodeId id{};
    // Block-local editable span. Container-only blocks use an empty span owned
    // by the container; their visible descendants carry their own spans.
    TextSpan source_span;
    TextSpan content_span;
    BlockStyle block_style;
    bool source_mode = false;
    bool source_code = false;
    std::uint64_t presentation_key = 0;

    // TextBlock
    std::vector<InlineRenderItem> inline_items;
    // CodeBlock
    std::u32string raw_source;
    std::vector<std::size_t> content_to_source;
    std::optional<std::string> language;
    std::u32string code_text;
    std::size_t line_count = 0;
    bool code_indented = false;
    // MathBlock
    std::u32string tex;
    MathDelimiter math_delim = MathDelimiter::BlockDollar;
    std::shared_ptr<RenderedMath> math_rendered;       // None in v1
    // Table
    std::vector<std::vector<InlineRenderItem>> table_cells;
    std::vector<TextSpan> table_cell_spans;
    std::vector<TableAlignment> table_aligns;
    std::size_t column_count = 0, row_count = 0;
    bool table_header_row = true;
    // Image block (block-level, alt-only)
    // Quote / Callout / Footnote
    std::vector<RenderBlock> child_blocks;
    // Display indentation introduced by the edge from the parent flow node to
    // this node. Cumulative indentation is derived only by walking child_blocks.
    std::size_t flow_local_indent_columns = 0;
    NodeId flow_anchor_owner_id{};
    std::string callout_kind;
    std::string footnote_label;
    // Frontmatter / Unsupported
    std::string raw;
    std::string reason_text;             // Unsupported reason text
    std::string extension_name;
    // Image block (block-level)
    std::string src;
    std::string alt;
    std::optional<std::string> title;
    std::optional<std::string> link;
    std::optional<float> image_width;
    std::optional<float> image_height;
};

struct RenderDiagnostic {
    enum class Sev { Info, Warning, Error };
    Sev severity = Sev::Warning;
    std::string message;
    std::optional<TextSpan> source_span;
};

struct RenderModel {
    std::uint64_t revision = 1;
    std::vector<RenderBlock> blocks;
    Outline outline;
    std::vector<RenderDiagnostic> diagnostics;
    std::vector<NodeId> editable_order;
};

} // namespace elmd
