// elmd.core.inline_source_edit — the sole mutation path for inline content.
export module elmd.core.inline_source_edit;
import std;
import elmd.core.ids;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.inline_parser;
import elmd.core.text_edit;

export namespace elmd {

namespace inline_source_edit_detail {

inline std::optional<SourceRange> adjacent_hard_break(
    const InlineCstNodes& nodes,
    std::size_t offset,
    bool backward) {
    for (const auto& node : nodes) {
        if (auto nested = adjacent_hard_break(node.children, offset, backward)) return nested;
        if (node.kind != InlineCstKind::HardBreak) continue;
        const bool adjacent = backward
            ? node.range.start < offset && offset <= node.range.end
            : node.range.start <= offset && offset < node.range.end;
        if (adjacent) return node.range;
    }
    return std::nullopt;
}

inline std::optional<SourceRange> translated_untouched_range(
    SourceRange range,
    const TextEdit& edit) {
    if (range.end <= edit.range.start) return range;
    if (range.start >= edit.range.end) {
        const auto delta = text_edit_delta(edit);
        const auto start = static_cast<std::ptrdiff_t>(range.start) + delta;
        const auto end = static_cast<std::ptrdiff_t>(range.end) + delta;
        if (start < 0 || end < start) return std::nullopt;
        return SourceRange{static_cast<std::size_t>(start), static_cast<std::size_t>(end)};
    }
    return std::nullopt;
}

inline bool same_source_slice(
    std::u32string_view before,
    SourceRange before_range,
    std::u32string_view after,
    SourceRange after_range) {
    return before_range.valid_for(before.size())
        && after_range.valid_for(after.size())
        && before.substr(before_range.start, before_range.length())
            == after.substr(after_range.start, after_range.length());
}

inline void reconcile_nodes(
    const InlineCstNodes& before_nodes,
    InlineCstNodes& after_nodes,
    std::u32string_view before_source,
    std::u32string_view after_source,
    const TextEdit& edit) {
    std::vector<bool> claimed(after_nodes.size(), false);
    for (const auto& before : before_nodes) {
        const auto expected = translated_untouched_range(before.range, edit);
        if (!expected) continue;
        for (std::size_t index = 0; index < after_nodes.size(); ++index) {
            auto& after = after_nodes[index];
            if (claimed[index] || after.kind != before.kind || after.status != before.status
                || after.range != *expected
                || !same_source_slice(before_source, before.range, after_source, after.range)) {
                continue;
            }
            claimed[index] = true;
            after.id = before.id;
            reconcile_nodes(before.children, after.children, before_source, after_source, edit);
            break;
        }
    }
}

inline void reconcile_tokens(
    const std::vector<InlineToken>& before_tokens,
    std::vector<InlineToken>& after_tokens,
    std::u32string_view before_source,
    std::u32string_view after_source,
    const TextEdit& edit) {
    std::vector<bool> claimed(after_tokens.size(), false);
    for (const auto& before : before_tokens) {
        const auto expected = translated_untouched_range(before.range, edit);
        if (!expected) continue;
        for (std::size_t index = 0; index < after_tokens.size(); ++index) {
            auto& after = after_tokens[index];
            if (claimed[index] || after.kind != before.kind || after.range != *expected
                || !same_source_slice(before_source, before.range, after_source, after.range)) {
                continue;
            }
            claimed[index] = true;
            after.id = before.id;
            break;
        }
    }
}

} // namespace inline_source_edit_detail

inline std::optional<SourceRange> inline_backward_delete_range(
    const InlineDocument& document,
    std::size_t offset) {
    offset = (std::min)(offset, document.source.size());
    if (offset == 0) return std::nullopt;
    if (auto hard_break = inline_source_edit_detail::adjacent_hard_break(
            document.tree.nodes, offset, true)) {
        return hard_break;
    }
    return SourceRange{offset - 1, offset};
}

inline std::optional<SourceRange> inline_forward_delete_range(
    const InlineDocument& document,
    std::size_t offset) {
    offset = (std::min)(offset, document.source.size());
    if (offset == document.source.size()) return std::nullopt;
    if (auto hard_break = inline_source_edit_detail::adjacent_hard_break(
            document.tree.nodes, offset, false)) {
        return hard_break;
    }
    return SourceRange{offset, offset + 1};
}

inline AppliedSourceEdit apply_inline_source_edit(
    NodeId owner_id,
    InlineDocument& document,
    const TextEdit& edit,
    const InlineParseContext& parse_context) {
    if (edit.container_id != owner_id) {
        throw std::invalid_argument("TextEdit container does not own this InlineDocument");
    }
    if (!edit.range.valid_for(document.source.size())) {
        throw std::out_of_range("TextEdit range is outside InlineDocument::source");
    }

    const auto before_source = document.source;
    const auto before_tree = document.tree;
    const auto removed = before_source.substr(edit.range.start, edit.range.length());
    apply_text_edit(document.source, edit);
    document.tree = parse_inline(document.source, parse_context);

    inline_source_edit_detail::reconcile_nodes(
        before_tree.nodes, document.tree.nodes, before_source, document.source, edit);
    inline_source_edit_detail::reconcile_tokens(
        before_tree.tokens, document.tree.tokens, before_source, document.source, edit);

    TextEdit inverse;
    inverse.container_id = owner_id;
    inverse.range = {edit.range.start, edit.range.start + edit.replacement.size()};
    inverse.replacement = removed;
    return AppliedSourceEdit{edit, std::move(inverse)};
}

} // namespace elmd
