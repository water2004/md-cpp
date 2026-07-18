#include <algorithm>
#include <cstddef>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "support/folia_test.hpp"
import folia.core.ast;
import folia.core.block_source;
import folia.core.block_tree;
import folia.core.document;
import folia.core.document_edit;
import folia.core.document_history;
import folia.core.document_ids;
import folia.core.document_operation_apply;
import folia.core.document_symbols;
import folia.core.document_text;
import folia.core.inline_cst;
import folia.core.inline_document;
import folia.core.parser;
import folia.core.serializer;
import folia.core.text_edit;

using namespace folia;
using namespace boost::ut;

#include "support/document_edit_test_support.hpp"

suite document_structure_tests = [] {

"cross_block_selection_deletes_structure_and_merges_sources"_test = [] {
    auto document = parse_document("alpha\n\nbeta\n\ngamma");
    expect(fatal(bool(document.root.children.size() == 3u)));
    if (document.root.children.size() != 3u) return;
    TextSelection selection{
        {document.root.children[0].id, 2, TextAffinity::Downstream},
        {document.root.children[2].id, 3, TextAffinity::Downstream}};
    auto transaction = test_edit::document_delete_selection(document, selection);
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(transaction->after.root.children.size() == 1u)));
    expect(fatal(bool(transaction->after.root.children.front().inline_content.source == U"alma")));
    expect(fatal(bool(transaction->selection_after.active.source_offset == 2u)));
    expect_document_valid(transaction->after);
};

"paste_normalizes_newlines_and_parses_clipboard_markdown"_test = [] {
    auto document = parse_document("ab");
    auto transaction = test_edit::document_paste_text(document, caret(first_editable(document), 1), U"X\r\nY");
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(transaction->after.root.children.size() == 1u)));
    expect(fatal(bool(transaction->after.root.children[0].inline_content.source == U"aX\nYb")));
    expect_document_valid(transaction->after);
};

"paste_semantically_merges_fragment_head_and_anchor_tail"_test = [] {
    auto document = parse_document("ab");
    auto transaction = test_edit::document_paste_text(
        document,
        caret(first_editable(document), 1),
        U"X\n\nY");
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(transaction->after.root.children.size() == 2u)));
    if (transaction->after.root.children.size() != 2u) return;
    expect(fatal(bool(transaction->after.root.children[0].inline_content.source == U"aX")));
    expect(fatal(bool(transaction->after.root.children[1].inline_content.source == U"Yb")));
    expect(fatal(bool(transaction->selection_after.active.container_id
        == transaction->after.root.children[1].id)));
    expect(fatal(bool(transaction->selection_after.active.source_offset == 1u)));
    expect_document_valid(transaction->after);
};

"paste_uses_literal_semantics_for_raw_blocks_and_table_cells"_test = [] {
    auto code = parse_document("```cpp\nabc\n```");
    auto code_paste = test_edit::document_paste_text(
        code,
        caret(code.root.children.front(), 7),
        U"# h\r\n- item");
    expect(fatal(bool(code_paste.has_value())));
    if (code_paste) {
        expect(fatal(bool(code_paste->after.root.children.size() == 1u)));
        expect(fatal(bool(code_paste->after.root.children.front().kind == BlockKind::CodeBlock)));
        expect(fatal(bool(code_paste->after.root.children.front().block_source.source()
            == U"```cpp\n# h\n- itemabc\n```")));
        expect_document_valid(code_paste->after);
    }

    auto table = parse_document("| a | b |\n| --- | --- |\n| c | d |");
    auto* first_cell = &table.root.children.front().children.front().children.front();
    auto cell_paste = test_edit::document_paste_text(
        table,
        TextSelection{
            {first_cell->id, 0, TextAffinity::Downstream},
            {first_cell->id, first_cell->inline_content.source.size(), TextAffinity::Upstream}},
        U"  x\r\ny  ");
    expect(fatal(bool(cell_paste.has_value())));
    if (cell_paste) {
        const auto& cell = cell_paste->after.root.children.front().children.front().children.front();
        expect(fatal(bool(cell.inline_content.source == U"x<br>y")));
        expect_document_valid(cell_paste->after);
    }
};

"paragraph_heading_conversion_preserves_inline_source"_test = [] {
    auto document = parse_document("__title__");
    auto heading = test_edit::document_set_heading(document, caret(first_editable(document), 3), 3);
    expect(fatal(bool(heading.has_value())));
    if (!heading) return;
    expect(fatal(bool(heading->after.root.children.front().kind == BlockKind::Heading)));
    expect(fatal(bool(heading->after.root.children.front().text_special().level == 3u)));
    expect(fatal(bool(heading->after.root.children.front().inline_content.source == U"__title__")));
    auto paragraph = test_edit::document_set_heading(heading->after, heading->selection_after, 0);
    expect(fatal(bool(paragraph.has_value())));
    if (paragraph) expect(fatal(bool(paragraph->after.root.children.front().kind == BlockKind::Paragraph)));
};

"quote_callout_and_list_commands_use_unified_children"_test = [] {
    auto document = parse_document("one\n\ntwo");
    TextSelection both{{document.root.children[0].id, 0, TextAffinity::Downstream}, {document.root.children[1].id, 3, TextAffinity::Downstream}};
    auto quote = test_edit::document_toggle_block_quote(document, both);
    expect(fatal(bool(quote.has_value())));
    if (quote) {
        expect(fatal(bool(quote->after.root.children.front().kind == BlockKind::BlockQuote)));
        expect(fatal(bool(quote->after.root.children.front().children.size() == 2u)));
    }
    auto list = test_edit::document_toggle_list(document, both, ListStyle::Bullet);
    expect(fatal(bool(list.has_value())));
    if (list) {
        const auto& root_list = list->after.root.children.front();
        expect(fatal(bool(root_list.kind == BlockKind::List)));
        expect(fatal(bool(root_list.children.size() == 2u)));
        expect(fatal(bool(root_list.children.front().kind == BlockKind::ListItem)));
        expect(fatal(bool(root_list.children.front().children.front().inline_content.source == U"one")));
    }
    auto callout = test_edit::document_toggle_callout(document, both, "NOTE");
    expect(fatal(bool(callout.has_value())));
    if (callout) {
        expect(fatal(bool(callout->after.root.children.front().kind == BlockKind::Callout)));
        expect(fatal(bool(callout->after.root.children.front().children.size() == 2u)));
    }
};

"callout_command_switches_kind_in_place_and_preserves_header_spacing"_test = [] {
    auto document = parse_document("> [!note]  title\n> body");
    auto const callout_id = document.root.children.front().id;
    auto const body_id = document.root.children.front().children[1].id;
    auto switched = test_edit::document_toggle_callout(
        document,
        TextSelection::caret({body_id, 0, TextAffinity::Downstream}),
        "tip");
    expect(fatal(bool(switched.has_value())));
    if (!switched) return;
    auto const* callout = find_block(switched->after.root, callout_id);
    expect(fatal(bool(callout != nullptr && callout->kind == BlockKind::Callout)));
    expect(fatal(bool(callout != nullptr && callout->container_special().callout_kind == "TIP")));
    expect(fatal(bool(callout != nullptr && callout->children.size() == 2u)));
    const auto markdown = serialize_markdown(switched->after);
    expect(fatal(bool(markdown == "> [!TIP]  title\n> body"))) << markdown;
    const auto reparsed = parse_document(markdown);
    expect(fatal(bool(reparsed.root.children.front().kind == BlockKind::Callout)));
    expect(fatal(bool(reparsed.root.children.front().container_special().callout_kind == "TIP")));
    expect_document_valid(switched->after);
};

"list_indent_and_outdent_move_existing_nodes"_test = [] {
    auto document = parse_document("- one\n- two\n");
    auto& list = document.root.children.front();
    const auto second_paragraph_id = list.children[1].children.front().id;
    auto indented = test_edit::document_indent_list_item(document, TextSelection::caret({second_paragraph_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(indented.has_value())));
    if (!indented) return;
    const auto& after_list = indented->after.root.children.front();
    expect(fatal(bool(after_list.children.size() == 1u)));
    expect(fatal(bool(after_list.children.front().children.back().kind == BlockKind::List)));
    auto outdented = test_edit::document_outdent_list_item(indented->after, indented->selection_after);
    expect(fatal(bool(outdented.has_value())));
    if (outdented) expect(fatal(bool(outdented->after.root.children.front().children.size() == 2u)));
};

"table_commands_operate_on_table_row_and_cell_nodes"_test = [] {
    auto document = parse_document("anchor");
    auto table = make_table_block(document, 1, 2);
    const auto first_cell_id = table.children.front().children.front().id;
    auto inserted = test_edit::document_insert_atomic_block(document, caret(first_editable(document), 0), std::move(table));
    expect(fatal(bool(inserted.has_value())));
    if (!inserted) return;
    TextSelection in_cell = TextSelection::caret({first_cell_id, 0, TextAffinity::Downstream});
    auto column = test_edit::document_edit_table(inserted->after, in_cell, DocumentTableEdit::InsertColumnRight);
    expect(fatal(bool(column.has_value())));
    if (!column) return;
    const auto& table_after_column = column->after.root.children[1];
    expect(fatal(bool(table_after_column.children.front().children.size() == 3u)));
    expect(fatal(bool(table_after_column.children[1].children.size() == 3u)));
    auto row = test_edit::document_edit_table(column->after, in_cell, DocumentTableEdit::InsertRowBelow);
    expect(fatal(bool(row.has_value())));
    if (row) expect(fatal(bool(row->after.root.children[1].children.size() == 3u)));
    auto aligned = test_edit::document_edit_table(column->after, in_cell, DocumentTableEdit::SetColumnAlignment, TableAlignment::Center);
    expect(fatal(bool(aligned.has_value())));
    if (aligned) expect(fatal(bool(
        aligned->after.root.children[1].table_special().table_aligns.front() == TableAlignment::Center)));
};

"random_source_edits_preserve_selection_and_losslessness"_test = [] {
    std::mt19937_64 random(0x5eed);
    auto document = parse_document("");
    normalize_document(document);
    auto position = caret(first_editable(document), 0);
    for (std::size_t step = 0; step < 500; ++step) {
        const bool insert = first_editable(document).inline_content.source.empty() || (random() & 1u) == 0;
        if (insert) {
            const char32_t value = U'a' + static_cast<char32_t>(random() % 26);
            auto transaction = test_edit::document_insert_text(document, position, std::u32string(1, value));
            expect(fatal(bool(transaction.has_value())));
            if (!transaction) break;
            document = std::move(transaction->after);
            position = transaction->selection_after;
        } else {
            auto transaction = test_edit::document_delete_backward(document, position);
            expect(fatal(bool(transaction.has_value())));
            if (!transaction) break;
            document = std::move(transaction->after);
            position = transaction->selection_after;
        }
        const auto& inline_document = first_editable(document).inline_content;
        expect(fatal(bool(position.active.source_offset <= inline_document.source.size())));
        expect_document_valid(document);
    }
};

"save_reload_after_edits_is_lossless"_test = [] {
    auto document = parse_document("*abc*\n\n[title](<url>)");
    auto transaction = test_edit::document_insert_text(document, caret(document.root.children[0], 3), U"X");
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    const auto saved = serialize_markdown(transaction->after);
    const auto reloaded = parse_document(saved);
    expect(fatal(bool(serialize_markdown(reloaded) == saved)));
    expect_document_valid(reloaded);
};

}; // suite document_structure_tests
