// folia.core.document_ids — document-owned monotonic NodeId allocation.
export module folia.core.document_ids;
import std;
import folia.core.ast;
import folia.core.document;
import folia.core.ids;
import folia.core.inline_cst;

export namespace folia {

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

inline NodeId allocate_document_node_id(EditorDocument& document) {
    if (document.next_node_id == 0) {
        throw std::logic_error("document node id cursor is not initialized");
    }
    return NodeId{document.next_node_id++};
}

// Blocks may cross a document boundary through a semantic paste/import. Keep
// their existing identities, but reserve the occupied range before assigning
// IDs to missing descendants.
inline void reserve_document_node_ids(EditorDocument& document, const BlockNode& block) {
    if (document.next_node_id == 0) {
        throw std::logic_error("document node id cursor is not initialized");
    }
    auto maximum = document.next_node_id - 1;
    document_id_detail::scan_block_ids(block, maximum);
    document.next_node_id = (std::max)(document.next_node_id, maximum + 1);
}

} // namespace folia
