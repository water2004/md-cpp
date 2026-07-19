// folia.core.outline — Outline + hierarchy projection from cached heading symbols.
export module folia.core.outline;
import std;
import folia.core.ids;
import folia.core.instrumentation;
import folia.core.slug;
import folia.core.symbols;
import folia.core.text_edit;

export namespace folia {

struct OutlineItem {
    NodeId id{};
    std::uint8_t level = 0;
    std::string title_plain_text;
    TextPosition position;
    std::string slug;
    std::vector<OutlineItem> children;

    OutlineItem() = default;
    OutlineItem(NodeId id_, std::uint8_t lvl, std::string t, std::string sl)
        : id(id_), level(lvl), title_plain_text(std::move(t)), position{id_, 0, TextAffinity::Downstream}, slug(std::move(sl)) {}
};

struct Outline {
    std::uint64_t revision = 0;
    // Changes only when the outline's headings/hierarchy are re-derived.
    // `revision` still tracks the owning document revision for consumers that
    // need freshness even when the visible outline is unchanged.
    std::uint64_t content_revision = 0;
    std::uint64_t content_key = 0;
    std::vector<OutlineItem> items;

    static Outline empty(std::uint64_t rev) {
        Outline o;
        o.revision = rev;
        o.content_revision = rev;
        return o;
    }

    const OutlineItem* find_active_item(TextPosition position) const {
        const OutlineItem* found = nullptr;
        for (const auto& it : items) {
            if (search_item_(it, position.container_id, found)) break;
        }
        return found;
    }
    const OutlineItem* find_item_by_slug(std::string_view s) const {
        for (const auto& it : items) if (it.slug == s) return &it;
        // recurse
        for (const auto& it : items) {
            const auto* r = find_item_by_slug_in_(it.children, s);
            if (r) return r;
        }
        return nullptr;
    }
    const OutlineItem* find_item_by_url_fragment(std::string_view fragment) const {
        if (!fragment.empty() && fragment.front() == '#') fragment.remove_prefix(1);
        return find_item_by_slug(percent_decode_url_component(fragment));
    }
    const OutlineItem* find_item_by_id(NodeId id) const {
        const OutlineItem* r = nullptr;
        for (const auto& it : items) if (find_item_by_id_(it, id, r)) return r;
        return r;
    }
    // Flat items (DFS, pre-order), used by TOC nav and render.
    std::vector<const OutlineItem*> flat_items() const {
        std::vector<const OutlineItem*> out;
        for (const auto& it : items) push_flat_(it, out);
        return out;
    }

private:
    static void push_flat_(const OutlineItem& it, std::vector<const OutlineItem*>& out) {
        out.push_back(&it);
        for (const auto& c : it.children) push_flat_(c, out);
    }
    static bool search_item_(const OutlineItem& it, NodeId container_id, const OutlineItem*& found) {
        // returns true if we should STOP searching siblings (deepest match found in this subtree)
        bool any_in = it.id == container_id;
        bool deeper = false;
        for (const auto& c : it.children) {
            if (search_item_(c, container_id, found)) { deeper = true; break; }
        }
        if (deeper) return true;
        if (any_in) { found = &it; return true; }
        return false;
    }
    static const OutlineItem* find_item_by_slug_in_(const std::vector<OutlineItem>& ch, std::string_view s) {
        for (const auto& it : ch) {
            if (it.slug == s) return &it;
            if (const auto* r = find_item_by_slug_in_(it.children, s)) return r;
        }
        return nullptr;
    }
    static bool find_item_by_id_(const OutlineItem& it, NodeId id, const OutlineItem*& r) {
        if (it.id == id) { r = &it; return true; }
        for (const auto& c : it.children) if (find_item_by_id_(c, id, r)) return true;
        return false;
    }
};

// Heading symbols are already maintained in document order by the editor.
// Projecting the outline from that cache avoids another full block-tree walk
// after a heading edit and during initial document construction.
inline Outline build_outline_from_headings(
    std::uint64_t revision,
    std::span<const HeadingSymbol> headings) {
    record_full_document_outline_derivation();
    std::vector<OutlineItem> linear;
    linear.reserve(headings.size());
    for (const auto& heading : headings) {
        linear.emplace_back(
            heading.node_id,
            heading.level,
            heading.title,
            std::string{});
    }

    // assign unique slugs
    {
        std::vector<std::string> titles;
        for (const auto& it : linear) titles.push_back(it.title_plain_text);
        auto slugs = generate_unique_slugs(titles);
        for (std::size_t i = 0; i < linear.size() && i < slugs.size(); ++i) linear[i].slug = slugs[i];
    }

    // stack-based hierarchy
    std::vector<OutlineItem> root;
    std::vector<OutlineItem*> stack;
    auto push_to_stack = [&](OutlineItem& item) {
        // get a stable pointer in `root` or the parent's children vector
        if (stack.empty()) {
            root.push_back(item);
            stack.push_back(&root.back());
        } else {
            stack.back()->children.push_back(item);
            stack.push_back(&stack.back()->children.back());
        }
    };
    for (auto& item : linear) {
        while (!stack.empty() && stack.back()->level >= item.level) stack.pop_back();
        push_to_stack(item);
    }
    Outline o;
    o.revision = revision;
    o.content_revision = revision;
    o.items = std::move(root);
    std::uint64_t content_key = 1469598103934665603ull;
    auto append_byte = [&](std::uint8_t value) {
        content_key ^= value;
        content_key *= 1099511628211ull;
    };
    auto append_scalar = [&](std::uint64_t value) {
        for (std::size_t index = 0; index < sizeof(value); ++index) {
            append_byte(static_cast<std::uint8_t>(value & 0xffu));
            value >>= 8;
        }
    };
    for (auto const& item : linear) {
        append_scalar(item.id.v);
        append_scalar(item.level);
        for (auto byte : item.title_plain_text) append_byte(static_cast<std::uint8_t>(byte));
        append_byte(0xffu);
        for (auto byte : item.slug) append_byte(static_cast<std::uint8_t>(byte));
        append_byte(0xfeu);
    }
    o.content_key = content_key;
    return o;
}

// Navigation: previous/next heading by slug in flat order.
inline const OutlineItem* navigate_prev_heading(const std::vector<OutlineItem>& items, std::string_view slug) {
    Outline tmp; tmp.items = items;
    auto flat = tmp.flat_items();
    for (std::size_t i = 0; i < flat.size(); ++i) {
        if (flat[i]->slug == slug && i > 0) return flat[i - 1];
    }
    return nullptr;
}
inline const OutlineItem* navigate_next_heading(const std::vector<OutlineItem>& items, std::string_view slug) {
    Outline tmp; tmp.items = items;
    auto flat = tmp.flat_items();
    for (std::size_t i = 0; i < flat.size(); ++i) {
        if (flat[i]->slug == slug && i + 1 < flat.size()) return flat[i + 1];
    }
    return nullptr;
}

} // namespace folia
