// CST-driven inline formatting over a block-local authoritative source.
//
// Formatting never infers an existing construct by comparing marker text.
// It locates the matching lossless CST node, preserves that node's exact
// opening/closing spelling, and applies one source TextEdit.
export module folia.core.document_inline_format;
import std;
import folia.core.document;
import folia.core.document_edit_primitives;
import folia.core.document_edit_validation;
import folia.core.inline_cst;
import folia.core.inline_document;
import folia.core.text_edit;

export namespace folia {

namespace document_inline_format_detail {

inline InlineCstKind cst_kind(InlineFormat format) {
    switch (format) {
        case InlineFormat::Emphasis: return InlineCstKind::Emphasis;
        case InlineFormat::Strong: return InlineCstKind::Strong;
        case InlineFormat::Strikethrough: return InlineCstKind::Strikethrough;
        case InlineFormat::Code: return InlineCstKind::CodeSpan;
        case InlineFormat::Math: return InlineCstKind::InlineMath;
    }
    return InlineCstKind::Error;
}

inline std::optional<std::pair<std::u32string, std::u32string>> default_markers(
    InlineFormat format,
    InlineSyntaxMode syntax_mode) {
    if (syntax_mode == InlineSyntaxMode::HtmlText) {
        switch (format) {
            case InlineFormat::Emphasis: return std::pair{std::u32string{U"<em>"}, std::u32string{U"</em>"}};
            case InlineFormat::Strong: return std::pair{std::u32string{U"<strong>"}, std::u32string{U"</strong>"}};
            case InlineFormat::Strikethrough: return std::pair{std::u32string{U"<del>"}, std::u32string{U"</del>"}};
            case InlineFormat::Code: return std::pair{std::u32string{U"<code>"}, std::u32string{U"</code>"}};
            case InlineFormat::Math: return std::nullopt;
        }
    }
    switch (format) {
        case InlineFormat::Emphasis: return std::pair{std::u32string{U"*"}, std::u32string{U"*"}};
        case InlineFormat::Strong: return std::pair{std::u32string{U"**"}, std::u32string{U"**"}};
        case InlineFormat::Strikethrough: return std::pair{std::u32string{U"~~"}, std::u32string{U"~~"}};
        case InlineFormat::Code: return std::pair{std::u32string{U"`"}, std::u32string{U"`"}};
        case InlineFormat::Math: return std::pair{std::u32string{U"$"}, std::u32string{U"$"}};
    }
    return {};
}

struct FormatNodeRanges {
    SourceRange full;
    SourceRange opening;
    SourceRange content;
    SourceRange closing;
};

inline bool contains_selection(
    const InlineCstNode& node,
    std::size_t start,
    std::size_t end,
    bool caret) {
    const auto& delim = node.delimiter_ranges();
    if (node.status != ParseStatus::Complete || !delim.closing) return false;
    if (!caret && start == node.range.start && end == node.range.end) return true;
    if (caret) return start >= delim.content.start && start <= delim.content.end;
    return start >= delim.content.start && end <= delim.content.end;
}

inline std::optional<FormatNodeRanges> find_innermost_format(
    const InlineCstNodes& nodes,
    InlineCstKind kind,
    std::size_t start,
    std::size_t end,
    bool caret) {
    for (const auto& node : nodes) {
        if (auto nested = find_innermost_format(node.children, kind, start, end, caret)) {
            return nested;
        }
        if (node.kind == kind && contains_selection(node, start, end, caret)) {
            return FormatNodeRanges{
                node.range,
                node.delimiter_ranges().opening,
                node.delimiter_ranges().content,
                *node.delimiter_ranges().closing,
            };
        }
    }
    return std::nullopt;
}

inline TextSelection directed_selection(
    const TextSelection& original,
    NodeId owner,
    std::size_t start,
    std::size_t end) {
    if (original.anchor.source_offset <= original.active.source_offset) {
        return {
            {owner, start, original.anchor.affinity},
            {owner, end, original.active.affinity},
        };
    }
    return {
        {owner, end, original.anchor.affinity},
        {owner, start, original.active.affinity},
    };
}

struct FormatSourceChange {
    SourceRange range;
    std::u32string replacement;
    TextSelection selection_after;
};

inline FormatSourceChange remove_existing_format(
    NodeId owner,
    const InlineDocument& document,
    const TextSelection& selection,
    SourceRange selected,
    const FormatNodeRanges& format) {
    const auto content = inline_source_slice(document, format.content);

    if (selection.is_caret()) {
        const auto offset = format.full.start
            + (selection.active.source_offset - format.content.start);
        return {
            format.full,
            content,
            TextSelection::caret({owner, offset, selection.active.affinity}),
        };
    }

    const auto selected_full_node = selected == format.full;
    const auto selected_full_content = selected == format.content;
    if (selected_full_node || selected_full_content) {
        return {
            format.full,
            content,
            directed_selection(
                selection,
                owner,
                format.full.start,
                format.full.start + content.size()),
        };
    }

    const auto opening = inline_source_slice(document, format.opening);
    const auto closing = inline_source_slice(document, format.closing);
    const auto prefix = document.source.substr(
        format.content.start,
        selected.start - format.content.start);
    const auto unformatted = document.source.substr(selected.start, selected.length());
    const auto suffix = document.source.substr(
        selected.end,
        format.content.end - selected.end);

    std::u32string replacement;
    if (!prefix.empty()) replacement += opening + prefix + closing;
    const auto selection_start = format.full.start + replacement.size();
    replacement += unformatted;
    const auto selection_end = format.full.start + replacement.size();
    if (!suffix.empty()) replacement += opening + suffix + closing;

    return {
        format.full,
        std::move(replacement),
        directed_selection(selection, owner, selection_start, selection_end),
    };
}

inline FormatSourceChange add_format(
    NodeId owner,
    const InlineDocument& document,
    const TextSelection& selection,
    SourceRange selected,
    const std::pair<std::u32string, std::u32string>& markers) {
    const auto& [opening, closing] = markers;
    auto replacement = opening + document.source.substr(selected.start, selected.length()) + closing;
    if (selection.is_caret()) {
        return {
            selected,
            std::move(replacement),
            TextSelection::caret({
                owner,
                selected.start + opening.size(),
                TextAffinity::Downstream,
            }),
        };
    }
    return {
        selected,
        std::move(replacement),
        directed_selection(
            selection,
            owner,
            selected.start + opening.size(),
            selected.end + opening.size()),
    };
}

} // namespace document_inline_format_detail

inline std::optional<DocumentTransaction> document_toggle_inline_format(
    EditorDocument& document,
    const TextSelection& selection,
    InlineFormat format) {
    if (selection.anchor.container_id != selection.active.container_id) return std::nullopt;

    auto* owner = document_edit_detail::find_inline_owner(
        document.root,
        selection.active.container_id);
    if (!owner) return std::nullopt;

    const auto start = (std::min)(selection.anchor.source_offset, selection.active.source_offset);
    const auto end = (std::max)(selection.anchor.source_offset, selection.active.source_offset);
    if (end > owner->source.size()) return std::nullopt;

    const auto selected = SourceRange{start, end};
    auto change = [&] {
        const auto existing = document_inline_format_detail::find_innermost_format(
            owner->tree.nodes,
            document_inline_format_detail::cst_kind(format),
            start,
            end,
            selection.is_caret());
        if (existing) {
            return std::optional{document_inline_format_detail::remove_existing_format(
                selection.active.container_id,
                *owner,
                selection,
                selected,
                *existing)};
        }
        const auto markers = document_inline_format_detail::default_markers(
            format,
            owner->syntax_mode);
        if (!markers) return std::optional<document_inline_format_detail::FormatSourceChange>{};
        return std::optional{document_inline_format_detail::add_format(
            selection.active.container_id,
            *owner,
            selection,
            selected,
            *markers)};
    }();
    if (!change) return std::nullopt;

    const auto revision_before = document.revision;
    document_edit_detail::NodeAllocator allocator(document);
    auto applied = document_edit_detail::edit_inline(
        document,
        selection.active.container_id,
        change->range,
        std::move(change->replacement),
        allocator);
    if (!applied) return std::nullopt;

    ++document.revision;
    return document_edit_detail::source_transaction(
        document,
        std::move(*applied),
        selection,
        change->selection_after,
        revision_before,
        DocumentTransactionReason::Format);
}

} // namespace folia
