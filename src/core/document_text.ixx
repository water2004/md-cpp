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
import elmd.core.text_edit;

export namespace elmd {

struct DocumentTextFragment {
    NodeId container_id{};
    std::u32string text;
};

inline InlineDocument* editable_inline_document(BlockNode& block) {
    switch (block.kind) {
        case BlockKind::Paragraph:
        case BlockKind::Heading:
        case BlockKind::TableCell:
            return &block.inline_content;
        case BlockKind::Callout:
            return block.callout_title ? &*block.callout_title : nullptr;
        default:
            return nullptr;
    }
}

inline const InlineDocument* editable_inline_document(const BlockNode& block) {
    switch (block.kind) {
        case BlockKind::Paragraph:
        case BlockKind::Heading:
        case BlockKind::TableCell:
            return &block.inline_content;
        case BlockKind::Callout:
            return block.callout_title ? &*block.callout_title : nullptr;
        default:
            return nullptr;
    }
}

inline std::optional<std::u32string> editable_block_text(const BlockNode& block) {
    if (const auto* document = editable_inline_document(block)) return document->source;
    switch (block.kind) {
        case BlockKind::CodeBlock:
            return block.code_text;
        case BlockKind::MathBlock:
            return block.tex;
        case BlockKind::Frontmatter:
        case BlockKind::LinkDefinition:
        case BlockKind::UnsupportedMarkup:
        case BlockKind::ImageBlock:
        case BlockKind::Toc:
        case BlockKind::ThematicBreak:
        case BlockKind::Extension:
            return std::u32string{U'\ufffc'};
        default:
            return std::nullopt;
    }
}

inline std::vector<DocumentTextFragment> document_text_fragments(const EditorDocument& document) {
    std::vector<DocumentTextFragment> result;
    walk_blocks(document.root, [&](const BlockNode& block) {
        if (block.kind == BlockKind::Document) return;
        if (auto text = editable_block_text(block)) {
            result.push_back({block.id, std::move(*text)});
        }
    });
    return result;
}

inline std::optional<std::u32string> document_editable_text(
    const EditorDocument& document,
    NodeId container_id) {
    const auto* block = find_block(document.root, container_id);
    return block ? editable_block_text(*block) : std::nullopt;
}

inline std::optional<std::u32string> document_selected_text(
    const EditorDocument& document,
    const TextSelection& selection) {
    const auto fragments = document_text_fragments(document);
    auto index_of = [&](NodeId id) -> std::optional<std::size_t> {
        for (std::size_t index = 0; index < fragments.size(); ++index) {
            if (fragments[index].container_id == id) return index;
        }
        return std::nullopt;
    };
    auto anchor_index = index_of(selection.anchor.container_id);
    auto active_index = index_of(selection.active.container_id);
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

    const auto start_offset = (std::min)(start.source_offset, fragments[start_index].text.size());
    const auto end_offset = (std::min)(end.source_offset, fragments[end_index].text.size());
    if (start_index == end_index) {
        return fragments[start_index].text.substr(start_offset, end_offset - start_offset);
    }

    std::u32string result = fragments[start_index].text.substr(start_offset);
    for (std::size_t index = start_index + 1; index <= end_index; ++index) {
        result.push_back(U'\n');
        const auto length = index == end_index ? end_offset : fragments[index].text.size();
        result.append(fragments[index].text, 0, length);
    }
    return result;
}

} // namespace elmd
