export module elmd.core.document_structure_edit;
import std;
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.block_tree;
import elmd.core.callout;
import elmd.core.document;
import elmd.core.document_ids;
import elmd.core.ids;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.inline_parser;
import elmd.core.inline_source_edit;
import elmd.core.text_edit;
import elmd.core.utf;
import elmd.core.document_edit_support;
import elmd.core.document_input_rules;
import elmd.core.document_source_edit;

export namespace elmd {

namespace document_structure_detail {

struct DirectRange {
    BlockPath parent_path;
    std::size_t first = 0;
    std::size_t last = 0;
};

inline std::optional<NodeId> nearest_container(
    const EditorDocument& document,
    NodeId id,
    std::initializer_list<BlockKind> kinds) {
    auto path = block_path(document.root, id);
    if (!path) return std::nullopt;
    while (!path->empty()) {
        const auto* candidate = block_at_path(document.root, *path);
        if (candidate && std::find(kinds.begin(), kinds.end(), candidate->kind) != kinds.end()) {
            return candidate->id;
        }
        path->pop_back();
    }
    return std::nullopt;
}

inline std::optional<DirectRange> direct_range(
    const EditorDocument& document,
    NodeId first,
    NodeId last) {
    auto first_path = block_path(document.root, first);
    auto last_path = block_path(document.root, last);
    if (!first_path || !last_path || first_path->empty() || last_path->empty()) return std::nullopt;
    const auto first_index = first_path->back();
    const auto last_index = last_path->back();
    first_path->pop_back();
    last_path->pop_back();
    if (*first_path != *last_path) return std::nullopt;
    return DirectRange{
        std::move(*first_path),
        (std::min)(first_index, last_index),
        (std::max)(first_index, last_index)};
}

inline bool insert_container_and_move_range(
    EditorDocument& document,
    const DirectRange& range,
    BlockNode container,
    std::vector<DocumentOperation>& operations) {
    auto* parent = block_at_path(document.root, range.parent_path);
    if (!parent || range.last >= parent->children.size()) return false;
    const auto parent_id = parent->id;
    const auto container_id = container.id;
    DocumentTreeEdit insert;
    insert.kind = DocumentTreeEditKind::Insert;
    insert.parent_id = parent_id;
    insert.index = range.first;
    insert.after = container;
    operations.emplace_back(std::move(insert));
    if (!insert_block(*parent, range.first, std::move(container))) return false;

    const auto count = range.last - range.first + 1;
    for (std::size_t target_index = 0; target_index < count; ++target_index) {
        parent = find_block(document.root, parent_id);
        auto* inserted = find_block(document.root, container_id);
        if (!parent || !inserted || range.first + 1 >= parent->children.size()) return false;
        DocumentTreeEdit move;
        move.kind = DocumentTreeEditKind::Move;
        move.parent_id = parent_id;
        move.index = range.first + 1;
        move.other_parent_id = container_id;
        move.other_index = target_index;
        move.moved_id = parent->children[range.first + 1].id;
        auto child = remove_block(*parent, range.first + 1);
        if (!child || !insert_block(*inserted, target_index, std::move(*child))) return false;
        operations.emplace_back(std::move(move));
    }
    return true;
}

inline bool unwrap_container(
    EditorDocument& document,
    const DirectRange& range,
    std::vector<DocumentOperation>& operations) {
    auto* parent = block_at_path(document.root, range.parent_path);
    if (!parent || range.first != range.last || range.first >= parent->children.size()) return false;
    const auto parent_id = parent->id;
    const auto container_id = parent->children[range.first].id;
    auto* container = find_block(document.root, container_id);
    if (!container) return false;
    const auto child_count = container->children.size();
    for (std::size_t target_index = 0; target_index < child_count; ++target_index) {
        parent = find_block(document.root, parent_id);
        container = find_block(document.root, container_id);
        if (!parent || !container || container->children.empty()) return false;
        DocumentTreeEdit move;
        move.kind = DocumentTreeEditKind::Move;
        move.parent_id = container_id;
        move.index = 0;
        move.other_parent_id = parent_id;
        move.other_index = range.first + target_index;
        move.moved_id = container->children.front().id;
        auto child = remove_block(*container, 0);
        if (!child || !insert_block(*parent, move.other_index, std::move(*child))) return false;
        operations.emplace_back(std::move(move));
    }
    parent = find_block(document.root, parent_id);
    const auto container_index = range.first + child_count;
    if (!parent || container_index >= parent->children.size()) return false;
    DocumentTreeEdit remove;
    remove.kind = DocumentTreeEditKind::Remove;
    remove.parent_id = parent_id;
    remove.index = container_index;
    remove.before = parent->children[container_index];
    operations.emplace_back(std::move(remove));
    parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(container_index));
    return true;
}

} // namespace document_structure_detail

inline std::optional<DocumentTransaction> document_set_heading(EditorDocument& document, const TextSelection& selection, std::uint8_t level) {
    if (level > 6 || selection.anchor.container_id != selection.active.container_id) return std::nullopt;
    const auto revision_before = document.revision;
    auto& after = document;
    auto* block = find_block(after.root, selection.active.container_id);
    if (!block || !document_edit_detail::text_block(block->kind)) return std::nullopt;
    DocumentTreeEdit update;
    update.kind = DocumentTreeEditKind::UpdatePayload;
    update.before = document_transaction_detail::payload_shell(*block);
    block->kind = level == 0 ? BlockKind::Paragraph : BlockKind::Heading;
    block->ensure_text_special().level = level;
    block->ensure_text_special().opening_marker = level == 0
        ? std::u32string{}
        : std::u32string(level, U'#') + U" ";
    block->ensure_text_special().closing_marker.clear();
    update.after = document_transaction_detail::payload_shell(*block);
    std::vector<DocumentOperation> operations;
    operations.emplace_back(std::move(update));
    ++after.revision;
    return make_recorded_document_transaction(
        std::move(operations),
        selection,
        selection,
        revision_before,
        after.revision,
        DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_toggle_block_quote(EditorDocument& document, const TextSelection& selection) {
    const auto revision_before = document.revision;
    auto& after = document; document_edit_detail::NodeAllocator allocator(after);
    auto first = selection.anchor.container_id;
    auto last = selection.active.container_id;
    const auto first_quote = document_structure_detail::nearest_container(
        after, first, {BlockKind::BlockQuote});
    const auto last_quote = document_structure_detail::nearest_container(
        after, last, {BlockKind::BlockQuote});
    if (first_quote && first_quote == last_quote) first = last = *first_quote;
    auto range = document_structure_detail::direct_range(
        after, first, last);
    if (!range) return std::nullopt;
    auto* parent = block_at_path(after.root, range->parent_path);
    if (!parent || range->last >= parent->children.size()) return std::nullopt;
    std::vector<DocumentOperation> operations;
    document_edit_detail::MutationRollback rollback(after, operations, revision_before);
    if (range->first == range->last && parent->children[range->first].kind == BlockKind::BlockQuote) {
        if (!document_structure_detail::unwrap_container(after, *range, operations)) return std::nullopt;
    } else {
        BlockNode quote;
        quote.id = allocator.allocate();
        quote.kind = BlockKind::BlockQuote;
        if (!document_structure_detail::insert_container_and_move_range(
                after, *range, std::move(quote), operations)) return std::nullopt;
    }
    ++after.revision;
    rollback.commit();
    return make_recorded_document_transaction(
        std::move(operations), selection, selection,
        revision_before, after.revision, DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_toggle_callout(EditorDocument& document, const TextSelection& selection, std::string kind) {
    kind = normalize_callout_kind(kind).value_or("NOTE");
    const auto revision_before = document.revision;
    auto& after = document; document_edit_detail::NodeAllocator allocator(after);
    auto first = selection.anchor.container_id;
    auto last = selection.active.container_id;
    const auto first_callout = document_structure_detail::nearest_container(
        after, first, {BlockKind::Callout});
    const auto last_callout = document_structure_detail::nearest_container(
        after, last, {BlockKind::Callout});
    if (first_callout && first_callout == last_callout) first = last = *first_callout;
    auto range = document_structure_detail::direct_range(
        after, first, last);
    if (!range) return std::nullopt;
    auto* parent = block_at_path(after.root, range->parent_path);
    if (!parent || range->last >= parent->children.size()) return std::nullopt;
    std::vector<DocumentOperation> operations;
    document_edit_detail::MutationRollback rollback(after, operations, revision_before);
    if (range->first == range->last && parent->children[range->first].kind == BlockKind::Callout) {
        const auto callout_id = parent->children[range->first].id;
        auto* callout = find_block(after.root, callout_id);
        if (!callout) return std::nullopt;
        if (callout->container_special().callout_kind != kind) {
            DocumentTreeEdit update;
            update.kind = DocumentTreeEditKind::UpdatePayload;
            update.before = document_transaction_detail::payload_shell(*callout);
            callout->ensure_container_special().callout_kind = kind;
            callout->ensure_text_special().opening_marker = rewrite_callout_opening_marker(
                callout->text_special().opening_marker, kind);
            update.after = document_transaction_detail::payload_shell(*callout);
            operations.emplace_back(std::move(update));
        } else {
            if (auto* title = callout_title_block(*callout)) {
                DocumentTreeEdit update;
                update.kind = DocumentTreeEditKind::UpdatePayload;
                update.before = document_transaction_detail::payload_shell(*title);
                title->kind = BlockKind::Paragraph;
                update.after = document_transaction_detail::payload_shell(*title);
                operations.emplace_back(std::move(update));
            }
            if (!document_structure_detail::unwrap_container(after, *range, operations)) {
                return std::nullopt;
            }
        }
    } else {
        BlockNode callout;
        callout.id = allocator.allocate();
        callout.kind = BlockKind::Callout;
        callout.ensure_container_special().callout_kind = kind;
        if (!document_structure_detail::insert_container_and_move_range(
                after, *range, std::move(callout), operations)) return std::nullopt;
    }
    ++after.revision;
    rollback.commit();
    return make_recorded_document_transaction(
        std::move(operations), selection, selection,
        revision_before, after.revision, DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_toggle_list(EditorDocument& document, const TextSelection& selection, document_edit_detail::ListStyle style) {
    const auto revision_before = document.revision;
    auto& after = document; document_edit_detail::NodeAllocator allocator(after);
    auto first = selection.anchor.container_id;
    auto last = selection.active.container_id;
    const auto first_list = document_structure_detail::nearest_container(
        after, first, {BlockKind::List, BlockKind::TaskList});
    const auto last_list = document_structure_detail::nearest_container(
        after, last, {BlockKind::List, BlockKind::TaskList});
    if (first_list && first_list == last_list) first = last = *first_list;
    auto range = document_structure_detail::direct_range(
        after, first, last);
    if (!range) return std::nullopt;
    auto* parent = block_at_path(after.root, range->parent_path);
    if (!parent || range->last >= parent->children.size()) return std::nullopt;
    const auto parent_id = parent->id;
    std::vector<DocumentOperation> operations;
    document_edit_detail::MutationRollback rollback(after, operations, revision_before);
    if (range->first == range->last
        && (parent->children[range->first].kind == BlockKind::List
            || parent->children[range->first].kind == BlockKind::TaskList)) {
        const auto list_id = parent->children[range->first].id;
        auto* list = find_block(after.root, list_id);
        if (!list) return std::nullopt;
        const auto item_count = list->children.size();
        std::size_t parent_index = range->first;
        for (std::size_t item_index = 0; item_index < item_count; ++item_index) {
            list = find_block(after.root, list_id);
            if (!list || list->children.empty()) return std::nullopt;
            const auto item_id = list->children.front().id;
            auto* item = find_block(after.root, item_id);
            if (!item) return std::nullopt;
            const auto child_count = item->children.size();
            for (std::size_t child_index = 0; child_index < child_count; ++child_index) {
                parent = find_block(after.root, parent_id);
                item = find_block(after.root, item_id);
                if (!parent || !item || item->children.empty()) return std::nullopt;
                DocumentTreeEdit move;
                move.kind = DocumentTreeEditKind::Move;
                move.parent_id = item_id;
                move.index = 0;
                move.other_parent_id = parent_id;
                move.other_index = parent_index++;
                move.moved_id = item->children.front().id;
                auto child = remove_block(*item, 0);
                if (!child || !insert_block(*parent, move.other_index, std::move(*child))) return std::nullopt;
                operations.emplace_back(std::move(move));
            }
            list = find_block(after.root, list_id);
            if (!list || list->children.empty()) return std::nullopt;
            DocumentTreeEdit remove_item;
            remove_item.kind = DocumentTreeEditKind::Remove;
            remove_item.parent_id = list_id;
            remove_item.index = 0;
            remove_item.before = list->children.front();
            operations.emplace_back(std::move(remove_item));
            list->children.erase(list->children.begin());
        }
        parent = find_block(after.root, parent_id);
        if (!parent || parent_index >= parent->children.size()) return std::nullopt;
        DocumentTreeEdit remove_list;
        remove_list.kind = DocumentTreeEditKind::Remove;
        remove_list.parent_id = parent_id;
        remove_list.index = parent_index;
        remove_list.before = parent->children[parent_index];
        operations.emplace_back(std::move(remove_list));
        parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(parent_index));
    } else {
        BlockNode list;
        list.id = allocator.allocate();
        list.kind = style == document_edit_detail::ListStyle::Task
            ? BlockKind::TaskList : BlockKind::List;
        list.ensure_list_special().ordered = style == document_edit_detail::ListStyle::Ordered;
        const auto list_id = list.id;
        DocumentTreeEdit insert_list;
        insert_list.kind = DocumentTreeEditKind::Insert;
        insert_list.parent_id = parent_id;
        insert_list.index = range->first;
        insert_list.after = list;
        operations.emplace_back(std::move(insert_list));
        if (!insert_block(*parent, range->first, std::move(list))) return std::nullopt;

        const auto count = range->last - range->first + 1;
        for (std::size_t item_index = 0; item_index < count; ++item_index) {
            auto* inserted_list = find_block(after.root, list_id);
            parent = find_block(after.root, parent_id);
            if (!inserted_list || !parent || range->first + 1 >= parent->children.size()) {
                return std::nullopt;
            }
            BlockNode item;
            item.id = allocator.allocate();
            item.kind = style == document_edit_detail::ListStyle::Task
                ? BlockKind::TaskListItem : BlockKind::ListItem;
            item.ensure_item_special().marker = style == document_edit_detail::ListStyle::Task ? U"- [ ] "
                : style == document_edit_detail::ListStyle::Ordered ? U"1. " : U"- ";
            const auto item_id = item.id;
            DocumentTreeEdit insert_item;
            insert_item.kind = DocumentTreeEditKind::Insert;
            insert_item.parent_id = list_id;
            insert_item.index = item_index;
            insert_item.after = item;
            operations.emplace_back(std::move(insert_item));
            if (!insert_block(*inserted_list, item_index, std::move(item))) return std::nullopt;

            DocumentTreeEdit move;
            move.kind = DocumentTreeEditKind::Move;
            move.parent_id = parent_id;
            move.index = range->first + 1;
            move.other_parent_id = item_id;
            move.other_index = 0;
            move.moved_id = parent->children[range->first + 1].id;
            auto child = remove_block(*parent, range->first + 1);
            auto* inserted_item = find_block(after.root, item_id);
            if (!child || !inserted_item || !insert_block(*inserted_item, 0, std::move(*child))) {
                return std::nullopt;
            }
            operations.emplace_back(std::move(move));
        }
    }
    ++after.revision;
    rollback.commit();
    return make_recorded_document_transaction(
        std::move(operations), selection, selection,
        revision_before, after.revision, DocumentTransactionReason::Structure);
}
using ListStyle = document_edit_detail::ListStyle;

inline std::optional<DocumentTransaction> document_toggle_task_checkbox(EditorDocument& document, const TextSelection& selection) {
    const auto revision_before = document.revision;
    auto& after = document;
    auto path = block_path(after.root, selection.active.container_id);
    if (!path) return std::nullopt;
    BlockNode* item = nullptr;
    while (!path->empty()) {
        auto* candidate = block_at_path(after.root, *path);
        if (candidate && candidate->kind == BlockKind::TaskListItem) {
            item = candidate;
            break;
        }
        path->pop_back();
    }
    if (!item) return std::nullopt;
    DocumentTreeEdit update;
    update.kind = DocumentTreeEditKind::UpdatePayload;
    update.before = document_transaction_detail::payload_shell(*item);
    auto& item_special = item->ensure_item_special();
    item_special.checked = !item_special.checked;
    const auto unchecked = item_special.marker.find(U"[ ]");
    const auto checked_lower = item_special.marker.find(U"[x]");
    const auto checked_upper = item_special.marker.find(U"[X]");
    if (item_special.checked && unchecked != std::u32string::npos) item_special.marker[unchecked + 1] = U'x';
    if (!item_special.checked && checked_lower != std::u32string::npos) item_special.marker[checked_lower + 1] = U' ';
    if (!item_special.checked && checked_upper != std::u32string::npos) item_special.marker[checked_upper + 1] = U' ';
    update.after = document_transaction_detail::payload_shell(*item);
    std::vector<DocumentOperation> operations;
    operations.emplace_back(std::move(update));
    ++after.revision;
    return make_recorded_document_transaction(
        std::move(operations),
        selection,
        selection,
        revision_before,
        after.revision,
        DocumentTransactionReason::Structure);
}

inline BlockNode make_code_block(std::optional<std::string> language = std::nullopt) {
    BlockNode block;
    block.kind = BlockKind::CodeBlock;
    auto source = U"```" + (language ? utf8_to_cps(*language) : std::u32string{}) + U"\n```";
    block.block_source = make_block_source(std::move(source), BlockSourceKind::FencedCode);
    return block;
}
inline BlockNode make_math_block() {
    BlockNode block;
    block.kind = BlockKind::MathBlock;
    block.ensure_atomic_special().math_delim = MathDelimiter::BlockDollar;
    block.block_source = make_block_source(U"$$\n$$", BlockSourceKind::DollarMath);
    return block;
}
inline BlockNode make_toc_block() { BlockNode block; block.kind = BlockKind::Toc; return block; }

inline BlockNode make_table_block(EditorDocument& document, std::size_t rows, std::size_t columns) {
    document_edit_detail::NodeAllocator allocator(document);
    BlockNode table; table.kind = BlockKind::Table; columns = (std::max)(columns, std::size_t{1}); table.ensure_table_special().table_aligns.assign(columns, TableAlignment::None);
    BlockNode header; header.id = allocator.allocate(); header.kind = BlockKind::TableRow; header.ensure_table_special().table_header_row = true;
    for (std::size_t column = 0; column < columns; ++column) {
        BlockNode cell; cell.id = allocator.allocate(); cell.kind = BlockKind::TableCell;
        cell.inline_content = document_edit_detail::make_inline(U"Header", document, allocator);
        header.children.push_back(std::move(cell));
    }
    table.children.push_back(std::move(header));
    for (std::size_t row_index = 0; row_index < rows; ++row_index) {
        BlockNode row; row.id = allocator.allocate(); row.kind = BlockKind::TableRow;
        for (std::size_t column = 0; column < columns; ++column) {
            BlockNode cell; cell.id = allocator.allocate(); cell.kind = BlockKind::TableCell;
            cell.inline_content = document_edit_detail::make_inline(U"Cell", document, allocator);
            row.children.push_back(std::move(cell));
        }
        table.children.push_back(std::move(row));
    }
    return table;
}

inline std::optional<DocumentTransaction> document_insert_atomic_block(EditorDocument& document, const TextSelection& selection, BlockNode block) {
    if (!selection.is_caret()) return std::nullopt;
    const auto revision_before = document.revision;
    auto& after = document;
    document_edit_detail::NodeAllocator allocator(after);
    reserve_document_node_ids(after, block);
    document_edit_detail::assign_missing_ids(block, after, allocator);
    auto path = block_path(after.root, selection.active.container_id);
    if (!path || path->empty()) return std::nullopt;
    auto parent_path = *path;
    const auto index = parent_path.back();
    parent_path.pop_back();
    auto* parent = block_at_path(after.root, parent_path);
    if (!parent || index >= parent->children.size()) return std::nullopt;
    const auto inserted_id = block.id;
    auto inserted_target = document_edit_detail::first_editable_position(block)
        .value_or(TextPosition{block.id, 0, TextAffinity::Downstream});
    if ((block.kind == BlockKind::CodeBlock || block.kind == BlockKind::MathBlock)
        && !block.block_source.tree().content_to_source.empty()) {
        inserted_target = {
            block.id,
            block.block_source.tree().content_to_source.front(),
            TextAffinity::Downstream,
        };
    }
    std::vector<DocumentOperation> operations;
    document_edit_detail::MutationRollback rollback(after, operations, revision_before);
    const auto replace_empty_paragraph = parent->children[index].kind == BlockKind::Paragraph
        && parent->children[index].inline_content.source.empty();
    if (replace_empty_paragraph) {
        const auto preserved_id = parent->children[index].id;
        block.id = preserved_id;
        if (inserted_target.container_id == inserted_id) {
            inserted_target.container_id = preserved_id;
        }
        DocumentTreeEdit remove;
        remove.kind = DocumentTreeEditKind::Remove;
        remove.parent_id = parent->id;
        remove.index = index;
        remove.before = parent->children[index];
        operations.emplace_back(std::move(remove));
        parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(index));

        DocumentTreeEdit insert;
        insert.kind = DocumentTreeEditKind::Insert;
        insert.parent_id = parent->id;
        insert.index = index;
        insert.after = block;
        operations.emplace_back(std::move(insert));
        if (!insert_block(*parent, index, std::move(block))) return std::nullopt;
    } else {
        DocumentTreeEdit insert;
        insert.kind = DocumentTreeEditKind::Insert;
        insert.parent_id = parent->id;
        insert.index = index + 1;
        insert.after = block;
        operations.emplace_back(std::move(insert));
        if (!insert_block(*parent, index + 1, std::move(block))) return std::nullopt;
    }
    ++after.revision;
    rollback.commit();
    return make_recorded_document_transaction(
        std::move(operations),
        selection,
        TextSelection::caret(inserted_target),
        revision_before,
        after.revision,
        DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_indent_list_item(EditorDocument& document, const TextSelection& selection) {
    const auto revision_before = document.revision;
    auto& after = document;
    auto path = block_path(after.root, selection.active.container_id);
    if (!path) return std::nullopt;
    while (!path->empty()) {
        const auto* node = block_at_path(after.root, *path);
        if (node && (node->kind == BlockKind::ListItem || node->kind == BlockKind::TaskListItem)) break;
        path->pop_back();
    }
    if (path->empty()) return std::nullopt;
    const auto item_id = block_at_path(after.root, *path)->id;
    auto list_path = *path;
    const auto item_index = list_path.back();
    list_path.pop_back();
    auto* list = block_at_path(after.root, list_path);
    if (!list || (list->kind != BlockKind::List && list->kind != BlockKind::TaskList) || item_index == 0) return std::nullopt;
    document_edit_detail::NodeAllocator allocator(after);
    const auto list_id = list->id;
    const auto previous_id = list->children[item_index - 1].id;
    auto* previous = find_block(after.root, previous_id);
    if (!previous) return std::nullopt;
    BlockNode* nested = nullptr;
    if (!previous->children.empty() && previous->children.back().kind == list->kind) {
        nested = &previous->children.back();
    }
    std::vector<DocumentOperation> operations;
    document_edit_detail::MutationRollback rollback(after, operations, revision_before);
    if (!nested) {
        BlockNode created;
        created.id = allocator.allocate();
        created.kind = list->kind;
        created.ensure_list_special().ordered = list->list_special().ordered;
        created.ensure_list_special().start = list->list_special().start;
        created.ensure_list_special().delimiter = list->list_special().delimiter;
        const auto nested_id = created.id;
        DocumentTreeEdit insert;
        insert.kind = DocumentTreeEditKind::Insert;
        insert.parent_id = previous_id;
        insert.index = previous->children.size();
        insert.after = created;
        operations.emplace_back(std::move(insert));
        previous->children.push_back(std::move(created));
        nested = find_block(after.root, nested_id);
    }
    if (!nested) return std::nullopt;
    const auto nested_id = nested->id;
    const auto target_index = nested->children.size();
    DocumentTreeEdit move;
    move.kind = DocumentTreeEditKind::Move;
    move.parent_id = list_id;
    move.index = item_index;
    move.other_parent_id = nested_id;
    move.other_index = target_index;
    list = find_block(after.root, list_id);
    nested = find_block(after.root, nested_id);
    if (!list || item_index >= list->children.size()) return std::nullopt;
    move.moved_id = list->children[item_index].id;
    auto item = list ? remove_block(*list, item_index) : std::nullopt;
    if (!item || !nested || !insert_block(*nested, target_index, std::move(*item))) return std::nullopt;
    operations.emplace_back(std::move(move));
    ++after.revision;
    rollback.commit();
    return make_recorded_document_transaction(
        std::move(operations), selection, selection,
        revision_before, after.revision, DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_outdent_list_item(EditorDocument& document, const TextSelection& selection) {
    const auto revision_before = document.revision;
    auto& after = document;
    auto path = block_path(after.root, selection.active.container_id);
    if (!path) return std::nullopt;
    while (!path->empty()) {
        const auto* node = block_at_path(after.root, *path);
        if (node && (node->kind == BlockKind::ListItem || node->kind == BlockKind::TaskListItem)) break;
        path->pop_back();
    }
    if (path->empty()) return std::nullopt;
    document_edit_detail::NodeAllocator allocator(after);
    auto recorded = document_input_rules::outdent_list_item(
        after,
        selection.active.container_id,
        allocator);
    if (!recorded || recorded->operations.empty()) return std::nullopt;
    ++after.revision;
    return make_recorded_document_transaction(
        std::move(recorded->operations),
        selection,
        selection,
        revision_before,
        after.revision,
        DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_edit_table(
    EditorDocument& document,
    const TextSelection& selection,
    DocumentTableEdit edit,
    TableAlignment alignment = TableAlignment::None,
    std::size_t argument = 0) {
    const auto revision_before = document.revision;
    auto& after = document;
    auto path = block_path(after.root, selection.active.container_id);
    if (!path || path->size() < 2) return std::nullopt;
    while (!path->empty() && block_at_path(after.root, *path)->kind != BlockKind::TableCell) path->pop_back();
    if (path->size() < 2) return std::nullopt;
    const auto cell_id = block_at_path(after.root, *path)->id;
    auto row_path = *path;
    const auto column = row_path.back();
    row_path.pop_back();
    auto table_path = row_path;
    const auto row_index = table_path.back();
    table_path.pop_back();
    auto* table = block_at_path(after.root, table_path);
    if (!table || table->kind != BlockKind::Table || table->children.empty()) return std::nullopt;
    document_edit_detail::NodeAllocator allocator(after);
    const auto column_count = table->children.front().children.size();
    auto make_cell = [&] {
        BlockNode cell;
        cell.id = allocator.allocate();
        cell.kind = BlockKind::TableCell;
        cell.inline_content = document_edit_detail::make_inline({}, after, allocator);
        return cell;
    };
    auto make_row = [&] {
        BlockNode row;
        row.id = allocator.allocate();
        row.kind = BlockKind::TableRow;
        for (std::size_t index = 0; index < column_count; ++index) row.children.push_back(make_cell());
        return row;
    };
    auto target_id = cell_id;
    const auto table_id = table->id;
    const auto table_payload_before = document_transaction_detail::payload_shell(*table);
    std::vector<DocumentOperation> operations;
    document_edit_detail::MutationRollback rollback(after, operations, revision_before);
    bool payload_changed = false;
    auto insert_node = [&](NodeId parent_id, std::size_t index, BlockNode node) {
        auto* owner = find_block(after.root, parent_id);
        if (!owner || index > owner->children.size()) return false;
        DocumentTreeEdit insert;
        insert.kind = DocumentTreeEditKind::Insert;
        insert.parent_id = parent_id;
        insert.index = index;
        insert.after = node;
        operations.emplace_back(std::move(insert));
        return insert_block(*owner, index, std::move(node));
    };
    auto remove_node = [&](NodeId parent_id, std::size_t index) {
        auto* owner = find_block(after.root, parent_id);
        if (!owner || index >= owner->children.size()) return false;
        DocumentTreeEdit remove;
        remove.kind = DocumentTreeEditKind::Remove;
        remove.parent_id = parent_id;
        remove.index = index;
        remove.before = owner->children[index];
        operations.emplace_back(std::move(remove));
        return remove_block(*owner, index).has_value();
    };
    auto move_node = [&](NodeId parent_id, std::size_t from, std::size_t to) {
        auto* owner = find_block(after.root, parent_id);
        if (!owner || from >= owner->children.size()) return false;
        auto node = remove_block(*owner, from);
        if (!node || to > owner->children.size()) return false;
        DocumentTreeEdit move;
        move.kind = DocumentTreeEditKind::Move;
        move.parent_id = parent_id;
        move.index = from;
        move.other_parent_id = parent_id;
        move.other_index = to;
        move.moved_id = node->id;
        operations.emplace_back(std::move(move));
        return insert_block(*owner, to, std::move(*node));
    };
    bool changed = true;
    switch (edit) {
        case DocumentTableEdit::MoveCellNext: {
            if (column + 1 < table->children[row_index].children.size()) target_id = table->children[row_index].children[column + 1].id;
            else if (row_index + 1 < table->children.size()) target_id = table->children[row_index + 1].children.front().id;
            else return std::nullopt;
            changed = false;
            break;
        }
        case DocumentTableEdit::MoveCellPrevious: {
            if (column > 0) target_id = table->children[row_index].children[column - 1].id;
            else if (row_index > 0) target_id = table->children[row_index - 1].children.back().id;
            else return std::nullopt;
            changed = false;
            break;
        }
        case DocumentTableEdit::InsertRowAbove:
            if (!insert_node(table_id, row_index, make_row())) return std::nullopt;
            break;
        case DocumentTableEdit::InsertRowBelow:
            if (!insert_node(table_id, row_index + 1, make_row())) return std::nullopt;
            break;
        case DocumentTableEdit::InsertRowAt: {
            const auto index = (std::min)(argument, table->children.size());
            if (!insert_node(table_id, index, make_row())) return std::nullopt;
            break;
        }
        case DocumentTableEdit::DeleteRow:
            if (row_index == 0 || table->children.size() <= 1) return std::nullopt;
            if (!remove_node(table_id, row_index)) return std::nullopt;
            table = find_block(after.root, table_id);
            if (!table) return std::nullopt;
            target_id = table->children[(std::min)(row_index - 1, table->children.size() - 1)].children[(std::min)(column, column_count - 1)].id;
            break;
        case DocumentTableEdit::MoveRowUp:
            if (row_index <= 1) return std::nullopt;
            if (!move_node(table_id, row_index, row_index - 1)) return std::nullopt;
            break;
        case DocumentTableEdit::MoveRowDown:
            if (row_index == 0 || row_index + 1 >= table->children.size()) return std::nullopt;
            if (!move_node(table_id, row_index, row_index + 1)) return std::nullopt;
            break;
        case DocumentTableEdit::MoveRowTo: {
            if (row_index == 0 || argument == 0 || argument >= table->children.size()) return std::nullopt;
            if (!move_node(table_id, row_index, argument)) return std::nullopt;
            break;
        }
        case DocumentTableEdit::InsertColumnLeft:
        case DocumentTableEdit::InsertColumnRight:
        case DocumentTableEdit::InsertColumnAt: {
            const auto index = edit == DocumentTableEdit::InsertColumnLeft ? column
                : edit == DocumentTableEdit::InsertColumnRight ? column + 1
                : (std::min)(argument, column_count);
            std::vector<NodeId> row_ids;
            for (const auto& row : table->children) row_ids.push_back(row.id);
            for (auto row_id : row_ids) {
                if (!insert_node(row_id, index, make_cell())) return std::nullopt;
            }
            table = find_block(after.root, table_id);
            if (!table) return std::nullopt;
            auto& aligns = table->ensure_table_special().table_aligns;
            aligns.insert(aligns.begin() + static_cast<std::ptrdiff_t>((std::min)(index, aligns.size())), TableAlignment::None);
            payload_changed = true;
            break;
        }
        case DocumentTableEdit::DeleteColumn: {
            if (column_count <= 1) return std::nullopt;
            {
                std::vector<NodeId> row_ids;
                for (const auto& row : table->children) row_ids.push_back(row.id);
                for (auto row_id : row_ids) {
                    if (!remove_node(row_id, column)) return std::nullopt;
                }
            }
            table = find_block(after.root, table_id);
            if (!table) return std::nullopt;
            auto& aligns = table->ensure_table_special().table_aligns;
            if (column < aligns.size()) aligns.erase(aligns.begin() + static_cast<std::ptrdiff_t>(column));
            payload_changed = true;
            target_id = table->children[row_index].children[(std::min)(column, column_count - 2)].id;
            break;
        }
        case DocumentTableEdit::MoveColumnLeft: {
            if (column == 0) return std::nullopt;
            {
                std::vector<NodeId> row_ids;
                for (const auto& row : table->children) row_ids.push_back(row.id);
                for (auto row_id : row_ids) {
                    if (!move_node(row_id, column, column - 1)) return std::nullopt;
                }
            }
            table = find_block(after.root, table_id);
            if (!table) return std::nullopt;
            auto& aligns = table->ensure_table_special().table_aligns;
            if (column < aligns.size()) std::swap(aligns[column], aligns[column - 1]);
            payload_changed = true;
            break;
        }
        case DocumentTableEdit::MoveColumnRight: {
            if (column + 1 >= column_count) return std::nullopt;
            {
                std::vector<NodeId> row_ids;
                for (const auto& row : table->children) row_ids.push_back(row.id);
                for (auto row_id : row_ids) {
                    if (!move_node(row_id, column, column + 1)) return std::nullopt;
                }
            }
            table = find_block(after.root, table_id);
            if (!table) return std::nullopt;
            auto& aligns = table->ensure_table_special().table_aligns;
            if (column + 1 < aligns.size()) std::swap(aligns[column], aligns[column + 1]);
            payload_changed = true;
            break;
        }
        case DocumentTableEdit::MoveColumnTo: {
            if (argument >= column_count || argument == column) return std::nullopt;
            std::vector<NodeId> row_ids;
            for (const auto& row : table->children) row_ids.push_back(row.id);
            for (auto row_id : row_ids) {
                if (!move_node(row_id, column, argument)) return std::nullopt;
            }
            table = find_block(after.root, table_id);
            if (!table) return std::nullopt;
            auto& aligns = table->ensure_table_special().table_aligns;
            auto value = aligns[column];
            aligns.erase(aligns.begin() + static_cast<std::ptrdiff_t>(column));
            aligns.insert(aligns.begin() + static_cast<std::ptrdiff_t>(argument), value);
            payload_changed = true;
            break;
        }
        case DocumentTableEdit::SetColumnAlignment:
            if (column >= table->table_special().table_aligns.size()) {
                table->ensure_table_special().table_aligns.resize(column_count, TableAlignment::None);
            }
            table->ensure_table_special().table_aligns[column] = alignment;
            payload_changed = true;
            break;
        case DocumentTableEdit::Normalize: {
            auto columns = (std::max)(std::size_t{1}, column_count);
            std::vector<NodeId> row_ids;
            for (const auto& row : table->children) row_ids.push_back(row.id);
            for (auto row_id : row_ids) {
                auto* row = find_block(after.root, row_id);
                if (!row) return std::nullopt;
                while (row->children.size() < columns) {
                    if (!insert_node(row_id, row->children.size(), make_cell())) return std::nullopt;
                    row = find_block(after.root, row_id);
                    if (!row) return std::nullopt;
                }
                while (row->children.size() > columns) {
                    if (!remove_node(row_id, row->children.size() - 1)) return std::nullopt;
                    row = find_block(after.root, row_id);
                    if (!row) return std::nullopt;
                }
            }
            table = find_block(after.root, table_id);
            if (!table) return std::nullopt;
            table->ensure_table_special().table_aligns.resize(columns, TableAlignment::None);
            payload_changed = true;
            break;
        }
    }
    if (payload_changed) {
        table = find_block(after.root, table_id);
        if (!table) return std::nullopt;
        DocumentTreeEdit update;
        update.kind = DocumentTreeEditKind::UpdatePayload;
        update.before = table_payload_before;
        update.after = document_transaction_detail::payload_shell(*table);
        operations.emplace_back(std::move(update));
    }
    if (changed) ++after.revision;
    const auto target = TextSelection::caret(TextPosition{target_id, 0, TextAffinity::Downstream});
    rollback.commit();
    return make_recorded_document_transaction(
        std::move(operations),
        selection,
        target,
        revision_before,
        after.revision,
        DocumentTransactionReason::Structure);
}


} // namespace elmd
