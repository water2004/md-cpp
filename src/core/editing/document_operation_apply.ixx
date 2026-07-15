// elmd.core.document_operation_apply — validates and replays reversible
// block-local source edits and unified-tree edits without document snapshots.
export module elmd.core.document_operation_apply;
import std;
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.document_text;
import elmd.core.document_transaction;
import elmd.core.ids;
import elmd.core.inline_document;
import elmd.core.inline_parser;
import elmd.core.inline_source_edit;
import elmd.core.text_edit;

export namespace elmd {

namespace document_operation_detail {

inline InlineDocument* inline_owner(BlockNode& root, NodeId id) {
    auto* block = find_block(root, id);
    return block ? editable_inline_document(*block) : nullptr;
}

inline void apply_payload(BlockNode& target, const BlockNode& shell) {
    auto children = std::move(target.children);
    std::optional<InlineDocument> preserved_inline;
    std::optional<BlockSourceDocument> preserved_block_source;
    if (auto* document = editable_inline_document(target)) {
        preserved_inline = std::move(*document);
    }
    if (editable_raw_block_source(target)) {
        preserved_block_source = std::move(target.block_source);
    }
    target = shell;
    target.children = std::move(children);
    if (preserved_inline) {
        if (auto* document = editable_inline_document(target)) {
            *document = std::move(*preserved_inline);
        }
    }
    if (preserved_block_source && editable_raw_block_source(target)) {
        target.block_source = std::move(*preserved_block_source);
    }
}

inline bool move_child(
    BlockNode& root,
    NodeId from_parent_id,
    std::size_t from_index,
    NodeId to_parent_id,
    std::size_t to_index) {
    auto* from_parent = find_block(root, from_parent_id);
    if (!from_parent || from_index >= from_parent->children.size()) return false;

    if (from_parent_id == to_parent_id) {
        if (to_index >= from_parent->children.size()) return false;
        auto node = remove_block(*from_parent, from_index);
        if (!node) return false;
        if (!insert_block(*from_parent, to_index, std::move(*node))) std::terminate();
        return true;
    }

    auto* to_parent = find_block(root, to_parent_id);
    if (!to_parent || to_index > to_parent->children.size()) return false;
    if (find_block(from_parent->children[from_index], to_parent_id)) return false;

    auto node = remove_block(*from_parent, from_index);
    if (!node) return false;
    to_parent = find_block(root, to_parent_id);
    if (!to_parent || to_index > to_parent->children.size()) {
        const auto restored = insert_block(*from_parent, from_index, std::move(*node));
        if (!restored) std::terminate();
        return false;
    }
    if (!insert_block(*to_parent, to_index, std::move(*node))) std::terminate();
    return true;
}

inline bool apply_tree_edit(BlockNode& root, const DocumentTreeEdit& edit, bool forward) {
    switch (edit.kind) {
        case DocumentTreeEditKind::Insert: {
            auto* parent = find_block(root, edit.parent_id);
            if (!parent) return false;
            if (forward) return insert_block(*parent, edit.index, edit.after);
            if (edit.index >= parent->children.size()
                || parent->children[edit.index].id != edit.after.id) return false;
            return remove_block(*parent, edit.index).has_value();
        }
        case DocumentTreeEditKind::Remove: {
            auto* parent = find_block(root, edit.parent_id);
            if (!parent) return false;
            if (!forward) return insert_block(*parent, edit.index, edit.before);
            if (edit.index >= parent->children.size()
                || parent->children[edit.index].id != edit.before.id) return false;
            return remove_block(*parent, edit.index).has_value();
        }
        case DocumentTreeEditKind::Move:
            return forward
                ? move_child(root, edit.parent_id, edit.index, edit.other_parent_id, edit.other_index)
                : move_child(root, edit.other_parent_id, edit.other_index, edit.parent_id, edit.index);
        case DocumentTreeEditKind::UpdatePayload: {
            auto* target = find_block(root, edit.before.id);
            if (!target) return false;
            apply_payload(*target, forward ? edit.after : edit.before);
            return true;
        }
    }
    return false;
}

inline void scan_ids(const InlineCstNodes& nodes, std::uint64_t& maximum) {
    for (const auto& node : nodes) {
        maximum = (std::max)(maximum, node.id.v);
        scan_ids(node.children, maximum);
    }
}

inline void scan_ids(const BlockNode& block, std::uint64_t& maximum) {
    maximum = (std::max)(maximum, block.id.v);
    scan_ids(block.inline_content.tree.nodes, maximum);
    for (const auto& token : block.inline_content.tree.tokens) {
        maximum = (std::max)(maximum, token.id.v);
    }
    for (const auto& child : block.children) scan_ids(child, maximum);
}

inline bool apply_text_edit(
    EditorDocument& document,
    const DocumentTextOperation& operation,
    bool forward) {
    const auto& edit = forward ? operation.forward : operation.inverse;
    if (auto* owner = inline_owner(document.root, edit.container_id)) {
        if (!edit.range.valid_for(owner->source.size())) return false;
        std::uint64_t maximum = 0;
        scan_ids(document.root, maximum);
        InlineParseContext context;
        context.dialect = document.dialect;
        context.allocate_id = [next = maximum + 1]() mutable { return NodeId{next++}; };
        apply_inline_source_edit(edit.container_id, *owner, edit, context);
        return true;
    }
    auto* block = find_block(document.root, edit.container_id);
    if (!block) return false;
    auto* source = editable_raw_block_source(*block);
    if (!source || !edit.range.valid_for(source->size())) return false;
    elmd::apply_text_edit(*source, edit);
    reparse_block_source(block->block_source);
    return true;
}

}  // namespace document_operation_detail

inline bool apply_document_operation(
    EditorDocument& document,
    const DocumentOperation& operation,
    bool forward) {
    return std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, DocumentTreeEdit>) {
            return document_operation_detail::apply_tree_edit(document.root, value, forward);
        } else {
            return document_operation_detail::apply_text_edit(document, value, forward);
        }
    }, operation);
}

// A failed replay is rolled back before returning false. This keeps Undo/Redo
// and command-level rollback from leaving a partially applied operation list.
inline bool apply_document_operations(
    EditorDocument& document,
    const std::vector<DocumentOperation>& operations,
    bool forward) {
    if (forward) {
        std::size_t applied = 0;
        for (; applied < operations.size(); ++applied) {
            if (apply_document_operation(document, operations[applied], true)) continue;
            while (applied > 0) {
                --applied;
                if (!apply_document_operation(document, operations[applied], false)) std::terminate();
            }
            return false;
        }
        return true;
    }

    std::size_t index = operations.size();
    while (index > 0) {
        --index;
        if (apply_document_operation(document, operations[index], false)) continue;
        for (auto restore = index + 1; restore < operations.size(); ++restore) {
            if (!apply_document_operation(document, operations[restore], true)) std::terminate();
        }
        return false;
    }
    return true;
}

}  // namespace elmd
