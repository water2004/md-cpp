// elmd.core.document_symbols — derive symbols directly from the block tree and
// each editable node's lossless inline CST.
export module elmd.core.document_symbols;
import std;
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.document;
import elmd.core.document_text;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.ids;
import elmd.core.symbols;
import elmd.core.utf;

export namespace elmd {

namespace document_symbols_detail {

inline void collect_inline_symbols(
    const InlineDocument& document,
    const InlineCstNodes& nodes,
    NodeId container_id,
    DocumentSymbolIndex& symbols) {
    for (const auto& node : nodes) {
        if (node.kind == InlineCstKind::Link || node.kind == InlineCstKind::Autolink) {
            std::u32string label;
            append_inline_visible_text(document, node.children, label);
            if (label.empty()) label = inline_source_slice(document, node.delim.content);
            symbols.links.push_back(LinkSymbol{node.id, node.href, cps_to_utf8(label)});
        } else if (node.kind == InlineCstKind::Image) {
            symbols.images.push_back(ImageSymbol{node.id, node.href, node.alt});
        } else if (node.kind == InlineCstKind::FootnoteRef) {
            symbols.footnote_references.push_back(FootnoteReferenceSymbol{
                node.id,
                container_id,
                node.range,
                node.label,
            });
        }
        collect_inline_symbols(document, node.children, container_id, symbols);
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
        symbols.math_blocks.push_back(MathSymbol{block.id, cps_to_utf8(block_source_content(block.block_source))});
    } else if (block.kind == BlockKind::CodeBlock) {
        const auto code = block_source_content(block.block_source);
        const auto lines = code.empty()
            ? std::size_t{0}
            : std::size_t{1} + static_cast<std::size_t>(std::count(code.begin(), code.end(), U'\n'));
        symbols.code_blocks.push_back(CodeBlockSymbol{block.id, block.block_source.tree.language, lines});
    }
    if (const auto* inline_document = editable_inline_document(block)) {
        collect_inline_symbols(*inline_document, inline_document->tree.nodes, block.id, symbols);
    }
    for (const auto& child : block.children) collect_block_symbols(child, symbols);
}

} // namespace document_symbols_detail

inline DocumentSymbolIndex build_document_symbol_index(const EditorDocument& document) {
    DocumentSymbolIndex symbols;
    for (const auto& block : document.root.children) document_symbols_detail::collect_block_symbols(block, symbols);
    return symbols;
}

} // namespace elmd
