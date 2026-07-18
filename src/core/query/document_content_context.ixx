// folia.core.document_content_context — semantic editor scope at one source position.
export module folia.core.document_content_context;
import std;
import folia.core.document;
import folia.core.inline_cst;
import folia.core.inline_document;
import folia.core.text_edit;

export namespace folia {

enum class DocumentContentContext {
    Normal,
    Code,
    Math,
};

inline bool cst_node_contains_caret(InlineCstNode const& node, TextPosition position) {
    return node.range.start <= position.source_offset
        && (position.source_offset < node.range.end
            || (position.source_offset == node.range.end
                && position.affinity == TextAffinity::Upstream));
}

inline std::optional<DocumentContentContext> inline_content_context(
    InlineCstNodes const& nodes,
    TextPosition position) {
    for (auto const& node : nodes) {
        if (!cst_node_contains_caret(node, position)) continue;
        if (auto nested = inline_content_context(node.children, position)) return nested;
        if (node.kind == InlineCstKind::CodeSpan) return DocumentContentContext::Code;
        if (node.kind == InlineCstKind::InlineMath) return DocumentContentContext::Math;
        return std::nullopt;
    }
    return std::nullopt;
}

inline DocumentContentContext document_content_context_at(
    EditorDocument const& document,
    TextPosition position) {
    auto const* block = find_document_block(document, position.container_id);
    if (!block) return DocumentContentContext::Normal;
    if (block->kind == BlockKind::CodeBlock) return DocumentContentContext::Code;
    if (block->kind == BlockKind::MathBlock) return DocumentContentContext::Math;
    return inline_content_context(block->inline_content.tree.nodes, position)
        .value_or(DocumentContentContext::Normal);
}

} // namespace folia
