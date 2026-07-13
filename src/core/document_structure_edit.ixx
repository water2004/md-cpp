export module elmd.core.document_structure_edit;
import std;
import elmd.core.ast;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.inline_parser;
import elmd.core.inline_source_edit;
import elmd.core.text_edit;
import elmd.core.utf;
import elmd.core.document_edit_support;
import elmd.core.document_source_edit;

export namespace elmd {

inline std::optional<DocumentTransaction> document_set_heading(const EditorDocument& document, const TextSelection& selection, std::uint8_t level) {
    if (level > 6 || selection.anchor.container_id != selection.active.container_id) return std::nullopt;
    auto after = document;
    auto* block = find_block(after.root, selection.active.container_id);
    if (!block || !document_edit_detail::text_block(block->kind)) return std::nullopt;
    DocumentTreeEdit update;
    update.kind = DocumentTreeEditKind::UpdatePayload;
    update.before = document_transaction_detail::payload_shell(*block);
    block->kind = level == 0 ? BlockKind::Paragraph : BlockKind::Heading;
    block->level = level;
    update.after = document_transaction_detail::payload_shell(*block);
    std::vector<DocumentOperation> operations;
    operations.emplace_back(std::move(update));
    ++after.revision;
    return make_recorded_document_transaction(
        std::move(after),
        std::move(operations),
        selection,
        selection,
        document.revision,
        DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_toggle_block_quote(const EditorDocument& document, const TextSelection& selection) {
    auto after = document; document_edit_detail::NodeAllocator allocator(after);
    if (!document_edit_detail::toggle_quote(after.root.children, selection.anchor.container_id, selection.active.container_id, allocator)) return std::nullopt;
    ++after.revision; return document_edit_detail::transaction(document, std::move(after), selection, selection, DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_toggle_callout(const EditorDocument& document, const TextSelection& selection, std::string kind) {
    auto after = document; document_edit_detail::NodeAllocator allocator(after);
    if (!document_edit_detail::toggle_callout(after.root.children, selection.anchor.container_id, selection.active.container_id, std::move(kind), allocator)) return std::nullopt;
    ++after.revision; return document_edit_detail::transaction(document, std::move(after), selection, selection, DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_toggle_list(const EditorDocument& document, const TextSelection& selection, document_edit_detail::ListStyle style) {
    auto after = document; document_edit_detail::NodeAllocator allocator(after);
    if (!document_edit_detail::toggle_list(after.root.children, selection.anchor.container_id, selection.active.container_id, style, allocator)) return std::nullopt;
    ++after.revision; return document_edit_detail::transaction(document, std::move(after), selection, selection, DocumentTransactionReason::Structure);
}
using ListStyle = document_edit_detail::ListStyle;

inline std::optional<DocumentTransaction> document_toggle_task_checkbox(const EditorDocument& document, const TextSelection& selection) {
    auto after = document;
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
    item->checked = !item->checked;
    const auto unchecked = item->marker.find(U"[ ]");
    const auto checked_lower = item->marker.find(U"[x]");
    const auto checked_upper = item->marker.find(U"[X]");
    if (item->checked && unchecked != std::u32string::npos) item->marker[unchecked + 1] = U'x';
    if (!item->checked && checked_lower != std::u32string::npos) item->marker[checked_lower + 1] = U' ';
    if (!item->checked && checked_upper != std::u32string::npos) item->marker[checked_upper + 1] = U' ';
    update.after = document_transaction_detail::payload_shell(*item);
    std::vector<DocumentOperation> operations;
    operations.emplace_back(std::move(update));
    ++after.revision;
    return make_recorded_document_transaction(
        std::move(after),
        std::move(operations),
        selection,
        selection,
        document.revision,
        DocumentTransactionReason::Structure);
}

inline BlockNode make_code_block(std::optional<std::string> language = std::nullopt) { BlockNode block; block.kind = BlockKind::CodeBlock; block.language = std::move(language); return block; }
inline BlockNode make_math_block() { BlockNode block; block.kind = BlockKind::MathBlock; block.math_delim = MathDelimiter::BlockDollar; return block; }
inline BlockNode make_toc_block() { BlockNode block; block.kind = BlockKind::Toc; return block; }

inline BlockNode make_table_block(const EditorDocument& document, std::size_t rows, std::size_t columns) {
    auto working = document; document_edit_detail::NodeAllocator allocator(working);
    BlockNode table; table.kind = BlockKind::Table; columns = (std::max)(columns, std::size_t{1}); table.table_aligns.assign(columns, TableAlignment::None);
    BlockNode header; header.id = allocator.allocate(); header.kind = BlockKind::TableRow; header.table_header_row = true;
    for (std::size_t column = 0; column < columns; ++column) {
        BlockNode cell; cell.id = allocator.allocate(); cell.kind = BlockKind::TableCell;
        cell.inline_content = document_edit_detail::make_inline(U"Header", working, allocator);
        header.children.push_back(std::move(cell));
    }
    table.children.push_back(std::move(header));
    for (std::size_t row_index = 0; row_index < rows; ++row_index) {
        BlockNode row; row.id = allocator.allocate(); row.kind = BlockKind::TableRow;
        for (std::size_t column = 0; column < columns; ++column) {
            BlockNode cell; cell.id = allocator.allocate(); cell.kind = BlockKind::TableCell;
            cell.inline_content = document_edit_detail::make_inline(U"Cell", working, allocator);
            row.children.push_back(std::move(cell));
        }
        table.children.push_back(std::move(row));
    }
    return table;
}

inline std::optional<DocumentTransaction> document_insert_atomic_block(const EditorDocument& document, const TextSelection& selection, BlockNode block) {
    if (!selection.is_caret()) return std::nullopt;
    auto after = document;
    document_edit_detail::NodeAllocator allocator(after);
    std::uint64_t block_maximum = 0;
    document_edit_detail::scan_block_ids(block, block_maximum);
    allocator.next = (std::max)(allocator.next, block_maximum + 1);
    document_edit_detail::assign_missing_ids(block, after, allocator);
    auto path = block_path(after.root, selection.active.container_id);
    if (!path || path->empty()) return std::nullopt;
    auto parent_path = *path;
    const auto index = parent_path.back();
    parent_path.pop_back();
    auto* parent = block_at_path(after.root, parent_path);
    if (!parent || index >= parent->children.size()) return std::nullopt;
    const auto inserted_id = block.id;
    DocumentTreeEdit insert;
    insert.kind = DocumentTreeEditKind::Insert;
    insert.parent_id = parent->id;
    insert.index = index + 1;
    insert.after = block;
    std::vector<DocumentOperation> operations;
    operations.emplace_back(std::move(insert));
    if (!insert_block(*parent, index + 1, std::move(block))) return std::nullopt;
    ++after.revision;
    return make_recorded_document_transaction(
        std::move(after),
        std::move(operations),
        selection,
        TextSelection::caret({inserted_id, 0, TextAffinity::Downstream}),
        document.revision,
        DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_insert_footnote(const EditorDocument& document, const TextSelection& selection, std::string label) {
    auto after = document; document_edit_detail::NodeAllocator allocator(after);
    BlockNode footnote; footnote.id = allocator.allocate(); footnote.kind = BlockKind::FootnoteDefinition; footnote.footnote_label = std::move(label); footnote.children.push_back(document_edit_detail::empty_paragraph(allocator, after));
    const auto target = TextPosition{footnote.children.front().id, 0, TextAffinity::Downstream};
    DocumentTreeEdit insert;
    insert.kind = DocumentTreeEditKind::Insert;
    insert.parent_id = after.root.id;
    insert.index = after.root.children.size();
    insert.after = footnote;
    std::vector<DocumentOperation> operations;
    operations.emplace_back(std::move(insert));
    after.root.children.push_back(std::move(footnote));
    ++after.revision;
    return make_recorded_document_transaction(
        std::move(after),
        std::move(operations),
        selection,
        TextSelection::caret(target),
        document.revision,
        DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_indent_list_item(const EditorDocument& document, const TextSelection& selection) {
    auto after = document;
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
    auto item = remove_block(*list, item_index);
    if (!item) return std::nullopt;
    auto& previous = list->children[item_index - 1];
    BlockNode* nested = nullptr;
    if (!previous.children.empty() && previous.children.back().kind == list->kind) nested = &previous.children.back();
    if (!nested) {
        BlockNode created;
        created.id = allocator.allocate();
        created.kind = list->kind;
        created.list_ordered = list->list_ordered;
        created.list_start = list->list_start;
        created.list_delimiter = list->list_delimiter;
        previous.children.push_back(std::move(created));
        nested = &previous.children.back();
    }
    nested->children.push_back(std::move(*item));
    ++after.revision;
    return document_edit_detail::transaction(document, std::move(after), selection, selection, DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_outdent_list_item(const EditorDocument& document, const TextSelection& selection) {
    auto after = document;
    auto path = block_path(after.root, selection.active.container_id);
    if (!path) return std::nullopt;
    while (!path->empty()) {
        const auto* node = block_at_path(after.root, *path);
        if (node && (node->kind == BlockKind::ListItem || node->kind == BlockKind::TaskListItem)) break;
        path->pop_back();
    }
    if (path->empty()) return std::nullopt;
    auto list_path = *path;
    const auto item_index = list_path.back();
    list_path.pop_back();
    auto* list = block_at_path(after.root, list_path);
    if (!list || (list->kind != BlockKind::List && list->kind != BlockKind::TaskList)) return std::nullopt;
    const auto list_id = list->id;
    auto item = remove_block(*list, item_index);
    if (!item) return std::nullopt;

    auto parent_item_path = list_path;
    if (!parent_item_path.empty()) parent_item_path.pop_back();
    auto* parent_item = block_at_path(after.root, parent_item_path);
    if (parent_item && (parent_item->kind == BlockKind::ListItem || parent_item->kind == BlockKind::TaskListItem)) {
        auto grand_list_path = parent_item_path;
        const auto parent_item_index = grand_list_path.back();
        grand_list_path.pop_back();
        auto* grand_list = block_at_path(after.root, grand_list_path);
        if (!grand_list) return std::nullopt;
        insert_block(*grand_list, parent_item_index + 1, std::move(*item));
        if (auto* nested = elmd::find_block(after.root, list_id); nested && nested->children.empty()) {
            auto* owner = find_parent_block(after.root, list_id);
            if (owner) {
                const auto nested_path = block_path(after.root, list_id);
                if (nested_path) remove_block(*owner, nested_path->back());
            }
        }
    } else {
        auto* list_parent = find_parent_block(after.root, list_id);
        auto list_location = block_path(after.root, list_id);
        if (!list_parent || !list_location) return std::nullopt;
        auto insertion = list_location->back() + 1;
        for (auto& child : item->children) insert_block(*list_parent, insertion++, std::move(child));
        if (auto* current_list = elmd::find_block(after.root, list_id); current_list && current_list->children.empty()) {
            remove_block(*list_parent, list_location->back());
        }
    }
    ++after.revision;
    return document_edit_detail::transaction(document, std::move(after), selection, selection, DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_edit_table(
    const EditorDocument& document,
    const TextSelection& selection,
    DocumentTableEdit edit,
    TableAlignment alignment = TableAlignment::None,
    std::size_t argument = 0) {
    auto after = document;
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
            table->children.insert(table->children.begin() + static_cast<std::ptrdiff_t>(row_index), make_row());
            break;
        case DocumentTableEdit::InsertRowBelow:
            table->children.insert(table->children.begin() + static_cast<std::ptrdiff_t>(row_index + 1), make_row());
            break;
        case DocumentTableEdit::InsertRowAt: {
            const auto index = (std::min)(argument, table->children.size());
            table->children.insert(table->children.begin() + static_cast<std::ptrdiff_t>(index), make_row());
            break;
        }
        case DocumentTableEdit::DeleteRow:
            if (row_index == 0 || table->children.size() <= 1) return std::nullopt;
            table->children.erase(table->children.begin() + static_cast<std::ptrdiff_t>(row_index));
            target_id = table->children[(std::min)(row_index - 1, table->children.size() - 1)].children[(std::min)(column, column_count - 1)].id;
            break;
        case DocumentTableEdit::MoveRowUp:
            if (row_index <= 1) return std::nullopt;
            std::swap(table->children[row_index], table->children[row_index - 1]);
            break;
        case DocumentTableEdit::MoveRowDown:
            if (row_index == 0 || row_index + 1 >= table->children.size()) return std::nullopt;
            std::swap(table->children[row_index], table->children[row_index + 1]);
            break;
        case DocumentTableEdit::MoveRowTo: {
            if (row_index == 0 || argument == 0 || argument >= table->children.size()) return std::nullopt;
            auto row = remove_block(*table, row_index);
            if (!row) return std::nullopt;
            insert_block(*table, argument, std::move(*row));
            break;
        }
        case DocumentTableEdit::InsertColumnLeft:
        case DocumentTableEdit::InsertColumnRight:
        case DocumentTableEdit::InsertColumnAt: {
            const auto index = edit == DocumentTableEdit::InsertColumnLeft ? column
                : edit == DocumentTableEdit::InsertColumnRight ? column + 1
                : (std::min)(argument, column_count);
            for (auto& row : table->children) row.children.insert(row.children.begin() + static_cast<std::ptrdiff_t>(index), make_cell());
            table->table_aligns.insert(table->table_aligns.begin() + static_cast<std::ptrdiff_t>((std::min)(index, table->table_aligns.size())), TableAlignment::None);
            break;
        }
        case DocumentTableEdit::DeleteColumn:
            if (column_count <= 1) return std::nullopt;
            for (auto& row : table->children) row.children.erase(row.children.begin() + static_cast<std::ptrdiff_t>(column));
            if (column < table->table_aligns.size()) table->table_aligns.erase(table->table_aligns.begin() + static_cast<std::ptrdiff_t>(column));
            target_id = table->children[row_index].children[(std::min)(column, column_count - 2)].id;
            break;
        case DocumentTableEdit::MoveColumnLeft:
            if (column == 0) return std::nullopt;
            for (auto& row : table->children) std::swap(row.children[column], row.children[column - 1]);
            if (column < table->table_aligns.size()) std::swap(table->table_aligns[column], table->table_aligns[column - 1]);
            break;
        case DocumentTableEdit::MoveColumnRight:
            if (column + 1 >= column_count) return std::nullopt;
            for (auto& row : table->children) std::swap(row.children[column], row.children[column + 1]);
            if (column + 1 < table->table_aligns.size()) std::swap(table->table_aligns[column], table->table_aligns[column + 1]);
            break;
        case DocumentTableEdit::MoveColumnTo: {
            if (argument >= column_count || argument == column) return std::nullopt;
            for (auto& row : table->children) {
                auto cell = remove_block(row, column);
                if (!cell) return std::nullopt;
                insert_block(row, argument, std::move(*cell));
            }
            auto value = table->table_aligns[column];
            table->table_aligns.erase(table->table_aligns.begin() + static_cast<std::ptrdiff_t>(column));
            table->table_aligns.insert(table->table_aligns.begin() + static_cast<std::ptrdiff_t>(argument), value);
            break;
        }
        case DocumentTableEdit::SetColumnAlignment:
            if (column >= table->table_aligns.size()) table->table_aligns.resize(column_count, TableAlignment::None);
            table->table_aligns[column] = alignment;
            break;
        case DocumentTableEdit::Normalize: {
            auto columns = (std::max)(std::size_t{1}, column_count);
            for (auto& row : table->children) {
                while (row.children.size() < columns) row.children.push_back(make_cell());
                if (row.children.size() > columns) row.children.resize(columns);
            }
            table->table_aligns.resize(columns, TableAlignment::None);
            break;
        }
    }
    if (changed) ++after.revision;
    const auto target = TextSelection::caret(TextPosition{target_id, 0, TextAffinity::Downstream});
    return document_edit_detail::transaction(document, std::move(after), selection, target, DocumentTransactionReason::Structure);
}


} // namespace elmd
