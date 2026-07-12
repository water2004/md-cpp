// elmd.core.outline — Outline + build_outline_from_blocks (stack hierarchy).
export module elmd.core.outline;
import std;
import elmd.core.types;
import elmd.core.ids;
import elmd.core.ast;
import elmd.core.slug;
import elmd.core.utf;

export namespace elmd {

struct OutlineItem {
    NodeId id{};
    std::uint8_t level = 0;
    std::string title_plain_text;
    TextRange<CharOffset> source_range;
    std::string slug;
    std::vector<OutlineItem> children;

    OutlineItem() = default;
    OutlineItem(NodeId id_, std::uint8_t lvl, std::string t, TextRange<CharOffset> sr, std::string sl)
        : id(id_), level(lvl), title_plain_text(std::move(t)), source_range(sr), slug(std::move(sl)) {}
};

struct Outline {
    std::uint64_t revision = 0;
    std::vector<OutlineItem> items;

    static Outline empty(std::uint64_t rev) { Outline o; o.revision = rev; return o; }

    const OutlineItem* find_active_item(CharOffset off) const {
        const OutlineItem* found = nullptr;
        for (const auto& it : items) {
            if (search_item_(it, off, found)) break;
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
    static bool search_item_(const OutlineItem& it, CharOffset off, const OutlineItem*& found) {
        // returns true if we should STOP searching siblings (deepest match found in this subtree)
        bool any_in = it.source_range.contains(off);
        bool deeper = false;
        for (const auto& c : it.children) {
            if (search_item_(c, off, found)) { deeper = true; break; }
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

// Build the outline from a list of blocks: extract a linear list of heading
// items (recursing into containers) and then build a hierarchy via a stack.
inline Outline build_outline_from_blocks(std::uint64_t revision, const std::vector<BlockNode>& blocks) {
    std::vector<OutlineItem> linear;
    auto extract = [&](auto& self, const std::vector<BlockNode>& bs) -> void {
        for (const auto& b : bs) {
            if (b.kind == BlockKind::Heading) {
                std::string title = cps_to_utf8(inline_visible_text(b.inline_content));
                linear.emplace_back(b.id, b.level, title, TextRange<CharOffset>{}, std::string{});
            } else if (!b.children.empty()) {
                self(self, b.children);
            }
        }
    };
    extract(extract, blocks);

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
    auto append_to_parent = [&](OutlineItem item) {
        if (stack.empty()) root.push_back(item);
        else stack.back()->children.push_back(item);
    };
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
    o.items = std::move(root);
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

} // namespace elmd
