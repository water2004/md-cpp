// elmd.core.inline_document — per-content-node inline source + lossless CST.
//
// An editable content node (Paragraph, Heading, TableCell, and the
// paragraph/heading bodies inside list items, task items, block quotes,
// callouts, footnotes) owns an `InlineDocument`: the character-exact Markdown
// source of its inline content plus a lossless editable CST over that source.
//
// Editing mutates `source` (via TextEdit) and re-parses the CST of *this node
// only*; the tree is structural/analysis state, never a second coordinate
// system. Saving emits `source` verbatim.
export module elmd.core.inline_document;
import std;
import elmd.core.ids;
import elmd.core.dialect;
import elmd.core.inline_cst;
import elmd.core.text_edit;
import elmd.core.utf;

export namespace elmd {

// Context the lossless inline parser needs: dialect, an id allocator seed,
// and (optionally) a link-definition resolver for reference links. This is a
// concrete type so InlineDocument can hold/forward it without a circular import.
struct InlineLinkDef {
    std::string href;
    std::optional<std::string> title;
};

struct InlineParseContext {
    MarkdownDialect dialect{};
    NodeId next_id{1};
    std::function<NodeId()> allocate_id;
    std::function<std::optional<InlineLinkDef>(const std::string&)> resolve_link_label;
};

struct InlineDocument {
    std::u32string source;
    InlineCstTree tree;

    std::u32string serialize() const { return source; }
};

// ---- queries over a CST (used by renderer / hit-test / format-state) ----

// Find the deepest node whose range covers `offset`. Returns nullptr if none.
// Descends into container children so the returned node is the most specific
// covering node (leaf or innermost container).
inline const InlineCstNode* node_at_offset(const InlineCstTree& tree, std::size_t offset) {
    const InlineCstNode* deepest = nullptr;
    std::function<const InlineCstNode*(const std::vector<InlineCstNode>&)> search =
        [&](const std::vector<InlineCstNode>& nodes) -> const InlineCstNode* {
        for (const auto& node : nodes) {
            if (!node.range.covers(offset)) continue;
            deepest = &node;
            search(node.children);
            return deepest;
        }
        return nullptr;
    };
    search(tree.nodes);
    return deepest;
}

// Is the offset inside a marker (opening or closing) of a delimited node?
inline bool offset_in_marker(const InlineCstNode& node, std::size_t offset) {
    if (node.delim.opening.contains(offset)) return true;
    if (node.delim.closing && node.delim.closing->contains(offset)) return true;
    return false;
}

// Does the node's content range cover the offset (half-open; the boundary at
// content.start counts as "in content", content.end counts as just past)?
inline bool offset_in_content(const InlineCstNode& node, std::size_t offset) {
    return offset >= node.delim.content.start && offset <= node.delim.content.end;
}

inline std::u32string inline_source_slice(const InlineDocument& document, SourceRange range) {
    if (!range.valid_for(document.source.size())) return {};
    return document.source.substr(range.start, range.length());
}

inline std::u32string decode_inline_entity(std::u32string_view raw) {
    if (raw == U"&amp;") return U"&";
    if (raw == U"&lt;") return U"<";
    if (raw == U"&gt;") return U">";
    if (raw == U"&quot;") return U"\"";
    if (raw == U"&apos;" || raw == U"&#39;") return U"'";
    if (raw == U"&nbsp;") return U"\u00a0";
    if (raw.size() >= 4 && raw[0] == U'&' && raw[1] == U'#' && raw.back() == U';') {
        std::uint32_t value = 0;
        std::size_t cursor = 2;
        int base = 10;
        if (cursor < raw.size() - 1 && (raw[cursor] == U'x' || raw[cursor] == U'X')) {
            base = 16;
            ++cursor;
        }
        bool valid = cursor < raw.size() - 1;
        for (; cursor < raw.size() - 1 && valid; ++cursor) {
            const auto ch = raw[cursor];
            int digit = -1;
            if (ch >= U'0' && ch <= U'9') digit = static_cast<int>(ch - U'0');
            else if (base == 16 && ch >= U'a' && ch <= U'f') digit = 10 + static_cast<int>(ch - U'a');
            else if (base == 16 && ch >= U'A' && ch <= U'F') digit = 10 + static_cast<int>(ch - U'A');
            else valid = false;
            if (digit >= base) valid = false;
            if (valid) value = value * static_cast<std::uint32_t>(base) + static_cast<std::uint32_t>(digit);
        }
        if (valid && value <= 0x10ffff && !(value >= 0xd800 && value <= 0xdfff)) {
            return std::u32string(1, static_cast<char32_t>(value));
        }
    }
    return std::u32string(raw);
}

inline void append_inline_visible_text(
    const InlineDocument& document,
    const InlineCstNodes& nodes,
    std::u32string& output) {
    for (const auto& node : nodes) {
        using K = InlineCstKind;
        switch (node.kind) {
            case K::Emphasis:
            case K::Strong:
            case K::Strikethrough:
            case K::Link:
            case K::HtmlElement:
                append_inline_visible_text(document, node.children, output);
                break;
            case K::Image:
                output += utf8_to_cps(node.alt);
                break;
            case K::WikiLink:
                output += utf8_to_cps(node.alias.value_or(node.target));
                break;
            case K::FootnoteRef:
                output += U"[^" + utf8_to_cps(node.label) + U"]";
                break;
            case K::CodeSpan:
            case K::InlineMath:
            case K::Autolink:
                output += inline_source_slice(document, node.delim.content);
                break;
            case K::Escape: {
                const auto raw = inline_source_slice(document, node.range);
                if (!raw.empty()) output.push_back(raw.back());
                break;
            }
            case K::Entity:
                output += decode_inline_entity(inline_source_slice(document, node.range));
                break;
            case K::SoftBreak:
            case K::HardBreak:
                output.push_back(U'\n');
                break;
            default:
                output += inline_source_slice(document, node.range);
                break;
        }
    }
}

inline std::u32string inline_visible_text(const InlineDocument& document) {
    std::u32string output;
    append_inline_visible_text(document, document.tree.nodes, output);
    return output;
}

inline bool inline_contains_kind(const InlineCstNodes& nodes, InlineCstKind kind) {
    for (const auto& node : nodes) {
        if (node.kind == kind || inline_contains_kind(node.children, kind)) return true;
    }
    return false;
}

inline bool inline_contains_kind(const InlineDocument& document, InlineCstKind kind) {
    return inline_contains_kind(document.tree.nodes, kind);
}

} // namespace elmd
