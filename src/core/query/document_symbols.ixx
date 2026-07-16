// elmd.core.document_symbols — derive symbols directly from the block tree and
// each editable node's lossless inline CST.
export module elmd.core.document_symbols;
import std;
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.document_text;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.ids;
import elmd.core.instrumentation;
import elmd.core.symbols;
import elmd.core.utf;

export namespace elmd {

namespace document_symbols_detail {

inline void append_symbols(DocumentSymbolIndex& destination, const DocumentSymbolIndex& source) {
    destination.headings.insert(destination.headings.end(), source.headings.begin(), source.headings.end());
    destination.footnotes.insert(destination.footnotes.end(), source.footnotes.begin(), source.footnotes.end());
    destination.footnote_references.insert(
        destination.footnote_references.end(),
        source.footnote_references.begin(),
        source.footnote_references.end());
    destination.links.insert(destination.links.end(), source.links.begin(), source.links.end());
    destination.images.insert(destination.images.end(), source.images.begin(), source.images.end());
    destination.math_blocks.insert(
        destination.math_blocks.end(), source.math_blocks.begin(), source.math_blocks.end());
    destination.code_blocks.insert(
        destination.code_blocks.end(), source.code_blocks.begin(), source.code_blocks.end());
}

inline bool has_symbols(const DocumentSymbolIndex& symbols) {
    return !symbols.headings.empty()
        || !symbols.footnotes.empty()
        || !symbols.footnote_references.empty()
        || !symbols.links.empty()
        || !symbols.images.empty()
        || !symbols.math_blocks.empty()
        || !symbols.code_blocks.empty();
}

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

inline DocumentSymbolIndex collect_block_symbols(const BlockNode& block) {
    DocumentSymbolIndex symbols;
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
    return symbols;
}

} // namespace document_symbols_detail

struct DocumentSymbolContributions {
    std::unordered_map<std::uint64_t, DocumentSymbolIndex> by_block;
};

inline DocumentSymbolIndex build_document_symbol_index(
    const EditorDocument& document,
    DocumentSymbolContributions* contributions = nullptr) {
    record_full_document_symbol_derivation();
    DocumentSymbolIndex symbols;
    if (contributions) contributions->by_block.clear();
    walk_blocks(document.root, [&](const BlockNode& block) {
        if (block.kind == BlockKind::Document) return;
        auto own = document_symbols_detail::collect_block_symbols(block);
        document_symbols_detail::append_symbols(symbols, own);
        if (contributions && document_symbols_detail::has_symbols(own)) {
            contributions->by_block.emplace(block.id.v, std::move(own));
        }
    });
    return symbols;
}

// A source edit can only change the symbols owned by its editable block. Most
// prose edits therefore compare equal to the cached empty contribution and do
// no document-wide work. If a real symbol changed, rebuild the flat public
// index from already-derived per-block contributions in tree order; no other
// block's source or inline CST is scanned.
inline bool update_document_symbol_index(
    const EditorDocument& document,
    const BlockNode& block,
    DocumentSymbolContributions& contributions,
    DocumentSymbolIndex& symbols) {
    record_local_symbol_derivation();
    auto updated = document_symbols_detail::collect_block_symbols(block);
    auto found = contributions.by_block.find(block.id.v);
    if (found == contributions.by_block.end()
        && !document_symbols_detail::has_symbols(updated)) return false;
    if (found != contributions.by_block.end() && found->second == updated) return false;
    if (document_symbols_detail::has_symbols(updated)) {
        contributions.by_block[block.id.v] = std::move(updated);
    } else {
        contributions.by_block.erase(block.id.v);
    }

    DocumentSymbolIndex rebuilt;
    walk_blocks(document.root, [&](const BlockNode& current) {
        const auto contribution = contributions.by_block.find(current.id.v);
        if (contribution != contributions.by_block.end()) {
            document_symbols_detail::append_symbols(rebuilt, contribution->second);
        }
    });
    symbols = std::move(rebuilt);
    return true;
}

} // namespace elmd
