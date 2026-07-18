#pragma once

namespace {

template <class T>
concept carries_document_snapshot = requires(T value) { value.after; };

static_assert(!carries_document_snapshot<DocumentTransaction>);

namespace test_edit {

// Low-level edit functions now mutate the authoritative document in place and
// return only their reversible operation log. These adapters deliberately copy
// at the test boundary so the existing black-box assertions can inspect a
// materialized result without reintroducing snapshots into production
// transactions.
struct MaterializedTransaction : DocumentTransaction {
    EditorDocument after;
};

template <class Invoke>
std::optional<MaterializedTransaction> materialize_transaction(
    EditorDocument document,
    Invoke&& invoke) {
    auto transaction = std::forward<Invoke>(invoke)(document);
    if (!transaction) return std::nullopt;
    MaterializedTransaction result;
    static_cast<DocumentTransaction&>(result) = std::move(*transaction);
    result.after = std::move(document);
    return result;
}

std::optional<MaterializedTransaction> document_insert_text(
    EditorDocument document, const TextSelection& selection, std::u32string_view text) {
    return materialize_transaction(std::move(document), [&](auto& working) {
        return elmd::document_insert_text(working, selection, text);
    });
}

std::optional<MaterializedTransaction> document_enter(
    EditorDocument document, const TextSelection& selection) {
    return materialize_transaction(std::move(document), [&](auto& working) {
        return elmd::document_enter(working, selection);
    });
}

std::optional<MaterializedTransaction> document_delete_backward(
    EditorDocument document, const TextSelection& selection) {
    return materialize_transaction(std::move(document), [&](auto& working) {
        return elmd::document_delete_backward(working, selection);
    });
}

std::optional<MaterializedTransaction> document_delete_forward(
    EditorDocument document, const TextSelection& selection) {
    return materialize_transaction(std::move(document), [&](auto& working) {
        return elmd::document_delete_forward(working, selection);
    });
}

std::optional<MaterializedTransaction> document_delete_selection(
    EditorDocument document, const TextSelection& selection) {
    return materialize_transaction(std::move(document), [&](auto& working) {
        return elmd::document_delete_selection(working, selection);
    });
}

std::optional<MaterializedTransaction> document_toggle_inline_format(
    EditorDocument document, const TextSelection& selection, InlineFormat format) {
    return materialize_transaction(std::move(document), [&](auto& working) {
        return elmd::document_toggle_inline_format(working, selection, format);
    });
}

std::optional<MaterializedTransaction> document_insert_link(
    EditorDocument document, const TextSelection& selection, std::string href,
    std::optional<std::string> title = std::nullopt) {
    return materialize_transaction(std::move(document), [&](auto& working) {
        return elmd::document_insert_link(working, selection, std::move(href), std::move(title));
    });
}

std::optional<MaterializedTransaction> document_insert_image(
    EditorDocument document, const TextSelection& selection, std::string path, std::string alt) {
    return materialize_transaction(std::move(document), [&](auto& working) {
        return elmd::document_insert_image(working, selection, std::move(path), std::move(alt));
    });
}

std::optional<MaterializedTransaction> document_insert_footnote(
    EditorDocument document, const TextSelection& selection) {
    auto symbols = build_document_symbol_index(document);
    return materialize_transaction(std::move(document), [&](auto& working) {
        return elmd::document_insert_footnote(working, symbols, selection);
    });
}

std::optional<MaterializedTransaction> document_create_footnote_definition(
    EditorDocument document, const TextSelection& selection, std::string label) {
    auto symbols = build_document_symbol_index(document);
    return materialize_transaction(std::move(document), [&](auto& working) {
        return elmd::document_create_footnote_definition(
            working, symbols, selection, std::move(label));
    });
}

std::optional<MaterializedTransaction> document_paste_text(
    EditorDocument document, const TextSelection& selection, std::u32string_view text) {
    return materialize_transaction(std::move(document), [&](auto& working) {
        return elmd::document_paste_text(working, selection, text);
    });
}

std::optional<MaterializedTransaction> document_set_heading(
    EditorDocument document, const TextSelection& selection, std::uint8_t level) {
    return materialize_transaction(std::move(document), [&](auto& working) {
        return elmd::document_set_heading(working, selection, level);
    });
}

std::optional<MaterializedTransaction> document_toggle_block_quote(
    EditorDocument document, const TextSelection& selection) {
    return materialize_transaction(std::move(document), [&](auto& working) {
        return elmd::document_toggle_block_quote(working, selection);
    });
}

std::optional<MaterializedTransaction> document_toggle_callout(
    EditorDocument document, const TextSelection& selection, std::string kind) {
    return materialize_transaction(std::move(document), [&](auto& working) {
        return elmd::document_toggle_callout(working, selection, std::move(kind));
    });
}

std::optional<MaterializedTransaction> document_toggle_list(
    EditorDocument document, const TextSelection& selection, ListStyle style) {
    return materialize_transaction(std::move(document), [&](auto& working) {
        return elmd::document_toggle_list(working, selection, style);
    });
}

std::optional<MaterializedTransaction> document_indent_list_item(
    EditorDocument document, const TextSelection& selection) {
    return materialize_transaction(std::move(document), [&](auto& working) {
        return elmd::document_indent_list_item(working, selection);
    });
}

std::optional<MaterializedTransaction> document_outdent_list_item(
    EditorDocument document, const TextSelection& selection) {
    return materialize_transaction(std::move(document), [&](auto& working) {
        return elmd::document_outdent_list_item(working, selection);
    });
}

std::optional<MaterializedTransaction> document_insert_atomic_block(
    EditorDocument document, const TextSelection& selection, BlockNode block) {
    return materialize_transaction(std::move(document), [&](auto& working) {
        return elmd::document_insert_atomic_block(working, selection, std::move(block));
    });
}

std::optional<MaterializedTransaction> document_edit_table(
    EditorDocument document,
    const TextSelection& selection,
    DocumentTableEdit edit,
    TableAlignment alignment = TableAlignment::None,
    std::size_t argument = 0) {
    return materialize_transaction(std::move(document), [&](auto& working) {
        return elmd::document_edit_table(working, selection, edit, alignment, argument);
    });
}

}  // namespace test_edit

EditorDocument parse_document(std::string source) {
    return parse_text(1, source).document;
}

const BlockNode* first_block(const BlockVec& blocks, BlockKind kind) {
    for (const auto& block : blocks) {
        if (block.kind == kind) return &block;
        if (const auto* nested = first_block(block.children, kind)) return nested;
    }
    return nullptr;
}

BlockNode& first_editable(EditorDocument& document) {
    BlockNode* found = nullptr;
    walk_blocks(document.root, [&](BlockNode& node) {
        if (!found && (node.kind == BlockKind::Paragraph || node.kind == BlockKind::Heading || node.kind == BlockKind::TableCell)) found = &node;
    });
    return *found;
}

const BlockNode& first_editable(const EditorDocument& document) {
    const BlockNode* found = nullptr;
    walk_blocks(document.root, [&](const BlockNode& node) {
        if (!found && (node.kind == BlockKind::Paragraph || node.kind == BlockKind::Heading || node.kind == BlockKind::TableCell)) found = &node;
    });
    return *found;
}

bool contains_html_tag(const InlineCstNodes& nodes, std::string_view tag) {
    for (auto const& node : nodes) {
        if (node.kind == InlineCstKind::HtmlElement
            && node.semantics().html_tag == tag) return true;
        if (contains_html_tag(node.children, tag)) return true;
    }
    return false;
}

TextSelection caret(const BlockNode& node, std::size_t offset = 0) {
    return TextSelection::caret(TextPosition{node.id, offset, TextAffinity::Downstream});
}

TextSelection range(const BlockNode& node, std::size_t start, std::size_t end) {
    return TextSelection{
        TextPosition{node.id, start, TextAffinity::Downstream},
        TextPosition{node.id, end, TextAffinity::Downstream}};
}

void expect_inline_lossless(const InlineDocument& inline_document) {
    expect(fatal(bool(tokens_partition_source(inline_document.tree, inline_document.source.size()))));
    expect(fatal(bool(roots_partition_source(inline_document.tree, inline_document.source.size()))));
    expect(fatal(bool(flatten_tokens(inline_document.tree, inline_document.source) == inline_document.source)));
    expect(fatal(bool(serialize_lossless(inline_document.tree, inline_document.source) == inline_document.source)));
}

void expect_document_valid(const EditorDocument& document) {
    expect(fatal(bool(validate_document(document).empty())));
    walk_blocks(document.root, [&](const BlockNode& node) {
        if (const auto* inline_document = editable_inline_document(node))
            expect_inline_lossless(*inline_document);
    });
}

std::pair<EditorDocument, TextSelection> type_text(
    EditorDocument document,
    TextSelection selection,
    std::u32string_view text) {
    for (const auto value : text) {
        auto transaction = test_edit::document_insert_text(document, selection, std::u32string(1, value));
        if (!transaction) throw std::runtime_error("typing failed");
        document = std::move(transaction->after);
        selection = transaction->selection_after;
    }
    return {std::move(document), selection};
}

} // namespace
