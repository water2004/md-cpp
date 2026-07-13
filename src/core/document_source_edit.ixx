export module elmd.core.document_source_edit;
import std;
import elmd.core.ast;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.document_text;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.inline_parser;
import elmd.core.inline_source_edit;
import elmd.core.text_edit;
import elmd.core.utf;
import elmd.core.document_edit_support;
import elmd.core.document_input_rules;

export namespace elmd {

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
    document_edit_detail::validate_blocks(document.root.children, ids, errors);
    return errors;
}

inline std::optional<DocumentTransaction> document_delete_selection(const EditorDocument&, const TextSelection&);

inline std::optional<DocumentTransaction> document_insert_text(const EditorDocument& document, const TextSelection& selection, std::u32string_view text) {
    if (text.empty()) return std::nullopt;
    auto working = document;
    auto current = selection;
    std::vector<DocumentOperation> operations;
    if (!selection.is_caret()) {
        auto deletion = document_delete_selection(document, selection);
        if (!deletion) return std::nullopt;
        operations.insert(
            operations.end(),
            std::make_move_iterator(deletion->operations.begin()),
            std::make_move_iterator(deletion->operations.end()));
        working = std::move(deletion->after);
        current = deletion->selection_after;
    }
    document_edit_detail::NodeAllocator allocator(working);
    auto inserted = document_edit_detail::insert_text(working, current.active, text, allocator);
    if (!inserted || !inserted->source_edit) return std::nullopt;
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
    ++working.revision;
    return make_recorded_document_transaction(
        std::move(working),
        std::move(operations),
        selection,
        TextSelection::caret(target),
        document.revision,
        DocumentTransactionReason::InsertText);
}

inline std::optional<DocumentTransaction> document_enter(const EditorDocument& document, const TextSelection& selection) {
    if (!selection.is_caret()) return std::nullopt;
    auto after = document;
    document_edit_detail::NodeAllocator allocator(after);
    TextPosition target;
    std::optional<AppliedSourceEdit> source_edit;
    std::vector<DocumentOperation> operations;
    if (auto exited = document_edit_detail::exit_empty_indented_code(after, selection.active, allocator)) {
        target = exited->target;
        operations = std::move(exited->operations);
    } else if (auto exited_quote = document_edit_detail::exit_empty_block_quote(after, selection.active, allocator)) {
        target = exited_quote->target;
        operations = std::move(exited_quote->operations);
    } else if (auto handled = document_input_rules::handle_enter(after, selection.active, allocator)) {
        target = handled->target;
        operations = std::move(handled->operations);
    } else if (auto* block = find_block(after.root, selection.active.container_id);
               block && (block->kind == BlockKind::CodeBlock || block->kind == BlockKind::MathBlock)) {
        auto inserted = document_edit_detail::insert_text(after, selection.active, U"\n", allocator);
        if (!inserted) return std::nullopt;
        source_edit = std::move(inserted->source_edit);
        target = selection.active;
        target.source_offset = inserted->offset;
        target.affinity = inserted->affinity;
    } else if (block && block->kind == BlockKind::TableCell) {
        const auto offset = (std::min)(selection.active.source_offset, block->inline_content.source.size());
        source_edit = document_edit_detail::edit_inline(after, block->id, {offset, offset}, U"<br>", allocator);
        if (!source_edit) return std::nullopt;
        target = {block->id, offset + 4, TextAffinity::Downstream};
    } else {
        auto split = document_edit_detail::split_direct(
            after,
            selection.active.container_id,
            selection.active.source_offset,
            allocator);
        if (!split) return std::nullopt;
        target = split->target;
        operations = std::move(split->operations);
    }
    ++after.revision;
    if (source_edit) {
        return document_edit_detail::source_transaction(
            std::move(after),
            std::move(*source_edit),
            selection,
            TextSelection::caret(target),
            document.revision,
            DocumentTransactionReason::Structure);
    }
    if (operations.empty()) return std::nullopt;
    return make_recorded_document_transaction(
        std::move(after),
        std::move(operations),
        selection,
        TextSelection::caret(target),
        document.revision,
        DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_paste_text(const EditorDocument& document, const TextSelection& selection, std::u32string_view text) {
    if (text.empty()) return std::nullopt;
    std::u32string normalized;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == U'\r') { if (index + 1 < text.size() && text[index + 1] == U'\n') ++index; normalized.push_back(U'\n'); }
        else normalized.push_back(text[index]);
    }
    auto working = document;
    auto current = selection;
    std::vector<DocumentOperation> operations;
    auto apply_step = [&](DocumentTransaction step) {
        operations.insert(
            operations.end(),
            std::make_move_iterator(step.operations.begin()),
            std::make_move_iterator(step.operations.end()));
        working = std::move(step.after);
        current = step.selection_after;
    };
    if (!current.is_caret()) {
        auto deletion = document_delete_selection(working, current); if (!deletion) return std::nullopt;
        apply_step(std::move(*deletion));
    }
    std::size_t start = 0;
    while (start <= normalized.size()) {
        const auto end = normalized.find(U'\n', start);
        const auto segment_end = end == std::u32string::npos ? normalized.size() : end;
        if (segment_end > start) {
            auto insertion = document_insert_text(working, current, std::u32string_view(normalized).substr(start, segment_end - start));
            if (!insertion) return std::nullopt;
            apply_step(std::move(*insertion));
        }
        if (end == std::u32string::npos) break;
        auto split = document_enter(working, current); if (!split) return std::nullopt;
        apply_step(std::move(*split));
        start = end + 1;
    }
    return make_recorded_document_transaction(
        std::move(working),
        std::move(operations),
        selection,
        current,
        document.revision,
        DocumentTransactionReason::Paste);
}

inline std::u32string format_marker(InlineFormat format) {
    switch (format) {
        case InlineFormat::Emphasis: return U"*";
        case InlineFormat::Strong: return U"**";
        case InlineFormat::Strikethrough: return U"~~";
        case InlineFormat::Code: return U"`";
        case InlineFormat::Math: return U"$";
    }
    return {};
}

inline std::optional<DocumentTransaction> document_toggle_inline_format(const EditorDocument& document, const TextSelection& selection, InlineFormat format) {
    if (selection.anchor.container_id != selection.active.container_id) return std::nullopt;
    auto after = document;
    document_edit_detail::NodeAllocator allocator(after);
    auto* inline_document = document_edit_detail::find_inline_owner(after.root.children, selection.active.container_id);
    if (!inline_document) return std::nullopt;
    auto start = (std::min)(selection.anchor.source_offset, selection.active.source_offset);
    auto end = (std::max)(selection.anchor.source_offset, selection.active.source_offset);
    start = (std::min)(start, inline_document->source.size()); end = (std::min)(end, inline_document->source.size());
    const auto marker = format_marker(format);
    SourceRange edit_range{start, end};
    std::u32string replacement;
    TextSelection result = selection;
    if (start >= marker.size() && end + marker.size() <= inline_document->source.size()
        && inline_document->source.substr(start - marker.size(), marker.size()) == marker
        && inline_document->source.substr(end, marker.size()) == marker) {
        edit_range = {start - marker.size(), end + marker.size()};
        replacement = inline_document->source.substr(start, end - start);
        result.anchor.source_offset -= marker.size(); result.active.source_offset -= marker.size();
    } else {
        replacement = marker + inline_document->source.substr(start, end - start) + marker;
        result.anchor.source_offset += marker.size(); result.active.source_offset += marker.size();
        if (selection.is_caret()) result = TextSelection::caret(TextPosition{selection.active.container_id, start + marker.size(), TextAffinity::Downstream});
    }
    auto applied = document_edit_detail::edit_inline(after, selection.active.container_id, edit_range, std::move(replacement), allocator);
    if (!applied) return std::nullopt;
    ++after.revision;
    return document_edit_detail::source_transaction(
        std::move(after), std::move(*applied), selection, result, document.revision, DocumentTransactionReason::Format);
}

inline std::optional<DocumentTransaction> document_insert_link(const EditorDocument& document, const TextSelection& selection, std::string href, std::optional<std::string> title) {
    if (selection.anchor.container_id != selection.active.container_id) return std::nullopt;
    auto after = document; document_edit_detail::NodeAllocator allocator(after);
    auto* owner = document_edit_detail::find_inline_owner(after.root.children, selection.active.container_id); if (!owner) return std::nullopt;
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
        std::move(after), std::move(*applied), selection, TextSelection::caret(target), document.revision, DocumentTransactionReason::Format);
}

inline std::optional<DocumentTransaction> document_insert_image(const EditorDocument& document, const TextSelection& selection, std::string path, std::string alt) {
    if (selection.anchor.container_id != selection.active.container_id) return std::nullopt;
    auto after = document; document_edit_detail::NodeAllocator allocator(after);
    auto* owner = document_edit_detail::find_inline_owner(after.root.children, selection.active.container_id); if (!owner) return std::nullopt;
    const auto start = (std::min)(selection.anchor.source_offset, selection.active.source_offset);
    const auto end = (std::min)((std::max)(selection.anchor.source_offset, selection.active.source_offset), owner->source.size());
    auto label = alt.empty() ? owner->source.substr(start, end - start) : utf8_to_cps(alt);
    const auto replacement = U"![" + label + U"](" + utf8_to_cps(path.empty() ? "image.png" : path) + U")";
    auto applied = document_edit_detail::edit_inline(
        after, selection.active.container_id, {start, end}, replacement, allocator);
    if (!applied) return std::nullopt;
    ++after.revision;
    return document_edit_detail::source_transaction(
        std::move(after),
        std::move(*applied),
        selection,
        TextSelection::caret({selection.active.container_id, start + 2 + label.size(), TextAffinity::Downstream}),
        document.revision,
        DocumentTransactionReason::Format);
}

inline std::optional<DocumentTransaction> document_insert_soft_break(const EditorDocument& document, const TextSelection& selection) {
    return document_insert_text(document, selection, U"\n");
}

inline std::optional<DocumentTransaction> document_delete_backward(const EditorDocument& document, const TextSelection& selection) {
    if (!selection.is_caret()) return document_delete_selection(document, selection);
    auto after = document; auto target = selection.active; document_edit_detail::NodeAllocator allocator(after);
    std::optional<AppliedSourceEdit> source_edit;
    std::vector<DocumentOperation> operations;
    auto const* selected_block = find_block(after.root, target.container_id);
    if (selected_block && document_edit_detail::atomic_block(selected_block->kind)) {
        auto removed = document_edit_detail::remove_atomic(after, target.container_id, allocator);
        if (!removed) return std::nullopt;
        target = removed->target;
        operations = std::move(removed->operations);
    } else if (target.source_offset > 0) {
        if (auto* owner = document_edit_detail::find_inline_owner(after.root.children, target.container_id)) {
            auto range = inline_backward_delete_range(*owner, target.source_offset);
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
        target = *unprefixed;
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
        return document_edit_detail::source_transaction(
            std::move(after), std::move(*source_edit), selection, TextSelection::caret(target), document.revision, DocumentTransactionReason::Delete);
    }
    if (!operations.empty()) {
        return make_recorded_document_transaction(
            std::move(after),
            std::move(operations),
            selection,
            TextSelection::caret(target),
            document.revision,
            DocumentTransactionReason::Delete);
    }
    return document_edit_detail::transaction(document, std::move(after), selection, TextSelection::caret(target), DocumentTransactionReason::Delete);
}

inline std::optional<DocumentTransaction> document_delete_forward(const EditorDocument& document, const TextSelection& selection) {
    if (!selection.is_caret()) return document_delete_selection(document, selection);
    auto after = document; auto target = selection.active; document_edit_detail::NodeAllocator allocator(after);
    std::optional<AppliedSourceEdit> source_edit;
    std::vector<DocumentOperation> operations;
    auto const* selected_block = find_block(after.root, target.container_id);
    if (selected_block && document_edit_detail::atomic_block(selected_block->kind)) {
        auto removed = document_edit_detail::remove_atomic(after, target.container_id, allocator);
        if (!removed) return std::nullopt;
        target = removed->target;
        operations = std::move(removed->operations);
        ++after.revision;
        return make_recorded_document_transaction(
            std::move(after),
            std::move(operations),
            selection,
            TextSelection::caret(target),
            document.revision,
            DocumentTransactionReason::Delete);
    }
    const auto length = document_edit_detail::editable_length(after, target.container_id); if (!length) return std::nullopt;
    if (target.source_offset < *length) {
        if (auto* owner = document_edit_detail::find_inline_owner(after.root.children, target.container_id)) {
            auto range = inline_forward_delete_range(*owner, target.source_offset);
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
        return document_edit_detail::source_transaction(
            std::move(after), std::move(*source_edit), selection, TextSelection::caret(target), document.revision, DocumentTransactionReason::Delete);
    }
    if (operations.empty()) return std::nullopt;
    return make_recorded_document_transaction(
        std::move(after),
        std::move(operations),
        selection,
        TextSelection::caret(target),
        document.revision,
        DocumentTransactionReason::Delete);
}

inline std::optional<DocumentTransaction> document_delete_selection(const EditorDocument& document, const TextSelection& selection) {
    if (selection.is_caret()) return std::nullopt;
    auto after = document; document_edit_detail::NodeAllocator allocator(after);
    if (selection.anchor.container_id == selection.active.container_id) {
        const auto start = (std::min)(selection.anchor.source_offset, selection.active.source_offset);
        const auto end = (std::max)(selection.anchor.source_offset, selection.active.source_offset);
        auto source_edit = document_edit_detail::erase_text(
            after, selection.active.container_id, {start, end}, allocator);
        if (!source_edit) return std::nullopt;
        ++after.revision;
        const auto target = TextPosition{selection.active.container_id, start, TextAffinity::Downstream};
        return document_edit_detail::source_transaction(
            std::move(after), std::move(*source_edit), selection, TextSelection::caret(target), document.revision, DocumentTransactionReason::Delete);
    }

    const auto nodes = document_text_fragments(after);
    auto index_of = [&](NodeId id) -> std::optional<std::size_t> {
        for (std::size_t index = 0; index < nodes.size(); ++index) {
            if (nodes[index].container_id == id) return index;
        }
        return std::nullopt;
    };
    auto anchor_index = index_of(selection.anchor.container_id);
    auto active_index = index_of(selection.active.container_id);
    if (!anchor_index || !active_index) return std::nullopt;
    auto first = selection.anchor;
    auto last = selection.active;
    if (*anchor_index > *active_index) {
        std::swap(anchor_index, active_index);
        std::swap(first, last);
    }
    auto* first_owner = document_edit_detail::find_inline_owner(after.root.children, first.container_id);
    auto* last_owner = document_edit_detail::find_inline_owner(after.root.children, last.container_id);
    if (!first_owner || !last_owner) return std::nullopt;
    first.source_offset = (std::min)(first.source_offset, first_owner->source.size());
    last.source_offset = (std::min)(last.source_offset, last_owner->source.size());
    auto replacement = first_owner->source.substr(0, first.source_offset)
        + last_owner->source.substr(last.source_offset);
    if (!document_edit_detail::edit_inline(after, first.container_id, {0, first_owner->source.size()}, std::move(replacement), allocator)) return std::nullopt;
    for (std::size_t index = *anchor_index + 1; index <= *active_index; ++index) {
        document_edit_detail::remove_node(after.root.children, nodes[index].container_id);
    }
    document_edit_detail::prune_empty_containers(after.root.children);
    if (after.root.children.empty()) after.root.children.push_back(document_edit_detail::empty_paragraph(allocator, after));
    ++after.revision;
    const auto target = TextPosition{first.container_id, first.source_offset, TextAffinity::Downstream};
    return document_edit_detail::transaction(document, std::move(after), selection, TextSelection::caret(target), DocumentTransactionReason::Delete);
}

inline std::optional<TextSelection> document_move_selection(const EditorDocument& document, const TextSelection& selection, DocumentMove movement, bool extend) {
    const auto nodes = document_text_fragments(document);
    if (nodes.empty()) return std::nullopt;
    auto index_of = [&](NodeId id) -> std::optional<std::size_t> {
        for (std::size_t index = 0; index < nodes.size(); ++index) {
            if (nodes[index].container_id == id) return index;
        }
        return std::nullopt;
    };
    auto index = index_of(selection.active.container_id); if (!index) return std::nullopt;
    auto target = selection.active; target.source_offset = (std::min)(target.source_offset, nodes[*index].text.size());
    if (!extend && !selection.is_caret()) {
        const auto anchor_index = index_of(selection.anchor.container_id).value_or(*index);
        const bool anchor_first = anchor_index < *index || (anchor_index == *index && selection.anchor.source_offset < selection.active.source_offset);
        target = (movement == DocumentMove::Left || movement == DocumentMove::Up || movement == DocumentMove::LineStart || movement == DocumentMove::DocumentStart)
            ? (anchor_first ? selection.anchor : selection.active) : (anchor_first ? selection.active : selection.anchor);
        return TextSelection::caret(target);
    }
    if (movement == DocumentMove::DocumentStart) target = {nodes.front().container_id, 0, TextAffinity::Downstream};
    else if (movement == DocumentMove::DocumentEnd) target = {nodes.back().container_id, nodes.back().text.size(), TextAffinity::Downstream};
    else if (movement == DocumentMove::Left) {
        if (target.source_offset > 0) --target.source_offset;
        else if (*index > 0) target = {nodes[*index - 1].container_id, nodes[*index - 1].text.size(), TextAffinity::Upstream};
    } else if (movement == DocumentMove::Right) {
        if (target.source_offset < nodes[*index].text.size()) ++target.source_offset;
        else if (*index + 1 < nodes.size()) target = {nodes[*index + 1].container_id, 0, TextAffinity::Downstream};
    } else {
        const auto& text = nodes[*index].text;
        const auto before = target.source_offset == 0 ? std::u32string::npos : text.rfind(U'\n', target.source_offset - 1);
        const auto line_start = before == std::u32string::npos ? 0 : before + 1;
        const auto after = text.find(U'\n', target.source_offset);
        const auto line_end = after == std::u32string::npos ? text.size() : after;
        if (movement == DocumentMove::LineStart) target.source_offset = line_start;
        else if (movement == DocumentMove::LineEnd) target.source_offset = line_end;
        else {
            const auto column = target.source_offset - line_start;
            if (movement == DocumentMove::Up && line_start > 0) {
                const auto previous_end = line_start - 1;
                const auto previous_break = previous_end == 0 ? std::u32string::npos : text.rfind(U'\n', previous_end - 1);
                const auto previous_start = previous_break == std::u32string::npos ? 0 : previous_break + 1;
                target.source_offset = previous_start + (std::min)(column, previous_end - previous_start);
            } else if (movement == DocumentMove::Down && line_end < text.size()) {
                const auto next_start = line_end + 1;
                const auto next_break = text.find(U'\n', next_start);
                const auto next_end = next_break == std::u32string::npos ? text.size() : next_break;
                target.source_offset = next_start + (std::min)(column, next_end - next_start);
            }
        }
    }
    if (target == selection.active) return std::nullopt;
    return extend ? TextSelection{selection.anchor, target} : TextSelection::caret(target);
}

inline std::optional<TextSelection> document_select_all(const EditorDocument& document) {
    const auto nodes = document_text_fragments(document);
    if (nodes.empty()) return std::nullopt;
    return TextSelection{
        {nodes.front().container_id, 0, TextAffinity::Downstream},
        {nodes.back().container_id, nodes.back().text.size(), TextAffinity::Downstream}};
}

} // namespace elmd
