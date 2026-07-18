// folia.core.html_inline_flow — browser-like whitespace projection for HTML
// inline formatting contexts. Source text remains untouched; this derives
// visible runs with exact source spans for the DirectWrite flow.
export module folia.core.html_inline_flow;
import std;
import folia.core.render_model;

export namespace folia {

inline bool is_html_collapsible_space(char32_t ch) {
    return ch == U' ' || ch == U'\t' || ch == U'\r' || ch == U'\n' || ch == U'\f';
}

inline bool html_inline_media_only(const std::vector<InlineRenderItem>& children) {
    bool saw_media = false;
    for (const auto& child : children) {
        if (child.kind == InlineRenderItem::Kind::Marker) continue;
        if (child.kind == InlineRenderItem::Kind::Text
            && std::ranges::all_of(child.text, is_html_collapsible_space)) continue;
        if (child.kind == InlineRenderItem::Kind::Image) {
            saw_media = true;
            continue;
        }
        if (child.kind == InlineRenderItem::Kind::Link
            && html_inline_media_only(child.special().semantic().children)) {
            saw_media = true;
            continue;
        }
        return false;
    }
    return saw_media;
}

inline void normalize_html_inline_whitespace(
    std::vector<InlineRenderItem>& items,
    bool trim_edges) {
    auto is_whitespace_item = [](const InlineRenderItem& item) {
        return item.kind == InlineRenderItem::Kind::Text
            && !item.text.empty()
            && std::ranges::all_of(item.text, is_html_collapsible_space);
    };

    std::vector<InlineRenderItem> segmented;
    segmented.reserve(items.size());
    for (auto& item : items) {
        if (item.kind == InlineRenderItem::Kind::Link) {
            auto& children = item.ensure_special().ensure_semantic().children;
            normalize_html_inline_whitespace(children, html_inline_media_only(children));
        }
        if (item.kind != InlineRenderItem::Kind::Text
            || item.text.size() != item.source_span.source_range.length()) {
            segmented.push_back(std::move(item));
            continue;
        }

        std::size_t cursor = 0;
        while (cursor < item.text.size()) {
            const auto whitespace = is_html_collapsible_space(item.text[cursor]);
            auto end = cursor + 1;
            while (end < item.text.size()
                && is_html_collapsible_space(item.text[end]) == whitespace) ++end;
            auto part = item;
            part.text = whitespace ? U" " : item.text.substr(cursor, end - cursor);
            part.source_span.source_range = {
                item.source_span.source_range.start + cursor,
                item.source_span.source_range.start + end};
            if (whitespace) part.style.link = false;
            segmented.push_back(std::move(part));
            cursor = end;
        }
    }

    std::vector<InlineRenderItem> collapsed;
    collapsed.reserve(segmented.size());
    bool previous_visible_was_space = false;
    for (auto& item : segmented) {
        if (item.kind == InlineRenderItem::Kind::Marker) {
            collapsed.push_back(std::move(item));
            continue;
        }
        const auto whitespace = is_whitespace_item(item);
        if (whitespace && previous_visible_was_space) continue;
        previous_visible_was_space = whitespace;
        collapsed.push_back(std::move(item));
    }

    if (trim_edges) {
        auto first_visible = std::ranges::find_if(collapsed, [](const InlineRenderItem& item) {
            return item.kind != InlineRenderItem::Kind::Marker;
        });
        if (first_visible != collapsed.end() && is_whitespace_item(*first_visible)) {
            collapsed.erase(first_visible);
        }
        auto last_visible = std::ranges::find_if(
            collapsed.rbegin(), collapsed.rend(), [](const InlineRenderItem& item) {
                return item.kind != InlineRenderItem::Kind::Marker;
            });
        if (last_visible != collapsed.rend() && is_whitespace_item(*last_visible)) {
            collapsed.erase(std::next(last_visible).base());
        }
    }
    items = std::move(collapsed);
}

} // namespace folia
