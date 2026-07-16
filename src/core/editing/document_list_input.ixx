// Structural list input rules: task upgrade, Enter, exit, split, and outdent.
export module elmd.core.document_list_input;
import std;
import elmd.core.ast;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.document_edit_support;
import elmd.core.ids;
import elmd.core.inline_document;
import elmd.core.text_edit;
import elmd.core.utf;
import elmd.core.document_input_syntax;

export namespace elmd::document_input_rules::detail {

inline BlockNode list_shell_from(const BlockNode& source, NodeId id) {
    BlockNode result;
    result.id = id;
    result.kind = source.kind;
    result.list_ordered = source.list_ordered;
    result.list_start = source.list_start;
    result.list_delimiter = source.list_delimiter;
    return result;
}

inline bool compatible_lists(const BlockNode& left, const BlockNode& right) {
    if (left.kind != right.kind || (left.kind != BlockKind::List && left.kind != BlockKind::TaskList)) return false;
    if (left.list_ordered != right.list_ordered || left.list_delimiter != right.list_delimiter) return false;
    return !left.list_ordered || right.list_start == left.list_start + left.children.size();
}

struct DirectListContext {
    BlockPath list_path;
    std::size_t item_index = 0;
    std::size_t child_index = 0;
};

inline std::optional<DirectListContext> direct_list_context(const EditorDocument& document, NodeId id) {
    auto path = block_path(document.root, id);
    if (!path || path->size() < 3) return std::nullopt;
    auto item_path = *path;
    const auto child_index = item_path.back();
    item_path.pop_back();
    const auto* item = block_at_path(document.root, item_path);
    if (!item || (item->kind != BlockKind::ListItem && item->kind != BlockKind::TaskListItem)) return std::nullopt;
    auto list_path = item_path;
    const auto item_index = list_path.back();
    list_path.pop_back();
    const auto* list = block_at_path(document.root, list_path);
    if (!list || (list->kind != BlockKind::List && list->kind != BlockKind::TaskList)) return std::nullopt;
    return DirectListContext{std::move(list_path), item_index, child_index};
}

inline std::optional<document_edit_detail::RecordedBlockEdit> upgrade_task_item(
    EditorDocument& document,
    TextPosition& target,
    document_edit_detail::NodeAllocator& allocator,
    BlockNode& leaf) {
    if (leaf.kind != BlockKind::Paragraph || target.source_offset != 4
        || leaf.inline_content.source.size() < 4) return std::nullopt;
    const auto prefix = leaf.inline_content.source.substr(0, 4);
    if (prefix[0] != U'[' || (prefix[1] != U' ' && prefix[1] != U'x' && prefix[1] != U'X')
        || prefix[2] != U']' || !horizontal_space(prefix[3])) return std::nullopt;
    auto context = direct_list_context(document, target.container_id);
    if (!context) return std::nullopt;
    auto* list = block_at_path(document.root, context->list_path);
    if (!list || list->kind != BlockKind::List || list->list_ordered
        || context->item_index >= list->children.size()) return std::nullopt;

    document_edit_detail::RecordedBlockEdit result;
    auto source_edit = document_edit_detail::edit_inline(
        document, target.container_id, {0, 4}, {}, allocator);
    if (!source_edit) return std::nullopt;
    document_edit_detail::append_source_operation(result.operations, std::move(*source_edit));
    target.source_offset = 0;
    result.target = target;
    const auto checked = prefix[1] == U'x' || prefix[1] == U'X';

    auto parent_path = context->list_path;
    if (parent_path.empty()) return std::nullopt;
    const auto list_index = parent_path.back();
    parent_path.pop_back();
    auto* parent = block_at_path(document.root, parent_path);
    if (!parent || list_index >= parent->children.size()) return std::nullopt;
    const auto parent_id = parent->id;
    const auto list_id = list->id;
    const auto item_id = list->children[context->item_index].id;
    auto normal_shell = list_shell_from(*list, NodeId{});

    auto* item = find_block(document.root, item_id);
    if (!item) return std::nullopt;
    DocumentTreeEdit update_item;
    update_item.kind = DocumentTreeEditKind::UpdatePayload;
    update_item.before = document_transaction_detail::payload_shell(*item);
    item->kind = BlockKind::TaskListItem;
    item->checked = checked;
    item->marker += prefix;
    update_item.after = document_transaction_detail::payload_shell(*item);
    result.operations.emplace_back(std::move(update_item));

    if (context->item_index == 0) {
        DocumentTreeEdit update_list;
        update_list.kind = DocumentTreeEditKind::UpdatePayload;
        update_list.before = document_transaction_detail::payload_shell(*list);
        list->kind = BlockKind::TaskList;
        update_list.after = document_transaction_detail::payload_shell(*list);
        result.operations.emplace_back(std::move(update_list));
    } else {
        BlockNode tasks;
        tasks.id = allocator.allocate();
        tasks.kind = BlockKind::TaskList;
        const auto tasks_id = tasks.id;
        DocumentTreeEdit insert_tasks;
        insert_tasks.kind = DocumentTreeEditKind::Insert;
        insert_tasks.parent_id = parent_id;
        insert_tasks.index = list_index + 1;
        insert_tasks.after = tasks;
        result.operations.emplace_back(std::move(insert_tasks));
        parent->children.insert(
            parent->children.begin() + static_cast<std::ptrdiff_t>(list_index + 1),
            std::move(tasks));

        list = find_block(document.root, list_id);
        auto* tasks_list = find_block(document.root, tasks_id);
        if (!list || !tasks_list) return std::nullopt;
        DocumentTreeEdit move_item;
        move_item.kind = DocumentTreeEditKind::Move;
        move_item.parent_id = list_id;
        move_item.index = context->item_index;
        move_item.other_parent_id = tasks_id;
        move_item.other_index = 0;
        auto moved = remove_block(*list, context->item_index);
        if (!moved || !insert_block(*tasks_list, 0, std::move(*moved))) return std::nullopt;
        result.operations.emplace_back(std::move(move_item));
    }

    list = find_block(document.root, list_id);
    parent = find_block(document.root, parent_id);
    if (!list || !parent) return std::nullopt;
    const auto first_after = context->item_index == 0 ? 1u : context->item_index;
    if (first_after < list->children.size()) {
        normal_shell.id = allocator.allocate();
        const auto after_id = normal_shell.id;
        const auto after_index = list_index + (context->item_index == 0 ? 1u : 2u);
        DocumentTreeEdit insert_after;
        insert_after.kind = DocumentTreeEditKind::Insert;
        insert_after.parent_id = parent_id;
        insert_after.index = after_index;
        insert_after.after = normal_shell;
        result.operations.emplace_back(std::move(insert_after));
        parent->children.insert(
            parent->children.begin() + static_cast<std::ptrdiff_t>(after_index),
            std::move(normal_shell));

        list = find_block(document.root, list_id);
        auto* after = find_block(document.root, after_id);
        if (!list || !after) return std::nullopt;
        std::size_t target_index = 0;
        while (first_after < list->children.size()) {
            DocumentTreeEdit move_after;
            move_after.kind = DocumentTreeEditKind::Move;
            move_after.parent_id = list_id;
            move_after.index = first_after;
            move_after.other_parent_id = after_id;
            move_after.other_index = target_index;
            auto moved = remove_block(*list, first_after);
            if (!moved || !insert_block(*after, target_index, std::move(*moved))) {
                return std::nullopt;
            }
            result.operations.emplace_back(std::move(move_after));
            ++target_index;
        }
    }
    return result;
}

inline std::u32string next_item_marker(const BlockNode& list, const BlockNode& item, std::size_t item_index) {
    if (list.kind == BlockKind::TaskList) return U"- [ ] ";
    if (!list.list_ordered) return item.marker.empty() ? U"- " : item.marker;
    std::u32string indent;
    std::size_t cursor = 0;
    while (cursor < item.marker.size() && item.marker[cursor] == U' ') indent.push_back(item.marker[cursor++]);
    while (cursor < item.marker.size() && item.marker[cursor] >= U'0' && item.marker[cursor] <= U'9') ++cursor;
    auto delimiter = cursor < item.marker.size() && (item.marker[cursor] == U'.' || item.marker[cursor] == U')')
        ? item.marker[cursor++] : list.list_delimiter;
    auto spacing = cursor < item.marker.size() ? item.marker.substr(cursor) : std::u32string(U" ");
    if (spacing.empty()) spacing = U" ";
    return indent + utf8_to_cps(std::to_string(list.list_start + item_index + 1))
        + std::u32string(1, delimiter) + spacing;
}

inline BlockNode empty_item(
    const BlockNode& list,
    const BlockNode& reference,
    std::size_t item_index,
    EditorDocument& document,
    document_edit_detail::NodeAllocator& allocator) {
    BlockNode item;
    item.id = allocator.allocate();
    item.kind = list.kind == BlockKind::TaskList ? BlockKind::TaskListItem : BlockKind::ListItem;
    item.marker = next_item_marker(list, reference, item_index);
    item.children.push_back(document_edit_detail::empty_paragraph(allocator, document));
    return item;
}

inline std::optional<document_edit_detail::RecordedBlockEdit> exit_empty_list_item(
    EditorDocument& document,
    TextPosition position,
    document_edit_detail::NodeAllocator& allocator) {
    auto context = direct_list_context(document, position.container_id);
    if (!context) return std::nullopt;
    auto* leaf = find_block(document.root, position.container_id);
    auto* list = block_at_path(document.root, context->list_path);
    if (!leaf || leaf->kind != BlockKind::Paragraph || !leaf->inline_content.source.empty()
        || !list || context->item_index >= list->children.size()) return std::nullopt;
    auto& item = list->children[context->item_index];
    if (item.children.size() != 1 || context->child_index != 0) return std::nullopt;

    auto parent_path = context->list_path;
    if (parent_path.empty()) return std::nullopt;
    const auto list_index = parent_path.back();
    parent_path.pop_back();
    auto* parent = block_at_path(document.root, parent_path);
    if (!parent || list_index >= parent->children.size()) return std::nullopt;

    auto paragraph = document_edit_detail::empty_paragraph(allocator, document);
    document_edit_detail::RecordedBlockEdit result;
    result.target = TextPosition{paragraph.id, 0, TextAffinity::Downstream};
    const auto parent_id = parent->id;
    const auto list_id = list->id;
    const auto item_count = list->children.size();

    if (item_count == 1) {
        DocumentTreeEdit remove_list;
        remove_list.kind = DocumentTreeEditKind::Remove;
        remove_list.parent_id = parent_id;
        remove_list.index = list_index;
        remove_list.before = parent->children[list_index];
        result.operations.emplace_back(std::move(remove_list));
        parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(list_index));

        DocumentTreeEdit insert_paragraph;
        insert_paragraph.kind = DocumentTreeEditKind::Insert;
        insert_paragraph.parent_id = parent_id;
        insert_paragraph.index = list_index;
        insert_paragraph.after = paragraph;
        result.operations.emplace_back(std::move(insert_paragraph));
        if (!insert_block(*parent, list_index, std::move(paragraph))) return std::nullopt;
        return result;
    }

    DocumentTreeEdit remove_item;
    remove_item.kind = DocumentTreeEditKind::Remove;
    remove_item.parent_id = list_id;
    remove_item.index = context->item_index;
    remove_item.before = item;
    result.operations.emplace_back(std::move(remove_item));
    list->children.erase(list->children.begin() + static_cast<std::ptrdiff_t>(context->item_index));

    if (context->item_index == 0) {
        DocumentTreeEdit update_list;
        update_list.kind = DocumentTreeEditKind::UpdatePayload;
        update_list.before = document_transaction_detail::payload_shell(*list);
        list->list_start += 1;
        update_list.after = document_transaction_detail::payload_shell(*list);
        result.operations.emplace_back(std::move(update_list));

        DocumentTreeEdit insert_paragraph;
        insert_paragraph.kind = DocumentTreeEditKind::Insert;
        insert_paragraph.parent_id = parent_id;
        insert_paragraph.index = list_index;
        insert_paragraph.after = paragraph;
        result.operations.emplace_back(std::move(insert_paragraph));
        parent = find_block(document.root, parent_id);
        if (!parent || !insert_block(*parent, list_index, std::move(paragraph))) return std::nullopt;
        return result;
    }

    DocumentTreeEdit insert_paragraph;
    insert_paragraph.kind = DocumentTreeEditKind::Insert;
    insert_paragraph.parent_id = parent_id;
    insert_paragraph.index = list_index + 1;
    insert_paragraph.after = paragraph;
    result.operations.emplace_back(std::move(insert_paragraph));
    if (!insert_block(*parent, list_index + 1, std::move(paragraph))) return std::nullopt;

    const auto trailing_count = item_count - context->item_index - 1;
    if (trailing_count > 0) {
        auto* current_list = find_block(document.root, list_id);
        if (!current_list) return std::nullopt;
        auto trailing = list_shell_from(*current_list, allocator.allocate());
        trailing.list_start = current_list->list_start + context->item_index + 1;
        const auto trailing_id = trailing.id;
        DocumentTreeEdit insert_trailing;
        insert_trailing.kind = DocumentTreeEditKind::Insert;
        insert_trailing.parent_id = parent_id;
        insert_trailing.index = list_index + 2;
        insert_trailing.after = trailing;
        result.operations.emplace_back(std::move(insert_trailing));
        parent = find_block(document.root, parent_id);
        if (!parent || !insert_block(*parent, list_index + 2, std::move(trailing))) return std::nullopt;

        for (std::size_t target_index = 0; target_index < trailing_count; ++target_index) {
            current_list = find_block(document.root, list_id);
            auto* trailing_list = find_block(document.root, trailing_id);
            if (!current_list || !trailing_list || context->item_index >= current_list->children.size()) {
                return std::nullopt;
            }
            DocumentTreeEdit move;
            move.kind = DocumentTreeEditKind::Move;
            move.parent_id = list_id;
            move.index = context->item_index;
            move.other_parent_id = trailing_id;
            move.other_index = target_index;
            auto moved = remove_block(*current_list, context->item_index);
            if (!moved || !insert_block(*trailing_list, target_index, std::move(*moved))) return std::nullopt;
            result.operations.emplace_back(std::move(move));
        }
    }
    return result;
}

inline std::optional<document_edit_detail::RecordedBlockEdit> split_list_item(
    EditorDocument& document,
    TextPosition position,
    document_edit_detail::NodeAllocator& allocator) {
    auto context = direct_list_context(document, position.container_id);
    if (!context) return std::nullopt;
    auto* list = block_at_path(document.root, context->list_path);
    if (!list || context->item_index >= list->children.size()) return std::nullopt;
    auto& item = list->children[context->item_index];
    if (context->child_index >= item.children.size()) return std::nullopt;
    auto& leaf = item.children[context->child_index];
    if (!document_edit_detail::text_block(leaf.kind)) return std::nullopt;
    const auto offset = (std::min)(position.source_offset, leaf.inline_content.source.size());
    document_edit_detail::RecordedBlockEdit result;

    auto insert_item = [&](BlockNode inserted, std::size_t index) {
        DocumentTreeEdit edit;
        edit.kind = DocumentTreeEditKind::Insert;
        edit.parent_id = list->id;
        edit.index = index;
        edit.after = inserted;
        result.operations.emplace_back(std::move(edit));
        result.target = document_edit_detail::first_editable_position(inserted)
            .value_or(TextPosition{inserted.id, 0, TextAffinity::Downstream});
        list->children.insert(
            list->children.begin() + static_cast<std::ptrdiff_t>(index),
            std::move(inserted));
    };

    if (context->child_index == 0 && offset == 0) {
        auto inserted = empty_item(*list, item, context->item_index == 0 ? 0 : context->item_index - 1, document, allocator);
        if (list->list_ordered) {
            inserted.marker = utf8_to_cps(std::to_string(list->list_start + context->item_index))
                + std::u32string(1, list->list_delimiter) + U" ";
            DocumentTreeEdit update;
            update.kind = DocumentTreeEditKind::UpdatePayload;
            update.before = document_transaction_detail::payload_shell(item);
            item.marker = next_item_marker(*list, item, context->item_index);
            update.after = document_transaction_detail::payload_shell(item);
            result.operations.emplace_back(std::move(update));
        }
        insert_item(std::move(inserted), context->item_index);
        return result;
    }

    BlockNode next;
    next.id = allocator.allocate();
    next.kind = item.kind;
    next.checked = false;
    next.marker = next_item_marker(*list, item, context->item_index);

    if (offset == leaf.inline_content.source.size() && context->child_index + 1 == item.children.size()) {
        auto paragraph = document_edit_detail::empty_paragraph(allocator, document);
        next.children.push_back(std::move(paragraph));
        insert_item(std::move(next), context->item_index + 1);
        return result;
    }

    const auto item_id = item.id;
    std::size_t first_moved_child = context->child_index;
    if (offset == 0) {
        result.target = {leaf.id, 0, TextAffinity::Downstream};
    } else {
        auto right_source = leaf.inline_content.source.substr(offset);
        auto edit = document_edit_detail::edit_inline(
            document,
            leaf.id,
            {offset, leaf.inline_content.source.size()},
            {},
            allocator);
        if (!edit) return std::nullopt;
        document_edit_detail::append_source_operation(result.operations, std::move(*edit));
        BlockNode right;
        right.id = allocator.allocate();
        right.kind = BlockKind::Paragraph;
        right.inline_content = document_edit_detail::make_inline(std::move(right_source), document, allocator);
        result.target = {right.id, 0, TextAffinity::Downstream};
        next.children.push_back(std::move(right));
        first_moved_child = context->child_index + 1;
    }

    const auto next_id = next.id;
    const auto initial_next_children = next.children.size();
    insert_item(std::move(next), context->item_index + 1);
    if (offset == 0) result.target = {position.container_id, 0, TextAffinity::Downstream};

    auto* current_item = find_block(document.root, item_id);
    auto* next_item = find_block(document.root, next_id);
    if (!current_item || !next_item || first_moved_child > current_item->children.size()) return std::nullopt;
    std::size_t target_index = initial_next_children;
    while (first_moved_child < current_item->children.size()) {
        DocumentTreeEdit move;
        move.kind = DocumentTreeEditKind::Move;
        move.parent_id = current_item->id;
        move.index = first_moved_child;
        move.other_parent_id = next_item->id;
        move.other_index = target_index;
        auto child = remove_block(*current_item, first_moved_child);
        if (!child || !insert_block(*next_item, target_index, std::move(*child))) return std::nullopt;
        result.operations.emplace_back(std::move(move));
        ++target_index;
    }
    return result;
}

inline std::optional<document_edit_detail::RecordedBlockEdit> outdent_nested_list_item(
    EditorDocument& document,
    const DirectListContext& context,
    TextPosition position) {
    auto parent_item_path = context.list_path;
    if (parent_item_path.empty()) return std::nullopt;
    parent_item_path.pop_back();
    const auto* parent_item = block_at_path(document.root, parent_item_path);
    if (!parent_item || (parent_item->kind != BlockKind::ListItem
        && parent_item->kind != BlockKind::TaskListItem)) return std::nullopt;

    auto grand_list_path = parent_item_path;
    if (grand_list_path.empty()) return std::nullopt;
    const auto parent_item_index = grand_list_path.back();
    grand_list_path.pop_back();
    const auto* grand_list = block_at_path(document.root, grand_list_path);
    if (!grand_list || (grand_list->kind != BlockKind::List
        && grand_list->kind != BlockKind::TaskList)) return std::nullopt;

    const auto nested_list_id = block_at_path(document.root, context.list_path)->id;
    auto* nested_list = block_at_path(document.root, context.list_path);
    if (!nested_list || context.item_index >= nested_list->children.size()) return std::nullopt;
    document_edit_detail::RecordedBlockEdit result;
    result.target = TextPosition{position.container_id, 0, TextAffinity::Downstream};
    const auto grand_list_id = grand_list->id;
    DocumentTreeEdit move;
    move.kind = DocumentTreeEditKind::Move;
    move.parent_id = nested_list_id;
    move.index = context.item_index;
    move.other_parent_id = grand_list_id;
    move.other_index = parent_item_index + 1;
    auto item = remove_block(*nested_list, context.item_index);
    auto* target_list = find_block(document.root, grand_list_id);
    if (!item || !target_list
        || !insert_block(*target_list, parent_item_index + 1, std::move(*item))) return std::nullopt;
    result.operations.emplace_back(std::move(move));

    if (auto* current = find_block(document.root, nested_list_id); current && current->children.empty()) {
        auto location = block_path(document.root, nested_list_id);
        auto* owner = find_parent_block(document.root, nested_list_id);
        if (!location || !owner || location->empty()) return std::nullopt;
        const auto index = location->back();
        if (index >= owner->children.size()) return std::nullopt;
        DocumentTreeEdit remove_list;
        remove_list.kind = DocumentTreeEditKind::Remove;
        remove_list.parent_id = owner->id;
        remove_list.index = index;
        remove_list.before = owner->children[index];
        result.operations.emplace_back(std::move(remove_list));
        if (!remove_block(*owner, index)) return std::nullopt;
    }
    return result;
}

inline std::optional<document_edit_detail::RecordedBlockEdit> remove_top_level_list_prefix(
    EditorDocument& document,
    const DirectListContext& context,
    TextPosition position,
    document_edit_detail::NodeAllocator& allocator) {
    if (context.list_path.empty()) return std::nullopt;
    auto parent_path = context.list_path;
    const auto list_index = parent_path.back();
    parent_path.pop_back();
    auto* parent = block_at_path(document.root, parent_path);
    if (!parent || list_index >= parent->children.size()) return std::nullopt;

    auto* list = &parent->children[list_index];
    if (context.item_index >= list->children.size()) return std::nullopt;
    auto* selected = &list->children[context.item_index];
    if (selected->children.empty()) return std::nullopt;
    document_edit_detail::RecordedBlockEdit result;
    result.target = TextPosition{position.container_id, 0, TextAffinity::Downstream};
    const auto parent_id = parent->id;
    const auto list_id = list->id;
    const auto selected_id = selected->id;
    const auto following_count = list->children.size() - context.item_index - 1;

    std::optional<NodeId> trailing_id;
    if (context.item_index > 0 && following_count > 0) {
        auto trailing = document_transaction_detail::payload_shell(*list);
        trailing.id = allocator.allocate();
        if (trailing.list_ordered) trailing.list_start += context.item_index + 1;
        trailing_id = trailing.id;
        DocumentTreeEdit insert_trailing;
        insert_trailing.kind = DocumentTreeEditKind::Insert;
        insert_trailing.parent_id = parent_id;
        insert_trailing.index = list_index + 1;
        insert_trailing.after = trailing;
        result.operations.emplace_back(std::move(insert_trailing));
        if (!insert_block(*parent, list_index + 1, std::move(trailing))) return std::nullopt;

        for (std::size_t target_index = 0; target_index < following_count; ++target_index) {
            list = find_block(document.root, list_id);
            auto* trailing_list = find_block(document.root, *trailing_id);
            if (!list || !trailing_list || context.item_index + 1 >= list->children.size()) {
                return std::nullopt;
            }
            DocumentTreeEdit move_after;
            move_after.kind = DocumentTreeEditKind::Move;
            move_after.parent_id = list_id;
            move_after.index = context.item_index + 1;
            move_after.other_parent_id = *trailing_id;
            move_after.other_index = target_index;
            auto item = remove_block(*list, context.item_index + 1);
            if (!item || !insert_block(*trailing_list, target_index, std::move(*item))) return std::nullopt;
            result.operations.emplace_back(std::move(move_after));
        }
    }

    selected = find_block(document.root, selected_id);
    if (!selected) return std::nullopt;
    const auto child_count = selected->children.size();
    for (std::size_t target_index = 0; target_index < child_count; ++target_index) {
        selected = find_block(document.root, selected_id);
        parent = find_block(document.root, parent_id);
        if (!selected || !parent || selected->children.empty()) return std::nullopt;
        DocumentTreeEdit move_child;
        move_child.kind = DocumentTreeEditKind::Move;
        move_child.parent_id = selected_id;
        move_child.index = 0;
        move_child.other_parent_id = parent_id;
        move_child.other_index = list_index + (context.item_index > 0 ? 1 : 0) + target_index;
        auto child = remove_block(*selected, 0);
        if (!child || !insert_block(*parent, move_child.other_index, std::move(*child))) return std::nullopt;
        result.operations.emplace_back(std::move(move_child));
    }

    list = find_block(document.root, list_id);
    if (!list || context.item_index >= list->children.size()) return std::nullopt;
    DocumentTreeEdit remove_item;
    remove_item.kind = DocumentTreeEditKind::Remove;
    remove_item.parent_id = list_id;
    remove_item.index = context.item_index;
    remove_item.before = list->children[context.item_index];
    result.operations.emplace_back(std::move(remove_item));
    list->children.erase(list->children.begin() + static_cast<std::ptrdiff_t>(context.item_index));

    if (context.item_index == 0 && following_count > 0) {
        DocumentTreeEdit update_list;
        update_list.kind = DocumentTreeEditKind::UpdatePayload;
        update_list.before = document_transaction_detail::payload_shell(*list);
        if (list->list_ordered) ++list->list_start;
        update_list.after = document_transaction_detail::payload_shell(*list);
        result.operations.emplace_back(std::move(update_list));
    }
    if (list->children.empty()) {
        parent = find_block(document.root, parent_id);
        const auto shifted_list_index = list_index + child_count;
        if (!parent || shifted_list_index >= parent->children.size()) return std::nullopt;
        DocumentTreeEdit remove_list;
        remove_list.kind = DocumentTreeEditKind::Remove;
        remove_list.parent_id = parent_id;
        remove_list.index = shifted_list_index;
        remove_list.before = parent->children[shifted_list_index];
        result.operations.emplace_back(std::move(remove_list));
        parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(shifted_list_index));
    }
    return result;
}

inline std::optional<document_edit_detail::RecordedBlockEdit> remove_list_prefix(
    EditorDocument& document,
    TextPosition position,
    document_edit_detail::NodeAllocator& allocator) {
    auto context = direct_list_context(document, position.container_id);
    if (!context || context->child_index != 0) return std::nullopt;
    if (auto nested = outdent_nested_list_item(document, *context, position)) return nested;
    return remove_top_level_list_prefix(document, *context, position, allocator);
}

} // namespace elmd::document_input_rules::detail
