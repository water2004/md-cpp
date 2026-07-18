#pragma once

namespace {

TextSelection caret(const BlockNode& node, std::size_t offset = 0) {
    return TextSelection::caret(TextPosition{node.id, offset, TextAffinity::Downstream});
}

TextSelection range(const BlockNode& node, std::size_t start, std::size_t end) {
    return {{node.id, start, TextAffinity::Downstream}, {node.id, end, TextAffinity::Downstream}};
}

const BlockNode& first_text(const Editor& editor) {
    const BlockNode* found = nullptr;
    walk_blocks(editor.document().root, [&](const BlockNode& node) {
        if (!found && (node.kind == BlockKind::Paragraph || node.kind == BlockKind::Heading || node.kind == BlockKind::TableCell)) found = &node;
    });
    return *found;
}

bool document_indexes_are_exact(const EditorDocument& document) {
    bool valid = true;
    std::size_t block_count = 0;
    std::vector<NodeId> editable_order;
    BlockPath path;
    auto visit = [&](auto& self, const BlockNode& block, NodeId parent_id, std::size_t child_index) -> void {
        ++block_count;
        const auto cached = document.cached_block_locators.find(block.id.v);
        valid = valid
            && cached != document.cached_block_locators.end()
            && cached->second == BlockLocator{parent_id, child_index}
            && document_block_path(document, block.id) == std::optional<BlockPath>{path};
        if (is_editable_block_owner(block.kind)) editable_order.push_back(block.id);
        for (std::size_t index = 0; index < block.children.size(); ++index) {
            path.push_back(index);
            self(self, block.children[index], block.id, index);
            path.pop_back();
        }
    };
    visit(visit, document.root, NodeId{}, 0);
    valid = valid
        && document.cached_block_locators.size() == block_count
        && document.cached_editable_order == editable_order
        && document.cached_editable_index.size() == editable_order.size();
    for (std::size_t index = 0; index < editable_order.size(); ++index) {
        const auto cached = document.cached_editable_index.find(editable_order[index].v);
        valid = valid
            && cached != document.cached_editable_index.end()
            && cached->second == index;
    }
    return valid;
}

} // namespace
