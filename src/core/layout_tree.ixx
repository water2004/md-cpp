// elmd.core.layout_tree — LayoutTree / LayoutBlock / GlyphRunLayout etc.
export module elmd.core.layout_tree;
import std;
import elmd.core.types;
import elmd.core.ids;
import elmd.core.dialect;
import elmd.core.render_model;
import elmd.core.text_measurer;

export namespace elmd {

struct GlyphRunLayout {
    TextRange<CharOffset> source_range;
    std::u32string text;
    std::vector<GlyphInfo> glyphs;
    InlineStyle style;
    LogicalPoint origin;
    float width{};
    float height{};
    MarkerVisibility marker_visibility = MarkerVisibility::Always;
};

struct GlyphRunLayoutInit {
    TextRange<CharOffset> source_range;
    std::u32string text;
    InlineStyle style;
    GlyphRunLayoutInit(TextRange<CharOffset> sr, std::u32string t, InlineStyle s)
        : source_range(sr), text(std::move(t)), style(std::move(s)) {}
};

struct EmbeddedLayout {
    TextRange<CharOffset> source_range;
    LogicalRect rect;
    // EmbeddedKind values: inline-math / inline-image
    std::u32string tex;
    std::string src;
    std::string alt;
};

struct LayoutItem;

struct TextLineLayout {
    TextRange<CharOffset> source_range;
    LogicalRect rect;
    float baseline{};
    std::vector<GlyphRunLayout> runs;
    TextLineLayout() = default;
    explicit TextLineLayout(TextRange<CharOffset> sr) : source_range(sr) {}
};

struct MathLayout { NodeId id; TextRange<CharOffset> source_range; LogicalRect rect; std::u32string tex; };

struct ImageLayout { NodeId id; TextRange<CharOffset> source_range; LogicalRect rect; std::string src; std::string alt; };

struct TableLayoutColumn { float width; TableAlignment alignment; };
struct TableLayoutCell { TextRange<CharOffset> source_range; LogicalRect rect; std::vector<LayoutItem> content; };
struct TableLayoutRow { LogicalRect rect; std::vector<TableLayoutCell> cells; bool is_header{}; };
struct TableLayout {
    NodeId id; TextRange<CharOffset> source_range; LogicalRect rect;
    std::vector<TableLayoutRow> rows; std::vector<TableLayoutColumn> columns;
};

struct LayoutItem {
    enum class Kind { Line, Embedded, Table, BlockMath, Image };
    Kind kind = Kind::Line;
    TextLineLayout line;
    EmbeddedLayout embedded;
    TableLayout table;
    MathLayout math;
    ImageLayout image;
};

enum class LayoutBlockKind {
    Paragraph, Blank, Heading, CodeBlock, MathBlock, Table, Image, ThematicBreak,
    Callout, Toc, Frontmatter, Footnote, UnsupportedMarkup, Extension,
};
struct LayoutBlockKindVal { LayoutBlockKind kind; std::uint8_t level{}; std::string extension; };

struct LayoutBlock {
    NodeId id{};
    TextRange<CharOffset> source_range;
    LogicalRect rect;
    std::optional<float> baseline;
    LayoutBlockKindVal kind;
    std::vector<LayoutItem> children;
    BlockStyle style;
    LayoutBlock() = default;
    LayoutBlock(NodeId id_, TextRange<CharOffset> sr, LayoutBlockKindVal k, BlockStyle s)
        : id(id_), source_range(sr), kind(k), style(std::move(s)) {}
};

struct LayoutTree {
    std::uint64_t revision = 0;
    std::vector<LayoutBlock> blocks;
    float total_height{};
    LogicalRect viewport;

    static LayoutTree empty(std::uint64_t rev) { LayoutTree t; t.revision = rev; return t; }
    const LayoutBlock* block_at_y(float y) const {
        for (const auto& b : blocks) if (b.rect.y <= y && y < b.rect.y + b.rect.height) return &b;
        return nullptr;
    }
    std::pair<const LayoutBlock*, const TextLineLayout*> line_at_point(LogicalPoint p) const {
        for (const auto& b : blocks) {
            for (const auto& ch : b.children) if (ch.kind == LayoutItem::Kind::Line && ch.line.rect.contains_point(p)) return {&b, &ch.line};
        }
        return {nullptr, nullptr};
    }
};

} // namespace elmd
