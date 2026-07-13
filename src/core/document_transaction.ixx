// elmd.core.document_transaction — reversible document operations produced by
// editing commands. History consumes this operation log directly; transactions
// never retain a complete "before" EditorDocument snapshot.
export module elmd.core.document_transaction;
import std;
import elmd.core.ast;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.document_text;
import elmd.core.inline_document;
import elmd.core.instrumentation;
import elmd.core.text_edit;

export namespace elmd {

enum class DocumentTransactionReason { InsertText, Delete, Paste, Format, Structure };

struct DocumentTextOperation {
    TextEdit forward;
    TextEdit inverse;
};

enum class DocumentTreeEditKind { Insert, Remove, Move, UpdatePayload };

struct DocumentTreeEdit {
    DocumentTreeEditKind kind = DocumentTreeEditKind::Insert;
    NodeId parent_id{};
    NodeId other_parent_id{};
    std::size_t index = 0;
    std::size_t other_index = 0;
    BlockNode before;
    BlockNode after;
};

using DocumentOperation = std::variant<DocumentTextOperation, DocumentTreeEdit>;

struct DocumentTransaction {
    EditorDocument after;
    std::vector<DocumentOperation> operations;
    TextSelection selection_before;
    TextSelection selection_after;
    std::uint64_t revision_before = 0;
    DocumentTransactionReason reason = DocumentTransactionReason::Structure;
};

namespace document_transaction_detail {

inline BlockNode payload_shell(const BlockNode& source) {
    auto shell = source;
    shell.children.clear();
    shell.inline_content = {};
    return shell;
}

inline bool payload_equal(const BlockNode& left, const BlockNode& right) {
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
        && left.callout_title.has_value() == right.callout_title.has_value()
        && left.footnote_label == right.footnote_label && left.toc_marker == right.toc_marker
        && left.fmt == right.fmt && left.raw == right.raw && left.unsup_reason == right.unsup_reason
        && left.ext_name == right.ext_name;
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

inline bool move_child(
    BlockNode& root,
    NodeId from_parent_id,
    std::size_t from_index,
    NodeId to_parent_id,
    std::size_t to_index) {
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

inline DocumentTextOperation text_difference(
    NodeId owner,
    std::u32string_view before,
    std::u32string_view after) {
    std::size_t prefix = 0;
    while (prefix < before.size() && prefix < after.size() && before[prefix] == after[prefix]) ++prefix;
    std::size_t suffix = 0;
    while (suffix < before.size() - prefix && suffix < after.size() - prefix
        && before[before.size() - 1 - suffix] == after[after.size() - 1 - suffix]) ++suffix;
    const auto before_end = before.size() - suffix;
    const auto after_end = after.size() - suffix;
    return DocumentTextOperation{
        TextEdit{owner, {prefix, before_end}, std::u32string(after.substr(prefix, after_end - prefix))},
        TextEdit{owner, {prefix, after_end}, std::u32string(before.substr(prefix, before_end - prefix))},
    };
}

inline void reconcile_structure(
    BlockNode& simulated,
    const BlockNode& desired,
    std::vector<DocumentOperation>& operations) {
    auto* simulated_parent = find_block(simulated, desired.id);
    if (!simulated_parent) return;
    for (std::size_t target_index = 0; target_index < desired.children.size(); ++target_index) {
        const auto& desired_child = desired.children[target_index];
        auto location = child_location(simulated, desired_child.id);
        if (!location) {
            DocumentTreeEdit edit;
            edit.kind = DocumentTreeEditKind::Insert;
            edit.parent_id = desired.id;
            edit.index = target_index;
            edit.after = desired_child;
            operations.emplace_back(edit);
            simulated_parent = find_block(simulated, desired.id);
            insert_block(*simulated_parent, target_index, desired_child);
        } else if (location->parent_id != desired.id || location->index != target_index) {
            DocumentTreeEdit edit;
            edit.kind = DocumentTreeEditKind::Move;
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
        DocumentTreeEdit edit;
        edit.kind = DocumentTreeEditKind::Remove;
        edit.parent_id = desired.id;
        edit.index = index;
        edit.before = simulated_parent->children[index];
        operations.emplace_back(edit);
        remove_block(*simulated_parent, index);
    }
    for (const auto& child : desired.children) reconcile_structure(simulated, child, operations);
}

inline void append_content_differences(
    const BlockNode& before_root,
    const BlockNode& after_node,
    std::vector<DocumentOperation>& operations) {
    if (const auto* before_node = find_block(before_root, after_node.id)) {
        if (!payload_equal(*before_node, after_node)) {
            DocumentTreeEdit edit;
            edit.kind = DocumentTreeEditKind::UpdatePayload;
            edit.before = payload_shell(*before_node);
            edit.after = payload_shell(after_node);
            operations.emplace_back(std::move(edit));
        }
        const auto* before_inline = editable_inline_document(*before_node);
        const auto* after_inline = editable_inline_document(after_node);
        if (before_inline && after_inline && before_inline->source != after_inline->source) {
            operations.emplace_back(text_difference(
                after_node.id,
                before_inline->source,
                after_inline->source));
        }
    }
    for (const auto& child : after_node.children) {
        append_content_differences(before_root, child, operations);
    }
}

} // namespace document_transaction_detail

inline DocumentTransaction make_document_transaction(
    const EditorDocument& before,
    EditorDocument after,
    TextSelection selection_before,
    TextSelection selection_after,
    DocumentTransactionReason reason) {
    DocumentTransaction transaction;
    transaction.selection_before = selection_before;
    transaction.selection_after = selection_after;
    transaction.revision_before = before.revision;
    transaction.reason = reason;
    record_full_tree_transaction_diff();
    auto simulated = before.root;
    document_transaction_detail::reconcile_structure(
        simulated,
        after.root,
        transaction.operations);
    document_transaction_detail::append_content_differences(
        before.root,
        after.root,
        transaction.operations);
    transaction.after = std::move(after);
    return transaction;
}

inline DocumentTransaction make_recorded_document_transaction(
    EditorDocument after,
    std::vector<DocumentOperation> operations,
    TextSelection selection_before,
    TextSelection selection_after,
    std::uint64_t revision_before,
    DocumentTransactionReason reason) {
    DocumentTransaction transaction;
    transaction.after = std::move(after);
    transaction.operations = std::move(operations);
    transaction.selection_before = selection_before;
    transaction.selection_after = selection_after;
    transaction.revision_before = revision_before;
    transaction.reason = reason;
    return transaction;
}

} // namespace elmd
