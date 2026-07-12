// Read-only projections derived from the authoritative block tree.
export module elmd.core.document_projection;
import std;
import elmd.core.ast;
import elmd.core.document;
import elmd.core.diagnostics;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.metadata;
import elmd.core.outline;
import elmd.core.serializer;
import elmd.core.symbols;
import elmd.core.utf;

export namespace elmd {

struct DocumentProjection {
    std::u32string markdown;
    DocumentMetadata metadata;
    std::vector<Diagnostic> diagnostics;
    DocumentSymbolIndex symbols;
    Outline outline;
};

namespace document_projection_detail {

inline void collect_inline_symbols(
    const InlineDocument& document,
    const InlineCstNodes& nodes,
    DocumentSymbolIndex& symbols) {
    for (const auto& node : nodes) {
        if (node.kind == InlineCstKind::Link || node.kind == InlineCstKind::Autolink) {
            std::u32string label;
            append_inline_visible_text(document, node.children, label);
            if (label.empty()) label = inline_source_slice(document, node.delim.content);
            symbols.links.push_back(LinkSymbol{node.id, node.href, cps_to_utf8(label)});
        } else if (node.kind == InlineCstKind::Image) {
            symbols.images.push_back(ImageSymbol{node.id, node.href, node.alt});
        }
        collect_inline_symbols(document, node.children, symbols);
    }
}

inline void collect_block_symbols(const BlockNode& block, DocumentSymbolIndex& symbols) {
    if (block.kind == BlockKind::Heading) {
        symbols.headings.push_back(HeadingSymbol{
            block.id, block.level, cps_to_utf8(inline_visible_text(block.inline_content)), block.slug});
    } else if (block.kind == BlockKind::FootnoteDefinition) {
        symbols.footnotes.push_back(FootnoteSymbol{block.id, block.footnote_label});
    } else if (block.kind == BlockKind::ImageBlock) {
        symbols.images.push_back(ImageSymbol{block.id, block.src, block.image_alt});
    } else if (block.kind == BlockKind::MathBlock) {
        symbols.math_blocks.push_back(MathSymbol{block.id, cps_to_utf8(block.tex)});
    } else if (block.kind == BlockKind::CodeBlock) {
        const auto lines = block.code_text.empty()
            ? std::size_t{0}
            : std::size_t{1} + static_cast<std::size_t>(std::count(block.code_text.begin(), block.code_text.end(), U'\n'));
        symbols.code_blocks.push_back(CodeBlockSymbol{block.id, block.language, lines});
    }
    if (block.kind == BlockKind::Paragraph || block.kind == BlockKind::Heading || block.kind == BlockKind::TableCell) {
        collect_inline_symbols(block.inline_content, block.inline_content.tree.nodes, symbols);
    }
    for (const auto& child : block.children) collect_block_symbols(child, symbols);
}

} // namespace document_projection_detail

inline DocumentSymbolIndex build_document_symbol_index(const EditorDocument& document) {
    DocumentSymbolIndex symbols;
    for (const auto& block : document.root.children) document_projection_detail::collect_block_symbols(block, symbols);
    return symbols;
}

inline DocumentProjection project_document(const EditorDocument& document) {
    DocumentProjection projection;
    projection.markdown = serialize_markdown_cps(document);
    projection.metadata = document.metadata;
    projection.diagnostics = document.diagnostics;
    projection.symbols = build_document_symbol_index(document);
    projection.outline = build_outline_from_blocks(document.revision, document.root.children);
    return projection;
}

} // namespace elmd
