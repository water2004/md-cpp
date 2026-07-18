// folia.core.document — EditorDocument, the parsed and editable document root.
export module folia.core.document;
import std;
import folia.core.types;
import folia.core.ids;
import folia.core.ast;
import folia.core.block_tree;
import folia.core.metadata;
import folia.core.diagnostics;
import folia.core.dialect;
import folia.core.instrumentation;

export namespace folia {

struct BlockLocator {
    NodeId parent_id{};
    std::size_t child_index = 0;

    bool operator==(BlockLocator const&) const = default;
};

struct EditorDocument {
    std::uint64_t revision = 1;
    // Monotonic identity source for every block, inline CST node, and inline
    // token owned by this document. The default root owns id 1, so 2 is the
    // first unassigned identity; imported subtrees must reserve their ids
    // explicitly before insertion.
    std::uint64_t next_node_id = 2;
    MarkdownDialect dialect = default_dialect();
    BlockNode root = [] {
        BlockNode node;
        node.id = NodeId(1);
        node.kind = BlockKind::Document;
        return node;
    }();
    DocumentMetadata metadata;
    std::vector<Diagnostic> diagnostics;
    // Exact final physical line ending, if the source ends with one. This is
    // deliberately not a boolean: saving must preserve CRLF/CR/LF verbatim.
    std::u32string trailing_line_ending;
    // Derived lookup acceleration only.  Store one tree edge per block rather
    // than an independently allocated root-to-node path.  Paths are rebuilt on
    // demand by following parent edges and are always validated against the
    // authoritative tree before use.
    mutable std::unordered_map<std::uint64_t, BlockLocator> cached_block_locators;
    // Tree-order navigation index. It contains node identities only; text
    // offsets remain block-local and are always resolved against the tree.
    std::vector<NodeId> cached_editable_order;
    std::unordered_map<std::uint64_t, std::size_t> cached_editable_index;

    static EditorDocument empty(std::uint64_t rev) {
        EditorDocument d;
        d.revision = rev;
        BlockNode paragraph;
        paragraph.id = NodeId(2);
        paragraph.kind = BlockKind::Paragraph;
        d.root.children.push_back(paragraph);
        d.next_node_id = 3;
        d.cached_block_locators.emplace(d.root.id.v, BlockLocator{});
        d.cached_block_locators.emplace(paragraph.id.v, BlockLocator{d.root.id, 0});
        d.cached_editable_order.push_back(paragraph.id);
        d.cached_editable_index.emplace(paragraph.id.v, 0);
        return d;
    }
};

inline void rebuild_document_block_index(EditorDocument& document) {
    record_full_document_block_index_scan();
    document.cached_block_locators.clear();
    document.cached_editable_order.clear();
    document.cached_editable_index.clear();
    auto visit = [&](auto& self, const BlockNode& block, NodeId parent_id, std::size_t child_index) -> void {
        document.cached_block_locators[block.id.v] = {parent_id, child_index};
        if (block.kind != BlockKind::Document && is_editable_block_owner(block.kind)) {
            document.cached_editable_index.emplace(
                block.id.v,
                document.cached_editable_order.size());
            document.cached_editable_order.push_back(block.id);
        }
        for (std::size_t index = 0; index < block.children.size(); ++index) {
            self(self, block.children[index], block.id, index);
        }
    };
    visit(visit, document.root, NodeId{}, 0);
}

inline std::optional<BlockPath> cached_document_block_path(
    const EditorDocument& document,
    NodeId id) {
    if (id == document.root.id) return BlockPath{};
    BlockPath reverse_path;
    auto current = id;
    for (std::size_t depth = 0; depth <= document.cached_block_locators.size(); ++depth) {
        auto found = document.cached_block_locators.find(current.v);
        if (found == document.cached_block_locators.end()
            || found->second.parent_id.v == 0) return std::nullopt;
        reverse_path.push_back(found->second.child_index);
        current = found->second.parent_id;
        if (current == document.root.id) {
            std::ranges::reverse(reverse_path);
            return reverse_path;
        }
    }
    return std::nullopt;
}

inline void cache_document_block_path(
    EditorDocument const& document,
    BlockPath const& path) {
    auto const* parent = &document.root;
    document.cached_block_locators[document.root.id.v] = {};
    for (std::size_t depth = 0; depth < path.size(); ++depth) {
        auto index = path[depth];
        if (index >= parent->children.size()) return;
        auto const& child = parent->children[index];
        document.cached_block_locators[child.id.v] = {parent->id, index};
        parent = &child;
    }
}

inline std::optional<std::size_t> document_editable_order_position(
    const EditorDocument& document,
    NodeId id) {
    const auto found = document.cached_editable_index.find(id.v);
    if (found == document.cached_editable_index.end()
        || found->second >= document.cached_editable_order.size()
        || document.cached_editable_order[found->second] != id) {
        return std::nullopt;
    }
    return found->second;
}

inline std::optional<BlockPath> document_block_path(
    const EditorDocument& document,
    NodeId id) {
    if (auto cached = cached_document_block_path(document, id)) {
        const auto* block = block_at_path(document.root, *cached);
        if (block && block->id == id) return cached;
        document.cached_block_locators.erase(id.v);
    }
    record_full_document_block_index_scan();
    auto path = block_path(document.root, id);
    if (path) cache_document_block_path(document, *path);
    return path;
}

inline std::optional<NodeId> document_top_level_block_id(
    EditorDocument const& document,
    NodeId id) {
    if (id == document.root.id) return std::nullopt;
    auto current = id;
    for (std::size_t depth = 0; depth <= document.cached_block_locators.size(); ++depth) {
        auto found = document.cached_block_locators.find(current.v);
        if (found == document.cached_block_locators.end()
            || found->second.parent_id.v == 0) return std::nullopt;
        if (found->second.parent_id == document.root.id) return current;
        current = found->second.parent_id;
    }
    return std::nullopt;
}

inline BlockNode* find_document_block(EditorDocument& document, NodeId id) {
    auto path = document_block_path(document, id);
    return path ? block_at_path(document.root, *path) : nullptr;
}

inline const BlockNode* find_document_block(const EditorDocument& document, NodeId id) {
    auto path = document_block_path(document, id);
    return path ? block_at_path(document.root, *path) : nullptr;
}

} // namespace folia
