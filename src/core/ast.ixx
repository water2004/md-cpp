// elmd.core.ast — unified Markdown block tree plus block-local inline CST.
// CommonMark+GFM+math+toc+frontmatter+footnotes+definition_lists+callouts+
// wiki_links+tables+images, raw_html → UnsupportedMarkup.
export module elmd.core.ast;
import std;
import elmd.core.ids;
import elmd.core.dialect;
import elmd.core.inline_document;

export namespace elmd {

struct BlockNode;

using BlockVec = std::vector<BlockNode>;

enum class BlockKind {
    Document,
    Paragraph, Heading, BlockQuote, List, TaskList, ListItem, TaskListItem,
    CodeBlock, MathBlock, Table, TableRow, TableCell,
    ImageBlock, Callout, FootnoteDefinition, Toc, Frontmatter,
    ThematicBreak, LinkDefinition, UnsupportedMarkup, Extension,
};

struct BlockNode {
    NodeId id{};
    BlockKind kind = BlockKind::Paragraph;
    // Payload — only the relevant per `kind`.
    BlockVec children;                  // the one structural child collection for every container
    InlineDocument inline_content;      // Paragraph / Heading / TableCell
    std::uint8_t level = 0;             // Heading
    std::string slug;                   // Heading
    std::u32string marker;              // ListItem / TaskListItem
    bool checked = false;               // TaskListItem
    bool list_ordered = false;
    std::uint64_t list_start = 1;
    char32_t list_delimiter = U'.';
    std::optional<std::string> language; // CodeBlock
    std::u32string code_text;           // CodeBlock
    bool code_indented = false;
    std::u32string tex;                 // MathBlock
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
    std::optional<InlineDocument> callout_title; // Callout
    std::string footnote_label;        // FootnoteDefinition
    TocMarkerKind toc_marker = TocMarkerKind::BracketToc; // Toc
    FrontmatterFormat fmt = FrontmatterFormat::Yaml; // Frontmatter
    std::string raw;                    // Frontmatter.raw / UnsupportedMarkup.raw
    UnsupportedMarkupReason unsup_reason = UnsupportedMarkupReason::RawHtmlDisabled;
    std::string ext_name;               // Extension
};

// Heading sibling helper used in several places.
inline const InlineDocument& heading_inline_content(const BlockNode& b) { return b.inline_content; }
inline bool is_heading_block(const BlockNode& b) { return b.kind == BlockKind::Heading; }

} // namespace elmd
