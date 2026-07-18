// folia.core.document_search_replace — rendered-mode semantic replacements.
// Search matches are already projected back to one or more block-local source
// ranges. This layer applies those edits as one reversible document transaction
// without serializing or reparsing the full document.
export module folia.core.document_search_replace;
import std;
import folia.core.document;
import folia.core.document_edit_support;
import folia.core.document_text;
import folia.core.document_transaction;
import folia.core.ids;
import folia.core.search;
import folia.core.selection;
import folia.core.text_edit;

export namespace folia {

namespace document_search_replace_detail {

struct PlannedReplacement {
    NodeId container_id{};
    SourceRange range;
    std::u32string replacement;
    std::size_t document_order = 0;
};

inline std::optional<std::vector<PlannedReplacement>> plan_replacements(
    EditorDocument const& document,
    std::span<const RenderedSearchMatch> matches) {
    std::vector<PlannedReplacement> plan;
    for (auto const& match : matches) {
        if (!match.replacement || match.source_ranges.empty()) return std::nullopt;
        auto order = document_editable_order_position(document, match.container_id);
        auto source = document_editable_text_view(document, match.container_id);
        if (!order || !source) return std::nullopt;
        for (std::size_t index = 0; index < match.source_ranges.size(); ++index) {
            auto range = match.source_ranges[index];
            if (!range.valid_for(source->size())) return std::nullopt;
            plan.push_back({
                match.container_id,
                range,
                index == 0 ? *match.replacement : std::u32string{},
                *order});
        }
    }
    std::ranges::sort(plan, [](auto const& left, auto const& right) {
        if (left.document_order != right.document_order) {
            return left.document_order > right.document_order;
        }
        if (left.range.start != right.range.start) return left.range.start > right.range.start;
        return left.range.end > right.range.end;
    });
    // Adjacent ranges are valid; overlapping source edits are ambiguous and
    // could otherwise erase an entity or normalized whitespace twice.
    for (std::size_t index = 1; index < plan.size(); ++index) {
        auto const& previous = plan[index - 1];
        auto const& current = plan[index];
        if (previous.container_id == current.container_id
            && current.range.end > previous.range.start) return std::nullopt;
    }
    return plan;
}

} // namespace document_search_replace_detail

inline std::optional<DocumentTransaction> document_replace_rendered_matches(
    EditorDocument& document,
    TextSelection selection_before,
    std::span<const RenderedSearchMatch> matches) {
    if (matches.empty()) return std::nullopt;
    auto plan = document_search_replace_detail::plan_replacements(document, matches);
    if (!plan || plan->empty()) return std::nullopt;

    // The first match is in document order because search results are emitted
    // by a tree walk. Keep its replacement as the post-command caret target.
    auto const first_container = matches.front().container_id;
    auto const first_offset = matches.front().source_ranges.front().start
        + matches.front().replacement->size();
    auto const revision_before = document.revision;
    std::vector<DocumentOperation> operations;
    document_edit_detail::MutationRollback rollback(document, operations, revision_before);
    document_edit_detail::NodeAllocator allocator(document);
    for (auto& edit : *plan) {
        auto applied = document_edit_detail::edit_block_source(
            document,
            edit.container_id,
            edit.range,
            std::move(edit.replacement),
            allocator);
        if (!applied) return std::nullopt;
        document_edit_detail::append_source_operation(operations, std::move(*applied));
    }
    ++document.revision;
    rollback.commit();
    return make_recorded_document_transaction(
        std::move(operations),
        selection_before,
        TextSelection::caret({first_container, first_offset, TextAffinity::Downstream}),
        revision_before,
        document.revision,
        DocumentTransactionReason::Replace);
}

} // namespace folia
