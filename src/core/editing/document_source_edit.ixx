export module folia.core.document_source_edit;
import std;
import folia.core.ast;
import folia.core.block_tree;
import folia.core.document;
import folia.core.document_text;
import folia.core.inline_cst;
import folia.core.inline_document;
import folia.core.inline_parser;
import folia.core.inline_source_edit;
import folia.core.text_edit;
import folia.core.utf;
import folia.core.document_edit_support;
import folia.core.document_block_reparse;
import folia.core.document_input_rules;

export namespace folia {

inline void normalize_document(EditorDocument& document) {
    document_edit_detail::NodeAllocator allocator(document);
    if (document.root.id.v == 0) document.root.id = allocator.allocate();
    document.root.kind = BlockKind::Document;
    if (document.root.children.empty()) document.root.children.push_back(document_edit_detail::empty_paragraph(allocator, document));
    document_edit_detail::normalize_blocks(document.root.children, document, allocator);
}

inline std::vector<DocumentInvariantError> validate_document(const EditorDocument& document) {
    std::vector<DocumentInvariantError> errors;
    std::unordered_set<std::uint64_t> ids;
    if (document.root.id.v == 0) errors.push_back({document.root.id, "document root has no id"});
    else ids.insert(document.root.id.v);
    if (document.root.children.empty()) errors.push_back({{}, "document has no blocks"});
    document_edit_detail::validate_blocks(
        document.root.children,
        BlockKind::Document,
        ids,
        errors);
    const auto maximum_id = ids.empty()
        ? std::uint64_t{0}
        : *std::ranges::max_element(ids);
    if (document.next_node_id == 0 || document.next_node_id <= maximum_id) {
        errors.push_back({
            {},
            "document node id cursor does not follow every owned node id"});
    }
    return errors;
}

inline std::optional<DocumentTransaction> document_delete_selection(EditorDocument&, const TextSelection&);

inline DocumentTransaction source_transaction_with_block_reparse(
    EditorDocument& after,
    AppliedSourceEdit edit,
    TextSelection selection_before,
    TextPosition target,
    std::uint64_t revision_before,
    DocumentTransactionReason reason) {
    std::vector<DocumentOperation> operations;
    const auto reparse_edit = edit;
    document_edit_detail::append_source_operation(operations, std::move(edit));
    document_edit_detail::MutationRollback rollback(after, operations, revision_before);
    document_edit_detail::NodeAllocator allocator(after);
    if (auto reclassified = document_edit_detail::reparse_edited_direct_block(
            after, target, allocator, &reparse_edit)) {
        target = reclassified->target;
        operations.insert(
            operations.end(),
            std::make_move_iterator(reclassified->operations.begin()),
            std::make_move_iterator(reclassified->operations.end()));
    }
    rollback.commit();
    return make_recorded_document_transaction(
        std::move(operations),
        selection_before,
        TextSelection::caret(target),
        revision_before,
        after.revision,
        reason);
}

inline std::optional<DocumentTransaction> document_insert_text(EditorDocument& document, const TextSelection& selection, std::u32string_view text) {
    if (text.empty()) return std::nullopt;
    const auto revision_before = document.revision;
    auto& working = document;
    auto current = selection;
    std::vector<DocumentOperation> operations;
    document_edit_detail::MutationRollback rollback(working, operations, revision_before);
    if (!selection.is_caret()) {
        auto deletion = document_delete_selection(working, selection);
        if (!deletion) return std::nullopt;
        operations.insert(
            operations.end(),
            std::make_move_iterator(deletion->operations.begin()),
            std::make_move_iterator(deletion->operations.end()));
        current = deletion->selection_after;
    }
    document_edit_detail::NodeAllocator allocator(working);
    auto inserted = document_edit_detail::insert_text(working, current.active, text, allocator);
    if (!inserted || !inserted->source_edit) return std::nullopt;
    const auto reparse_edit = *inserted->source_edit;
    document_edit_detail::append_source_operation(
        operations, std::move(*inserted->source_edit));
    auto target = current.active; target.source_offset = inserted->offset; target.affinity = inserted->affinity;
    auto structural = document_input_rules::apply_after_text_insert(working, target, allocator);
    if (structural) {
        if (structural->operations.empty()) return std::nullopt;
        target = structural->target;
        operations.insert(
            operations.end(),
            std::make_move_iterator(structural->operations.begin()),
            std::make_move_iterator(structural->operations.end()));
    }
    if (auto reclassified = document_edit_detail::reparse_edited_direct_block(
            working, target, allocator, &reparse_edit)) {
        target = reclassified->target;
        operations.insert(
            operations.end(),
            std::make_move_iterator(reclassified->operations.begin()),
            std::make_move_iterator(reclassified->operations.end()));
    }
    working.revision = revision_before + 1;
    rollback.commit();
    return make_recorded_document_transaction(
        std::move(operations),
        selection,
        TextSelection::caret(target),
        revision_before,
        working.revision,
        DocumentTransactionReason::InsertText);
}

inline std::optional<DocumentTransaction> document_enter(EditorDocument& document, const TextSelection& selection) {
    const auto revision_before = document.revision;
    auto& after = document;
    auto current = selection;
    std::vector<DocumentOperation> operations;
    document_edit_detail::MutationRollback rollback(after, operations, revision_before);
    if (!current.is_caret()) {
        auto deletion = document_delete_selection(after, current);
        if (!deletion) return std::nullopt;
        operations.insert(
            operations.end(),
            std::make_move_iterator(deletion->operations.begin()),
            std::make_move_iterator(deletion->operations.end()));
        current = deletion->selection_after;
    }
    document_edit_detail::NodeAllocator allocator(after);
    TextPosition target;
    std::optional<AppliedSourceEdit> source_edit;
    if (auto exited = document_edit_detail::exit_empty_indented_code(after, current.active, allocator)) {
        target = exited->target;
        operations.insert(
            operations.end(),
            std::make_move_iterator(exited->operations.begin()),
            std::make_move_iterator(exited->operations.end()));
    } else if (auto exited_raw = document_edit_detail::exit_complete_raw_block_at_end(
                   after, current.active, allocator)) {
        target = exited_raw->target;
        operations.insert(
            operations.end(),
            std::make_move_iterator(exited_raw->operations.begin()),
            std::make_move_iterator(exited_raw->operations.end()));
    } else if (auto exited_container = document_edit_detail::exit_empty_flow_container(after, current.active, allocator)) {
        target = exited_container->target;
        operations.insert(
            operations.end(),
            std::make_move_iterator(exited_container->operations.begin()),
            std::make_move_iterator(exited_container->operations.end()));
    } else if (auto handled = document_input_rules::handle_enter(after, current.active, allocator)) {
        target = handled->target;
        operations.insert(
            operations.end(),
            std::make_move_iterator(handled->operations.begin()),
            std::make_move_iterator(handled->operations.end()));
    } else if (auto* block = find_block(after.root, current.active.container_id);
               block && (block->kind == BlockKind::CodeBlock || block->kind == BlockKind::MathBlock)) {
        auto inserted = document_edit_detail::insert_text(after, current.active, U"\n", allocator);
        if (!inserted) return std::nullopt;
        source_edit = std::move(inserted->source_edit);
        target = current.active;
        target.source_offset = inserted->offset;
        target.affinity = inserted->affinity;
    } else if (block && block->kind == BlockKind::TableCell) {
        const auto offset = (std::min)(current.active.source_offset, block->inline_content.source.size());
        source_edit = document_edit_detail::edit_inline(after, block->id, {offset, offset}, U"<br>", allocator);
        if (!source_edit) return std::nullopt;
        target = {block->id, offset + 4, TextAffinity::Downstream};
    } else {
        auto split = document_edit_detail::split_direct(
            after,
            current.active.container_id,
            current.active.source_offset,
            allocator);
        if (!split) return std::nullopt;
        target = split->target;
        operations.insert(
            operations.end(),
            std::make_move_iterator(split->operations.begin()),
            std::make_move_iterator(split->operations.end()));
    }
    after.revision = revision_before + 1;
    if (source_edit) {
        const auto reparse_edit = *source_edit;
        document_edit_detail::append_source_operation(operations, std::move(*source_edit));
        if (auto reclassified = document_edit_detail::reparse_edited_direct_block(
                after, target, allocator, &reparse_edit)) {
            target = reclassified->target;
            operations.insert(
                operations.end(),
                std::make_move_iterator(reclassified->operations.begin()),
                std::make_move_iterator(reclassified->operations.end()));
        }
    }
    if (operations.empty()) return std::nullopt;
    rollback.commit();
    return make_recorded_document_transaction(
        std::move(operations),
        selection,
        TextSelection::caret(target),
        revision_before,
        after.revision,
        DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_insert_link(EditorDocument& document, const TextSelection& selection, std::string href, std::optional<std::string> title) {
    if (selection.anchor.container_id != selection.active.container_id) return std::nullopt;
    const auto revision_before = document.revision;
    auto& after = document; document_edit_detail::NodeAllocator allocator(after);
    auto* owner = document_edit_detail::find_inline_owner(after, selection.active.container_id); if (!owner) return std::nullopt;
    const auto start = (std::min)(selection.anchor.source_offset, selection.active.source_offset);
    const auto end = (std::min)((std::max)(selection.anchor.source_offset, selection.active.source_offset), owner->source.size());
    const auto label = owner->source.substr(start, end - start);
    std::u32string replacement = U"[" + label + U"](" + utf8_to_cps(href);
    if (title) replacement += U" \"" + utf8_to_cps(*title) + U"\"";
    replacement += U")";
    auto applied = document_edit_detail::edit_inline(
        after, selection.active.container_id, {start, end}, std::move(replacement), allocator);
    if (!applied) return std::nullopt;
    ++after.revision;
    const auto target = TextPosition{selection.active.container_id, start + 1 + label.size(), TextAffinity::Downstream};
    return document_edit_detail::source_transaction(
        after, std::move(*applied), selection, TextSelection::caret(target), revision_before, DocumentTransactionReason::Format);
}

inline std::optional<DocumentTransaction> document_insert_image(EditorDocument& document, const TextSelection& selection, std::string path, std::string alt) {
    if (selection.anchor.container_id != selection.active.container_id) return std::nullopt;
    const auto revision_before = document.revision;
    auto& after = document; document_edit_detail::NodeAllocator allocator(after);
    auto* owner = document_edit_detail::find_inline_owner(after, selection.active.container_id); if (!owner) return std::nullopt;
    const auto start = (std::min)(selection.anchor.source_offset, selection.active.source_offset);
    const auto end = (std::min)((std::max)(selection.anchor.source_offset, selection.active.source_offset), owner->source.size());
    auto label = alt.empty() ? owner->source.substr(start, end - start) : utf8_to_cps(alt);
    const auto replacement = U"![" + label + U"](" + utf8_to_cps(path.empty() ? "image.png" : path) + U")";
    auto applied = document_edit_detail::edit_inline(
        after, selection.active.container_id, {start, end}, replacement, allocator);
    if (!applied) return std::nullopt;
    ++after.revision;
    return document_edit_detail::source_transaction(
        after,
        std::move(*applied),
        selection,
        TextSelection::caret({selection.active.container_id, start + 2 + label.size(), TextAffinity::Downstream}),
        revision_before,
        DocumentTransactionReason::Format);
}

inline std::optional<DocumentTransaction> document_insert_soft_break(EditorDocument& document, const TextSelection& selection) {
    return document_insert_text(document, selection, U"\n");
}

inline std::optional<DocumentTransaction> document_delete_backward(EditorDocument& document, const TextSelection& selection) {
    if (!selection.is_caret()) return document_delete_selection(document, selection);
    const auto revision_before = document.revision;
    auto& after = document; auto target = selection.active; document_edit_detail::NodeAllocator allocator(after);
    std::optional<AppliedSourceEdit> source_edit;
    std::vector<DocumentOperation> operations;
    auto const* selected_block = find_document_block(after, target.container_id);
    if (selected_block && document_edit_detail::atomic_block(selected_block->kind)) {
        auto removed = document_edit_detail::remove_atomic(after, target.container_id, allocator);
        if (!removed) return std::nullopt;
        target = removed->target;
        operations = std::move(removed->operations);
    } else if (target.source_offset > 0) {
        if (auto* owner = document_edit_detail::find_inline_owner(after, target.container_id)) {
            auto range = inline_backward_delete_range(
                *owner,
                target.source_offset,
                selected_block && selected_block->kind == BlockKind::TableCell);
            if (!range) return std::nullopt;
            source_edit = document_edit_detail::edit_inline(after, target.container_id, *range, {}, allocator);
            if (!source_edit) return std::nullopt;
            target.source_offset = range->start;
        } else {
            source_edit = document_edit_detail::erase_text(
                after, target.container_id, {target.source_offset - 1, target.source_offset}, allocator);
            if (!source_edit) return std::nullopt;
            --target.source_offset;
        }
    } else if (auto unprefixed = document_input_rules::handle_backspace_at_start(after, target, allocator)) {
        target = unprefixed->target;
        operations = std::move(unprefixed->operations);
    } else if (auto joined_parent = document_edit_detail::join_parent_inline_owner(
                   after, target.container_id, allocator)) {
        target = joined_parent->target;
        operations = std::move(joined_parent->operations);
    } else if (auto joined = document_edit_detail::join_adjacent(
                   after, target.container_id, true, allocator)) {
        target = joined->target;
        operations = std::move(joined->operations);
    } else {
        auto removed = document_edit_detail::remove_atomic(after, target.container_id, allocator);
        if (!removed) return std::nullopt;
        target = removed->target;
        operations = std::move(removed->operations);
    }
    ++after.revision;
    if (source_edit) {
        return source_transaction_with_block_reparse(
            after, std::move(*source_edit), selection, target,
            revision_before, DocumentTransactionReason::Delete);
    }
    if (operations.empty()) return std::nullopt;
    return make_recorded_document_transaction(
        std::move(operations),
        selection,
        TextSelection::caret(target),
        revision_before,
        after.revision,
        DocumentTransactionReason::Delete);
}

inline std::optional<DocumentTransaction> document_delete_forward(EditorDocument& document, const TextSelection& selection) {
    if (!selection.is_caret()) return document_delete_selection(document, selection);
    const auto revision_before = document.revision;
    auto& after = document; auto target = selection.active; document_edit_detail::NodeAllocator allocator(after);
    std::optional<AppliedSourceEdit> source_edit;
    std::vector<DocumentOperation> operations;
    auto const* selected_block = find_document_block(after, target.container_id);
    if (selected_block && document_edit_detail::atomic_block(selected_block->kind)) {
        auto removed = document_edit_detail::remove_atomic(after, target.container_id, allocator);
        if (!removed) return std::nullopt;
        target = removed->target;
        operations = std::move(removed->operations);
        ++after.revision;
        return make_recorded_document_transaction(
            std::move(operations),
            selection,
            TextSelection::caret(target),
            revision_before,
            after.revision,
            DocumentTransactionReason::Delete);
    }
    const auto length = document_edit_detail::editable_length(after, target.container_id); if (!length) return std::nullopt;
    if (target.source_offset < *length) {
        if (auto* owner = document_edit_detail::find_inline_owner(after, target.container_id)) {
            auto range = inline_forward_delete_range(
                *owner,
                target.source_offset,
                selected_block && selected_block->kind == BlockKind::TableCell);
            if (!range) return std::nullopt;
            source_edit = document_edit_detail::edit_inline(after, target.container_id, *range, {}, allocator);
            if (!source_edit) return std::nullopt;
        } else {
            source_edit = document_edit_detail::erase_text(
                after, target.container_id, {target.source_offset, target.source_offset + 1}, allocator);
            if (!source_edit) return std::nullopt;
        }
    } else if (auto joined_child = document_edit_detail::join_first_child_into_inline_owner(
                   after, target.container_id, allocator)) {
        target = joined_child->target;
        operations = std::move(joined_child->operations);
    } else if (auto joined = document_edit_detail::join_adjacent(
                   after, target.container_id, false, allocator)) {
        target = joined->target;
        operations = std::move(joined->operations);
    } else {
        return std::nullopt;
    }
    ++after.revision;
    if (source_edit) {
        return source_transaction_with_block_reparse(
            after, std::move(*source_edit), selection, target,
            revision_before, DocumentTransactionReason::Delete);
    }
    if (operations.empty()) return std::nullopt;
    return make_recorded_document_transaction(
        std::move(operations),
        selection,
        TextSelection::caret(target),
        revision_before,
        after.revision,
        DocumentTransactionReason::Delete);
}

inline std::optional<DocumentTransaction> document_delete_selection(EditorDocument& document, const TextSelection& selection) {
    if (selection.is_caret()) return std::nullopt;
    const auto revision_before = document.revision;
    auto& after = document; document_edit_detail::NodeAllocator allocator(after);
    if (selection.anchor.container_id == selection.active.container_id) {
        const auto start = (std::min)(selection.anchor.source_offset, selection.active.source_offset);
        const auto end = (std::max)(selection.anchor.source_offset, selection.active.source_offset);
        auto source_edit = document_edit_detail::erase_text(
            after, selection.active.container_id, {start, end}, allocator);
        if (!source_edit) return std::nullopt;
        ++after.revision;
        const auto target = TextPosition{selection.active.container_id, start, TextAffinity::Downstream};
        return source_transaction_with_block_reparse(
            after, std::move(*source_edit), selection, target,
            revision_before, DocumentTransactionReason::Delete);
    }

    const auto& order = after.cached_editable_order;
    auto anchor_index = document_editable_order_position(
        after, selection.anchor.container_id);
    auto active_index = document_editable_order_position(
        after, selection.active.container_id);
    if (!anchor_index || !active_index) return std::nullopt;
    auto first = selection.anchor;
    auto last = selection.active;
    if (*anchor_index > *active_index) {
        std::swap(anchor_index, active_index);
        std::swap(first, last);
    }
    auto first_source = document_editable_text(after, first.container_id);
    auto last_source = document_editable_text(after, last.container_id);
    if (!first_source || !last_source) return std::nullopt;
    first.source_offset = (std::min)(first.source_offset, first_source->size());
    last.source_offset = (std::min)(last.source_offset, last_source->size());
    auto suffix = last_source->substr(last.source_offset);
    auto source_edit = document_edit_detail::edit_block_source(
        after,
        first.container_id,
        {first.source_offset, first_source->size()},
        std::move(suffix),
        allocator);
    if (!source_edit) return std::nullopt;
    const auto reparse_edit = *source_edit;
    std::vector<DocumentOperation> operations;
    document_edit_detail::append_source_operation(operations, std::move(*source_edit));
    document_edit_detail::MutationRollback rollback(after, operations, revision_before);
    std::vector<NodeId> removed_ids;
    removed_ids.reserve(*active_index - *anchor_index);
    for (std::size_t index = *anchor_index + 1; index <= *active_index; ++index) {
        removed_ids.push_back(order[index]);
    }
    // Remove in reverse tree order so cached sibling paths ahead of the
    // mutation remain valid throughout this transaction.
    for (auto id = removed_ids.rbegin(); id != removed_ids.rend(); ++id) {
        auto remove = document_edit_detail::remove_node_recorded(
            after,
            *id);
        if (!remove) return std::nullopt;
        operations.emplace_back(std::move(*remove));
    }
    document_edit_detail::prune_empty_containers_recorded(after.root, operations);
    if (after.root.children.empty()) {
        auto paragraph = document_edit_detail::empty_paragraph(allocator, after);
        DocumentTreeEdit insert;
        insert.kind = DocumentTreeEditKind::Insert;
        insert.parent_id = after.root.id;
        insert.index = 0;
        insert.after = paragraph;
        operations.emplace_back(std::move(insert));
        after.root.children.push_back(std::move(paragraph));
    }
    ++after.revision;
    auto target = TextPosition{first.container_id, first.source_offset, TextAffinity::Downstream};
    if (auto reclassified = document_edit_detail::reparse_edited_direct_block(
            after, target, allocator, &reparse_edit)) {
        target = reclassified->target;
        operations.insert(
            operations.end(),
            std::make_move_iterator(reclassified->operations.begin()),
            std::make_move_iterator(reclassified->operations.end()));
    }
    rollback.commit();
    return make_recorded_document_transaction(
        std::move(operations),
        selection,
        TextSelection::caret(target),
        revision_before,
        after.revision,
        DocumentTransactionReason::Delete);
}

inline std::optional<TextSelection> document_move_selection(const EditorDocument& document, const TextSelection& selection, DocumentMove movement, bool extend) {
    const auto& order = document.cached_editable_order;
    if (order.empty()) return std::nullopt;
    auto text_at = [&](std::size_t index) -> std::optional<std::u32string_view> {
        if (index >= order.size()) return std::nullopt;
        const auto* block = find_document_block(document, order[index]);
        return block ? editable_block_text_view(*block) : std::nullopt;
    };
    auto index = document_editable_order_position(
        document, selection.active.container_id);
    if (!index) return std::nullopt;
    auto text = text_at(*index); if (!text) return std::nullopt;
    auto target = selection.active; target.source_offset = (std::min)(target.source_offset, text->size());
    if (!extend && !selection.is_caret()) {
        const auto anchor_index = document_editable_order_position(
            document, selection.anchor.container_id).value_or(*index);
        const bool anchor_first = anchor_index < *index || (anchor_index == *index && selection.anchor.source_offset < selection.active.source_offset);
        target = (movement == DocumentMove::Left || movement == DocumentMove::Up || movement == DocumentMove::LineStart || movement == DocumentMove::DocumentStart)
            ? (anchor_first ? selection.anchor : selection.active) : (anchor_first ? selection.active : selection.anchor);
        return TextSelection::caret(target);
    }
    if (movement == DocumentMove::DocumentStart) target = {order.front(), 0, TextAffinity::Downstream};
    else if (movement == DocumentMove::DocumentEnd) {
        auto last = text_at(order.size() - 1); if (!last) return std::nullopt;
        target = {order.back(), last->size(), TextAffinity::Downstream};
    }
    else if (movement == DocumentMove::Left) {
        if (target.source_offset > 0) --target.source_offset;
        else if (*index > 0) {
            auto previous = text_at(*index - 1); if (!previous) return std::nullopt;
            target = {order[*index - 1], previous->size(), TextAffinity::Upstream};
        }
    } else if (movement == DocumentMove::Right) {
        if (target.source_offset < text->size()) ++target.source_offset;
        else if (*index + 1 < order.size()) target = {order[*index + 1], 0, TextAffinity::Downstream};
    } else {
        const auto before = target.source_offset == 0 ? std::u32string::npos : text->rfind(U'\n', target.source_offset - 1);
        const auto line_start = before == std::u32string::npos ? 0 : before + 1;
        const auto after = text->find(U'\n', target.source_offset);
        const auto line_end = after == std::u32string::npos ? text->size() : after;
        if (movement == DocumentMove::LineStart) target.source_offset = line_start;
        else if (movement == DocumentMove::LineEnd) target.source_offset = line_end;
        else {
            const auto column = target.source_offset - line_start;
            if (movement == DocumentMove::Up && line_start > 0) {
                const auto previous_end = line_start - 1;
                const auto previous_break = previous_end == 0 ? std::u32string::npos : text->rfind(U'\n', previous_end - 1);
                const auto previous_start = previous_break == std::u32string::npos ? 0 : previous_break + 1;
                target.source_offset = previous_start + (std::min)(column, previous_end - previous_start);
            } else if (movement == DocumentMove::Down && line_end < text->size()) {
                const auto next_start = line_end + 1;
                const auto next_break = text->find(U'\n', next_start);
                const auto next_end = next_break == std::u32string::npos ? text->size() : next_break;
                target.source_offset = next_start + (std::min)(column, next_end - next_start);
            }
        }
    }
    if (target == selection.active) return std::nullopt;
    return extend ? TextSelection{selection.anchor, target} : TextSelection::caret(target);
}

inline std::optional<TextSelection> document_select_all(const EditorDocument& document) {
    const auto& order = document.cached_editable_order;
    if (order.empty()) return std::nullopt;
    const auto* last = find_document_block(document, order.back());
    auto last_text = last ? editable_block_text_view(*last) : std::nullopt;
    if (!last_text) return std::nullopt;
    return TextSelection{
        {order.front(), 0, TextAffinity::Downstream},
        {order.back(), last_text->size(), TextAffinity::Downstream}};
}

} // namespace folia
