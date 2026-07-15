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
        return ordered->fragments[ordered->start_index].text.substr(
            range.start,
            range.length());
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
    for (auto& block : selected) block.separator_before.reset();
    return serialize_markdown_fragment(selected);
}

} // namespace elmd

