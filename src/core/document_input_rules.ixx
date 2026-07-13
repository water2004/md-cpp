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

inline std::size_t coalesce_list(BlockNode& parent, std::size_t index) {
    if (index >= parent.children.size()) return index;
    if (index > 0 && compatible_lists(parent.children[index - 1], parent.children[index])) {
        auto& previous = parent.children[index - 1];
        auto& current = parent.children[index];
        previous.children.insert(previous.children.end(),
            std::make_move_iterator(current.children.begin()),
            std::make_move_iterator(current.children.end()));
        parent.children.erase(parent.children.begin() + static_cast<std::ptrdiff_t>(index));
        --index;
    }
    if (index + 1 < parent.children.size() && compatible_lists(parent.children[index], parent.children[index + 1])) {
        auto& current = parent.children[index];
        auto& next = parent.children[index + 1];
        current.children.insert(current.children.end(),
            std::make_move_iterator(next.children.begin()),
            std::make_move_iterator(next.children.end()));
        parent.children.erase(parent.children.begin() + static_cast<std::ptrdiff_t>(index + 1));
    }
    return index;
}

inline bool replace_paragraph_with_container(
    EditorDocument& document,
    const BlockPath& path,
    const MarkerMatch& match,
    TextPosition& target,
    document_edit_detail::NodeAllocator& allocator) {
    if (path.empty()) return false;
    auto parent_path = path;
    const auto index = parent_path.back();
    parent_path.pop_back();
    auto* parent = block_at_path(document.root, parent_path);
    if (!parent || index >= parent->children.size()) return false;
    auto& source_block = parent->children[index];
    if (source_block.kind != BlockKind::Paragraph) return false;
    const auto exact_marker = source_block.inline_content.source.substr(0, match.length);
    if (!document_edit_detail::edit_inline(document, source_block.id, {0, match.length}, {}, allocator)) return false;
    target = {source_block.id, target.source_offset - match.length, TextAffinity::Downstream};

    if (match.kind == MarkerMatch::Kind::Heading) {
        source_block.kind = BlockKind::Heading;
        source_block.level = match.heading_level;
        source_block.opening_marker = exact_marker;
        source_block.closing_marker.clear();
        source_block.slug.clear();
        return true;
    }

    auto content = std::move(source_block);
    if (match.kind == MarkerMatch::Kind::Quote) {
        BlockNode quote;
        quote.id = allocator.allocate();
        quote.kind = BlockKind::BlockQuote;
        quote.opening_marker = exact_marker;
        quote.children.push_back(std::move(content));
        parent->children[index] = std::move(quote);
        return true;
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
    item.children.push_back(std::move(content));
    list.children.push_back(std::move(item));
    parent->children[index] = std::move(list);
    coalesce_list(*parent, index);
    return true;
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

inline bool upgrade_task_item(
    EditorDocument& document,
    TextPosition& target,
    document_edit_detail::NodeAllocator& allocator) {
    auto* leaf = find_block(document.root, target.container_id);
    if (!leaf || leaf->kind != BlockKind::Paragraph || target.source_offset != 4
        || leaf->inline_content.source.size() < 4) return false;
    const auto prefix = leaf->inline_content.source.substr(0, 4);
    if (prefix[0] != U'[' || (prefix[1] != U' ' && prefix[1] != U'x' && prefix[1] != U'X')
        || prefix[2] != U']' || !horizontal_space(prefix[3])) return false;
    auto context = direct_list_context(document, target.container_id);
    if (!context) return false;
    auto* list = block_at_path(document.root, context->list_path);
    if (!list || list->kind != BlockKind::List || list->list_ordered
        || context->item_index >= list->children.size()) return false;
    if (!document_edit_detail::edit_inline(document, target.container_id, {0, 4}, {}, allocator)) return false;
    target.source_offset = 0;
    const auto checked = prefix[1] == U'x' || prefix[1] == U'X';

    auto parent_path = context->list_path;
    if (parent_path.empty()) return false;
    const auto list_index = parent_path.back();
    parent_path.pop_back();
    auto* parent = block_at_path(document.root, parent_path);
    if (!parent || list_index >= parent->children.size()) return false;
    auto original = std::move(parent->children[list_index]);
    const auto item_count = original.children.size();
    if (context->item_index >= item_count) return false;

    BlockVec replacement;
    if (context->item_index > 0) {
        auto before = list_shell_from(original, original.id);
        before.children.insert(before.children.end(),
            std::make_move_iterator(original.children.begin()),
            std::make_move_iterator(original.children.begin() + static_cast<std::ptrdiff_t>(context->item_index)));
        replacement.push_back(std::move(before));
    }

    BlockNode tasks;
    tasks.id = context->item_index == 0 ? original.id : allocator.allocate();
    tasks.kind = BlockKind::TaskList;
    auto item = std::move(original.children[context->item_index]);
    item.kind = BlockKind::TaskListItem;
    item.checked = checked;
    item.marker += prefix;
    tasks.children.push_back(std::move(item));
    replacement.push_back(std::move(tasks));

    if (context->item_index + 1 < item_count) {
        auto after = list_shell_from(original, allocator.allocate());
        after.list_start = original.list_start + context->item_index + 1;
        after.children.insert(after.children.end(),
            std::make_move_iterator(original.children.begin() + static_cast<std::ptrdiff_t>(context->item_index + 1)),
            std::make_move_iterator(original.children.end()));
        replacement.push_back(std::move(after));
    }
    parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(list_index));
    parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(list_index),
        std::make_move_iterator(replacement.begin()), std::make_move_iterator(replacement.end()));
    return true;
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

inline std::optional<TextPosition> exit_empty_list_item(
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
    const auto target = TextPosition{paragraph.id, 0, TextAffinity::Downstream};
    auto original = std::move(parent->children[list_index]);
    BlockVec replacement;
    if (context->item_index > 0) {
        auto before = list_shell_from(original, original.id);
        before.children.insert(before.children.end(),
            std::make_move_iterator(original.children.begin()),
            std::make_move_iterator(original.children.begin() + static_cast<std::ptrdiff_t>(context->item_index)));
        replacement.push_back(std::move(before));
    }
    replacement.push_back(std::move(paragraph));
    if (context->item_index + 1 < original.children.size()) {
        auto after = list_shell_from(original, context->item_index == 0 ? original.id : allocator.allocate());
        after.list_start = original.list_start + context->item_index + 1;
        after.children.insert(after.children.end(),
            std::make_move_iterator(original.children.begin() + static_cast<std::ptrdiff_t>(context->item_index + 1)),
            std::make_move_iterator(original.children.end()));
        replacement.push_back(std::move(after));
    }
    parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(list_index));
    parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(list_index),
        std::make_move_iterator(replacement.begin()), std::make_move_iterator(replacement.end()));
    return target;
}

inline std::optional<TextPosition> split_list_item(
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

    if (context->child_index == 0 && offset == 0) {
        auto inserted = empty_item(*list, item, context->item_index == 0 ? 0 : context->item_index - 1, document, allocator);
        const auto target = TextPosition{inserted.children.front().id, 0, TextAffinity::Downstream};
        if (list->list_ordered) inserted.marker = utf8_to_cps(std::to_string(list->list_start + context->item_index))
            + std::u32string(1, list->list_delimiter) + U" ";
        list->children.insert(list->children.begin() + static_cast<std::ptrdiff_t>(context->item_index), std::move(inserted));
        return target;
    }

    BlockNode next;
    next.id = allocator.allocate();
    next.kind = item.kind;
    next.checked = false;
    next.marker = next_item_marker(*list, item, context->item_index);

    if (offset == leaf.inline_content.source.size() && context->child_index + 1 == item.children.size()) {
        auto paragraph = document_edit_detail::empty_paragraph(allocator, document);
        const auto target = TextPosition{paragraph.id, 0, TextAffinity::Downstream};
        next.children.push_back(std::move(paragraph));
        list->children.insert(list->children.begin() + static_cast<std::ptrdiff_t>(context->item_index + 1), std::move(next));
        return target;
    }

    TextPosition target;
    if (offset == 0) {
        target = {leaf.id, 0, TextAffinity::Downstream};
        next.children.insert(next.children.end(),
            std::make_move_iterator(item.children.begin() + static_cast<std::ptrdiff_t>(context->child_index)),
            std::make_move_iterator(item.children.end()));
        item.children.erase(item.children.begin() + static_cast<std::ptrdiff_t>(context->child_index), item.children.end());
    } else {
        auto right_source = leaf.inline_content.source.substr(offset);
        leaf.inline_content.source.erase(offset);
        document_edit_detail::reparse(leaf.inline_content, document, allocator);
        BlockNode right;
        right.id = allocator.allocate();
        right.kind = BlockKind::Paragraph;
        right.inline_content = document_edit_detail::make_inline(std::move(right_source), document, allocator);
        target = {right.id, 0, TextAffinity::Downstream};
        next.children.push_back(std::move(right));
        next.children.insert(next.children.end(),
            std::make_move_iterator(item.children.begin() + static_cast<std::ptrdiff_t>(context->child_index + 1)),
            std::make_move_iterator(item.children.end()));
        item.children.erase(item.children.begin() + static_cast<std::ptrdiff_t>(context->child_index + 1), item.children.end());
    }
    list->children.insert(list->children.begin() + static_cast<std::ptrdiff_t>(context->item_index + 1), std::move(next));
    return target;
}

inline std::optional<TextPosition> open_fenced_block(
    EditorDocument& document,
    TextPosition position,
    document_edit_detail::NodeAllocator&) {
    auto path = block_path(document.root, position.container_id);
    if (!path || path->empty()) return std::nullopt;
    auto* block = block_at_path(document.root, *path);
    if (!block || block->kind != BlockKind::Paragraph
        || position.source_offset != block->inline_content.source.size()) return std::nullopt;
    auto fence = recognize_fence(block->inline_content.source);
    if (!fence) return std::nullopt;

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
    return TextPosition{position.container_id, 0, TextAffinity::Downstream};
}

inline std::optional<TextPosition> close_fenced_code(
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

    block->closing_marker = std::u32string(line);
    block->code_text.erase(line_start > 0 ? line_start - 1 : line_start);
    auto parent_path = *path;
    const auto index = parent_path.back();
    parent_path.pop_back();
    auto* parent = block_at_path(document.root, parent_path);
    if (!parent) return std::nullopt;
    auto paragraph = document_edit_detail::empty_paragraph(allocator, document);
    const auto target = TextPosition{paragraph.id, 0, TextAffinity::Downstream};
    insert_block(*parent, index + 1, std::move(paragraph));
    return target;
}

} // namespace detail

inline bool apply_after_text_insert(
    EditorDocument& document,
    TextPosition& target,
    document_edit_detail::NodeAllocator& allocator) {
    if (auto closed = detail::close_fenced_code(document, target, allocator)) {
        target = *closed;
        return true;
    }
    if (detail::upgrade_task_item(document, target, allocator)) return true;
    auto path = block_path(document.root, target.container_id);
    auto* block = path ? block_at_path(document.root, *path) : nullptr;
    if (!block || block->kind != BlockKind::Paragraph) return false;
    auto marker = detail::recognize_marker(block->inline_content.source, target.source_offset);
    return marker && detail::replace_paragraph_with_container(document, *path, *marker, target, allocator);
}

inline std::optional<TextPosition> handle_enter(
    EditorDocument& document,
    TextPosition position,
    document_edit_detail::NodeAllocator& allocator) {
    if (auto opened = detail::open_fenced_block(document, position, allocator)) return opened;
    if (auto closed = detail::close_fenced_code(document, position, allocator)) return closed;
    if (auto exited = detail::exit_empty_list_item(document, position, allocator)) return exited;
    return detail::split_list_item(document, position, allocator);
}

} // namespace elmd::document_input_rules
