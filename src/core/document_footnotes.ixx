// elmd.core.document_footnotes — semantic footnote indexing, navigation, and
// atomic source/tree transactions. No serialized Markdown or UI state is
// consulted when resolving references and definitions.
export module elmd.core.document_footnotes;
import std;
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.document_edit_support;
import elmd.core.document_symbols;
import elmd.core.document_text;
import elmd.core.document_transaction;
import elmd.core.inline_document;
import elmd.core.symbols;
import elmd.core.text_edit;
import elmd.core.utf;

export namespace elmd {

struct FootnoteResolution {
    std::string label;
    std::optional<FootnoteReferenceSymbol> reference;
    std::optional<FootnoteSymbol> definition;
};

inline const FootnoteSymbol* find_footnote_definition(
    const DocumentSymbolIndex& symbols,
    std::string_view label) {
    const auto found = std::ranges::find_if(symbols.footnotes, [&](const auto& footnote) {
        return footnote.label == label;
    });
    return found == symbols.footnotes.end() ? nullptr : &*found;
}

inline const FootnoteReferenceSymbol* find_footnote_reference(
    const DocumentSymbolIndex& symbols,
    std::string_view label) {
    const auto found = std::ranges::find_if(symbols.footnote_references, [&](const auto& reference) {
        return reference.label == label;
    });
    return found == symbols.footnote_references.end() ? nullptr : &*found;
}

inline std::optional<FootnoteResolution> resolve_footnote_reference(
    const DocumentSymbolIndex& symbols,
    TextPosition position) {
    const auto found = std::ranges::find_if(symbols.footnote_references, [&](const auto& reference) {
        return reference.container_id == position.container_id
            && reference.source_range.covers(position.source_offset);
    });
    if (found == symbols.footnote_references.end()) return std::nullopt;
    FootnoteResolution result;
    result.label = found->label;
    result.reference = *found;
    if (const auto* definition = find_footnote_definition(symbols, found->label)) {
        result.definition = *definition;
    }
    return result;
}

inline std::string next_footnote_label(const DocumentSymbolIndex& symbols) {
    std::unordered_set<std::string> used;
    for (const auto& definition : symbols.footnotes) used.insert(definition.label);
    for (const auto& reference : symbols.footnote_references) used.insert(reference.label);
    for (std::uint64_t candidate = 1;; ++candidate) {
        auto label = std::to_string(candidate);
        if (!used.contains(label)) return label;
    }
}

inline std::optional<TextPosition> footnote_definition_target(
    const EditorDocument& document,
    std::string_view label) {
    const auto symbols = build_document_symbol_index(document);
    const auto* definition = find_footnote_definition(symbols, label);
    if (!definition) return std::nullopt;
    const auto* block = find_block(document.root, definition->node_id);
    return block ? document_edit_detail::first_editable_position(*block) : std::nullopt;
}

inline std::optional<TextPosition> first_footnote_reference_target(
    const EditorDocument& document,
    std::string_view label) {
    const auto symbols = build_document_symbol_index(document);
    const auto* reference = find_footnote_reference(symbols, label);
    if (!reference) return std::nullopt;
    return TextPosition{
        reference->container_id,
        reference->source_range.start,
        TextAffinity::Downstream,
    };
}

namespace document_footnotes_detail {

inline BlockNode make_definition(
    const EditorDocument& owner,
    document_edit_detail::NodeAllocator& allocator,
    std::string label) {
    BlockNode definition;
    definition.id = allocator.allocate();
    definition.kind = BlockKind::FootnoteDefinition;
    definition.footnote_label = std::move(label);
    definition.opening_marker = U"[^" + utf8_to_cps(definition.footnote_label) + U"]: ";
    definition.children.push_back(document_edit_detail::empty_paragraph(allocator, owner));
    return definition;
}

inline void append_preview_text(std::u32string& preview, std::u32string_view text) {
    if (text.empty()) return;
    if (!preview.empty() && preview.back() != U' ') preview.push_back(U' ');
    for (const auto ch : text) preview.push_back(ch == U'\n' ? U' ' : ch);
}

inline void collect_preview(const BlockNode& block, std::u32string& preview) {
    if (const auto* document = editable_inline_document(block)) {
        append_preview_text(preview, inline_visible_text(*document));
    } else if (block.kind == BlockKind::CodeBlock || block.kind == BlockKind::MathBlock) {
        append_preview_text(preview, block_source_content(block.block_source));
    }
    for (const auto& child : block.children) collect_preview(child, preview);
}

} // namespace document_footnotes_detail

inline std::string footnote_preview(
    const EditorDocument& document,
    std::string_view label,
    std::size_t maximum_codepoints = 100) {
    const auto symbols = build_document_symbol_index(document);
    const auto* definition = find_footnote_definition(symbols, label);
    if (!definition) return {};
    const auto* block = find_block(document.root, definition->node_id);
    if (!block) return {};
    std::u32string preview;
    document_footnotes_detail::collect_preview(*block, preview);
    if (preview.size() > maximum_codepoints) preview.resize(maximum_codepoints);
    return cps_to_utf8(preview);
}

inline std::optional<DocumentTransaction> document_create_footnote_definition(
    const EditorDocument& document,
    const TextSelection& selection,
    std::string label) {
    if (label.empty()) return std::nullopt;
    const auto symbols = build_document_symbol_index(document);
    if (find_footnote_definition(symbols, label)) return std::nullopt;

    auto after = document;
    document_edit_detail::NodeAllocator allocator(after);
    auto definition = document_footnotes_detail::make_definition(after, allocator, std::move(label));
    const auto target = document_edit_detail::first_editable_position(definition);
    if (!target) return std::nullopt;

    DocumentTreeEdit insert;
    insert.kind = DocumentTreeEditKind::Insert;
    insert.parent_id = after.root.id;
    insert.index = after.root.children.size();
    insert.after = definition;
    std::vector<DocumentOperation> operations;
    operations.emplace_back(std::move(insert));
    after.root.children.push_back(std::move(definition));
    ++after.revision;
    return make_recorded_document_transaction(
        std::move(after),
        std::move(operations),
        selection,
        TextSelection::caret(*target),
        document.revision,
        DocumentTransactionReason::Structure);
}

inline std::optional<DocumentTransaction> document_insert_footnote(
    const EditorDocument& document,
    const TextSelection& selection) {
    if (selection.anchor.container_id != selection.active.container_id) return std::nullopt;
    auto after = document;
    document_edit_detail::NodeAllocator allocator(after);
    auto* owner = document_edit_detail::find_inline_owner(
        after.root.children, selection.active.container_id);
    if (!owner) return std::nullopt;

    const auto offset = selection.is_caret()
        ? selection.active.source_offset
        : (std::max)(selection.anchor.source_offset, selection.active.source_offset);
    if (offset > owner->source.size()) return std::nullopt;
    const auto label = next_footnote_label(build_document_symbol_index(after));
    const auto reference_source = U"[^" + utf8_to_cps(label) + U"]";
    auto source_edit = document_edit_detail::edit_inline(
        after,
        selection.active.container_id,
        {offset, offset},
        reference_source,
        allocator);
    if (!source_edit) return std::nullopt;

    auto definition = document_footnotes_detail::make_definition(after, allocator, label);
    const auto target = document_edit_detail::first_editable_position(definition);
    if (!target) return std::nullopt;
    std::vector<DocumentOperation> operations;
    document_edit_detail::append_source_operation(operations, std::move(*source_edit));
    DocumentTreeEdit insert;
    insert.kind = DocumentTreeEditKind::Insert;
    insert.parent_id = after.root.id;
    insert.index = after.root.children.size();
    insert.after = definition;
    operations.emplace_back(std::move(insert));
    after.root.children.push_back(std::move(definition));
    ++after.revision;
    return make_recorded_document_transaction(
        std::move(after),
        std::move(operations),
        selection,
        TextSelection::caret(*target),
        document.revision,
        DocumentTransactionReason::Structure);
}

} // namespace elmd
