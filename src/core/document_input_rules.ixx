// elmd.core.document_input_rules — local block-input rules and structural
// Enter behavior. Rules inspect only the active block-local source and mutate
// the unified block tree; they never serialize or re-parse the document.
export module elmd.core.document_input_rules;
import std;
import elmd.core.ast;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.inline_document;
import elmd.core.text_edit;
import elmd.core.utf;
import elmd.core.document_edit_support;

export namespace elmd::document_input_rules {

namespace detail {

inline bool horizontal_space(char32_t value) {
    return value == U' ' || value == U'\t';
}

inline std::size_t leading_spaces(std::u32string_view value) {
    std::size_t count = 0;
    while (count < value.size() && value[count] == U' ' && count < 4) ++count;
    return count;
}

struct MarkerMatch {
    enum class Kind { Heading, Quote, BulletList, OrderedList, TaskList } kind;
    std::size_t length = 0;
    std::uint8_t heading_level = 0;
    std::uint64_t list_start = 1;
    char32_t list_delimiter = U'.';
    bool checked = false;
};

inline std::optional<MarkerMatch> recognize_marker(std::u32string_view source, std::size_t caret) {
    if (caret == 0 || caret > source.size()) return std::nullopt;
    auto prefix = source.substr(0, caret);
    const auto indent = leading_spaces(prefix);
    if (indent > 3) return std::nullopt;
    auto cursor = indent;

    if (cursor < prefix.size() && prefix[cursor] == U'#') {
        const auto marker_start = cursor;
        while (cursor < prefix.size() && prefix[cursor] == U'#') ++cursor;
        const auto level = cursor - marker_start;
        if (level >= 1 && level <= 6 && cursor + 1 == prefix.size() && horizontal_space(prefix[cursor])) {
            return MarkerMatch{MarkerMatch::Kind::Heading, caret, static_cast<std::uint8_t>(level)};
        }
    }

    cursor = indent;
    if (cursor + 2 == prefix.size() && prefix[cursor] == U'>' && horizontal_space(prefix[cursor + 1])) {
        return MarkerMatch{MarkerMatch::Kind::Quote, caret};
    }

    cursor = indent;
    if (cursor < prefix.size() && (prefix[cursor] == U'-' || prefix[cursor] == U'+' || prefix[cursor] == U'*')) {
        ++cursor;
        if (cursor >= prefix.size() || !horizontal_space(prefix[cursor])) return std::nullopt;
        while (cursor < prefix.size() && horizontal_space(prefix[cursor])) ++cursor;
        if (cursor + 4 == prefix.size() && prefix[cursor] == U'['
            && (prefix[cursor + 1] == U' ' || prefix[cursor + 1] == U'x' || prefix[cursor + 1] == U'X')
            && prefix[cursor + 2] == U']' && horizontal_space(prefix[cursor + 3])) {
            return MarkerMatch{
                MarkerMatch::Kind::TaskList,
                caret,
                0,
                1,
                U'.',
                prefix[cursor + 1] == U'x' || prefix[cursor + 1] == U'X'};
        }
        if (cursor == prefix.size()) return MarkerMatch{MarkerMatch::Kind::BulletList, caret};
        return std::nullopt;
    }

    cursor = indent;
    const auto number_start = cursor;
    std::uint64_t number = 0;
    while (cursor < prefix.size() && prefix[cursor] >= U'0' && prefix[cursor] <= U'9'
        && cursor - number_start < 9) {
        number = number * 10 + static_cast<std::uint64_t>(prefix[cursor] - U'0');
        ++cursor;
    }
    if (cursor > number_start && cursor - number_start <= 9 && cursor + 2 == prefix.size()
        && (prefix[cursor] == U'.' || prefix[cursor] == U')') && horizontal_space(prefix[cursor + 1])) {
        return MarkerMatch{MarkerMatch::Kind::OrderedList, caret, 0, number, prefix[cursor]};
    }
    return std::nullopt;
}

inline std::u32string trim_horizontal(std::u32string_view value) {
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && horizontal_space(value[begin])) ++begin;
    while (end > begin && horizontal_space(value[end - 1])) --end;
    return std::u32string(value.substr(begin, end - begin));
}

struct FenceMatch {
    char32_t marker = U'`';
    std::size_t count = 0;
    std::u32string info;
    std::u32string exact;
};

inline std::optional<FenceMatch> recognize_fence(std::u32string_view source) {
    const auto indent = leading_spaces(source);
    if (indent > 3 || indent >= source.size()) return std::nullopt;
    const auto marker = source[indent];
    if (marker != U'`' && marker != U'~') return std::nullopt;
    auto cursor = indent;
    while (cursor < source.size() && source[cursor] == marker) ++cursor;
    const auto count = cursor - indent;
    if (count < 3) return std::nullopt;
    auto info = trim_horizontal(source.substr(cursor));
    if (marker == U'`' && info.find(U'`') != std::u32string::npos) return std::nullopt;
    return FenceMatch{marker, count, std::move(info), std::u32string(source)};
}

inline std::optional<std::pair<char32_t, std::size_t>> opening_fence(const BlockNode& block) {
    const auto& marker = block.opening_marker;
    const auto indent = leading_spaces(marker);
    if (indent > 3 || indent >= marker.size()) return std::nullopt;
    const auto value = marker[indent];
    if (value != U'`' && value != U'~') return std::nullopt;
    auto cursor = indent;
    while (cursor < marker.size() && marker[cursor] == value) ++cursor;
    if (cursor - indent < 3) return std::nullopt;
    return std::pair{value, cursor - indent};
}

inline bool closing_fence_line(std::u32string_view line, char32_t marker, std::size_t minimum) {
    const auto indent = leading_spaces(line);
    if (indent > 3 || indent >= line.size()) return false;
    auto cursor = indent;
    while (cursor < line.size() && line[cursor] == marker) ++cursor;
    if (cursor - indent < minimum) return false;
    while (cursor < line.size() && horizontal_space(line[cursor])) ++cursor;
    return cursor == line.size();
}

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

inline std::optional<document_edit_detail::RecordedBlockEdit> replace_paragraph_with_container(
    EditorDocument& document,
    const BlockPath& path,
    const MarkerMatch& match,
    TextPosition& target,
    document_edit_detail::NodeAllocator& allocator) {
    if (path.empty()) return std::nullopt;
    auto parent_path = path;
    const auto index = parent_path.back();
    parent_path.pop_back();
    auto* parent = block_at_path(document.root, parent_path);
    if (!parent || index >= parent->children.size()) return std::nullopt;
    auto& source_block = parent->children[index];
    if (source_block.kind != BlockKind::Paragraph) return std::nullopt;
    document_edit_detail::RecordedBlockEdit result;
    result.target = target;
    const auto parent_id = parent->id;
    const auto exact_marker = source_block.inline_content.source.substr(0, match.length);
    auto marker_edit = document_edit_detail::edit_inline(
        document, source_block.id, {0, match.length}, {}, allocator);
    if (!marker_edit) return std::nullopt;
    document_edit_detail::append_source_operation(result.operations, std::move(*marker_edit));
    target = {source_block.id, target.source_offset - match.length, TextAffinity::Downstream};
    result.target = target;

    if (match.kind == MarkerMatch::Kind::Heading) {
        DocumentTreeEdit update;
        update.kind = DocumentTreeEditKind::UpdatePayload;
        update.before = document_transaction_detail::payload_shell(source_block);
        source_block.kind = BlockKind::Heading;
        source_block.level = match.heading_level;
        source_block.opening_marker = exact_marker;
        source_block.closing_marker.clear();
        source_block.slug.clear();
        update.after = document_transaction_detail::payload_shell(source_block);
        result.operations.emplace_back(std::move(update));
        return result;
    }

    if (match.kind == MarkerMatch::Kind::Quote) {
        BlockNode quote;
        quote.id = allocator.allocate();
        quote.kind = BlockKind::BlockQuote;
        quote.opening_marker = exact_marker;
        const auto quote_id = quote.id;
        DocumentTreeEdit insert;
        insert.kind = DocumentTreeEditKind::Insert;
        insert.parent_id = parent_id;
        insert.index = index;
        insert.after = quote;
        result.operations.emplace_back(std::move(insert));
        parent->children.insert(
            parent->children.begin() + static_cast<std::ptrdiff_t>(index),
            std::move(quote));

        DocumentTreeEdit move;
        move.kind = DocumentTreeEditKind::Move;
        move.parent_id = parent_id;
        move.index = index + 1;
        move.other_parent_id = quote_id;
        move.other_index = 0;
        auto content = remove_block(*parent, index + 1);
        auto* inserted_quote = find_block(document.root, quote_id);
        if (!content || !inserted_quote || !insert_block(*inserted_quote, 0, std::move(*content))) {
            return std::nullopt;
        }
        result.operations.emplace_back(std::move(move));
        return result;
    }

    BlockNode list;
    list.id = allocator.allocate();
    list.kind = match.kind == MarkerMatch::Kind::TaskList ? BlockKind::TaskList : BlockKind::List;
    list.list_ordered = match.kind == MarkerMatch::Kind::OrderedList;
    list.list_start = list.list_ordered ? match.list_start : 1;
    list.list_delimiter = match.list_delimiter;
    BlockNode item;
    item.id = allocator.allocate();
    item.kind = match.kind == MarkerMatch::Kind::TaskList ? BlockKind::TaskListItem : BlockKind::ListItem;
    item.marker = exact_marker;
    item.checked = match.checked;
    list.children.push_back(std::move(item));
    const auto list_id = list.id;
    const auto item_id = list.children.front().id;
    DocumentTreeEdit insert;
    insert.kind = DocumentTreeEditKind::Insert;
    insert.parent_id = parent_id;
    insert.index = index;
    insert.after = list;
    result.operations.emplace_back(std::move(insert));
    parent->children.insert(
        parent->children.begin() + static_cast<std::ptrdiff_t>(index),
        std::move(list));

    DocumentTreeEdit move_content;
    move_content.kind = DocumentTreeEditKind::Move;
    move_content.parent_id = parent_id;
    move_content.index = index + 1;
    move_content.other_parent_id = item_id;
    move_content.other_index = 0;
    auto content = remove_block(*parent, index + 1);
    auto* inserted_item = find_block(document.root, item_id);
    if (!content || !inserted_item || !insert_block(*inserted_item, 0, std::move(*content))) {
        return std::nullopt;
    }
    result.operations.emplace_back(std::move(move_content));

    auto* inserted_list = find_block(document.root, list_id);
    parent = find_block(document.root, parent_id);
    if (!inserted_list || !parent) return std::nullopt;
    std::size_t effective_index = index;
    if (effective_index > 0 && compatible_lists(parent->children[effective_index - 1], *inserted_list)) {
        auto* previous = &parent->children[effective_index - 1];
        const auto target_index = previous->children.size();
        DocumentTreeEdit move_item;
        move_item.kind = DocumentTreeEditKind::Move;
        move_item.parent_id = list_id;
        move_item.index = 0;
        move_item.other_parent_id = previous->id;
        move_item.other_index = target_index;
        auto moved_item = remove_block(*inserted_list, 0);
        if (!moved_item || !insert_block(*previous, target_index, std::move(*moved_item))) {
            return std::nullopt;
        }
        result.operations.emplace_back(std::move(move_item));

        DocumentTreeEdit remove_list;
        remove_list.kind = DocumentTreeEditKind::Remove;
        remove_list.parent_id = parent_id;
        remove_list.index = effective_index;
        remove_list.before = parent->children[effective_index];
        result.operations.emplace_back(std::move(remove_list));
        parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(effective_index));
        --effective_index;
        inserted_list = &parent->children[effective_index];
    }

    if (effective_index + 1 < parent->children.size()
        && compatible_lists(*inserted_list, parent->children[effective_index + 1])) {
        const auto next_id = parent->children[effective_index + 1].id;
        auto* next = &parent->children[effective_index + 1];
        while (!next->children.empty()) {
            DocumentTreeEdit move_item;
            move_item.kind = DocumentTreeEditKind::Move;
            move_item.parent_id = next_id;
            move_item.index = 0;
            move_item.other_parent_id = inserted_list->id;
            move_item.other_index = inserted_list->children.size();
            auto moved_item = remove_block(*next, 0);
            if (!moved_item || !insert_block(
                    *inserted_list, move_item.other_index, std::move(*moved_item))) {
                return std::nullopt;
            }
            result.operations.emplace_back(std::move(move_item));
        }
        DocumentTreeEdit remove_list;
        remove_list.kind = DocumentTreeEditKind::Remove;
        remove_list.parent_id = parent_id;
        remove_list.index = effective_index + 1;
        remove_list.before = parent->children[effective_index + 1];
        result.operations.emplace_back(std::move(remove_list));
        parent->children.erase(
            parent->children.begin() + static_cast<std::ptrdiff_t>(effective_index + 1));
    }
    return result;
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
    document_edit_detail::NodeAllocator& allocator) {
    auto* leaf = find_block(document.root, target.container_id);
    if (!leaf || leaf->kind != BlockKind::Paragraph || target.source_offset != 4
        || leaf->inline_content.source.size() < 4) return std::nullopt;
    const auto prefix = leaf->inline_content.source.substr(0, 4);
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

inline std::optional<document_edit_detail::RecordedBlockEdit> open_fenced_block(
    EditorDocument& document,
    TextPosition position,
    document_edit_detail::NodeAllocator& allocator) {
    auto path = block_path(document.root, position.container_id);
    if (!path || path->empty()) return std::nullopt;
    auto* block = block_at_path(document.root, *path);
    if (!block || block->kind != BlockKind::Paragraph
        || position.source_offset != block->inline_content.source.size()) return std::nullopt;
    auto fence = recognize_fence(block->inline_content.source);
    if (!fence) return std::nullopt;

    document_edit_detail::RecordedBlockEdit result;
    result.target = TextPosition{position.container_id, 0, TextAffinity::Downstream};
    auto source_edit = document_edit_detail::edit_inline(
        document,
        block->id,
        {0, block->inline_content.source.size()},
        {},
        allocator);
    if (!source_edit) return std::nullopt;
    document_edit_detail::append_source_operation(result.operations, std::move(*source_edit));
    block = find_block(document.root, position.container_id);
    if (!block) return std::nullopt;

    DocumentTreeEdit update;
    update.kind = DocumentTreeEditKind::UpdatePayload;
    update.before = document_transaction_detail::payload_shell(*block);
    BlockNode replacement;
    replacement.id = block->id;
    replacement.opening_marker = fence->exact + U"\n";
    auto info = cps_to_utf8(fence->info);
    if (info == "math" && document.dialect.math.fenced_math) {
        replacement.kind = BlockKind::MathBlock;
        replacement.math_delim = MathDelimiter::FencedMath;
    } else {
        replacement.kind = BlockKind::CodeBlock;
        if (!info.empty()) replacement.language = std::move(info);
    }
    *block = std::move(replacement);
    update.after = document_transaction_detail::payload_shell(*block);
    result.operations.emplace_back(std::move(update));
    return result;
}

inline std::optional<document_edit_detail::RecordedBlockEdit> close_fenced_code(
    EditorDocument& document,
    TextPosition position,
    document_edit_detail::NodeAllocator& allocator) {
    auto path = block_path(document.root, position.container_id);
    if (!path || path->empty()) return std::nullopt;
    auto* block = block_at_path(document.root, *path);
    if (!block || block->kind != BlockKind::CodeBlock || position.source_offset != block->code_text.size()) return std::nullopt;
    auto fence = opening_fence(*block);
    if (!fence) return std::nullopt;
    auto line_start = block->code_text.empty() ? 0 : block->code_text.rfind(U'\n');
    line_start = line_start == std::u32string::npos ? 0 : line_start + 1;
    auto line = std::u32string_view(block->code_text).substr(line_start);
    if (!closing_fence_line(line, fence->first, fence->second)) return std::nullopt;

    document_edit_detail::RecordedBlockEdit result;
    const auto closing_marker = std::u32string(line);
    const auto erase_start = line_start > 0 ? line_start - 1 : line_start;
    auto source_edit = document_edit_detail::edit_block_source(
        document,
        block->id,
        {erase_start, block->code_text.size()},
        {},
        allocator);
    if (!source_edit) return std::nullopt;
    document_edit_detail::append_source_operation(result.operations, std::move(*source_edit));
    block = find_block(document.root, position.container_id);
    if (!block) return std::nullopt;

    DocumentTreeEdit update;
    update.kind = DocumentTreeEditKind::UpdatePayload;
    update.before = document_transaction_detail::payload_shell(*block);
    block->closing_marker = closing_marker;
    update.after = document_transaction_detail::payload_shell(*block);
    result.operations.emplace_back(std::move(update));
    auto parent_path = *path;
    const auto index = parent_path.back();
    parent_path.pop_back();
    auto* parent = block_at_path(document.root, parent_path);
    if (!parent) return std::nullopt;
    auto paragraph = document_edit_detail::empty_paragraph(allocator, document);
    const auto target = TextPosition{paragraph.id, 0, TextAffinity::Downstream};
    DocumentTreeEdit insert;
    insert.kind = DocumentTreeEditKind::Insert;
    insert.parent_id = parent->id;
    insert.index = index + 1;
    insert.after = paragraph;
    result.operations.emplace_back(std::move(insert));
    insert_block(*parent, index + 1, std::move(paragraph));
    result.target = target;
    return result;
}

inline std::optional<document_edit_detail::RecordedBlockEdit> remove_heading_prefix(
    EditorDocument& document,
    TextPosition position) {
    auto* block = find_block(document.root, position.container_id);
    if (!block || block->kind != BlockKind::Heading) return std::nullopt;
    document_edit_detail::RecordedBlockEdit result;
    result.target = TextPosition{block->id, 0, TextAffinity::Downstream};
    DocumentTreeEdit update;
    update.kind = DocumentTreeEditKind::UpdatePayload;
    update.before = document_transaction_detail::payload_shell(*block);
    block->kind = BlockKind::Paragraph;
    block->level = 0;
    block->slug.clear();
    block->opening_marker.clear();
    block->closing_marker.clear();
    update.after = document_transaction_detail::payload_shell(*block);
    result.operations.emplace_back(std::move(update));
    return result;
}

inline std::optional<document_edit_detail::RecordedBlockEdit> remove_callout_prefix(
    EditorDocument& document,
    TextPosition position,
    document_edit_detail::NodeAllocator& allocator) {
    auto path = block_path(document.root, position.container_id);
    if (!path || path->empty()) return std::nullopt;
    auto* selected = block_at_path(document.root, *path);
    if (!selected) return std::nullopt;

    if (selected->kind == BlockKind::Callout && selected->callout_title) {
        auto parent_path = *path;
        const auto callout_index = parent_path.back();
        parent_path.pop_back();
        auto* parent = block_at_path(document.root, parent_path);
        if (!parent || callout_index >= parent->children.size()) return std::nullopt;
        document_edit_detail::RecordedBlockEdit result;
        result.target = TextPosition{position.container_id, 0, TextAffinity::Downstream};
        const auto parent_id = parent->id;
        const auto callout_id = selected->id;
        BlockNode quote;
        quote.id = allocator.allocate();
        quote.kind = BlockKind::BlockQuote;
        const auto quote_id = quote.id;
        DocumentTreeEdit insert_quote;
        insert_quote.kind = DocumentTreeEditKind::Insert;
        insert_quote.parent_id = parent_id;
        insert_quote.index = callout_index;
        insert_quote.after = quote;
        result.operations.emplace_back(std::move(insert_quote));
        parent->children.insert(
            parent->children.begin() + static_cast<std::ptrdiff_t>(callout_index),
            std::move(quote));

        DocumentTreeEdit move_callout;
        move_callout.kind = DocumentTreeEditKind::Move;
        move_callout.parent_id = parent_id;
        move_callout.index = callout_index + 1;
        move_callout.other_parent_id = quote_id;
        move_callout.other_index = 0;
        auto moved_callout = remove_block(*parent, callout_index + 1);
        auto* inserted_quote = find_block(document.root, quote_id);
        if (!moved_callout || !inserted_quote
            || !insert_block(*inserted_quote, 0, std::move(*moved_callout))) return std::nullopt;
        result.operations.emplace_back(std::move(move_callout));

        auto* callout = find_block(document.root, callout_id);
        if (!callout || !callout->callout_title) return std::nullopt;
        if (!callout->children.empty()
            && document_edit_detail::text_block(callout->children.front().kind)) {
            const auto offset = callout->callout_title->source.size();
            auto appended = U"\n" + callout->children.front().inline_content.source;
            auto edit = document_edit_detail::edit_inline(
                document,
                callout_id,
                {offset, offset},
                std::move(appended),
                allocator);
            if (!edit) return std::nullopt;
            document_edit_detail::append_source_operation(result.operations, std::move(*edit));
            callout = find_block(document.root, callout_id);
            if (!callout || callout->children.empty()) return std::nullopt;
            DocumentTreeEdit remove_body;
            remove_body.kind = DocumentTreeEditKind::Remove;
            remove_body.parent_id = callout_id;
            remove_body.index = 0;
            remove_body.before = callout->children.front();
            result.operations.emplace_back(std::move(remove_body));
            callout->children.erase(callout->children.begin());
        }

        callout = find_block(document.root, callout_id);
        inserted_quote = find_block(document.root, quote_id);
        if (!callout || !inserted_quote) return std::nullopt;
        std::size_t quote_child_index = 1;
        while (!callout->children.empty()) {
            DocumentTreeEdit move_child;
            move_child.kind = DocumentTreeEditKind::Move;
            move_child.parent_id = callout_id;
            move_child.index = 0;
            move_child.other_parent_id = quote_id;
            move_child.other_index = quote_child_index;
            auto child = remove_block(*callout, 0);
            if (!child || !insert_block(*inserted_quote, quote_child_index, std::move(*child))) {
                return std::nullopt;
            }
            result.operations.emplace_back(std::move(move_child));
            ++quote_child_index;
        }

        DocumentTreeEdit update;
        update.kind = DocumentTreeEditKind::UpdatePayload;
        update.before = document_transaction_detail::payload_shell(*callout);
        auto title = std::move(*callout->callout_title);
        *callout = BlockNode{};
        callout->id = callout_id;
        callout->kind = BlockKind::Paragraph;
        callout->inline_content = std::move(title);
        update.after = document_transaction_detail::payload_shell(*callout);
        result.operations.emplace_back(std::move(update));
        return result;
    }

    if (path->size() < 2) return std::nullopt;
    auto callout_path = *path;
    const auto child_index = callout_path.back();
    callout_path.pop_back();
    auto* callout = block_at_path(document.root, callout_path);
    if (!callout || callout->kind != BlockKind::Callout || callout->callout_title
        || child_index != 0) return std::nullopt;

    document_edit_detail::RecordedBlockEdit result;
    result.target = TextPosition{position.container_id, 0, TextAffinity::Downstream};
    DocumentTreeEdit update;
    update.kind = DocumentTreeEditKind::UpdatePayload;
    update.before = document_transaction_detail::payload_shell(*callout);
    auto children = std::move(callout->children);
    const auto id = callout->id;
    *callout = BlockNode{};
    callout->id = id;
    callout->kind = BlockKind::BlockQuote;
    callout->children = std::move(children);
    update.after = document_transaction_detail::payload_shell(*callout);
    result.operations.emplace_back(std::move(update));
    return result;
}

inline BlockNode container_shell_from(const BlockNode& source, NodeId id) {
    auto result = source;
    result.id = id;
    result.children.clear();
    return result;
}

inline std::optional<document_edit_detail::RecordedBlockEdit> remove_quote_prefix(
    EditorDocument& document,
    TextPosition position) {
    auto path = block_path(document.root, position.container_id);
    if (!path || path->size() < 2) return std::nullopt;

    auto quote_path = *path;
    const auto child_index = quote_path.back();
    quote_path.pop_back();
    const auto* quote = block_at_path(document.root, quote_path);
    // Only the first direct child owns the quote's opening structural boundary.
    // At the start of a later child, Backspace must fall through to the uniform
    // sibling join path and stay inside the quote.
    if (!quote || quote->kind != BlockKind::BlockQuote || child_index != 0
        || child_index >= quote->children.size()) {
        return std::nullopt;
    }

    auto parent_path = quote_path;
    const auto quote_index = parent_path.back();
    parent_path.pop_back();
    auto* parent = block_at_path(document.root, parent_path);
    if (!parent || quote_index >= parent->children.size()) return std::nullopt;

    document_edit_detail::RecordedBlockEdit result;
    result.target = TextPosition{position.container_id, 0, TextAffinity::Downstream};
    const auto parent_id = parent->id;
    const auto quote_id = parent->children[quote_index].id;
    DocumentTreeEdit move;
    move.kind = DocumentTreeEditKind::Move;
    move.parent_id = quote_id;
    move.index = 0;
    move.other_parent_id = parent_id;
    move.other_index = quote_index;
    auto* current_quote = find_block(document.root, quote_id);
    auto selected = current_quote ? remove_block(*current_quote, 0) : std::nullopt;
    if (!selected || !insert_block(*parent, quote_index, std::move(*selected))) return std::nullopt;
    result.operations.emplace_back(std::move(move));

    current_quote = find_block(document.root, quote_id);
    if (current_quote && current_quote->children.empty()) {
        parent = find_block(document.root, parent_id);
        if (!parent || quote_index + 1 >= parent->children.size()) return std::nullopt;
        DocumentTreeEdit remove_quote;
        remove_quote.kind = DocumentTreeEditKind::Remove;
        remove_quote.parent_id = parent_id;
        remove_quote.index = quote_index + 1;
        remove_quote.before = parent->children[quote_index + 1];
        result.operations.emplace_back(std::move(remove_quote));
        parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(quote_index + 1));
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

} // namespace detail

inline std::optional<document_edit_detail::RecordedBlockEdit> apply_after_text_insert(
    EditorDocument& document,
    TextPosition& target,
    document_edit_detail::NodeAllocator& allocator) {
    if (auto closed = detail::close_fenced_code(document, target, allocator)) {
        target = closed->target;
        return closed;
    }
    if (auto task = detail::upgrade_task_item(document, target, allocator)) {
        target = task->target;
        return task;
    }
    auto path = block_path(document.root, target.container_id);
    auto* block = path ? block_at_path(document.root, *path) : nullptr;
    if (!block || block->kind != BlockKind::Paragraph) return std::nullopt;
    auto marker = detail::recognize_marker(block->inline_content.source, target.source_offset);
    if (!marker) return std::nullopt;
    return detail::replace_paragraph_with_container(document, *path, *marker, target, allocator);
}

inline std::optional<document_edit_detail::RecordedBlockEdit> handle_enter(
    EditorDocument& document,
    TextPosition position,
    document_edit_detail::NodeAllocator& allocator) {
    if (auto opened = detail::open_fenced_block(document, position, allocator)) {
        return opened;
    }
    if (auto closed = detail::close_fenced_code(document, position, allocator)) {
        return closed;
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
