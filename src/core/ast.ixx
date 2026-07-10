// elmd.core.ast — Markdown AST: BlockNode and InlineNode. NO HTML variants.
// CommonMark+GFM+math+toc+frontmatter+footnotes+definition_lists+callouts+
// wiki_links+tables+images, raw_html → UnsupportedMarkup.
//
// We model both enums as a single struct carrying a "kind" discriminant so
// that BlockNode/InlineNode are mutually-recursive value types without the
// aliasing/pay-load limits of std::variant with recursive types.
export module elmd.core.ast;
import std;
import elmd.core.ids;
import elmd.core.dialect;
import elmd.core.utf;

export namespace elmd {

// (forward declarations resolved below)
struct InlineNode;
struct BlockNode;

using InlineVec = std::vector<InlineNode>;
using BlockVec = std::vector<BlockNode>;

struct ListItem     { NodeId id{}; std::u32string marker; BlockVec children; };
struct TaskListItem { NodeId id{}; bool checked = false; std::u32string marker; BlockVec children; };
struct TableCell    { NodeId id{}; InlineVec children; };
struct TableRow     { NodeId id{}; std::vector<TableCell> cells; };

enum class BlockKind {
    Paragraph, Heading, BlockQuote, List, TaskList, CodeBlock, MathBlock,
    Table, ImageBlock, Callout, FootnoteDefinition, Toc, Frontmatter,
    ThematicBreak, UnsupportedMarkup, Extension,
};

enum class InlineKind {
    Text, Emphasis, Strong, Strike, InlineCode, InlineMath, Link, Image,
    FootnoteRef, WikiLink, SoftBreak, HardBreak, UnsupportedMarkup, Extension,
};

struct InlineNode {
    NodeId id{};
    InlineKind kind = InlineKind::Text;
    // Payload — only the relevant fields are populated per `kind`.
    std::u32string text;        // Text / InlineCode.code / InlineMath.tex / UnsupportedMarkup.raw
    MathDelimiter math_delim = MathDelimiter::InlineDollar;
    std::string href;           // Link / Image
    std::string alt;            // Image
    std::optional<std::string> title; // Link / Image
    InlineVec children;         // Emphasis / Strong / Strike / Link
    std::string label;          // FootnoteRef
    std::string target;         // WikiLink
    std::optional<std::string> alias; // WikiLink
    UnsupportedMarkupReason unsup_reason = UnsupportedMarkupReason::RawHtmlDisabled;
    std::string ext_name;       // Extension (payload = Text only in v1)
    std::u32string ext_text;    // Extension Text payload

    static InlineNode text_node(NodeId id, std::u32string t) {
        InlineNode n; n.id = id; n.kind = InlineKind::Text; n.text = std::move(t); return n;
    }
    static InlineNode text_node(NodeId id, std::string utf8) {
        return text_node(id, elmd::utf8_to_cps(utf8));
    }
};

struct BlockNode {
    NodeId id{};
    BlockKind kind = BlockKind::Paragraph;
    // Payload — only the relevant per `kind`.
    InlineVec children;                 // Paragraph / Heading
    BlockVec quote_children;            // BlockQuote / Callout / FootnoteDefinition
    std::uint8_t level = 0;             // Heading
    std::string slug;                   // Heading
    std::vector<ListItem> list_items;    // List
    std::vector<TaskListItem> task_items;// TaskList
    bool list_ordered = false;
    std::uint64_t list_start = 1;
    char32_t list_delimiter = U'.';
    std::optional<std::string> language; // CodeBlock
    std::u32string code_text;           // CodeBlock
    bool code_indented = false;
    std::u32string tex;                 // MathBlock
    MathDelimiter math_delim = MathDelimiter::BlockDollar; // MathBlock / InlineMath (in inline)
    std::vector<TableCell> table_header;
    std::vector<TableRow> table_rows;
    std::vector<TableAlignment> table_aligns;
    std::string src;        // ImageBlock
    std::string image_alt;
    std::optional<std::string> image_title;
    std::string callout_kind;           // Callout
    std::optional<InlineVec> callout_title; // Callout
    std::string footnote_label;        // FootnoteDefinition
    TocMarkerKind toc_marker = TocMarkerKind::BracketToc; // Toc
    FrontmatterFormat fmt = FrontmatterFormat::Yaml; // Frontmatter
    std::string raw;                    // Frontmatter.raw / UnsupportedMarkup.raw
    UnsupportedMarkupReason unsup_reason = UnsupportedMarkupReason::RawHtmlDisabled;
    std::string ext_name;               // Extension
};

// Heading sibling helper used in several places.
inline const InlineVec& heading_children(const BlockNode& b) { return b.children; }
inline bool is_heading_block(const BlockNode& b) { return b.kind == BlockKind::Heading; }

// text_content: flatten a single inline to plain text (matches Rust text_content).
inline std::u32string inline_text_content(const InlineNode& n) {
    using K = InlineKind;
    std::u32string out;
    auto join_children = [&](const InlineVec& ch) {
        for (const auto& c : ch) { auto t = inline_text_content(c); out.insert(out.end(), t.begin(), t.end()); }
    };
    switch (n.kind) {
        case K::Text: case K::InlineCode: case K::InlineMath: case K::UnsupportedMarkup:
            return n.text;
        case K::Emphasis: case K::Strong: case K::Strike: join_children(n.children); return out;
        case K::Link: join_children(n.children); return out;
        case K::Image:
            return elmd::utf8_to_cps(n.alt);
        case K::FootnoteRef: {
            std::string s = "[^" + n.label + "]";
            return elmd::utf8_to_cps(s);
        }
        case K::WikiLink: {
            std::string s = n.alias ? "[[" + n.target + "|" + *n.alias + "]]" : "[[" + n.target + "]]";
            return elmd::utf8_to_cps(s);
        }
        case K::SoftBreak: return std::u32string(1, U'\n');
        case K::HardBreak: return elmd::utf8_to_cps("  \n");
        case K::Extension: {
            std::string s = n.ext_text.empty() ? "[ext:" + n.ext_name + "]" : "[ext:" + n.ext_name + ":" + elmd::cps_to_utf8(n.ext_text) + "]";
            return elmd::utf8_to_cps(s);
        }
    }
    return out;
}

inline std::u32string block_inline_text_content(const InlineVec& v) {
    std::u32string out;
    for (const auto& c : v) { auto t = inline_text_content(c); out.insert(out.end(), t.begin(), t.end()); }
    return out;
}

} // namespace elmd
