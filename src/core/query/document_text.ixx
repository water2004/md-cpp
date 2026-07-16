// elmd.core.document_text — tree-order text queries over editable block owners.
//
// These fragments retain their block-local TextPosition coordinate. A flat
// ACP/UTF-16 projection is a platform concern and must not become core editor
// state or a second authoritative selection coordinate.
export module elmd.core.document_text;
import std;
import elmd.core.ast;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.ids;
import elmd.core.inline_document;
import elmd.core.instrumentation;
import elmd.core.text_edit;

export namespace elmd {

struct DocumentTextFragment {
    NodeId container_id{};
    // View into the owning EditorDocument. Callers must not retain fragments
    // across a document mutation.
    std::u32string_view text;
};

inline InlineDocument* editable_inline_document(BlockNode& block) {
    switch (block.kind) {
        case BlockKind::Paragraph:
        case BlockKind::Heading:
        case BlockKind::CalloutTitle:
        case BlockKind::TableCell:
            return &block.inline_content;
        default:
            return nullptr;
    }
}

inline const InlineDocument* editable_inline_document(const BlockNode& block) {
    switch (block.kind) {
        case BlockKind::Paragraph:
        case BlockKind::Heading:
        case BlockKind::CalloutTitle:
        case BlockKind::TableCell:
            return &block.inline_content;
        default:
            return nullptr;
    }
}

inline std::u32string* editable_raw_block_source(BlockNode& block) {
    switch (block.kind) {
        case BlockKind::CodeBlock:
        case BlockKind::MathBlock:
            return &block.block_source.source;
        default:
            return nullptr;
    }
}

inline const std::u32string* editable_raw_block_source(const BlockNode& block) {
    switch (block.kind) {
        case BlockKind::CodeBlock:
        case BlockKind::MathBlock:
            return &block.block_source.source;
        default:
            return nullptr;
    }
}

inline std::optional<std::u32string_view> editable_block_text_view(const BlockNode& block) {
    if (const auto* document = editable_inline_document(block)) return document->source;
    if (const auto* source = editable_raw_block_source(block)) return *source;
    switch (block.kind) {
        case BlockKind::Frontmatter:
        case BlockKind::LinkDefinition:
        case BlockKind::UnsupportedMarkup:
        case BlockKind::ImageBlock:
        case BlockKind::Toc:
        case BlockKind::ThematicBreak:
        case BlockKind::Extension:
            return std::u32string_view{U"\ufffc", 1};
        default:
            return std::nullopt;
    }
}

inline std::optional<std::u32string> editable_block_text(const BlockNode& block) {
    auto view = editable_block_text_view(block);
    return view ? std::optional<std::u32string>{std::u32string{*view}} : std::nullopt;
}

inline std::optional<TextPosition> first_document_text_position(
    const EditorDocument& document) {
    auto find = [&](auto& self, const BlockNode& block) -> std::optional<TextPosition> {
        if (block.kind != BlockKind::Document && editable_block_text_view(block)) {
            return TextPosition{block.id, 0, TextAffinity::Downstream};
        }
        for (const auto& child : block.children) {
            if (auto position = self(self, child)) return position;
        }
        return std::nullopt;
    };
    return find(find, document.root);
}

inline std::vector<DocumentTextFragment> document_text_fragments(const EditorDocument& document) {
    record_full_document_text_projection();
    std::vector<DocumentTextFragment> result;
    walk_blocks(document.root, [&](const BlockNode& block) {
        if (block.kind == BlockKind::Document) return;
        if (auto text = editable_block_text_view(block)) {
            result.push_back({block.id, *text});
        }
    });
    return result;
}

inline std::optional<std::u32string> document_editable_text(
    const EditorDocument& document,
    NodeId container_id) {
    const auto* block = find_document_block(document, container_id);
    if (!block) return std::nullopt;
    auto text = editable_block_text_view(*block);
    return text ? std::optional<std::u32string>{std::u32string{*text}} : std::nullopt;
}

inline std::optional<std::u32string> document_selected_text(
    const EditorDocument& document,
    const TextSelection& selection) {
    const auto& order = document.cached_editable_order;
    auto text_at = [&](std::size_t index) -> std::optional<std::u32string_view> {
        if (index >= order.size()) return std::nullopt;
        const auto* block = find_document_block(document, order[index]);
        return block ? editable_block_text_view(*block) : std::nullopt;
    };
    auto anchor_index = document_editable_order_position(
        document, selection.anchor.container_id);
    auto active_index = document_editable_order_position(
        document, selection.active.container_id);
    if (!anchor_index || !active_index) return std::nullopt;

    auto start = selection.anchor;
    auto end = selection.active;
    auto start_index = *anchor_index;
    auto end_index = *active_index;
    if (end_index < start_index
        || (start_index == end_index && end.source_offset < start.source_offset)) {
        std::swap(start, end);
        std::swap(start_index, end_index);
    }

    auto start_text = text_at(start_index);
    auto end_text = text_at(end_index);
    if (!start_text || !end_text) return std::nullopt;
    const auto start_offset = (std::min)(start.source_offset, start_text->size());
    const auto end_offset = (std::min)(end.source_offset, end_text->size());
    if (start_index == end_index) {
        return std::u32string{
            start_text->substr(start_offset, end_offset - start_offset)};
    }

    std::u32string result{start_text->substr(start_offset)};
    for (std::size_t index = start_index + 1; index <= end_index; ++index) {
        auto text = index == end_index ? end_text : text_at(index);
        if (!text) return std::nullopt;
        result.push_back(U'\n');
        const auto length = index == end_index ? end_offset : text->size();
        result.append(*text, 0, length);
    }
    return result;
}

} // namespace elmd
