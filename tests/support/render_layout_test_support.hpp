#pragma once

static RenderModel build_model(const std::string& src) {
    auto out = parse_text(1, src);
    return build_render_model(
        out.document,
        out.outline,
        out.symbols,
        default_theme_profile());
}
static const RenderBlock* find_render_block(const RenderBlock& root, RenderBlockKind kind) {
    if (root.kind == kind) return &root;
    for (auto const& child : root.child_blocks) {
        if (auto found = find_render_block(child, kind)) return found;
    }
    return nullptr;
}

static const RenderBlock* first_render_leaf(const RenderBlock& root) {
    if (root.child_blocks.empty()) return &root;
    for (auto const& child : root.child_blocks) {
        if (auto found = first_render_leaf(child)) return found;
    }
    return nullptr;
}

static const InlineRenderItem* find_text_item(
    const RenderBlock& root,
    std::u32string_view text) {
    for (auto const& item : root.inline_items) {
        if (item.kind == InlineRenderItem::Kind::Text && item.text == text) return &item;
        if (item.payload && item.payload->semantic_payload) {
            for (auto const& child : item.payload->semantic_payload->children) {
                if (child.kind == InlineRenderItem::Kind::Text && child.text == text) return &child;
            }
        }
    }
    if (root.payload) {
        for (auto const& row : root.special().table_cells) {
            for (auto const& item : row) {
                if (item.kind == InlineRenderItem::Kind::Text && item.text == text) return &item;
            }
        }
    }
    for (auto const& child : root.child_blocks) {
        if (auto found = find_text_item(child, text)) return found;
    }
    return nullptr;
}

static const InlineRenderItem* find_text_item(
    const RenderModel& model,
    std::u32string_view text) {
    for (auto const& block : model.blocks) {
        if (auto found = find_text_item(block, text)) return found;
    }
    return nullptr;
}

static const RenderBlock* find_render_block(const RenderBlock& root, NodeId id) {
    if (root.id == id) return &root;
    for (auto const& child : root.child_blocks) {
        if (auto found = find_render_block(child, id)) return found;
    }
    return nullptr;
}

static std::optional<std::size_t> flow_indent_for(
    const RenderBlock& root,
    NodeId id,
    std::size_t parent_indent = 0) {
    const auto indent = parent_indent + root.flow_local_indent_columns;
    if (root.id == id) return indent;
    for (auto const& child : root.child_blocks) {
        if (auto found = flow_indent_for(child, id, indent)) return found;
    }
    return std::nullopt;
}

static BlockNode make_render_block(BlockKind kind, std::uint64_t& next_id) {
    BlockNode block;
    block.id = NodeId{next_id++};
    block.kind = kind;
    return block;
}

static BlockNode make_render_text_block(BlockKind kind, std::u32string source, std::uint64_t& next_id) {
    auto block = make_render_block(kind, next_id);
    block.inline_content.source = std::move(source);
    InlineParseContext context;
    context.allocate_id = [&] { return NodeId{next_id++}; };
    reparse_inline_document(block.inline_content, context);
    return block;
}

static RenderModel build_model(BlockNode block, std::uint64_t next_id) {
    EditorDocument document;
    document.root.id = NodeId{next_id++};
    document.root.kind = BlockKind::Document;
    document.root.children.push_back(std::move(block));
    document.revision = 1;
    rebuild_document_block_index(document);
    const auto symbols = build_document_symbol_index(document);
    return build_render_model(
        document,
        Outline::empty(1),
        symbols,
        default_theme_profile());
}

