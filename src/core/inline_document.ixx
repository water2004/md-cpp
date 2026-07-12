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

} // namespace elmd
