// Document invariants and source-edit transaction materialization.
export module folia.core.document_edit_validation;
import std;
import folia.core.ast;
import folia.core.block_source;
import folia.core.document;
import folia.core.document_text;
import folia.core.ids;
import folia.core.inline_cst;
import folia.core.inline_document;
import folia.core.inline_source_edit;
import folia.core.text_edit;
import folia.core.document_edit_primitives;

export namespace folia::document_edit_detail {

inline void validate_inline_document(NodeId owner, const InlineDocument& document, std::unordered_set<std::uint64_t>& ids, std::vector<DocumentInvariantError>& errors) {
    if (!tokens_partition_source(document.tree, document.source.size())) errors.push_back({owner, "inline tokens do not partition source"});
    if (!roots_partition_source(document.tree, document.source.size())) errors.push_back({owner, "inline roots do not partition source"});
    if (flatten_tokens(document.tree, document.source) != document.source) errors.push_back({owner, "inline CST is not lossless"});
    std::function<void(const InlineCstNodes&)> scan = [&](const InlineCstNodes& nodes) {
        for (const auto& node : nodes) {
            if (node.id.v == 0 || !ids.insert(node.id.v).second) errors.push_back({node.id, "duplicate or missing node id"});
            scan(node.children);
        }
    };
    scan(document.tree.nodes);
}
inline void validate_blocks(
    const BlockVec& blocks,
    BlockKind parent_kind,
    std::unordered_set<std::uint64_t>& ids,
    std::vector<DocumentInvariantError>& errors) {
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        const auto& block = blocks[index];
        if (block.id.v == 0 || !ids.insert(block.id.v).second) errors.push_back({block.id, "duplicate or missing block id"});
        if (block.kind == BlockKind::CalloutTitle
            && (parent_kind != BlockKind::Callout || index != 0)) {
            errors.push_back({block.id, "callout title is not the first child of a callout"});
        }
        if (block.kind == BlockKind::CalloutTitle && !block.children.empty()) {
            errors.push_back({block.id, "callout title owns structural children"});
        }
        if (const auto* document = editable_inline_document(block)) {
            validate_inline_document(block.id, *document, ids, errors);
        }
        if (block.kind == BlockKind::CodeBlock || block.kind == BlockKind::MathBlock) {
            if (!block_source_tokens_partition(block.block_source)) {
                errors.push_back({block.id, "block-source tokens do not partition source"});
            }
            if (flatten_block_source_tokens(block.block_source) != block.block_source.source()) {
                errors.push_back({block.id, "block-source CST is not lossless"});
            }
        }
        validate_blocks(block.children, block.kind, ids, errors);
    }
}

inline DocumentTransaction source_transaction(
    EditorDocument& after,
    AppliedSourceEdit edit,
    TextSelection selection_before,
    TextSelection selection_after,
    std::uint64_t revision_before,
    DocumentTransactionReason reason) {
    std::vector<DocumentOperation> operations;
    operations.emplace_back(DocumentTextOperation{
        std::move(edit.forward),
        std::move(edit.inverse),
    });
    return make_recorded_document_transaction(
        std::move(operations),
        selection_before,
        selection_after,
        revision_before,
        after.revision,
        reason);
}

} // namespace folia::document_edit_detail
