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
    Color color{};
    Color background{};
    static MathStyle inline_default(ThemeProfile const& theme = default_theme_profile()) {
        return {theme.typography.body.size, MathDisplayMode::Inline, theme.colors.math_fg, theme.colors.math_bg};
    }
    static MathStyle block_default(ThemeProfile const& theme = default_theme_profile()) {
        return {theme.typography.body.size, MathDisplayMode::Block, theme.colors.math_fg, theme.colors.math_bg};
    }
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

    static BlockStyle paragraph(ThemeLayout const& theme = default_theme_profile().layout) {
        BlockStyle s; s.margin_bottom = theme.paragraph_margin_bottom; return s;
    }
    static BlockStyle list(ThemeLayout const& theme = default_theme_profile().layout) {
        BlockStyle s; s.margin_bottom = theme.list_margin_bottom; s.padding_left = theme.list_padding_left; return s;
    }
    static BlockStyle thematic_break(ThemeLayout const& theme = default_theme_profile().layout) {
        BlockStyle s; s.margin_top = theme.thematic_break_margin; s.margin_bottom = theme.thematic_break_margin; return s;
    }
    static BlockStyle heading(std::uint8_t level, ThemeLayout const& theme = default_theme_profile().layout) {
        BlockStyle s;
        const auto index = (std::min)(std::size_t{5}, level == 0 ? std::size_t{} : static_cast<std::size_t>(level - 1));
        s.margin_top = theme.heading_margin_top[index];
        s.margin_bottom = theme.heading_margin_bottom;
        return s;
    }
    static BlockStyle code(ThemeLayout const& theme = default_theme_profile().layout) {
        BlockStyle s; s.margin_top = theme.code_margin; s.margin_bottom = theme.code_margin;
        s.padding_top = theme.code_padding_vertical; s.padding_bottom = theme.code_padding_vertical;
        s.padding_left = theme.code_padding_horizontal; s.padding_right = theme.code_padding_horizontal;
        return s;
    }
    static BlockStyle blockquote(ThemeLayout const& theme = default_theme_profile().layout) {
        BlockStyle s; s.margin_top = theme.quote_margin; s.margin_bottom = theme.quote_margin;
        s.padding_top = theme.quote_padding_vertical; s.padding_bottom = theme.quote_padding_vertical;
        s.padding_left = theme.quote_padding_left; s.padding_right = theme.quote_padding_right;
        s.border_left = BorderSide{theme.quote_border_width, {}};
        return s;
    }
    static BlockStyle math(ThemeLayout const& theme = default_theme_profile().layout) {
        BlockStyle s; s.margin_top = theme.math_margin; s.margin_bottom = theme.math_margin;
        s.padding_top = theme.math_padding_vertical; s.padding_bottom = theme.math_padding_vertical;
        s.padding_left = theme.math_padding_horizontal; s.padding_right = theme.math_padding_horizontal;
        // Display math is an atomic editable block. Keep its container
        // visible while the source delimiters are revealed for editing.
        s.background = Color{};
        return s;
    }
    static BlockStyle table(ThemeLayout const& theme = default_theme_profile().layout) {
        BlockStyle s; s.margin_top = theme.table_margin; s.margin_bottom = theme.table_margin; return s;
    }
    static BlockStyle image(ThemeLayout const& theme = default_theme_profile().layout) {
        BlockStyle s; s.margin_top = theme.image_margin; s.margin_bottom = theme.image_margin; return s;
    }
    static BlockStyle toc(ThemeLayout const& theme = default_theme_profile().layout) {
        BlockStyle s; s.margin_top = theme.toc_margin; s.margin_bottom = theme.toc_margin;
        s.padding_left = theme.toc_padding_left; s.border_left = BorderSide{theme.toc_border_width, {}}; return s;
    }
    static BlockStyle callout(std::string_view /*kind*/, ThemeLayout const& theme = default_theme_profile().layout) {
        BlockStyle s; s.margin_top = theme.callout_margin; s.margin_bottom = theme.callout_margin;
        s.padding_left = theme.callout_padding_left;
        s.padding_top = theme.callout_padding_vertical; s.padding_bottom = theme.callout_padding_vertical;
        s.border_left = BorderSide{theme.callout_border_width, {}};
        return s;
    }
    static BlockStyle frontmatter(ThemeLayout const& theme = default_theme_profile().layout) {
        BlockStyle s; s.margin_bottom = theme.frontmatter_margin_bottom;
        s.padding_top = theme.frontmatter_padding_vertical; s.padding_bottom = theme.frontmatter_padding_vertical;
        s.padding_left = theme.frontmatter_padding_horizontal; s.padding_right = theme.frontmatter_padding_horizontal;
        return s;
    }
    static BlockStyle footnote(ThemeLayout const& theme = default_theme_profile().layout) {
        BlockStyle s;
        s.margin_top = theme.footnote_margin;
        s.margin_bottom = theme.footnote_margin;
        s.padding_top = theme.footnote_padding_vertical;
        s.padding_bottom = theme.footnote_padding_vertical;
        s.padding_left = theme.footnote_padding_horizontal;
        s.padding_right = theme.footnote_padding_horizontal;
        // A definition is one list item container whose children may contain
        // paragraphs, lists, quotes, code, and other block content.
        s.background = Color{};
        return s;
    }
    static BlockStyle unsupported(ThemeLayout const& theme = default_theme_profile().layout) {
        BlockStyle s; s.margin_top = theme.unsupported_margin; s.margin_bottom = theme.unsupported_margin;
        s.padding_left = theme.unsupported_padding_left;
        s.padding_top = theme.unsupported_padding_vertical; s.padding_bottom = theme.unsupported_padding_vertical;
        return s;
    }
    static BlockStyle extension(ThemeLayout const& theme = default_theme_profile().layout) {
        BlockStyle s; s.margin_top = theme.extension_margin; s.margin_bottom = theme.extension_margin; return s;
    }
};

struct InlineRenderItem;

struct InlineRenderSemanticPayload {
    std::u32string source_text;          // exact source spelling for this span
    std::string href, src, alt;          // Image/Link
    std::string footnote_label;          // FootnoteReference / generated footnote controls
    std::optional<std::string> title;
    std::optional<float> image_width;
    std::optional<float> image_height;
    bool block_image = false;            // Image occupying its own block flow line
    std::vector<InlineRenderItem> children; // Link children
};

struct InlineRenderPayload {
    std::u32string display_text;         // generated/normalized visual spelling
    std::optional<NodeId> marker_owner;
    MathDisplayMode display = MathDisplayMode::Inline;
    MathDelimiter math_delim = MathDelimiter::InlineDollar;
    MarkerStyle marker_style;
    MarkerVisibility visibility = MarkerVisibility::WhenCaretInsideNode;
    MarkerRole marker_role = MarkerRole::Syntax;
    bool task_checked = false;
    // Generated block markers have an empty source span. This records which
    // visual edge of that marker is the actual source boundary.
    std::optional<TextAffinity> generated_boundary_affinity;
    // Link/image/math/footnote data is uncommon compared with syntax markers;
    // keep it behind a second payload so marker allocations stay compact.
    std::shared_ptr<InlineRenderSemanticPayload> semantic_payload;

    InlineRenderSemanticPayload const& semantic() const {
        static const InlineRenderSemanticPayload empty{};
        return semantic_payload ? *semantic_payload : empty;
    }

    InlineRenderSemanticPayload& ensure_semantic() {
        if (!semantic_payload) semantic_payload = std::make_shared<InlineRenderSemanticPayload>();
        return *semantic_payload;
    }
};

struct InlineRenderItem {
    enum class Kind { Text, Math, Image, Link, FootnoteReference, Marker };
    Kind kind = Kind::Text;
    TextSpan source_span;
    std::u32string text;
    std::optional<NodeId> id;             // Math/Image/Link
    InlineStyle style;
    SourceSyntaxKind source_syntax = SourceSyntaxKind::None;
    std::shared_ptr<InlineRenderPayload> payload;

    InlineRenderPayload const& special() const {
        static const InlineRenderPayload empty{};
        return payload ? *payload : empty;
    }

    InlineRenderPayload& ensure_special() {
        if (!payload) payload = std::make_shared<InlineRenderPayload>();
        return *payload;
    }

    static InlineRenderItem plain_text(std::u32string t, TextSpan span) {
        InlineRenderItem i; i.kind = Kind::Text; i.text = std::move(t); i.source_span = span; i.style = InlineStyle::plain();
        return i;
    }
};

enum class RenderBlockKind {
    Text, Blank, Quote, Code, Math, Table, Image, ThematicBreak, Toc, Callout, Frontmatter, Footnote, Unsupported, Extension,
};

struct RenderBlockSpecial {
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
    std::shared_ptr<RenderedMath> math_rendered;
    // Table
    std::vector<std::vector<InlineRenderItem>> table_cells;
    std::vector<TextSpan> table_cell_spans;
    std::vector<TableAlignment> table_aligns;
    std::size_t column_count = 0, row_count = 0;
    bool table_header_row = true;
    // Container labels / raw and extension payloads
    std::string callout_kind;
    std::string footnote_label;
    std::string raw;
    std::string reason_text;
    std::string extension_name;
    // Image block
    std::string src;
    std::string alt;
    std::optional<std::string> title;
    std::optional<std::string> link;
    std::optional<float> image_width;
    std::optional<float> image_height;
    // Local GIF headers are probed before viewport materialization so image
    // placeholders keep stable dimensions without walking every inline item.
    std::vector<std::string> inline_image_sources;
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
    // One-based logical source line. Zero outside source mode. Kept outside
    // the editable text so gutters never participate in hit testing.
    std::uint32_t source_line_number = 0;
    // Virtualized rich-text models keep an inexpensive block-shaped entry for
    // offscreen content.  A materialized entry owns the derived inline render
    // projection; the authoritative block tree remains the source of truth.
    bool materialized = true;
    // Fingerprint of the authoritative block-tree inputs used to build this
    // projection. It permits block-local render-model rebuilding.
    std::uint64_t source_key = 0;
    std::uint64_t presentation_key = 0;
    std::shared_ptr<const std::u32string> source_code_context;
    std::size_t source_code_context_offset = 0;

    // TextBlock
    std::vector<InlineRenderItem> inline_items;
    // Image block (block-level, alt-only)
    // Quote / Callout / Footnote
    std::vector<RenderBlock> child_blocks;
    // Display indentation introduced by the edge from the parent flow node to
    // this node. Cumulative indentation is derived only by walking child_blocks.
    std::size_t flow_local_indent_columns = 0;
    NodeId flow_anchor_owner_id{};
    // Cheap geometry hints computed once with the render projection. The UI
    // must not rescan every inline item merely to estimate offscreen height.
    std::uint32_t estimated_characters = 0;
    std::uint32_t estimated_line_breaks = 0;
    std::uint8_t text_heading_level = 0;
    std::shared_ptr<RenderBlockSpecial> payload;

    RenderBlockSpecial const& special() const {
        static const RenderBlockSpecial empty{};
        return payload ? *payload : empty;
    }

    RenderBlockSpecial& ensure_special() {
        if (!payload) payload = std::make_shared<RenderBlockSpecial>();
        return *payload;
    }
};

struct RenderDiagnostic {
    enum class Sev { Info, Warning, Error };
    Sev severity = Sev::Warning;
    std::string message;
    std::optional<TextSpan> source_span;
};

struct RenderModelUpdate {
    bool structural = false;
    bool structural_locality_known = true;
    std::vector<NodeId> changed_owners;
    std::vector<NodeId> structural_anchors;
};

struct RenderModel {
    std::uint64_t revision = 1;
    std::vector<RenderBlock> blocks;
    bool virtualized = false;
    // Only populated for virtualized rich-text models.  Tracking the sparse
    // working set avoids scanning every block when the viewport moves.
    std::unordered_set<std::size_t> materialized_block_indices;
    Outline outline;
    std::vector<RenderDiagnostic> diagnostics;
    std::vector<NodeId> editable_order;
    // Stable document-order rank, built with the render projection instead of
    // reconstructed by every viewport draw.
    std::unordered_map<std::uint64_t, std::size_t> editable_index;
    // Maps every editable owner to the top-level render block that contains
    // it, allowing local source transactions to invalidate one subtree.
    std::unordered_map<std::uint64_t, NodeId> editable_top_level;
    std::uint64_t document_dependency_key = 0;
    std::size_t rebuilt_block_count = 0;
    std::size_t reused_block_count = 0;
    // Populated only when the previous model was updated in place without a
    // structural or document-wide dependency change.
    bool incremental_update = false;
    std::vector<std::size_t> changed_block_indices;
};

} // namespace elmd
