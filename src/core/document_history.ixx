// elmd.core.document_history — reversible source and block-tree operation log.
//
// Normal undo/redo stores only the edits that changed the document. It never
// retains complete EditorDocument snapshots.
export module elmd.core.document_history;
import std;
import elmd.core.ast;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.document_text;
import elmd.core.document_transaction;
import elmd.core.inline_document;
import elmd.core.inline_parser;
import elmd.core.inline_source_edit;
import elmd.core.text_edit;

export namespace elmd {

struct DocumentHistoryEntry {
    std::vector<DocumentOperation> operations;
    TextSelection selection_before;
    TextSelection selection_after;
    std::uint64_t revision_before = 0;
    std::uint64_t revision_after = 0;
};

namespace document_history_detail {

inline InlineDocument* inline_owner(BlockNode& root, NodeId id) {
    auto* block = find_block(root, id);
    return block ? editable_inline_document(*block) : nullptr;
}

inline const InlineDocument* inline_owner(const BlockNode& root, NodeId id) {
    const auto* block = find_block(root, id);
    return block ? editable_inline_document(*block) : nullptr;
}

inline void apply_payload(BlockNode& target, const BlockNode& shell) {
    auto children = std::move(target.children);
    std::optional<InlineDocument> preserved_inline;
    if (auto* document = editable_inline_document(target)) {
        preserved_inline = std::move(*document);
    }
    target = shell;
    target.children = std::move(children);
    if (preserved_inline) {
        if (auto* document = editable_inline_document(target)) {
            *document = std::move(*preserved_inline);
        }
    }
}

inline bool move_child(BlockNode& root, NodeId from_parent_id, std::size_t from_index,
                       NodeId to_parent_id, std::size_t to_index) {
    if (from_parent_id == to_parent_id) {
        auto* parent = find_block(root, from_parent_id);
        if (!parent || from_index >= parent->children.size()) return false;
        auto node = remove_block(*parent, from_index);
        if (!node || to_index > parent->children.size()) return false;
        return insert_block(*parent, to_index, std::move(*node));
    }
    auto* from_parent = find_block(root, from_parent_id);
    if (!from_parent || from_index >= from_parent->children.size()) return false;
    auto node = remove_block(*from_parent, from_index);
    if (!node) return false;
    auto* to_parent = find_block(root, to_parent_id);
    if (!to_parent || to_index > to_parent->children.size()) return false;
    return insert_block(*to_parent, to_index, std::move(*node));
}

inline bool apply_tree_edit(BlockNode& root, const DocumentTreeEdit& edit, bool forward) {
    switch (edit.kind) {
        case DocumentTreeEditKind::Insert: {
            auto* parent = find_block(root, edit.parent_id);
            if (!parent) return false;
            if (forward) return insert_block(*parent, edit.index, edit.after);
            return remove_block(*parent, edit.index).has_value();
        }
        case DocumentTreeEditKind::Remove: {
            auto* parent = find_block(root, edit.parent_id);
            if (!parent) return false;
            if (forward) return remove_block(*parent, edit.index).has_value();
            return insert_block(*parent, edit.index, edit.before);
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
    for (const auto& token : block.inline_content.tree.tokens) maximum = (std::max)(maximum, token.id.v);
    if (block.callout_title) {
        scan_ids(block.callout_title->tree.nodes, maximum);
        for (const auto& token : block.callout_title->tree.tokens) maximum = (std::max)(maximum, token.id.v);
    }
    for (const auto& child : block.children) scan_ids(child, maximum);
}

inline bool apply_text_history_edit(EditorDocument& document, const DocumentTextOperation& operation, bool forward) {
    const auto& edit = forward ? operation.forward : operation.inverse;
    auto* owner = inline_owner(document.root, edit.container_id);
    if (!owner) return false;
    std::uint64_t maximum = 0;
    scan_ids(document.root, maximum);
    InlineParseContext context;
    context.dialect = document.dialect;
    context.allocate_id = [next = maximum + 1]() mutable { return NodeId{next++}; };
    apply_inline_source_edit(edit.container_id, *owner, edit, context);
    return true;
}

inline DocumentHistoryEntry make_entry(const DocumentTransaction& transaction) {
    DocumentHistoryEntry entry;
    entry.operations = transaction.operations;
    entry.selection_before = transaction.selection_before;
    entry.selection_after = transaction.selection_after;
    entry.revision_before = transaction.revision_before;
    entry.revision_after = transaction.after.revision;
    return entry;
}

inline bool apply_entry(EditorDocument& document, TextSelection& selection,
                        const DocumentHistoryEntry& entry, bool forward) {
    if (forward) {
        for (const auto& operation : entry.operations) {
            const auto ok = std::visit([&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, DocumentTreeEdit>) return apply_tree_edit(document.root, value, true);
                else return apply_text_history_edit(document, value, true);
            }, operation);
            if (!ok) return false;
        }
        document.revision = entry.revision_after;
        selection = entry.selection_after;
    } else {
        for (auto operation = entry.operations.rbegin(); operation != entry.operations.rend(); ++operation) {
            const auto ok = std::visit([&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, DocumentTreeEdit>) return apply_tree_edit(document.root, value, false);
                else return apply_text_history_edit(document, value, false);
            }, *operation);
            if (!ok) return false;
        }
        document.revision = entry.revision_before;
        selection = entry.selection_before;
    }
    return true;
}

} // namespace document_history_detail

class DocumentHistory {
public:
    explicit DocumentHistory(std::size_t capacity = 1000) : capacity_(capacity) {}

    void push(const DocumentTransaction& transaction) {
        undo_.push_back(document_history_detail::make_entry(transaction));
        redo_.clear();
        if (undo_.size() > capacity_) undo_.erase(undo_.begin());
    }

    bool undo(EditorDocument& document, TextSelection& selection) {
        if (undo_.empty()) return false;
        auto entry = std::move(undo_.back());
        undo_.pop_back();
        if (!document_history_detail::apply_entry(document, selection, entry, false)) return false;
        redo_.push_back(std::move(entry));
        return true;
    }

    bool redo(EditorDocument& document, TextSelection& selection) {
        if (redo_.empty()) return false;
        auto entry = std::move(redo_.back());
        redo_.pop_back();
        if (!document_history_detail::apply_entry(document, selection, entry, true)) return false;
        undo_.push_back(std::move(entry));
        return true;
    }

    void clear() { undo_.clear(); redo_.clear(); }
    bool has_undo() const { return !undo_.empty(); }
    bool has_redo() const { return !redo_.empty(); }

private:
    std::size_t capacity_;
    std::vector<DocumentHistoryEntry> undo_;
    std::vector<DocumentHistoryEntry> redo_;
};

} // namespace elmd
