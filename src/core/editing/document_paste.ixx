// elmd.core.document_paste — semantic Markdown parsing and block-tree splicing.
export module elmd.core.document_paste;
import std;
import elmd.core.ast;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.document_edit_support;
import elmd.core.document_source_edit;
import elmd.core.document_text;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.parser;
import elmd.core.text_edit;
import elmd.core.utf;

export namespace elmd {

namespace document_paste_detail {

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
        || block.kind == BlockKind::CalloutTitle;
}

inline bool atx_heading(const BlockNode& block) {
    return block.kind == BlockKind::Heading
        && !block.text_special().opening_marker.empty()
        && block.text_special().opening_marker.find(U'#') != std::u32string::npos;
}

inline bool offset_in_link_destination(
    const InlineCstNodes& nodes,
    std::u32string_view source,
    std::size_t offset) {
    for (const auto& node : nodes) {
        if (node.kind == InlineCstKind::Link && node.status == ParseStatus::Complete
            && node.delimiter_ranges().closing && node.delimiter_ranges().closing->valid_for(source.size())) {
            auto cursor = node.delimiter_ranges().content.end;
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
    return utf8_to_cps(node.semantics().href);
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
    for (const auto value : list.children.front().item_special().marker) {
        if (value == U'-' || value == U'+' || value == U'*') return value;
    }
    return U'-';
}

inline bool paste_compatible_lists(const BlockNode& current, const BlockNode& pasted) {
    if (current.kind != pasted.kind
        || (current.kind != BlockKind::List && current.kind != BlockKind::TaskList)) {
        return false;
    }
    if (current.list_special().ordered != pasted.list_special().ordered) return false;
    if (current.list_special().ordered) {
        return current.list_special().delimiter == pasted.list_special().delimiter;
    }
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
    const auto split_removes_head = anchor->kind == BlockKind::CalloutTitle
        && position.source_offset == 0;
    const auto original_head_separator = anchor->separator_before;
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
    if (!anchor && !split_removes_head) return std::nullopt;
    const auto* head_inline = anchor ? editable_inline_document(*anchor) : nullptr;
    const auto head_was_empty = !head_inline || head_inline->source.empty();
    const auto removable_empty_head = anchor
        && (anchor->kind == BlockKind::Paragraph || anchor->kind == BlockKind::Heading);
    const auto head_separator = anchor ? anchor->separator_before : original_head_separator;
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

} // namespace document_paste_detail

inline std::optional<DocumentTransaction> document_paste_text(
    EditorDocument& document,
    const TextSelection& selection,
    std::u32string_view text) {
    if (text.empty()) return std::nullopt;
    auto normalized = document_paste_detail::normalize_newlines(text);
    const auto revision_before = document.revision;
    auto& working = document;
    auto current = selection;
    std::vector<DocumentOperation> operations;
    document_edit_detail::MutationRollback rollback(working, operations, revision_before);
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
        target = document_paste_detail::paste_literal(
            working,
            current.active,
            std::move(normalized),
            allocator,
            operations);
    } else {
        auto parsed = parse_block_fragment(normalized, working.dialect);
        if (parsed.blocks.empty()) {
            target = document_paste_detail::paste_literal(
                working,
                current.active,
                std::move(normalized),
                allocator,
                operations);
        } else {
            target = document_paste_detail::paste_parsed_blocks(
                working,
                current.active,
                std::move(parsed.blocks),
                allocator,
                operations);
        }
    }
    if (!target || operations.empty()) return std::nullopt;
    working.revision = revision_before + 1;
    rollback.commit();
    return make_recorded_document_transaction(
        std::move(operations),
        selection,
        TextSelection::caret(*target),
        revision_before,
        working.revision,
        DocumentTransactionReason::Paste);
}

} // namespace elmd
