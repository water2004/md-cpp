// elmd.core.ast — unified Markdown block tree plus block-local inline CST.
// CommonMark+GFM+math+toc+frontmatter+footnotes+definition_lists+callouts+
// wiki_links+tables+images, raw_html → UnsupportedMarkup.
export module elmd.core.ast;
import std;
import elmd.core.block_source;
import elmd.core.ids;
import elmd.core.dialect;
import elmd.core.inline_document;

export namespace elmd {

struct BlockNode;

using BlockVec = std::vector<BlockNode>;

enum class BlockKind {
    Document,
    Paragraph, Heading, CalloutTitle, BlockQuote, List, TaskList, ListItem, TaskListItem,
    CodeBlock, MathBlock, Table, TableRow, TableCell,
    ImageBlock, Callout, FootnoteDefinition, Toc, Frontmatter,
    ThematicBreak, LinkDefinition, UnsupportedMarkup, Extension,
};

inline bool is_editable_block_owner(BlockKind kind) {
    switch (kind) {
        case BlockKind::Paragraph:
        case BlockKind::Heading:
        case BlockKind::CalloutTitle:
        case BlockKind::TableCell:
        case BlockKind::CodeBlock:
        case BlockKind::MathBlock:
        case BlockKind::ImageBlock:
        case BlockKind::Toc:
        case BlockKind::Frontmatter:
        case BlockKind::ThematicBreak:
        case BlockKind::LinkDefinition:
        case BlockKind::UnsupportedMarkup:
        case BlockKind::Extension:
            return true;
        default:
            return false;
    }
}

struct BlockNodeSpecial {
    std::uint8_t level = 0;             // Heading
    std::string slug;                   // Heading
    std::u32string marker;              // ListItem / TaskListItem
    bool checked = false;               // TaskListItem
    bool list_ordered = false;
    std::uint64_t list_start = 1;
    char32_t list_delimiter = U'.';
    bool code_indented = false;
    MathDelimiter math_delim = MathDelimiter::BlockDollar; // MathBlock / InlineMath (in inline)
    std::vector<TableAlignment> table_aligns;
    bool table_header_row = false;      // TableRow
    std::string src;        // ImageBlock
    std::string image_alt;
    std::optional<std::string> image_title;
    std::optional<std::string> image_link;
    std::optional<float> image_width;
    std::optional<float> image_height;
    std::u32string opening_marker;
    std::u32string closing_marker;
    std::string callout_kind;           // Callout
    std::string footnote_label;        // FootnoteDefinition
    TocMarkerKind toc_marker = TocMarkerKind::BracketToc; // Toc
    FrontmatterFormat fmt = FrontmatterFormat::Yaml; // Frontmatter
    std::string raw;                    // Frontmatter.raw / UnsupportedMarkup.raw
    UnsupportedMarkupReason unsup_reason = UnsupportedMarkupReason::RawHtmlDisabled;
    std::string ext_name;               // Extension
    // Exact source between the preceding direct sibling and this block.
    // This is parser/serializer ownership metadata, never an editing
    // coordinate and never part of a block's local editable source.
    std::optional<std::u32string> separator_before;
};

struct BlockNode {
    NodeId id{};
    BlockKind kind = BlockKind::Paragraph;
    BlockVec children;                  // the one structural child collection for every container
    InlineDocument inline_content;      // Paragraph / Heading / CalloutTitle / TableCell
    BlockSourceDocument block_source;   // CodeBlock / MathBlock, exact full Markdown source
    // Paragraphs and structural containers must not carry the full payload of
    // every unrelated block kind. The optional payload retains ordinary value
    // semantics through the deep-copy operations below.
    std::unique_ptr<BlockNodeSpecial> payload;

    BlockNode() = default;
    BlockNode(BlockNode const& other)
        : id(other.id), kind(other.kind), children(other.children),
          inline_content(other.inline_content), block_source(other.block_source),
          payload(other.payload ? std::make_unique<BlockNodeSpecial>(*other.payload) : nullptr) {}
    BlockNode& operator=(BlockNode const& other) {
        if (this == &other) return *this;
        id = other.id;
        kind = other.kind;
        children = other.children;
        inline_content = other.inline_content;
        block_source = other.block_source;
        payload = other.payload ? std::make_unique<BlockNodeSpecial>(*other.payload) : nullptr;
        return *this;
    }
    BlockNode(BlockNode&&) noexcept = default;
    BlockNode& operator=(BlockNode&&) noexcept = default;

    BlockNodeSpecial const& special() const {
        static const BlockNodeSpecial empty{};
        return payload ? *payload : empty;
    }

    BlockNodeSpecial& ensure_special() {
        if (!payload) payload = std::make_unique<BlockNodeSpecial>();
        return *payload;
    }
};

// Heading sibling helper used in several places.
inline const InlineDocument& heading_inline_content(const BlockNode& b) { return b.inline_content; }
inline bool is_heading_block(const BlockNode& b) { return b.kind == BlockKind::Heading; }

} // namespace elmd
