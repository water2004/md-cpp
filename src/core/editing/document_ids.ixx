// elmd.core.document_ids — document-owned monotonic NodeId allocation.
export module elmd.core.document_ids;
import std;
import elmd.core.ast;
import elmd.core.document;
import elmd.core.ids;
import elmd.core.inline_cst;
import elmd.core.instrumentation;

export namespace elmd {

namespace document_id_detail {

inline void scan_inline_ids(const InlineCstNodes& nodes, std::uint64_t& maximum) {
    for (const auto& node : nodes) {
        maximum = (std::max)(maximum, node.id.v);
        scan_inline_ids(node.children, maximum);
    }
}

inline void scan_block_ids(const BlockNode& block, std::uint64_t& maximum) {
    maximum = (std::max)(maximum, block.id.v);
    scan_inline_ids(block.inline_content.tree.nodes, maximum);
    for (const auto& child : block.children) scan_block_ids(child, maximum);
}

} // namespace document_id_detail

// Parsed and editor-created documents initialize the cursor eagerly. This
// compatibility boundary is only for externally assembled documents, and its
// instrumentation makes an accidental normal-edit fallback test-visible.
inline void ensure_document_node_id_cursor(EditorDocument& document) {
    if (document.next_node_id != 0) return;
    record_full_document_node_id_scan();
    std::uint64_t maximum = 0;
    document_id_detail::scan_block_ids(document.root, maximum);
    document.next_node_id = maximum + 1;
}

inline NodeId allocate_document_node_id(EditorDocument& document) {
    ensure_document_node_id_cursor(document);
    return NodeId{document.next_node_id++};
}

// Blocks may cross a document boundary through a semantic paste/import. Keep
// their existing identities, but reserve the occupied range before assigning
// IDs to missing descendants.
inline void reserve_document_node_ids(EditorDocument& document, const BlockNode& block) {
    ensure_document_node_id_cursor(document);
    auto maximum = document.next_node_id - 1;
    document_id_detail::scan_block_ids(block, maximum);
    document.next_node_id = (std::max)(document.next_node_id, maximum + 1);
}

} // namespace elmd
