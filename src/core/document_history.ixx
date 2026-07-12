// elmd.core.document_history — reversible source and block-tree operation log.
//
// Normal undo/redo stores only the edits that changed the document. It never
// retains complete EditorDocument snapshots.
export module elmd.core.document_history;
import std;
import elmd.core.ast;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.document_edit;
import elmd.core.inline_parser;
import elmd.core.inline_source_edit;
import elmd.core.text_edit;

export namespace elmd {

struct HistoryTextEdit {
    TextEdit forward;
    TextEdit inverse;
};

enum class HistoryTreeEditKind { Insert, Remove, Move, UpdatePayload };

struct HistoryTreeEdit {
    HistoryTreeEditKind kind = HistoryTreeEditKind::Insert;
    NodeId parent_id{};
    NodeId other_parent_id{};
    std::size_t index = 0;
    std::size_t other_index = 0;
    BlockNode before;
    BlockNode after;
};

using HistoryOperation = std::variant<HistoryTextEdit, HistoryTreeEdit>;

struct DocumentHistoryEntry {
    std::vector<HistoryOperation> operations;
    TextSelection selection_before;
    TextSelection selection_after;
    std::uint64_t revision_before = 0;
    std::uint64_t revision_after = 0;
};

namespace document_history_detail {

inline bool editable_kind(BlockKind kind) {
    return kind == BlockKind::Paragraph || kind == BlockKind::Heading || kind == BlockKind::TableCell;
}

inline InlineDocument* inline_owner(BlockNode& root, NodeId id) {
    auto* block = find_block(root, id);
    return block && editable_kind(block->kind) ? &block->inline_content : nullptr;
}

inline const InlineDocument* inline_owner(const BlockNode& root, NodeId id) {
    const auto* block = find_block(root, id);
    return block && editable_kind(block->kind) ? &block->inline_content : nullptr;
}

inline BlockNode payload_shell(const BlockNode& source) {
    auto shell = source;
    shell.children.clear();
    shell.inline_content = {};
    return shell;
}

inline bool payload_equal(const BlockNode& left, const BlockNode& right) {
    const auto inline_optional_equal = [](const std::optional<InlineDocument>& a, const std::optional<InlineDocument>& b) {
        if (a.has_value() != b.has_value()) return false;
        return !a || a->source == b->source;
    };
    return left.id == right.id && left.kind == right.kind && left.level == right.level
        && left.slug == right.slug && left.marker == right.marker && left.checked == right.checked
        && left.list_ordered == right.list_ordered && left.list_start == right.list_start
        && left.list_delimiter == right.list_delimiter && left.language == right.language
        && left.code_text == right.code_text && left.code_indented == right.code_indented
        && left.tex == right.tex && left.math_delim == right.math_delim
        && left.table_aligns == right.table_aligns && left.table_header_row == right.table_header_row
        && left.src == right.src && left.image_alt == right.image_alt
        && left.image_title == right.image_title && left.image_link == right.image_link
        && left.image_width == right.image_width && left.image_height == right.image_height
        && left.opening_marker == right.opening_marker && left.closing_marker == right.closing_marker
        && left.callout_kind == right.callout_kind
        && inline_optional_equal(left.callout_title, right.callout_title)
        && left.footnote_label == right.footnote_label && left.toc_marker == right.toc_marker
        && left.fmt == right.fmt && left.raw == right.raw && left.unsup_reason == right.unsup_reason
        && left.ext_name == right.ext_name;
}

inline void apply_payload(BlockNode& target, const BlockNode& shell) {
    auto children = std::move(target.children);
    auto inline_content = std::move(target.inline_content);
    target = shell;
    target.children = std::move(children);
    target.inline_content = std::move(inline_content);
}

struct ChildLocation {
    NodeId parent_id{};
    std::size_t index = 0;
};

inline std::optional<ChildLocation> child_location(const BlockNode& root, NodeId id) {
    const auto* parent = find_parent_block(root, id);
    if (!parent) return std::nullopt;
    for (std::size_t index = 0; index < parent->children.size(); ++index) {
        if (parent->children[index].id == id) return ChildLocation{parent->id, index};
    }
    return std::nullopt;
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

inline bool apply_tree_edit(BlockNode& root, const HistoryTreeEdit& edit, bool forward) {
    switch (edit.kind) {
        case HistoryTreeEditKind::Insert: {
            auto* parent = find_block(root, edit.parent_id);
            if (!parent) return false;
            if (forward) return insert_block(*parent, edit.index, edit.after);
            return remove_block(*parent, edit.index).has_value();
        }
        case HistoryTreeEditKind::Remove: {
            auto* parent = find_block(root, edit.parent_id);
            if (!parent) return false;
            if (forward) return remove_block(*parent, edit.index).has_value();
            return insert_block(*parent, edit.index, edit.before);
        }
        case HistoryTreeEditKind::Move:
            return forward
                ? move_child(root, edit.parent_id, edit.index, edit.other_parent_id, edit.other_index)
                : move_child(root, edit.other_parent_id, edit.other_index, edit.parent_id, edit.index);
        case HistoryTreeEditKind::UpdatePayload: {
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

inline bool apply_text_history_edit(EditorDocument& document, const HistoryTextEdit& operation, bool forward) {
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

inline HistoryTextEdit text_difference(NodeId owner, std::u32string_view before, std::u32string_view after) {
    std::size_t prefix = 0;
    while (prefix < before.size() && prefix < after.size() && before[prefix] == after[prefix]) ++prefix;
    std::size_t suffix = 0;
    while (suffix < before.size() - prefix && suffix < after.size() - prefix
        && before[before.size() - 1 - suffix] == after[after.size() - 1 - suffix]) ++suffix;
    const auto before_end = before.size() - suffix;
    const auto after_end = after.size() - suffix;
    HistoryTextEdit change;
    change.forward = TextEdit{owner, {prefix, before_end}, std::u32string(after.substr(prefix, after_end - prefix))};
    change.inverse = TextEdit{owner, {prefix, after_end}, std::u32string(before.substr(prefix, before_end - prefix))};
    return change;
}

inline void reconcile_structure(BlockNode& simulated, const BlockNode& desired, std::vector<HistoryOperation>& operations) {
    auto* simulated_parent = find_block(simulated, desired.id);
    if (!simulated_parent) return;
    for (std::size_t target_index = 0; target_index < desired.children.size(); ++target_index) {
        const auto& desired_child = desired.children[target_index];
        auto location = child_location(simulated, desired_child.id);
        if (!location) {
            HistoryTreeEdit edit;
            edit.kind = HistoryTreeEditKind::Insert;
            edit.parent_id = desired.id;
            edit.index = target_index;
            edit.after = desired_child;
            operations.emplace_back(edit);
            simulated_parent = find_block(simulated, desired.id);
            insert_block(*simulated_parent, target_index, desired_child);
        } else if (location->parent_id != desired.id || location->index != target_index) {
            HistoryTreeEdit edit;
            edit.kind = HistoryTreeEditKind::Move;
            edit.parent_id = location->parent_id;
            edit.index = location->index;
            edit.other_parent_id = desired.id;
            edit.other_index = target_index;
            operations.emplace_back(edit);
            move_child(simulated, edit.parent_id, edit.index, edit.other_parent_id, edit.other_index);
        }
        simulated_parent = find_block(simulated, desired.id);
    }
    simulated_parent = find_block(simulated, desired.id);
    while (simulated_parent && simulated_parent->children.size() > desired.children.size()) {
        const auto index = simulated_parent->children.size() - 1;
        HistoryTreeEdit edit;
        edit.kind = HistoryTreeEditKind::Remove;
        edit.parent_id = desired.id;
        edit.index = index;
        edit.before = simulated_parent->children[index];
        operations.emplace_back(edit);
        remove_block(*simulated_parent, index);
    }
    for (const auto& child : desired.children) reconcile_structure(simulated, child, operations);
}

inline void append_content_differences(const BlockNode& before_root, const BlockNode& after_node,
                                       std::vector<HistoryOperation>& operations) {
    if (const auto* before_node = find_block(before_root, after_node.id)) {
        if (!payload_equal(*before_node, after_node)) {
            HistoryTreeEdit edit;
            edit.kind = HistoryTreeEditKind::UpdatePayload;
            edit.before = payload_shell(*before_node);
            edit.after = payload_shell(after_node);
            operations.emplace_back(std::move(edit));
        }
        if (editable_kind(before_node->kind) && editable_kind(after_node.kind)
            && before_node->inline_content.source != after_node.inline_content.source) {
            operations.emplace_back(text_difference(after_node.id, before_node->inline_content.source, after_node.inline_content.source));
        }
    }
    for (const auto& child : after_node.children) append_content_differences(before_root, child, operations);
}

inline DocumentHistoryEntry make_entry(const DocumentTransaction& transaction) {
    DocumentHistoryEntry entry;
    entry.selection_before = transaction.selection_before;
    entry.selection_after = transaction.selection_after;
    entry.revision_before = transaction.before.revision;
    entry.revision_after = transaction.after.revision;
    auto simulated = transaction.before.root;
    reconcile_structure(simulated, transaction.after.root, entry.operations);
    append_content_differences(transaction.before.root, transaction.after.root, entry.operations);
    return entry;
}

inline bool apply_entry(EditorDocument& document, TextSelection& selection,
                        const DocumentHistoryEntry& entry, bool forward) {
    if (forward) {
        for (const auto& operation : entry.operations) {
            const auto ok = std::visit([&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, HistoryTreeEdit>) return apply_tree_edit(document.root, value, true);
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
                if constexpr (std::is_same_v<T, HistoryTreeEdit>) return apply_tree_edit(document.root, value, false);
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
