// elmd.core.document_clipboard — structural Markdown clipboard fragments.
//
// Copy slices the unified block tree in document order, retaining every
// selected structural ancestor while truncating only the two boundary source
// owners. Paste lives in this module as well so clipboard parsing and semantic
// tree splicing remain separate from ordinary character input.
export module elmd.core.document_clipboard;
import std;
import elmd.core.ast;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.document_edit_support;
import elmd.core.document_source_edit;
import elmd.core.document_text;
import elmd.core.inline_cst;
import elmd.core.parser;
import elmd.core.serializer;
import elmd.core.text_edit;

export namespace elmd {

namespace document_clipboard_detail {

struct OrderedSelection {
    TextPosition start;
    TextPosition end;
    std::size_t start_index = 0;
    std::size_t end_index = 0;
    std::vector<DocumentTextFragment> fragments;
    std::unordered_map<std::uint64_t, std::size_t> index_by_id;
};

inline std::optional<OrderedSelection> order_selection(
    const EditorDocument& document,
    const TextSelection& selection) {
    OrderedSelection ordered;
    ordered.fragments = document_text_fragments(document);
    for (std::size_t index = 0; index < ordered.fragments.size(); ++index) {
        ordered.index_by_id.emplace(ordered.fragments[index].container_id.v, index);
    }
    const auto anchor = ordered.index_by_id.find(selection.anchor.container_id.v);
    const auto active = ordered.index_by_id.find(selection.active.container_id.v);
    if (anchor == ordered.index_by_id.end() || active == ordered.index_by_id.end()) {
        return std::nullopt;
    }
    ordered.start = selection.anchor;
    ordered.end = selection.active;
    ordered.start_index = anchor->second;
    ordered.end_index = active->second;
    if (ordered.end_index < ordered.start_index
        || (ordered.start_index == ordered.end_index
            && ordered.end.source_offset < ordered.start.source_offset)) {
        std::swap(ordered.start, ordered.end);
        std::swap(ordered.start_index, ordered.end_index);
    }
    ordered.start.source_offset = (std::min)(
        ordered.start.source_offset,
        ordered.fragments[ordered.start_index].text.size());
    ordered.end.source_offset = (std::min)(
        ordered.end.source_offset,
        ordered.fragments[ordered.end_index].text.size());
    return ordered;
}

inline SourceRange selected_range(
    const OrderedSelection& selection,
    std::size_t index) {
    if (index < selection.start_index || index > selection.end_index) return {};
    const auto length = selection.fragments[index].text.size();
    const auto start = index == selection.start_index
        ? selection.start.source_offset
        : 0;
    const auto end = index == selection.end_index
        ? selection.end.source_offset
        : length;
    return {(std::min)(start, length), (std::min)(end, length)};
}

inline std::optional<std::pair<std::size_t, std::size_t>> descendant_fragment_span(
    const BlockNode& block,
    const OrderedSelection& selection) {
    std::optional<std::pair<std::size_t, std::size_t>> span;
    const auto self = selection.index_by_id.find(block.id.v);
    if (self != selection.index_by_id.end()) span = {self->second, self->second};
    for (const auto& child : block.children) {
        auto child_span = descendant_fragment_span(child, selection);
        if (!child_span) continue;
        if (!span) span = child_span;
        else {
            span->first = (std::min)(span->first, child_span->first);
            span->second = (std::max)(span->second, child_span->second);
        }
    }
    return span;
}

inline bool intersects_selection(
    const std::pair<std::size_t, std::size_t>& span,
    const OrderedSelection& selection) {
    return span.first <= selection.end_index && selection.start_index <= span.second;
}

inline void truncate_owner(
    BlockNode& copy,
    SourceRange range,
    std::size_t full_length) {
    if (auto* document = editable_inline_document(copy)) {
        document->source = document->source.substr(range.start, range.length());
        // A partial heading selection does not include its structural marker:
        // copied text therefore becomes a paragraph. A fully selected heading
        // retains the heading block and its exact original marker spelling.
        if (copy.kind == BlockKind::Heading
            && (range.start != 0 || range.end != full_length)) {
            copy.kind = BlockKind::Paragraph;
            copy.level = 0;
            copy.opening_marker.clear();
            copy.closing_marker.clear();
        }
        return;
    }
    if (auto* source = editable_raw_block_source(copy)) {
        *source = source->substr(range.start, range.length());
    }
}

inline std::optional<BlockNode> slice_block(
    const BlockNode& block,
    const OrderedSelection& selection) {
    const auto span = descendant_fragment_span(block, selection);
    if (!span || !intersects_selection(*span, selection)) return std::nullopt;

    // MarkText's table selection semantics copy a table as a unit whenever a
    // cross-cell/cross-block range intersects it. A same-cell selection takes
    // the exact source substring through the same-owner fast path below.
    if (block.kind == BlockKind::Table) return block;

    BlockNode copy = block;
    copy.children.clear();
    bool has_selected_owner = false;
    const auto self = selection.index_by_id.find(block.id.v);
    if (self != selection.index_by_id.end()
        && self->second >= selection.start_index
        && self->second <= selection.end_index) {
        const auto range = selected_range(selection, self->second);
        if (!range.empty()) {
            has_selected_owner = true;
            truncate_owner(copy, range, selection.fragments[self->second].text.size());
        }
    }

    for (const auto& child : block.children) {
        if (auto selected = slice_block(child, selection)) {
            copy.children.push_back(std::move(*selected));
        }
    }

    if (!has_selected_owner && copy.children.empty()) return std::nullopt;
    if (block.kind == BlockKind::Callout && !has_selected_owner) {
        copy.callout_title.reset();
    }
    return copy;
}

inline std::u32string normalize_newlines(std::u32string_view text) {
    std::u32string result;
    result.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != U'\r') {
            result.push_back(text[index]);
            continue;
        }
        if (index + 1 < text.size() && text[index + 1] == U'\n') ++index;
        result.push_back(U'\n');
    }
    return result;
}

inline void assign_fresh_ids(
    InlineCstNodes& nodes,
    document_edit_detail::NodeAllocator& allocator) {
    for (auto& node : nodes) {
        node.id = allocator.allocate();
        assign_fresh_ids(node.children, allocator);
    }
}

inline void assign_fresh_ids(
    InlineDocument& document,
    document_edit_detail::NodeAllocator& allocator) {
    assign_fresh_ids(document.tree.nodes, allocator);
    for (auto& token : document.tree.tokens) token.id = allocator.allocate();
}

inline void assign_fresh_ids(
    BlockNode& block,
    document_edit_detail::NodeAllocator& allocator) {
    block.id = allocator.allocate();
    if (auto* inline_document = editable_inline_document(block)) {
        assign_fresh_ids(*inline_document, allocator);
    }
    for (auto& child : block.children) assign_fresh_ids(child, allocator);
}

inline bool record_insert(
    EditorDocument& document,
    NodeId parent_id,
    std::size_t index,
    BlockNode node,
    std::vector<DocumentOperation>& operations) {
    auto* parent = find_block(document.root, parent_id);
    if (!parent || index > parent->children.size()) return false;
    DocumentTreeEdit insert;
    insert.kind = DocumentTreeEditKind::Insert;
    insert.parent_id = parent_id;
    insert.index = index;
    insert.after = node;
    operations.emplace_back(std::move(insert));
    return insert_block(*parent, index, std::move(node));
}

inline std::optional<TextPosition> record_source_insert(
    EditorDocument& document,
    NodeId owner_id,
    std::size_t offset,
    std::u32string source,
    document_edit_detail::NodeAllocator& allocator,
    std::vector<DocumentOperation>& operations) {
    auto* block = find_block(document.root, owner_id);
    if (!block) return std::nullopt;
    const auto length = document_edit_detail::local_position_length(*block);
    if (!length) return std::nullopt;
    offset = (std::min)(offset, *length);
    auto edit = document_edit_detail::edit_block_source(
        document,
        owner_id,
        {offset, offset},
        std::move(source),
        allocator);
    if (!edit) return std::nullopt;
    const auto inserted = edit->forward.replacement.size();
    document_edit_detail::append_source_operation(operations, std::move(*edit));
    return TextPosition{owner_id, offset + inserted, TextAffinity::Downstream};
}

inline NodeId last_sewable_owner(const BlockNode& block) {
    for (auto child = block.children.rbegin(); child != block.children.rend(); ++child) {
        if (auto id = last_sewable_owner(*child); id.v != 0) return id;
    }
    return block.kind == BlockKind::Paragraph || block.kind == BlockKind::Heading
        ? block.id
        : NodeId{};
}

inline std::optional<TextPosition> sew_tail_and_remove(
    EditorDocument& document,
    NodeId tail_id,
    NodeId target_id,
    document_edit_detail::NodeAllocator& allocator,
    std::vector<DocumentOperation>& operations) {
    auto* tail = find_block(document.root, tail_id);
    auto* target = find_block(document.root, target_id);
    if (!tail || !target) return std::nullopt;
    const auto* tail_inline = editable_inline_document(*tail);
    auto* target_inline = editable_inline_document(*target);
    if (!tail_inline || !target_inline) return std::nullopt;
    const auto seam = target_inline->source.size();
    const auto tail_source = tail_inline->source;
    if (!tail_source.empty()) {
        auto appended = record_source_insert(
            document,
            target_id,
            seam,
            tail_source,
            allocator,
            operations);
        if (!appended) return std::nullopt;
    }
    auto remove = document_edit_detail::remove_node_recorded(document, tail_id);
    if (!remove) return std::nullopt;
    operations.emplace_back(std::move(*remove));
    return TextPosition{target_id, seam, TextAffinity::Downstream};
}

inline bool direct_inline_anchor(const BlockNode& block) {
    return block.kind == BlockKind::Paragraph
        || block.kind == BlockKind::Heading
        || (block.kind == BlockKind::Callout && block.callout_title.has_value());
}

inline bool atx_heading(const BlockNode& block) {
    return block.kind == BlockKind::Heading
        && !block.opening_marker.empty()
        && block.opening_marker.find(U'#') != std::u32string::npos;
}

inline bool offset_in_link_destination(
    const InlineCstNodes& nodes,
    std::u32string_view source,
    std::size_t offset) {
    for (const auto& node : nodes) {
        if (node.kind == InlineCstKind::Link && node.status == ParseStatus::Complete
            && node.delim.closing && node.delim.closing->valid_for(source.size())) {
            auto cursor = node.delim.content.end;
            if (cursor < source.size() && source[cursor] == U']') ++cursor;
            if (cursor < source.size() && source[cursor] == U'(') {
                ++cursor;
                while (cursor < source.size()
                    && (source[cursor] == U' ' || source[cursor] == U'\t')) ++cursor;
                const auto angle = cursor < source.size() && source[cursor] == U'<';
                if (angle) ++cursor;
                const auto destination_start = cursor;
                std::size_t destination_end = cursor;
                if (angle) {
                    while (destination_end < node.range.end
                        && source[destination_end] != U'>') ++destination_end;
                } else {
                    std::size_t depth = 0;
                    while (destination_end < node.range.end) {
                        const auto value = source[destination_end];
                        if (value == U'\\' && destination_end + 1 < node.range.end) {
                            destination_end += 2;
                            continue;
                        }
                        if (value == U'(') {
                            ++depth;
                        } else if (value == U')') {
                            if (depth == 0) break;
                            --depth;
                        } else if ((value == U' ' || value == U'\t') && depth == 0) {
                            break;
                        }
                        ++destination_end;
                    }
                }
                if (destination_start <= offset && offset <= destination_end) return true;
            }
        }
        if (offset_in_link_destination(node.children, source, offset)) return true;
    }
    return false;
}

inline std::optional<std::u32string> whole_pasted_link_destination(const BlockVec& blocks) {
    if (blocks.size() != 1 || blocks.front().kind != BlockKind::Paragraph) {
        return std::nullopt;
    }
    const auto& document = blocks.front().inline_content;
    if (document.tree.nodes.size() != 1) return std::nullopt;
    const auto& node = document.tree.nodes.front();
    if (node.kind != InlineCstKind::Link || node.status != ParseStatus::Complete
        || node.range.start != 0 || node.range.end != document.source.size()) {
        return std::nullopt;
    }
    return utf8_to_cps(node.href);
}

inline void split_soft_lines_for_atx_heading(
    BlockVec& blocks,
    const BlockNode& anchor) {
    if (!atx_heading(anchor) || blocks.size() != 1
        || blocks.front().kind != BlockKind::Paragraph) return;
    auto& source = blocks.front().inline_content.source;
    const auto newline = source.find(U'\n');
    if (newline == std::u32string::npos) return;
    auto trailing = blocks.front();
    trailing.inline_content.source = source.substr(newline + 1);
    trailing.separator_before.reset();
    source.erase(newline);
    blocks.push_back(std::move(trailing));
}

struct DirectListPasteContext {
    NodeId list_id{};
    NodeId item_id{};
    std::size_t item_index = 0;
};

inline char32_t unordered_marker(const BlockNode& list) {
    if (list.children.empty()) return U'-';
    for (const auto value : list.children.front().marker) {
        if (value == U'-' || value == U'+' || value == U'*') return value;
    }
    return U'-';
}

inline bool paste_compatible_lists(const BlockNode& current, const BlockNode& pasted) {
    if (current.kind != pasted.kind
        || (current.kind != BlockKind::List && current.kind != BlockKind::TaskList)) {
        return false;
    }
    if (current.list_ordered != pasted.list_ordered) return false;
    if (current.list_ordered) return current.list_delimiter == pasted.list_delimiter;
    return unordered_marker(current) == unordered_marker(pasted);
}

inline std::optional<DirectListPasteContext> direct_list_paste_context(
    const EditorDocument& document,
    NodeId anchor_id,
    const BlockNode& pasted) {
    auto path = block_path(document.root, anchor_id);
    if (!path || path->size() < 3) return std::nullopt;
    auto item_path = *path;
    item_path.pop_back();
    const auto* item = block_at_path(document.root, item_path);
    if (!item || (item->kind != BlockKind::ListItem
        && item->kind != BlockKind::TaskListItem)) return std::nullopt;
    auto list_path = item_path;
    const auto item_index = list_path.back();
    list_path.pop_back();
    const auto* list = block_at_path(document.root, list_path);
    if (!list || !paste_compatible_lists(*list, pasted)) return std::nullopt;
    return DirectListPasteContext{list->id, item->id, item_index};
}

inline std::optional<TextPosition> paste_matching_list(
    EditorDocument& document,
    TextPosition position,
    BlockVec blocks,
    DirectListPasteContext context,
    document_edit_detail::NodeAllocator& allocator,
    std::vector<DocumentOperation>& operations) {
    if (blocks.empty()) return std::nullopt;
    auto pasted_list = std::move(blocks.front());
    blocks.erase(blocks.begin());

    auto split = document_edit_detail::split_direct(
        document,
        position.container_id,
        position.source_offset,
        allocator);
    if (!split) return std::nullopt;
    operations.insert(
        operations.end(),
        std::make_move_iterator(split->operations.begin()),
        std::make_move_iterator(split->operations.end()));
    const auto tail_id = split->target.container_id;
    const auto head_id = position.container_id;
    NodeId seam_owner = head_id;

    auto* current_list = find_block(document.root, context.list_id);
    auto* current_item = find_block(document.root, context.item_id);
    auto* head = find_block(document.root, head_id);
    if (!current_list || !current_item || !head) return std::nullopt;
    const auto can_fold = current_list->kind != BlockKind::TaskList
        && !pasted_list.children.empty()
        && !pasted_list.children.front().children.empty()
        && pasted_list.children.front().children.front().kind == BlockKind::Paragraph;

    std::size_t first_unconsumed_item = 0;
    if (can_fold) {
        auto& first_item = pasted_list.children.front();
        auto first_paragraph = std::move(first_item.children.front());
        first_item.children.erase(first_item.children.begin());
        const auto* head_inline = editable_inline_document(*head);
        if (!head_inline) return std::nullopt;
        if (!first_paragraph.inline_content.source.empty()) {
            auto merged = record_source_insert(
                document,
                head_id,
                head_inline->source.size(),
                first_paragraph.inline_content.source,
                allocator,
                operations);
            if (!merged) return std::nullopt;
        }
        for (auto& child : first_item.children) {
            auto tail_path = block_path(document.root, tail_id);
            if (!tail_path || tail_path->empty()) return std::nullopt;
            auto parent_path = *tail_path;
            const auto index = parent_path.back();
            parent_path.pop_back();
            auto* parent = block_at_path(document.root, parent_path);
            if (!parent || parent->id != context.item_id) return std::nullopt;
            const auto child_id = child.id;
            if (!record_insert(document, parent->id, index, std::move(child), operations)) {
                return std::nullopt;
            }
            if (const auto* inserted = find_block(document.root, child_id)) {
                const auto candidate = last_sewable_owner(*inserted);
                if (candidate.v != 0) seam_owner = candidate;
            }
        }
        first_unconsumed_item = 1;
    }

    std::size_t destination = context.item_index + 1;
    for (std::size_t index = first_unconsumed_item;
         index < pasted_list.children.size();
         ++index) {
        auto item = std::move(pasted_list.children[index]);
        const auto item_id = item.id;
        if (!record_insert(
                document,
                context.list_id,
                destination++,
                std::move(item),
                operations)) return std::nullopt;
        if (const auto* inserted = find_block(document.root, item_id)) {
            const auto candidate = last_sewable_owner(*inserted);
            if (candidate.v != 0) seam_owner = candidate;
        }
    }

    // Blocks following the pasted list belong beside the enclosing list, not
    // inside the current item. This preserves the clipboard fragment's own
    // top-level structure at the semantic insertion level.
    std::size_t after_list = 1;
    for (auto& block : blocks) {
        auto list_path = block_path(document.root, context.list_id);
        if (!list_path || list_path->empty()) return std::nullopt;
        auto parent_path = *list_path;
        const auto list_index = parent_path.back();
        parent_path.pop_back();
        auto* parent = block_at_path(document.root, parent_path);
        if (!parent) return std::nullopt;
        const auto block_id = block.id;
        if (!record_insert(
                document,
                parent->id,
                list_index + after_list++,
                std::move(block),
                operations)) return std::nullopt;
        if (const auto* inserted = find_block(document.root, block_id)) {
            seam_owner = last_sewable_owner(*inserted);
        }
    }

    if (seam_owner.v == 0) return split->target;
    return sew_tail_and_remove(
        document,
        tail_id,
        seam_owner,
        allocator,
        operations);
}

inline std::optional<TextPosition> paste_literal(
    EditorDocument& document,
    TextPosition position,
    std::u32string normalized,
    document_edit_detail::NodeAllocator& allocator,
    std::vector<DocumentOperation>& operations) {
    auto* block = find_block(document.root, position.container_id);
    if (!block) return std::nullopt;
    if (block->kind == BlockKind::TableCell) {
        std::size_t first = 0;
        std::size_t last = normalized.size();
        auto whitespace = [](char32_t value) {
            return value == U' ' || value == U'\t' || value == U'\n';
        };
        while (first < last && whitespace(normalized[first])) ++first;
        while (last > first && whitespace(normalized[last - 1])) --last;
        normalized = normalized.substr(first, last - first);
        std::u32string cell_source;
        for (const auto value : normalized) {
            if (value == U'\n') cell_source += U"<br>";
            else cell_source.push_back(value);
        }
        normalized = std::move(cell_source);
    }
    return record_source_insert(
        document,
        position.container_id,
        position.source_offset,
        std::move(normalized),
        allocator,
        operations);
}

inline std::optional<TextPosition> paste_parsed_blocks(
    EditorDocument& document,
    TextPosition position,
    BlockVec blocks,
    document_edit_detail::NodeAllocator& allocator,
    std::vector<DocumentOperation>& operations) {
    auto* anchor = find_block(document.root, position.container_id);
    if (!anchor) return std::nullopt;
    split_soft_lines_for_atx_heading(blocks, *anchor);

    if (const auto* inline_document = editable_inline_document(*anchor);
        inline_document
        && offset_in_link_destination(
            inline_document->tree.nodes,
            inline_document->source,
            position.source_offset)) {
        if (auto destination = whole_pasted_link_destination(blocks)) {
            return record_source_insert(
                document,
                anchor->id,
                position.source_offset,
                std::move(*destination),
                allocator,
                operations);
        }
    }

    // One paragraph is an inline source splice. The parser still classified
    // the clipboard first, but no block structure needs to be manufactured.
    if (direct_inline_anchor(*anchor) && blocks.size() == 1
        && blocks.front().kind == BlockKind::Paragraph) {
        return record_source_insert(
            document,
            anchor->id,
            position.source_offset,
            blocks.front().inline_content.source,
            allocator,
            operations);
    }
    if (direct_inline_anchor(*anchor) && blocks.size() == 1
        && blocks.front().kind == BlockKind::Heading) {
        const auto* source = editable_inline_document(*anchor);
        if (source && !source->source.empty()) {
            return record_source_insert(
                document,
                anchor->id,
                position.source_offset,
                blocks.front().inline_content.source,
                allocator,
                operations);
        }
    }

    for (auto& block : blocks) assign_fresh_ids(block, allocator);

    if (direct_inline_anchor(*anchor) && !blocks.empty()) {
        if (auto list_context = direct_list_paste_context(
                document,
                anchor->id,
                blocks.front())) {
            return paste_matching_list(
                document,
                position,
                std::move(blocks),
                *list_context,
                allocator,
                operations);
        }
    }

    if (!direct_inline_anchor(*anchor)) {
        auto path = block_path(document.root, anchor->id);
        if (!path || path->empty()) return std::nullopt;
        auto parent_path = *path;
        auto index = parent_path.back();
        parent_path.pop_back();
        auto* parent = block_at_path(document.root, parent_path);
        if (!parent) return std::nullopt;
        if (position.source_offset > 0) ++index;
        TextPosition target = position;
        for (auto& block : blocks) {
            const auto inserted_id = block.id;
            if (!record_insert(document, parent->id, index++, std::move(block), operations)) {
                return std::nullopt;
            }
            if (const auto* inserted = find_block(document.root, inserted_id)) {
                target = document_edit_detail::last_editable_position(*inserted).value_or(target);
            }
        }
        return target;
    }

    const auto head_id = anchor->id;
    auto split = document_edit_detail::split_direct(
        document,
        head_id,
        position.source_offset,
        allocator);
    if (!split) return std::nullopt;
    const auto tail_id = split->target.container_id;
    operations.insert(
        operations.end(),
        std::make_move_iterator(split->operations.begin()),
        std::make_move_iterator(split->operations.end()));

    anchor = find_block(document.root, head_id);
    if (!anchor) return std::nullopt;
    const auto* head_inline = editable_inline_document(*anchor);
    const auto head_was_empty = !head_inline || head_inline->source.empty();
    const auto removable_empty_head = anchor->kind == BlockKind::Paragraph
        || anchor->kind == BlockKind::Heading;
    const auto head_separator = anchor->separator_before;
    bool merged_first = false;
    if (head_inline && !blocks.empty() && blocks.front().kind == BlockKind::Paragraph) {
        const auto pasted = blocks.front().inline_content.source;
        if (!pasted.empty()) {
            auto merged = record_source_insert(
                document,
                head_id,
                head_inline->source.size(),
                pasted,
                allocator,
                operations);
            if (!merged) return std::nullopt;
        }
        blocks.erase(blocks.begin());
        merged_first = true;
    } else if (head_inline && !blocks.empty() && blocks.front().kind == BlockKind::Heading
               && !head_was_empty) {
        const auto pasted = blocks.front().inline_content.source;
        auto merged = record_source_insert(
            document,
            head_id,
            head_inline->source.size(),
            pasted,
            allocator,
            operations);
        if (!merged) return std::nullopt;
        blocks.erase(blocks.begin());
        merged_first = true;
    }

    if (!blocks.empty()) {
        blocks.front().separator_before = !merged_first && head_was_empty
            ? head_separator
            : std::nullopt;
    }
    std::vector<NodeId> inserted_ids;
    inserted_ids.reserve(blocks.size());
    for (auto& block : blocks) {
        auto tail_path = block_path(document.root, tail_id);
        if (!tail_path || tail_path->empty()) return std::nullopt;
        auto parent_path = *tail_path;
        const auto index = parent_path.back();
        parent_path.pop_back();
        auto* parent = block_at_path(document.root, parent_path);
        if (!parent) return std::nullopt;
        inserted_ids.push_back(block.id);
        if (!record_insert(document, parent->id, index, std::move(block), operations)) {
            return std::nullopt;
        }
    }

    NodeId seam_owner = head_inline ? head_id : NodeId{};
    if (!inserted_ids.empty()) {
        const auto* last = find_block(document.root, inserted_ids.back());
        if (last) seam_owner = last_sewable_owner(*last);
    }
    TextPosition target = split->target;
    if (seam_owner.v != 0) {
        auto sewn = sew_tail_and_remove(
            document,
            tail_id,
            seam_owner,
            allocator,
            operations);
        if (!sewn) return std::nullopt;
        target = *sewn;
    } else {
        const auto* tail = find_block(document.root, tail_id);
        const auto* tail_inline = tail ? editable_inline_document(*tail) : nullptr;
        if (tail_inline && tail_inline->source.empty()) {
            auto remove = document_edit_detail::remove_node_recorded(document, tail_id);
            if (!remove) return std::nullopt;
            operations.emplace_back(std::move(*remove));
            if (!inserted_ids.empty()) {
                if (const auto* last = find_block(document.root, inserted_ids.back())) {
                    target = document_edit_detail::last_editable_position(*last).value_or(target);
                }
            }
        }
    }

    // NEWLINE semantics: a structural paste at an empty paragraph/heading
    // replaces that wrapper. Transfer its preceding separator to the first
    // inserted block before recording the removal.
    if (!merged_first && head_was_empty && removable_empty_head
        && !inserted_ids.empty()) {
        auto remove = document_edit_detail::remove_node_recorded(document, head_id);
        if (!remove) return std::nullopt;
        operations.emplace_back(std::move(*remove));
    }
    return target;
}

} // namespace document_clipboard_detail

// Return the exact source substring for a one-owner selection. Cross-owner
// selections are represented by a recursively pruned block tree, preserving
// list/quote/task/callout/footnote structure at arbitrary nesting depth.
inline std::optional<std::u32string> document_selected_markdown(
    const EditorDocument& document,
    const TextSelection& selection) {
    auto ordered = document_clipboard_detail::order_selection(document, selection);
    if (!ordered) return std::nullopt;
    if (ordered->start_index == ordered->end_index) {
        const auto range = document_clipboard_detail::selected_range(
            *ordered,
            ordered->start_index);
        return ordered->fragments[ordered->start_index].text.substr(
            range.start,
            range.length());
    }

    BlockVec selected;
    selected.reserve(document.root.children.size());
    for (const auto& block : document.root.children) {
        if (auto copy = document_clipboard_detail::slice_block(block, *ordered)) {
            selected.push_back(std::move(*copy));
        }
    }
    // Parser-owned separator metadata can split a physical blank line between
    // the preceding block's trailing range and the following block. Once a
    // boundary block is truncated that ownership is no longer valid. Let the
    // fragment serializer choose an unambiguous block separator for every
    // selected top-level sibling instead of accidentally turning the next
    // paragraph into a lazy list continuation.
    for (auto& block : selected) block.separator_before.reset();
    return serialize_markdown_fragment(selected);
}

inline std::optional<DocumentTransaction> document_paste_text(
    EditorDocument& document,
    const TextSelection& selection,
    std::u32string_view text) {
    if (text.empty()) return std::nullopt;
    auto normalized = document_clipboard_detail::normalize_newlines(text);
    const auto revision_before = document.revision;
    auto& working = document;
    auto current = selection;
    std::vector<DocumentOperation> operations;
    if (!current.is_caret()) {
        auto deletion = document_delete_selection(working, current);
        if (!deletion) return std::nullopt;
        operations.insert(
            operations.end(),
            std::make_move_iterator(deletion->operations.begin()),
            std::make_move_iterator(deletion->operations.end()));
        current = deletion->selection_after;
    }

    document_edit_detail::NodeAllocator allocator(working);
    auto* anchor = find_block(working.root, current.active.container_id);
    if (!anchor) return std::nullopt;
    std::optional<TextPosition> target;
    if (anchor->kind == BlockKind::CodeBlock
        || anchor->kind == BlockKind::MathBlock
        || anchor->kind == BlockKind::TableCell) {
        target = document_clipboard_detail::paste_literal(
            working,
            current.active,
            std::move(normalized),
            allocator,
            operations);
    } else {
        auto parsed = parse_block_fragment(normalized, working.dialect);
        if (parsed.blocks.empty()) {
            target = document_clipboard_detail::paste_literal(
                working,
                current.active,
                std::move(normalized),
                allocator,
                operations);
        } else {
            target = document_clipboard_detail::paste_parsed_blocks(
                working,
                current.active,
                std::move(parsed.blocks),
                allocator,
                operations);
        }
    }
    if (!target || operations.empty()) return std::nullopt;
    working.revision = revision_before + 1;
    return make_recorded_document_transaction(
        std::move(operations),
        selection,
        TextSelection::caret(*target),
        revision_before,
        working.revision,
        DocumentTransactionReason::Paste);
}

} // namespace elmd
