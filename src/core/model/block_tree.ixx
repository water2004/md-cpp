// Uniform structural operations for the Markdown block tree.
export module elmd.core.block_tree;
import std;
import elmd.core.ast;
import elmd.core.ids;

export namespace elmd {

using BlockPath = std::vector<std::size_t>;

inline BlockNode* block_at_path(BlockNode& root, const BlockPath& path) {
    auto* node = &root;
    for (const auto index : path) {
        if (index >= node->children.size()) return nullptr;
        node = &node->children[index];
    }
    return node;
}

inline const BlockNode* block_at_path(const BlockNode& root, const BlockPath& path) {
    const auto* node = &root;
    for (const auto index : path) {
        if (index >= node->children.size()) return nullptr;
        node = &node->children[index];
    }
    return node;
}

inline std::optional<BlockPath> block_path(const BlockNode& root, NodeId id) {
    BlockPath path;
    std::function<bool(const BlockNode&)> visit = [&](const BlockNode& node) {
        if (node.id == id) return true;
        for (std::size_t index = 0; index < node.children.size(); ++index) {
            path.push_back(index);
            if (visit(node.children[index])) return true;
            path.pop_back();
        }
        return false;
    };
    if (!visit(root)) return std::nullopt;
    return path;
}

inline BlockNode* find_block(BlockNode& root, NodeId id) {
    const auto path = block_path(root, id);
    return path ? block_at_path(root, *path) : nullptr;
}

inline const BlockNode* find_block(const BlockNode& root, NodeId id) {
    const auto path = block_path(root, id);
    return path ? block_at_path(root, *path) : nullptr;
}

inline BlockNode* find_parent_block(BlockNode& root, NodeId id) {
    auto path = block_path(root, id);
    if (!path || path->empty()) return nullptr;
    path->pop_back();
    return block_at_path(root, *path);
}

inline const BlockNode* find_parent_block(const BlockNode& root, NodeId id) {
    auto path = block_path(root, id);
    if (!path || path->empty()) return nullptr;
    path->pop_back();
    return block_at_path(root, *path);
}

template <class Visitor>
inline void walk_blocks(BlockNode& root, Visitor&& visitor) {
    visitor(root);
    for (auto& child : root.children) walk_blocks(child, visitor);
}

template <class Visitor>
inline void walk_blocks(const BlockNode& root, Visitor&& visitor) {
    visitor(root);
    for (const auto& child : root.children) walk_blocks(child, visitor);
}

inline bool insert_block(BlockNode& parent, std::size_t index, BlockNode node) {
    if (index > parent.children.size()) return false;
    parent.children.insert(parent.children.begin() + static_cast<std::ptrdiff_t>(index), std::move(node));
    return true;
}

inline std::optional<BlockNode> remove_block(BlockNode& parent, std::size_t index) {
    if (index >= parent.children.size()) return std::nullopt;
    BlockNode removed = std::move(parent.children[index]);
    parent.children.erase(parent.children.begin() + static_cast<std::ptrdiff_t>(index));
    return removed;
}

inline bool replace_block(BlockNode& parent, std::size_t index, BlockNode replacement) {
    if (index >= parent.children.size()) return false;
    parent.children[index] = std::move(replacement);
    return true;
}

inline bool move_block(BlockNode& from_parent, std::size_t from_index, BlockNode& to_parent, std::size_t to_index) {
    if (from_index >= from_parent.children.size() || to_index > to_parent.children.size()) return false;
    auto node = remove_block(from_parent, from_index);
    if (!node) return false;
    if (&from_parent == &to_parent && to_index > from_index) --to_index;
    return insert_block(to_parent, to_index, std::move(*node));
}

} // namespace elmd
