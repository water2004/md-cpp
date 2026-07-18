// elmd.core.search — mode-specific document search projections.
//
// Rendered mode searches a stable projection of visible editable text. Syntax
// markers and HTML tags never enter that projection. Source mode searches the
// authoritative flat Markdown buffer directly. Both modes share only the
// Unicode regular-expression matcher.
export module elmd.core.search;
import std;
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.document_text;
import elmd.core.ids;
import elmd.core.render_builder;
import elmd.core.render_model;
import elmd.core.text_edit;

export namespace elmd {

struct SearchOptions {
    bool regular_expression = false;
    bool case_sensitive = false;

    bool operator==(SearchOptions const&) const = default;
};

struct SearchTextMatch {
    SourceRange range;
    std::u32string matched_text;
    // Expanded replacement text. This is empty when no replacement template
    // was requested; it may also legitimately be empty after expansion.
    std::optional<std::u32string> replacement;
};

struct SearchTextResult {
    std::vector<SearchTextMatch> matches;
    std::optional<std::string> error;

    bool valid() const { return !error.has_value(); }
};

struct RenderedSearchFragment {
    NodeId container_id{};
    std::u32string text;
    // One source range per visible code point. A decoded entity or collapsed
    // HTML whitespace run may map one visible character to several source
    // characters. Markdown/HTML markers have no entry at all.
    std::vector<SourceRange> source_ranges;

    bool invariant_holds() const { return text.size() == source_ranges.size(); }
};

struct RenderedSearchMatch {
    NodeId container_id{};
    SourceRange visible_range;
    std::u32string matched_text;
    std::optional<std::u32string> replacement;
    // A match can cross hidden formatting markers, so it may map to several
    // disjoint source ranges within one authoritative inline source.
    std::vector<SourceRange> source_ranges;

    bool is_zero_length() const { return visible_range.empty(); }
};

struct RenderedSearchResult {
    std::vector<RenderedSearchMatch> matches;
    std::optional<std::string> error;

    bool valid() const { return !error.has_value(); }
};

namespace search_detail {

inline void append_item_projection(
    RenderedSearchFragment& projection,
    InlineRenderItem const& item) {
    using Kind = InlineRenderItem::Kind;
    if (item.kind == Kind::Marker
        || item.kind == Kind::Math
        || item.kind == Kind::Image
        || item.kind == Kind::FootnoteReference) {
        return;
    }
    if (item.kind == Kind::Link) {
        for (auto const& child : item.special().semantic().children) {
            append_item_projection(projection, child);
        }
        return;
    }
    if (item.kind != Kind::Text || item.text.empty()) return;

    auto const source = item.source_span.source_range;
    projection.text.append(item.text);
    if (source.length() == item.text.size()) {
        for (std::size_t index = 0; index < item.text.size(); ++index) {
            projection.source_ranges.push_back({
                source.start + index,
                source.start + index + 1});
        }
        return;
    }
    // Entity decoding and HTML whitespace collapsing deliberately retain one
    // atomic source range rather than inventing a second logical offset.
    projection.source_ranges.insert(
        projection.source_ranges.end(),
        item.text.size(),
        source);
}

inline RenderedSearchFragment project_inline_owner(
    Builder& builder,
    BlockNode const& block) {
    RenderedSearchFragment projection;
    projection.container_id = block.id;
    auto const preserve_soft_breaks = block.kind == BlockKind::Heading;
    auto items = builder.build_inline_document(
        block.inline_content,
        block.id,
        InlineStyle::plain(),
        preserve_soft_breaks);
    for (auto const& item : items) append_item_projection(projection, item);
    return projection;
}

inline RenderedSearchFragment project_code_owner(BlockNode const& block) {
    RenderedSearchFragment projection;
    projection.container_id = block.id;
    auto const content = block_source_content(block.block_source);
    projection.text = content;
    projection.source_ranges.reserve(content.size());
    for (std::size_t index = 0; index < content.size(); ++index) {
        projection.source_ranges.push_back({
            block_source_offset_for_content(block.block_source, index),
            block_source_offset_for_content(block.block_source, index + 1)});
    }
    return projection;
}

inline std::vector<SourceRange> source_ranges_for_visible_match(
    RenderedSearchFragment const& fragment,
    SourceRange visible_range) {
    std::vector<SourceRange> result;
    if (visible_range.empty() || !visible_range.valid_for(fragment.source_ranges.size())) {
        return result;
    }
    for (std::size_t index = visible_range.start; index < visible_range.end; ++index) {
        auto const current = fragment.source_ranges[index];
        if (current.empty()) continue;
        if (!result.empty() && current.start <= result.back().end) {
            result.back().end = (std::max)(result.back().end, current.end);
        } else if (result.empty() || current != result.back()) {
            result.push_back(current);
        }
    }
    return result;
}

inline std::size_t source_boundary_for_visible_offset(
    RenderedSearchFragment const& fragment,
    std::size_t offset) {
    offset = (std::min)(offset, fragment.source_ranges.size());
    if (offset < fragment.source_ranges.size()) return fragment.source_ranges[offset].start;
    if (!fragment.source_ranges.empty()) return fragment.source_ranges.back().end;
    return 0;
}

} // namespace search_detail

SearchTextResult search_text(
    std::u32string_view text,
    std::u32string_view query,
    SearchOptions options = {});

SearchTextResult search_text_for_replacement(
    std::u32string_view text,
    std::u32string_view query,
    std::u32string_view replacement_template,
    SearchOptions options = {});

inline std::vector<RenderedSearchFragment> rendered_search_fragments(
    EditorDocument const& document) {
    std::vector<RenderedSearchFragment> result;
    Builder builder(default_theme_profile());
    walk_blocks(document.root, [&](BlockNode const& block) {
        if (editable_inline_document(block)) {
            auto projection = search_detail::project_inline_owner(builder, block);
            if (projection.invariant_holds()) result.push_back(std::move(projection));
            return;
        }
        if (block.kind == BlockKind::CodeBlock) {
            auto projection = search_detail::project_code_owner(block);
            if (projection.invariant_holds()) result.push_back(std::move(projection));
        }
        // Math, images, generated controls and structural containers are not
        // ordinary visible text. Their Markdown remains searchable in source
        // mode, but they do not pollute rendered-mode results.
    });
    return result;
}

namespace search_detail {

inline RenderedSearchResult search_rendered_document_impl(
    std::span<const RenderedSearchFragment> fragments,
    std::u32string_view query,
    SearchOptions options,
    std::u32string_view const* replacement_template) {
    RenderedSearchResult result;
    for (auto const& fragment : fragments) {
        auto text_result = replacement_template
            ? search_text_for_replacement(
                fragment.text,
                query,
                *replacement_template,
                options)
            : search_text(fragment.text, query, options);
        if (!text_result.valid()) {
            result.error = std::move(text_result.error);
            result.matches.clear();
            return result;
        }
        for (auto& text_match : text_result.matches) {
            RenderedSearchMatch match;
            match.container_id = fragment.container_id;
            match.visible_range = text_match.range;
            match.matched_text = std::move(text_match.matched_text);
            match.replacement = std::move(text_match.replacement);
            match.source_ranges = search_detail::source_ranges_for_visible_match(
                fragment,
                match.visible_range);
            if (match.visible_range.empty()) {
                auto boundary = search_detail::source_boundary_for_visible_offset(
                    fragment,
                    match.visible_range.start);
                match.source_ranges.push_back({boundary, boundary});
            }
            result.matches.push_back(std::move(match));
        }
    }
    return result;
}

} // namespace search_detail

inline RenderedSearchResult search_rendered_document(
    EditorDocument const& document,
    std::u32string_view query,
    SearchOptions options = {}) {
    auto fragments = rendered_search_fragments(document);
    return search_detail::search_rendered_document_impl(
        fragments,
        query,
        options,
        nullptr);
}

inline RenderedSearchResult search_rendered_document_for_replacement(
    EditorDocument const& document,
    std::u32string_view query,
    std::u32string_view replacement_template,
    SearchOptions options = {}) {
    auto fragments = rendered_search_fragments(document);
    return search_detail::search_rendered_document_impl(
        fragments,
        query,
        options,
        &replacement_template);
}

inline RenderedSearchResult search_rendered_fragments(
    std::span<const RenderedSearchFragment> fragments,
    std::u32string_view query,
    SearchOptions options = {}) {
    return search_detail::search_rendered_document_impl(
        fragments, query, options, nullptr);
}

inline RenderedSearchResult search_rendered_fragments_for_replacement(
    std::span<const RenderedSearchFragment> fragments,
    std::u32string_view query,
    std::u32string_view replacement_template,
    SearchOptions options = {}) {
    return search_detail::search_rendered_document_impl(
        fragments, query, options, &replacement_template);
}

} // namespace elmd
