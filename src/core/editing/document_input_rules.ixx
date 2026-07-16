// Public coordinator for local block-input and structural Enter/Backspace rules.
// Recognition and tree transformations live in narrowly scoped helper modules.
export module elmd.core.document_input_rules;
import std;
import elmd.core.ast;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.document_edit_support;
import elmd.core.ids;
import elmd.core.text_edit;
import elmd.core.document_input_syntax;
import elmd.core.document_list_input;
import elmd.core.document_block_input;

export namespace elmd::document_input_rules {

inline std::optional<document_edit_detail::RecordedBlockEdit> apply_after_text_insert(
    EditorDocument& document,
    TextPosition& target,
    document_edit_detail::NodeAllocator& allocator) {
    auto* block = find_document_block(document, target.container_id);
    if (!block || block->kind != BlockKind::Paragraph) return std::nullopt;
    if (auto task = detail::upgrade_task_item(document, target, allocator, *block)) {
        target = task->target;
        return task;
    }
    auto marker = detail::recognize_marker(block->inline_content.source, target.source_offset);
    if (!marker) return std::nullopt;
    auto path = document_block_path(document, target.container_id);
    if (!path) return std::nullopt;
    return detail::replace_paragraph_with_container(document, *path, *marker, target, allocator);
}

inline std::optional<document_edit_detail::RecordedBlockEdit> handle_enter(
    EditorDocument& document,
    TextPosition position,
    document_edit_detail::NodeAllocator& allocator) {
    if (auto opened = detail::open_raw_block(document, position, allocator)) {
        return opened;
    }
    if (auto exited = detail::exit_empty_list_item(document, position, allocator)) {
        return exited;
    }
    return detail::split_list_item(document, position, allocator);
}

inline std::optional<document_edit_detail::RecordedBlockEdit> outdent_list_item(
    EditorDocument& document,
    NodeId descendant_id,
    document_edit_detail::NodeAllocator& allocator) {
    auto path = block_path(document.root, descendant_id);
    if (!path) return std::nullopt;
    BlockNode* item = nullptr;
    while (!path->empty()) {
        auto* candidate = block_at_path(document.root, *path);
        if (candidate && (candidate->kind == BlockKind::ListItem
            || candidate->kind == BlockKind::TaskListItem)) {
            item = candidate;
            break;
        }
        path->pop_back();
    }
    if (!item || item->children.empty()) return std::nullopt;
    return detail::remove_list_prefix(
        document,
        TextPosition{item->children.front().id, 0, TextAffinity::Downstream},
        allocator);
}

// Structural prefixes live in the block tree, outside the editable leaf's
// InlineDocument.source. At source offset zero, Backspace is therefore a
// block-tree command (unwrap/outdent) regardless of visual text affinity.
inline std::optional<document_edit_detail::RecordedBlockEdit> handle_backspace_at_start(
    EditorDocument& document,
    TextPosition position,
    document_edit_detail::NodeAllocator& allocator) {
    if (position.source_offset != 0) return std::nullopt;
    if (auto heading = detail::remove_heading_prefix(document, position)) return heading;
    if (auto callout = detail::remove_callout_prefix(document, position, allocator)) return callout;
    if (auto quote = detail::remove_quote_prefix(document, position)) return quote;
    return detail::remove_list_prefix(document, position, allocator);
}

} // namespace elmd::document_input_rules
