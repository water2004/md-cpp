// Incremental maintenance for EditorDocument's derived block-locator and
// editable-owner order indexes. The block tree remains authoritative.
export module elmd.core.document_index_update;
import std;
import elmd.core.ast;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.document_transaction;
import elmd.core.ids;
import elmd.core.instrumentation;

export namespace elmd {

namespace document_index_update_detail {

inline void collect_ids(
    const BlockNode& block,
    std::unordered_set<std::uint64_t>& blocks,
    std::unordered_set<std::uint64_t>& editable) {
    if (block.id.v != 0) {
        blocks.insert(block.id.v);
        if (is_editable_block_owner(block.kind)) editable.insert(block.id.v);
    }
    for (const auto& child : block.children) collect_ids(child, blocks, editable);
}

inline void index_subtree(
    EditorDocument& document,
    const BlockNode& block,
    NodeId parent_id,
    std::size_t child_index) {
    document.cached_block_locators[block.id.v] = {parent_id, child_index};
    for (std::size_t index = 0; index < block.children.size(); ++index) {
        index_subtree(document, block.children[index], block.id, index);
    }
}

inline std::optional<BlockPath> validated_cached_path(
    const EditorDocument& document,
    NodeId id) {
    auto path = cached_document_block_path(document, id);
    if (!path) return std::nullopt;
    const auto* block = block_at_path(document.root, *path);
    return block && block->id == id
        ? std::move(path)
        : std::nullopt;
}

inline void rebuild_editable_positions(
    EditorDocument& document,
    std::size_t start = 0) {
    start = (std::min)(start, document.cached_editable_order.size());
    if (start == 0) document.cached_editable_index.clear();
    for (std::size_t index = start; index < document.cached_editable_order.size(); ++index) {
        document.cached_editable_index[document.cached_editable_order[index].v] = index;
    }
}

inline std::optional<NodeId> last_editable_owner(const BlockNode& block) {
    for (auto child = block.children.rbegin(); child != block.children.rend(); ++child) {
        if (auto owner = last_editable_owner(*child)) return owner;
    }
    return is_editable_block_owner(block.kind)
        ? std::optional<NodeId>{block.id}
        : std::nullopt;
}

inline std::optional<NodeId> previous_editable_owner(
    const EditorDocument& document,
    const BlockPath& path) {
    for (std::size_t depth = path.size(); depth > 0; --depth) {
        BlockPath parent_path(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(depth - 1));
        const auto* parent = block_at_path(document.root, parent_path);
        if (!parent) return std::nullopt;
        const auto child_index = path[depth - 1];
        for (std::size_t index = child_index; index > 0; --index) {
            if (auto owner = last_editable_owner(parent->children[index - 1])) return owner;
        }
        if (is_editable_block_owner(parent->kind)) return parent->id;
    }
    return std::nullopt;
}

inline void collect_editable_order(const BlockNode& block, std::vector<NodeId>& order) {
    if (is_editable_block_owner(block.kind)) order.push_back(block.id);
    for (const auto& child : block.children) collect_editable_order(child, order);
}

struct AddedRoot {
    NodeId id{};
    BlockPath path;
};

} // namespace document_index_update_detail

inline bool update_document_block_index(
    EditorDocument& document,
    const std::vector<DocumentOperation>& operations,
    bool forward) {
    using namespace document_index_update_detail;

    std::unordered_map<std::uint64_t, std::size_t> affected_parents;
    std::unordered_set<std::uint64_t> removed_blocks;
    std::unordered_set<std::uint64_t> removed_editable;
    std::unordered_set<std::uint64_t> added_blocks;
    std::unordered_set<std::uint64_t> added_editable;
    std::unordered_set<std::uint64_t> relocated_roots;
    std::vector<NodeId> added_roots;

    auto note_parent = [&](NodeId id, std::size_t index) {
        auto [found, inserted] = affected_parents.emplace(id.v, index);
        if (!inserted) found->second = (std::min)(found->second, index);
    };

    for (const auto& operation : operations) {
        const auto* tree = std::get_if<DocumentTreeEdit>(&operation);
        if (!tree) continue;
        if (tree->kind == DocumentTreeEditKind::Move) {
            if (tree->moved_id.v == 0) return false;
            note_parent(tree->parent_id, tree->index);
            note_parent(tree->other_parent_id, tree->other_index);
            relocated_roots.insert(tree->moved_id.v);
            continue;
        }
        if (tree->kind == DocumentTreeEditKind::UpdatePayload) {
            if (tree->before.id.v == 0
                || tree->before.id != tree->after.id
                || is_editable_block_owner(tree->before.kind)
                != is_editable_block_owner(tree->after.kind)) {
                return false;
            }
            continue;
        }

        note_parent(tree->parent_id, tree->index);
        const bool adds = tree->kind == DocumentTreeEditKind::Insert
            ? forward
            : !forward;
        const auto& payload = tree->kind == DocumentTreeEditKind::Insert
            ? tree->after
            : tree->before;
        if (adds) {
            if (payload.id.v != 0) {
                added_roots.push_back(payload.id);
                collect_ids(payload, added_blocks, added_editable);
            }
        } else {
            collect_ids(payload, removed_blocks, removed_editable);
        }
    }

    for (const auto id : removed_blocks) document.cached_block_locators.erase(id);

    struct PendingParent {
        NodeId id{};
        std::size_t start = 0;
        bool done = false;
    };
    std::vector<PendingParent> pending;
    pending.reserve(affected_parents.size());
    for (const auto& [id, start] : affected_parents) {
        pending.push_back({NodeId{id}, start, false});
    }

    std::size_t remaining = pending.size();
    while (remaining != 0) {
        bool progressed = false;
        for (auto& item : pending) {
            if (item.done) continue;
            if (removed_blocks.contains(item.id.v)
                && !added_blocks.contains(item.id.v)
                && !relocated_roots.contains(item.id.v)) {
                item.done = true;
                --remaining;
                progressed = true;
                continue;
            }
            auto parent_path = validated_cached_path(document, item.id);
            if (!parent_path) continue;
            auto* parent = block_at_path(document.root, *parent_path);
            if (!parent || parent->id != item.id) return false;
            const auto start = (std::min)(item.start, parent->children.size());
            for (std::size_t index = start; index < parent->children.size(); ++index) {
                index_subtree(document, parent->children[index], parent->id, index);
            }
            item.done = true;
            --remaining;
            progressed = true;
        }
        if (!progressed) return false;
    }

    std::unordered_set<std::uint64_t> relocated_blocks;
    for (const auto id : relocated_roots) {
        if (removed_blocks.contains(id)) continue;
        const auto path = validated_cached_path(document, NodeId{id});
        if (!path) return false;
        const auto* block = block_at_path(document.root, *path);
        if (!block || block->id.v != id) return false;
        collect_ids(*block, relocated_blocks, removed_editable);
        added_roots.push_back(block->id);
    }

    if (!removed_editable.empty()) {
        std::erase_if(document.cached_editable_order, [&](NodeId id) {
            return removed_editable.contains(id.v);
        });
        rebuild_editable_positions(document);
    }

    std::vector<AddedRoot> added;
    added.reserve(added_roots.size());
    for (const auto id : added_roots) {
        const auto path = validated_cached_path(document, id);
        if (!path) return false;
        added.push_back({id, *path});
    }
    std::ranges::sort(added, [](const auto& left, const auto& right) {
        return left.path < right.path;
    });

    for (const auto& root : added) {
        const auto* block = block_at_path(document.root, root.path);
        if (!block || block->id != root.id) return false;
        std::vector<NodeId> owners;
        collect_editable_order(*block, owners);
        std::erase_if(owners, [&](NodeId id) {
            return document.cached_editable_index.contains(id.v);
        });
        if (owners.empty()) continue;

        std::size_t insertion = 0;
        if (auto previous = previous_editable_owner(document, root.path)) {
            const auto found = document.cached_editable_index.find(previous->v);
            if (found == document.cached_editable_index.end()) return false;
            insertion = found->second + 1;
        }
        insertion = (std::min)(insertion, document.cached_editable_order.size());
        document.cached_editable_order.insert(
            document.cached_editable_order.begin() + static_cast<std::ptrdiff_t>(insertion),
            owners.begin(),
            owners.end());
        rebuild_editable_positions(document, insertion);
    }

    record_incremental_document_block_index_repair();
    return true;
}

} // namespace elmd
