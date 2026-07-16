// elmd.core.document_copy — lossless Markdown extraction from a structural selection.
export module elmd.core.document_copy;
import std;
import elmd.core.ast;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.document_text;
import elmd.core.serializer;
import elmd.core.text_edit;

export namespace elmd {

namespace document_copy_detail {

struct OrderedSelection {
    const EditorDocument* document = nullptr;
    TextPosition start;
    TextPosition end;
    std::size_t start_index = 0;
    std::size_t end_index = 0;
};

inline std::optional<std::u32string_view> owner_text(
    const OrderedSelection& selection,
    std::size_t index) {
    if (!selection.document
        || index >= selection.document->cached_editable_order.size()) {
        return std::nullopt;
    }
    const auto* block = find_document_block(
        *selection.document,
        selection.document->cached_editable_order[index]);
    return block ? editable_block_text_view(*block) : std::nullopt;
}

inline std::optional<OrderedSelection> order_selection(
    const EditorDocument& document,
    const TextSelection& selection) {
    OrderedSelection ordered;
    ordered.document = &document;
    const auto anchor = document_editable_order_position(
        document, selection.anchor.container_id);
    const auto active = document_editable_order_position(
        document, selection.active.container_id);
    if (!anchor || !active) return std::nullopt;
    ordered.start = selection.anchor;
    ordered.end = selection.active;
    ordered.start_index = *anchor;
    ordered.end_index = *active;
    if (ordered.end_index < ordered.start_index
        || (ordered.start_index == ordered.end_index
            && ordered.end.source_offset < ordered.start.source_offset)) {
        std::swap(ordered.start, ordered.end);
        std::swap(ordered.start_index, ordered.end_index);
    }
    const auto start_text = owner_text(ordered, ordered.start_index);
    const auto end_text = owner_text(ordered, ordered.end_index);
    if (!start_text || !end_text) return std::nullopt;
    ordered.start.source_offset = (std::min)(
        ordered.start.source_offset, start_text->size());
    ordered.end.source_offset = (std::min)(
        ordered.end.source_offset, end_text->size());
    return ordered;
}

inline SourceRange selected_range(
    const OrderedSelection& selection,
    std::size_t index) {
    if (index < selection.start_index || index > selection.end_index) return {};
    const auto text = owner_text(selection, index);
    if (!text) return {};
    const auto length = text->size();
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
    const auto self = selection.document
        ? document_editable_order_position(*selection.document, block.id)
        : std::nullopt;
    if (self) span = {*self, *self};
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
            copy.ensure_special().level = 0;
            copy.ensure_special().opening_marker.clear();
            copy.ensure_special().closing_marker.clear();
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
    const auto self = selection.document
        ? document_editable_order_position(*selection.document, block.id)
        : std::nullopt;
    if (self && *self >= selection.start_index && *self <= selection.end_index) {
        const auto range = selected_range(selection, *self);
        if (!range.empty()) {
            has_selected_owner = true;
            const auto text = owner_text(selection, *self);
            if (!text) return std::nullopt;
            truncate_owner(copy, range, text->size());
        }
    }

    for (const auto& child : block.children) {
        if (auto selected = slice_block(child, selection)) {
            copy.children.push_back(std::move(*selected));
        }
    }

    if (!has_selected_owner && copy.children.empty()) return std::nullopt;
    return copy;
}

} // namespace document_copy_detail

// Return the exact source substring for a one-owner selection. Cross-owner
// selections are represented by a recursively pruned block tree, preserving
// list/quote/task/callout/footnote structure at arbitrary nesting depth.
inline std::optional<std::u32string> document_selected_markdown(
    const EditorDocument& document,
    const TextSelection& selection) {
    auto ordered = document_copy_detail::order_selection(document, selection);
    if (!ordered) return std::nullopt;
    if (ordered->start_index == ordered->end_index) {
        const auto range = document_copy_detail::selected_range(
            *ordered,
            ordered->start_index);
        const auto text = document_copy_detail::owner_text(
            *ordered, ordered->start_index);
        if (!text) return std::nullopt;
        return std::u32string{text->substr(range.start, range.length())};
    }

    BlockVec selected;
    selected.reserve(document.root.children.size());
    for (const auto& block : document.root.children) {
        if (auto copy = document_copy_detail::slice_block(block, *ordered)) {
            selected.push_back(std::move(*copy));
        }
    }
    // Parser-owned separator metadata can split a physical blank line between
    // the preceding block's trailing range and the following block. Once a
    // boundary block is truncated that ownership is no longer valid. Let the
    // fragment serializer choose an unambiguous block separator for every
    // selected top-level sibling instead of accidentally turning the next
    // paragraph into a lazy list continuation.
    for (auto& block : selected) block.ensure_special().separator_before.reset();
    return serialize_markdown_fragment(selected);
}

} // namespace elmd
