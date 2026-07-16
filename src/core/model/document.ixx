// elmd.core.document — EditorDocument, the parsed and editable document root.
export module elmd.core.document;
import std;
import elmd.core.types;
import elmd.core.ids;
import elmd.core.ast;
import elmd.core.block_tree;
import elmd.core.metadata;
import elmd.core.diagnostics;
import elmd.core.dialect;
import elmd.core.instrumentation;

export namespace elmd {

struct EditorDocument {
    std::uint64_t revision = 1;
    // Monotonic identity source for every block, inline CST node, and inline
    // token owned by this document. Zero is reserved for externally assembled
    // documents that need one lazy calibration before their first edit.
    std::uint64_t next_node_id = 0;
    MarkdownDialect dialect = default_dialect();
    BlockNode root = [] {
        BlockNode node;
        node.id = NodeId(1);
        node.kind = BlockKind::Document;
        return node;
    }();
    DocumentMetadata metadata;
    std::vector<Diagnostic> diagnostics;
    bool trailing_newline = false;
    // Derived lookup acceleration only. Paths are validated against the
    // authoritative tree before use and lazily repaired after structure edits.
    mutable std::unordered_map<std::uint64_t, BlockPath> cached_block_paths;
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
        d.cached_block_paths.emplace(d.root.id.v, BlockPath{});
        d.cached_block_paths.emplace(paragraph.id.v, BlockPath{0});
        d.cached_editable_order.push_back(paragraph.id);
        d.cached_editable_index.emplace(paragraph.id.v, 0);
        return d;
    }
};

inline void rebuild_document_block_index(EditorDocument& document) {
    record_full_document_block_index_scan();
    document.cached_block_paths.clear();
    document.cached_editable_order.clear();
    document.cached_editable_index.clear();
    BlockPath path;
    auto visit = [&](auto& self, const BlockNode& block) -> void {
        document.cached_block_paths[block.id.v] = path;
        if (block.kind != BlockKind::Document && is_editable_block_owner(block.kind)) {
            document.cached_editable_index.emplace(
                block.id.v,
                document.cached_editable_order.size());
            document.cached_editable_order.push_back(block.id);
        }
        for (std::size_t index = 0; index < block.children.size(); ++index) {
            path.push_back(index);
            self(self, block.children[index]);
            path.pop_back();
        }
    };
    visit(visit, document.root);
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
    if (auto cached = document.cached_block_paths.find(id.v);
        cached != document.cached_block_paths.end()) {
        const auto* block = block_at_path(document.root, cached->second);
        if (block && block->id == id) return cached->second;
        document.cached_block_paths.erase(cached);
    }
    record_full_document_block_index_scan();
    auto path = block_path(document.root, id);
    if (path) document.cached_block_paths[id.v] = *path;
    return path;
}

inline BlockNode* find_document_block(EditorDocument& document, NodeId id) {
    auto path = document_block_path(document, id);
    return path ? block_at_path(document.root, *path) : nullptr;
}

inline const BlockNode* find_document_block(const EditorDocument& document, NodeId id) {
    auto path = document_block_path(document, id);
    return path ? block_at_path(document.root, *path) : nullptr;
}

} // namespace elmd
