#include <algorithm>
#include <cstddef>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "support/folia_test.hpp"
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.document_edit;
import elmd.core.document_history;
import elmd.core.document_ids;
import elmd.core.document_operation_apply;
import elmd.core.document_symbols;
import elmd.core.document_text;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.parser;
import elmd.core.serializer;
import elmd.core.text_edit;

using namespace elmd;
using namespace boost::ut;

#include "support/document_edit_test_support.hpp"

suite document_operation_tests = [] {

"low_level_edit_mutates_only_the_authoritative_document"_test = [] {
    auto document = parse_document("alpha\n\nbeta");
    const auto revision_before = document.revision;
    const auto selection = caret(document.root.children[0], 2);

    auto transaction = elmd::document_insert_text(document, selection, U"X");

    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(document.root.children.size() == 2u)));
    if (document.root.children.size() != 2u) return;
    expect(document.root.children[0].inline_content.source == U"alXpha");
    expect(document.root.children[1].inline_content.source == U"beta");
    expect(transaction->revision_before == revision_before);
    expect(transaction->revision_after == document.revision);
    expect(transaction->revision_after == revision_before + 1);
    expect_document_valid(document);
};

"operation_replay_rolls_back_an_earlier_edit_when_a_later_edit_is_invalid"_test = [] {
    auto document = parse_document("alpha");
    const auto before = serialize_markdown(document);
    const auto id = document.root.children.front().id;
    std::vector<DocumentOperation> operations;
    operations.emplace_back(DocumentTextOperation{
        TextEdit{id, {2, 2}, U"X"},
        TextEdit{id, {2, 3}, {}},
    });
    operations.emplace_back(DocumentTextOperation{
        TextEdit{NodeId{999999}, {0, 0}, U"Y"},
        TextEdit{NodeId{999999}, {0, 1}, {}},
    });

    expect(!apply_document_operations(document, operations, true));
    expect(serialize_markdown(document) == before);
    expect_document_valid(document);
};

"invalid_cross_parent_move_does_not_remove_the_source_block"_test = [] {
    auto document = parse_document("alpha\n\nbeta");
    const auto before = serialize_markdown(document);
    DocumentTreeEdit move;
    move.kind = DocumentTreeEditKind::Move;
    move.parent_id = document.root.id;
    move.index = 0;
    move.other_parent_id = NodeId{999999};
    move.other_index = 0;
    move.moved_id = document.root.children.front().id;

    expect(!apply_document_operation(document, DocumentOperation{move}, true));
    expect(serialize_markdown(document) == before);
    expect(document.root.children.size() == 2u);
    expect_document_valid(document);

    move.other_parent_id = document.root.id;
    move.other_index = 1;
    move.moved_id = NodeId{999999};
    expect(!apply_document_operation(document, DocumentOperation{move}, true));
    expect(serialize_markdown(document) == before);
    expect(document.root.children.size() == 2u);
    expect_document_valid(document);
};

"failed_history_replay_preserves_the_document_selection_and_stack"_test = [] {
    auto document = parse_document("alpha");
    const auto before = serialize_markdown(document);
    auto selection = caret(document.root.children.front(), 2);
    const auto selection_before = selection;
    DocumentTransaction transaction;
    transaction.selection_before = selection;
    transaction.selection_after = selection;
    transaction.revision_before = document.revision;
    transaction.revision_after = document.revision + 1;
    transaction.operations.emplace_back(DocumentTextOperation{
        TextEdit{NodeId{999999}, {0, 0}, U"X"},
        TextEdit{NodeId{999999}, {0, 1}, {}},
    });
    DocumentHistory history;
    history.push(transaction);

    expect(!history.undo(document, selection));
    expect(history.has_undo());
    expect(!history.has_redo());
    expect(serialize_markdown(document) == before);
    expect(selection == selection_before);
    expect_document_valid(document);
};

"insert_text_edits_only_the_target_inline_source"_test = [] {
    auto document = parse_document("alpha\n\nbeta");
    const auto first_id = document.root.children[0].id;
    const auto second_before = document.root.children[1].inline_content;
    auto transaction = test_edit::document_insert_text(document, caret(document.root.children[0], 2), U"X");
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(transaction->after.root.children[0].inline_content.source == U"alXpha")));
    expect(fatal(bool(transaction->after.root.children[1].inline_content.source == second_before.source)));
    expect(fatal(bool(transaction->after.root.children[0].id == first_id)));
    expect(fatal(bool(transaction->selection_after.active.source_offset == 3u)));
    expect_document_valid(transaction->after);
};

"typing_complete_html_reclassifies_one_block_and_keeps_slot_ids_editable"_test = [] {
    constexpr std::u32string_view html = U"<table><tr><td>value</td></tr></table>";
    auto document = parse_document("x");
    auto selection = range(first_editable(document), 0, 1);
    auto first = test_edit::document_insert_text(document, selection, html.substr(0, 1));
    expect(fatal(first.has_value()));
    if (!first) return;
    document = std::move(first->after);
    selection = first->selection_after;
    auto typed = type_text(std::move(document), selection, html.substr(1));
    document = std::move(typed.first);
    selection = typed.second;

    expect(fatal(document.root.children.size() == 1u));
    if (document.root.children.empty()) return;
    expect(fatal(document.root.children.front().kind == BlockKind::Table));
    expect(fatal(document.root.children.front().has_html_source()));
    expect(fatal(serialize_markdown(document)
        == "<table><tr><td>value</td></tr></table>"));
    const auto* selected = find_document_block(document, selection.active.container_id);
    expect(fatal(selected != nullptr));
    expect(fatal(selected && selected->kind == BlockKind::TableCell));

    auto edit = test_edit::document_insert_text(document, selection, U"!");
    expect(fatal(edit.has_value()));
    if (!edit) return;
    expect(fatal(serialize_markdown(edit->after)
        == "<table><tr><td>value!</td></tr></table>"));
    expect_document_valid(edit->after);
};

"html_cell_source_edit_undo_redo_preserves_original_envelope"_test = [] {
    const std::string source =
        "<TABLE data-x='1'><tbody><tr><td>alpha</td><td>β</td></tr></tbody></TABLE>";
    auto document = parse_document(source);
    const auto* cell = first_block(document.root.children, BlockKind::TableCell);
    expect(fatal(cell != nullptr));
    if (!cell) return;
    auto selection = caret(*cell, cell->inline_content.source.size());
    auto transaction = elmd::document_insert_text(document, selection, U"!");
    expect(fatal(transaction.has_value()));
    if (!transaction) return;
    selection = transaction->selection_after;
    expect(fatal(serialize_markdown(document)
        == "<TABLE data-x='1'><tbody><tr><td>alpha!</td><td>β</td></tr></tbody></TABLE>"));

    DocumentHistory history;
    history.push(*transaction);
    expect(fatal(history.undo(document, selection)));
    expect(fatal(serialize_markdown(document) == source));
    expect(fatal(history.redo(document, selection)));
    expect(fatal(serialize_markdown(document)
        == "<TABLE data-x='1'><tbody><tr><td>alpha!</td><td>β</td></tr></tbody></TABLE>"));
    expect_document_valid(document);
};

"html_table_structure_edits_remain_html_and_restore_exactly"_test = [] {
    const std::string source =
        "<TABLE data-x='1'><tbody><tr class='r'><td class='c'>a</td><td>b</td></tr>"
        "<tr><td>c</td><td>d</td></tr></tbody></TABLE>";
    auto document = parse_document(source);
    const auto* cell = first_block(document.root.children, BlockKind::TableCell);
    expect(fatal(cell != nullptr));
    if (!cell) return;
    auto selection = caret(*cell);
    auto transaction = elmd::document_edit_table(
        document,
        selection,
        DocumentTableEdit::InsertColumnRight);
    expect(fatal(transaction.has_value()));
    if (!transaction) return;
    selection = transaction->selection_after;
    const std::string changed =
        "<TABLE data-x='1'><tr class='r'><td class='c'>a</td><td></td><td>b</td></tr>"
        "<tr><td>c</td><td></td><td>d</td></tr></TABLE>";
    expect(fatal(serialize_markdown(document) == changed));
    expect(fatal(serialize_markdown(document).find('|') == std::string::npos));

    DocumentHistory history;
    history.push(*transaction);
    expect(fatal(history.undo(document, selection)));
    expect(fatal(serialize_markdown(document) == source));
    expect(fatal(history.redo(document, selection)));
    expect(fatal(serialize_markdown(document) == changed));
    expect_document_valid(document);
};

"recursive_html_structure_edits_use_one_generic_rebuilder"_test = [] {
    const std::string list_source =
        "<DIV data-x='1'><ul class='u'><li data-i='1'>alpha</li></ul></DIV>";
    auto list_document = parse_document(list_source);
    const auto* paragraph = first_block(list_document.root.children, BlockKind::Paragraph);
    expect(fatal(paragraph != nullptr));
    if (!paragraph) return;

    auto split = test_edit::document_enter(list_document, caret(*paragraph, 2));
    expect(fatal(split.has_value()));
    if (!split) return;
    const std::string split_source =
        "<DIV data-x='1'><ul class='u'><li data-i='1'>al</li><li>pha</li></ul></DIV>";
    expect(fatal(serialize_markdown(split->after) == split_source));
    const auto* right = find_document_block(
        split->after,
        split->selection_after.active.container_id);
    expect(fatal(right != nullptr));
    expect(fatal(right && right->inline_content.syntax_mode == InlineSyntaxMode::HtmlText));

    auto working = std::move(split->after);
    auto selection = split->selection_after;
    DocumentHistory history;
    history.push(*split);
    expect(fatal(history.undo(working, selection)));
    expect(fatal(serialize_markdown(working) == list_source));
    expect(fatal(history.redo(working, selection)));
    expect(fatal(serialize_markdown(working) == split_source));

    const std::string quote_source = "<section class='s'><p>x</p></section>";
    auto quote_document = parse_document(quote_source);
    const auto* quote_paragraph = first_block(
        quote_document.root.children,
        BlockKind::Paragraph);
    expect(fatal(quote_paragraph != nullptr));
    if (!quote_paragraph) return;
    auto wrapped = test_edit::document_toggle_block_quote(
        quote_document,
        caret(*quote_paragraph));
    expect(fatal(wrapped.has_value()));
    if (!wrapped) return;
    expect(fatal(serialize_markdown(wrapped->after)
        == "<section class='s'><blockquote><p>x</p></blockquote></section>"));
    expect_document_valid(wrapped->after);

    auto image_document = parse_document("<section><p>x</p></section>");
    BlockNode image;
    image.id = allocate_document_node_id(image_document);
    image.kind = BlockKind::ImageBlock;
    image.ensure_image_special().src = "x\" onerror=\"alert(1)";
    image.ensure_image_special().image_alt = "\" onload=\"x";
    image_document.root.children.front().children.push_back(std::move(image));
    const auto image_source = serialize_markdown(image_document);
    expect(fatal(image_source
        == "<section><p>x</p><img src=\"x&quot; onerror=&quot;alert(1)\" "
           "alt=\"&quot; onload=&quot;x\"></section>"));
    expect(fatal(image_source.find(" onerror=\"") == std::string::npos));
};

"html_text_formatting_uses_safe_html_source_markers"_test = [] {
    auto document = parse_document("<p>alpha</p>");
    const auto& paragraph = first_editable(document);
    auto strong = test_edit::document_toggle_inline_format(
        document,
        range(paragraph, 0, 5),
        InlineFormat::Strong);
    expect(fatal(strong.has_value()));
    if (!strong) return;
    expect(fatal(serialize_markdown(strong->after)
        == "<p><strong>alpha</strong></p>"));
    const auto& updated = first_editable(strong->after);
    expect(fatal(updated.inline_content.syntax_mode == InlineSyntaxMode::HtmlText));
    expect(fatal(!inline_contains_kind(updated.inline_content, InlineCstKind::Strong)));
    expect(fatal(contains_html_tag(updated.inline_content.tree.nodes, "strong")));
    expect_document_valid(strong->after);

    auto heading_document = parse_document("<p class='old'>title</p>");
    const auto& heading_source = first_editable(heading_document);
    auto heading = test_edit::document_set_heading(
        heading_document,
        caret(heading_source),
        2);
    expect(fatal(heading.has_value()));
    if (heading) {
        expect(fatal(serialize_markdown(heading->after) == "<h2>title</h2>"));
        expect_document_valid(heading->after);
    }
};

"selection_replacement_uses_source_offsets_including_markers"_test = [] {
    auto document = parse_document("**alpha**");
    const auto& paragraph = first_editable(document);
    auto transaction = test_edit::document_insert_text(document, range(paragraph, 2, 7), U"beta");
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(first_editable(transaction->after).inline_content.source == U"**beta**")));
    expect(fatal(bool(transaction->selection_after.active.source_offset == 6u)));
    expect_document_valid(transaction->after);
};

"marker_input_keeps_an_editable_incomplete_cst"_test = [] {
    auto document = parse_document("abc");
    const auto& paragraph = first_editable(document);
    auto transaction = test_edit::document_insert_text(document, caret(paragraph, 1), U"*");
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    const auto& inline_document = first_editable(transaction->after).inline_content;
    expect(fatal(bool(inline_document.source == U"a*bc")));
    expect(fatal(bool(transaction->selection_after.active.source_offset == 2u)));
    expect_inline_lossless(inline_document);
};

"source_transactions_publish_reversible_text_operations"_test = [] {
    auto document = parse_document("abc");
    const auto& paragraph = first_editable(document);
    auto transaction = test_edit::document_insert_text(document, caret(paragraph, 1), U"X");
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(transaction->revision_before == document.revision)));
    expect(fatal(bool(transaction->operations.size() == 1u)));
    const auto* operation = transaction->operations.empty()
        ? nullptr
        : std::get_if<DocumentTextOperation>(&transaction->operations.front());
    expect(fatal(bool(operation != nullptr)));
    if (!operation) return;
    expect(fatal(bool(operation->forward.container_id == paragraph.id)));
    expect(fatal(bool(operation->forward.range == SourceRange{1, 1})));
    expect(fatal(bool(operation->forward.replacement == U"X")));
    expect(fatal(bool(operation->inverse.container_id == paragraph.id)));
    expect(fatal(bool(operation->inverse.range == SourceRange{1, 2})));
    expect(fatal(bool(operation->inverse.replacement.empty())));
};

"structural_transactions_publish_tree_and_source_operations"_test = [] {
    auto document = parse_document("");
    normalize_document(document);
    const auto paragraph_id = first_editable(document).id;
    const auto initial_selection = caret(first_editable(document));
    auto [heading, selection] = type_text(
        std::move(document), initial_selection, U"# ");
    expect(fatal(bool(heading.root.children.front().kind == BlockKind::Heading)));
    expect(fatal(bool(selection.active.container_id == paragraph_id)));

    auto original = parse_document("");
    normalize_document(original);
    auto marker = test_edit::document_insert_text(original, caret(first_editable(original)), U"# ");
    expect(fatal(bool(marker.has_value())));
    if (!marker) return;
    const auto tree_count = std::ranges::count_if(marker->operations, [](const auto& operation) {
        return std::holds_alternative<DocumentTreeEdit>(operation);
    });
    const auto text_count = std::ranges::count_if(marker->operations, [](const auto& operation) {
        return std::holds_alternative<DocumentTextOperation>(operation);
    });
    expect(fatal(bool(tree_count == 1u)));
    // History records the actual semantic sequence: insert the marker source,
    // consume that source into block structure, then update the block payload.
    // It does not collapse the command into a before/after whole-tree diff.
    expect(fatal(bool(text_count == 2u)));
};

}; // suite document_operation_tests
