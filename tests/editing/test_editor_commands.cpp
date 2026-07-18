#include <cstddef>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "support/folia_test.hpp"
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.block_tree;
import elmd.core.command;
import elmd.core.dialect;
import elmd.core.document;
import elmd.core.document_edit;
import elmd.core.document_ids;
import elmd.core.document_text;
import elmd.core.editor;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.instrumentation;
import elmd.core.input;
import elmd.core.render_builder;
import elmd.core.render_model;
import elmd.core.serializer;
import elmd.core.text_edit;
import elmd.core.theme;
import elmd.core.utf;

using namespace elmd;
using namespace boost::ut;

#include "support/editor_test_support.hpp"

suite editor_command_tests = [] {

"enter_and_cross_block_merge_are_history_transactions"_test = [] {
    Editor editor("alpha");
    editor.set_selection(caret(first_text(editor), 2));
    expect(fatal(bool(editor.execute_document_enter(editor.selection()).has_value())));
    expect(fatal(bool(editor.document().root.children.size() == 2u)));
    const auto second_id = editor.document().root.children[1].id;
    expect(fatal(bool(editor.selection().active.container_id == second_id)));
    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.document().root.children.size() == 1u)));
    expect(fatal(bool(editor.markdown_utf8() == "alpha")));
    expect(fatal(bool(editor.redo())));
    expect(fatal(bool(editor.execute_document_delete_backward(editor.selection()).has_value())));
    expect(fatal(bool(editor.document().root.children.size() == 1u)));
    expect(fatal(bool(editor.markdown_utf8() == "alpha")));
};

"tree_operation_history_restores_inserted_subtrees_and_ids"_test = [] {
    Editor editor("anchor");
    const auto original_selection = caret(first_text(editor), 0);
    editor.set_selection(original_selection);
    Command insert_table;
    insert_table.kind = CommandKind::InsertTable;
    insert_table.rows = 1;
    insert_table.cols = 2;
    expect(fatal(bool(editor.execute_command(insert_table))));
    expect(fatal(bool(editor.document().root.children.size() == 2u)));
    const auto inserted = editor.document().root.children[1];
    const auto after_selection = editor.selection();

    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.document().root.children.size() == 1u)));
    expect(fatal(bool(editor.selection() == original_selection)));
    expect(fatal(bool(editor.redo())));
    expect(fatal(bool(editor.document().root.children.size() == 2u)));
    expect(fatal(bool(editor.document().root.children[1].id == inserted.id)));
    expect(fatal(bool(editor.document().root.children[1].children.front().id == inserted.children.front().id)));
    expect(fatal(bool(editor.selection() == after_selection)));
};

"tree_move_history_replays_list_indent_without_snapshots"_test = [] {
    Editor editor("- one\n- two");
    const auto& list = editor.document().root.children.front();
    const auto second_text_id = list.children[1].children.front().id;
    editor.set_selection(TextSelection::caret({second_text_id, 1, TextAffinity::Downstream}));
    const auto before_markdown = editor.markdown_utf8();
    const auto before_selection = editor.selection();
    expect(fatal(bool(editor.execute_document_indent_list_item(editor.selection()).has_value())));
    const auto after_markdown = editor.markdown_utf8();
    const auto after_selection = editor.selection();
    expect(fatal(bool(after_markdown != before_markdown)));

    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.markdown_utf8() == before_markdown)));
    expect(fatal(bool(editor.selection() == before_selection)));
    expect(fatal(bool(editor.redo())));
    expect(fatal(bool(editor.markdown_utf8() == after_markdown)));
    expect(fatal(bool(editor.selection() == after_selection)));
};

"source_offset_movement_counts_markers"_test = [] {
    Editor editor("**abc**");
    editor.set_selection(caret(first_text(editor), 0));
    for (std::size_t offset = 1; offset <= 7; ++offset) {
        expect(fatal(bool(editor.execute_document_move(DocumentMove::Right, false))));
        expect(fatal(bool(editor.selection().active.source_offset == offset)));
    }
    expect(fatal(bool(!editor.execute_document_move(DocumentMove::Right, false))));
    expect(fatal(bool(editor.execute_document_move(DocumentMove::Left, true))));
    expect(fatal(bool(!editor.selection().is_caret())));
};

"select_all_uses_container_ids_and_source_offsets"_test = [] {
    Editor editor("one\n\ntwo");
    Command command;
    command.kind = CommandKind::SelectAll;
    expect(fatal(bool(editor.execute_command(command))));
    expect(fatal(bool(editor.selection().anchor.container_id == editor.document().root.children.front().id)));
    expect(fatal(bool(editor.selection().anchor.source_offset == 0u)));
    expect(fatal(bool(editor.selection().active.container_id == editor.document().root.children.back().id)));
    expect(fatal(bool(editor.selection().active.source_offset == 3u)));
};

"execute_command_routes_text_delete_and_format_to_source_edits"_test = [] {
    Editor editor("abc");
    editor.set_selection(caret(first_text(editor), 1));
    expect(fatal(bool(editor.execute_command(Command::InsertText(U"X")))));
    expect(fatal(bool(editor.markdown_utf8() == "aXbc")));
    Command backspace; backspace.kind = CommandKind::DeleteBackward;
    expect(fatal(bool(editor.execute_command(backspace))));
    expect(fatal(bool(editor.markdown_utf8() == "abc")));
    editor.set_selection(range(first_text(editor), 0, 3));
    Command strong; strong.kind = CommandKind::ToggleStrong;
    expect(fatal(bool(editor.execute_command(strong))));
    expect(fatal(bool(editor.markdown_utf8() == "**abc**")));
};

"link_image_and_atomic_commands_update_the_unified_tree"_test = [] {
    Editor link("title");
    link.set_selection(range(first_text(link), 0, 5));
    Command insert_link; insert_link.kind = CommandKind::InsertLink; insert_link.href = U"url";
    expect(fatal(bool(link.execute_command(insert_link))));
    expect(fatal(bool(link.markdown_utf8() == "[title](url)")));

    Editor table("anchor");
    table.set_selection(caret(first_text(table), 0));
    Command insert_table; insert_table.kind = CommandKind::InsertTable; insert_table.rows = 1; insert_table.cols = 2;
    expect(fatal(bool(table.execute_command(insert_table))));
    expect(fatal(bool(table.document().root.children.size() == 2u)));
    const auto& table_node = table.document().root.children[1];
    expect(fatal(bool(table_node.kind == BlockKind::Table)));
    expect(fatal(bool(table_node.children.size() == 2u)));
    expect(fatal(bool(table_node.children.front().kind == BlockKind::TableRow)));
    expect(fatal(bool(table_node.children.front().children.front().kind == BlockKind::TableCell)));
};

"simple_structure_commands_publish_reversible_operations"_test = [] {
    auto verify = [](
        Editor& editor,
        const std::string& before_markdown,
        TextSelection before,
        const std::optional<DocumentTransaction>& transaction,
        std::string_view label) {
        expect(fatal(bool(transaction.has_value()))) << label;
        if (!transaction) return;
        const auto counters = read_core_operation_counters();
        expect(fatal(bool(counters.full_document_parses == 0u))) << label;
        expect(fatal(bool(counters.full_document_serializations == 0u))) << label;
        expect(fatal(bool(counters.full_tree_transaction_diffs == 0u))) << label;
        expect(fatal(bool(!transaction->operations.empty()))) << label;
        const auto after_markdown = editor.markdown_utf8();
        const auto after = editor.selection();
        expect(fatal(bool(editor.undo()))) << label;
        expect(fatal(bool(editor.markdown_utf8() == before_markdown))) << label;
        expect(fatal(bool(editor.selection() == before))) << label;
        expect(fatal(bool(editor.redo()))) << label;
        expect(fatal(bool(editor.markdown_utf8() == after_markdown))) << label;
        expect(fatal(bool(editor.selection() == after))) << label;
    };

    Editor heading("title");
    auto heading_before = caret(first_text(heading), 2);
    const auto heading_markdown = heading.markdown_utf8();
    heading.set_selection(heading_before);
    reset_core_operation_counters();
    auto heading_transaction = heading.execute_document_set_heading(heading.selection(), 2);
    verify(heading, heading_markdown, heading_before, heading_transaction, "heading");

    Editor task("- [ ] item");
    auto task_before = caret(first_text(task), 1);
    const auto task_markdown = task.markdown_utf8();
    task.set_selection(task_before);
    reset_core_operation_counters();
    auto task_transaction = task.execute_document_toggle_task_checkbox(task.selection());
    verify(task, task_markdown, task_before, task_transaction, "task checkbox");
    expect(fatal(bool(task.markdown_utf8() == "- [x] item")));

    Editor nested_atomic("> body");
    auto atomic_before = caret(first_text(nested_atomic), 2);
    const auto atomic_markdown = nested_atomic.markdown_utf8();
    nested_atomic.set_selection(atomic_before);
    Command code;
    code.kind = CommandKind::InsertCodeBlock;
    reset_core_operation_counters();
    auto atomic_transaction = nested_atomic.execute_document_insert_atomic_block(
        nested_atomic.selection(), code);
    expect(fatal(bool(nested_atomic.document().root.children.front().children.size() == 2u)));
    verify(nested_atomic, atomic_markdown, atomic_before, atomic_transaction, "nested atomic");

    Editor footnote("body");
    auto footnote_before = caret(first_text(footnote), 4);
    const auto footnote_markdown = footnote.markdown_utf8();
    footnote.set_selection(footnote_before);
    Command insert_footnote;
    insert_footnote.kind = CommandKind::InsertFootnote;
    insert_footnote.text = U"note";
    reset_core_operation_counters();
    auto footnote_transaction = footnote.execute_document_insert_footnote(
        footnote.selection(), insert_footnote);
    verify(footnote, footnote_markdown, footnote_before, footnote_transaction, "footnote");
};

"container_toggles_move_existing_nodes_and_preserve_callout_titles"_test = [] {
    auto verify = [](
        Editor& editor,
        const std::string& before_markdown,
        TextSelection before,
        const std::optional<DocumentTransaction>& transaction,
        std::string_view label) {
        expect(fatal(bool(transaction.has_value()))) << label;
        if (!transaction) return;
        const auto counters = read_core_operation_counters();
        expect(fatal(bool(counters.full_document_parses == 0u))) << label;
        expect(fatal(bool(counters.full_document_serializations == 0u))) << label;
        expect(fatal(bool(counters.full_tree_transaction_diffs == 0u))) << label;
        expect(fatal(bool(!transaction->operations.empty()))) << label;
        const auto after_markdown = editor.markdown_utf8();
        const auto after = editor.selection();
        expect(fatal(bool(editor.undo()))) << label;
        expect(fatal(bool(editor.markdown_utf8() == before_markdown))) << label;
        expect(fatal(bool(editor.selection() == before))) << label;
        expect(fatal(bool(editor.redo()))) << label;
        expect(fatal(bool(editor.markdown_utf8() == after_markdown))) << label;
        expect(fatal(bool(editor.selection() == after))) << label;
    };

    Editor wrap_quote("one\n\ntwo");
    TextSelection quote_range{
        {wrap_quote.document().root.children.front().id, 0, TextAffinity::Downstream},
        {wrap_quote.document().root.children.back().id, 3, TextAffinity::Upstream}};
    const auto wrap_quote_markdown = wrap_quote.markdown_utf8();
    wrap_quote.set_selection(quote_range);
    reset_core_operation_counters();
    auto wrapped_quote = wrap_quote.execute_document_toggle_block_quote(wrap_quote.selection());
    verify(wrap_quote, wrap_quote_markdown, quote_range, wrapped_quote, "wrap quote");

    Editor unwrap_quote("> one\n>\n> two");
    const auto& quote = unwrap_quote.document().root.children.front();
    TextSelection quoted_range{
        {quote.children.front().id, 0, TextAffinity::Downstream},
        {quote.children.back().id, 3, TextAffinity::Upstream}};
    const auto unwrap_quote_markdown = unwrap_quote.markdown_utf8();
    unwrap_quote.set_selection(quoted_range);
    reset_core_operation_counters();
    auto unwrapped_quote = unwrap_quote.execute_document_toggle_block_quote(unwrap_quote.selection());
    verify(unwrap_quote, unwrap_quote_markdown, quoted_range, unwrapped_quote, "unwrap quote");

    Command callout_command;
    callout_command.callout_kind = U"NOTE";
    Editor wrap_callout("one\n\ntwo");
    TextSelection callout_range{
        {wrap_callout.document().root.children.front().id, 0, TextAffinity::Downstream},
        {wrap_callout.document().root.children.back().id, 3, TextAffinity::Upstream}};
    const auto wrap_callout_markdown = wrap_callout.markdown_utf8();
    wrap_callout.set_selection(callout_range);
    reset_core_operation_counters();
    auto wrapped_callout = wrap_callout.execute_document_toggle_callout(
        wrap_callout.selection(), callout_command);
    verify(wrap_callout, wrap_callout_markdown, callout_range, wrapped_callout, "wrap callout");

    Editor unwrap_callout("> [!NOTE] title\n> body");
    const auto callout_id = unwrap_callout.document().root.children.front().id;
    const auto title_id = unwrap_callout.document().root.children.front().children.front().id;
    const auto body_id = unwrap_callout.document().root.children.front().children[1].id;
    const auto callout_before = TextSelection::caret({body_id, 0, TextAffinity::Downstream});
    const auto unwrap_callout_markdown = unwrap_callout.markdown_utf8();
    unwrap_callout.set_selection(callout_before);
    reset_core_operation_counters();
    auto unwrapped_callout = unwrap_callout.execute_document_toggle_callout(
        unwrap_callout.selection(), callout_command);
    expect(fatal(bool(find_block(unwrap_callout.document().root, callout_id) == nullptr)));
    const auto* unwrapped_title = find_block(unwrap_callout.document().root, title_id);
    expect(fatal(bool(unwrapped_title != nullptr && unwrapped_title->kind == BlockKind::Paragraph)));
    verify(
        unwrap_callout,
        unwrap_callout_markdown,
        callout_before,
        unwrapped_callout,
        "unwrap titled callout");

    Editor wrap_list("one\n\ntwo");
    TextSelection list_range{
        {wrap_list.document().root.children.front().id, 0, TextAffinity::Downstream},
        {wrap_list.document().root.children.back().id, 3, TextAffinity::Upstream}};
    const auto wrap_list_markdown = wrap_list.markdown_utf8();
    wrap_list.set_selection(list_range);
    reset_core_operation_counters();
    auto wrapped_list = wrap_list.execute_document_toggle_list(wrap_list.selection(), false, false);
    verify(wrap_list, wrap_list_markdown, list_range, wrapped_list, "wrap list");

    Editor unwrap_list("- one\n- two");
    const auto list_owner = unwrap_list.document().root.children.front().children.front().children.front().id;
    const auto list_before = TextSelection::caret({list_owner, 0, TextAffinity::Downstream});
    const auto unwrap_list_markdown = unwrap_list.markdown_utf8();
    unwrap_list.set_selection(list_before);
    reset_core_operation_counters();
    auto unwrapped_list = unwrap_list.execute_document_toggle_list(unwrap_list.selection(), false, false);
    verify(unwrap_list, unwrap_list_markdown, list_before, unwrapped_list, "unwrap list");
};

"list_indent_and_outdent_record_item_moves_at_arbitrary_depth"_test = [] {
    auto exercise = [](
        Editor& editor,
        TextSelection before,
        bool indent,
        std::string_view label) {
        const auto before_markdown = editor.markdown_utf8();
        editor.set_selection(before);
        reset_core_operation_counters();
        auto transaction = indent
            ? editor.execute_document_indent_list_item(editor.selection())
            : editor.execute_document_outdent_list_item(editor.selection());
        expect(fatal(bool(transaction.has_value()))) << label;
        if (!transaction) return;
        const auto counters = read_core_operation_counters();
        expect(fatal(bool(counters.full_document_parses == 0u))) << label;
        expect(fatal(bool(counters.full_document_serializations == 0u))) << label;
        expect(fatal(bool(counters.full_tree_transaction_diffs == 0u))) << label;
        expect(fatal(bool(counters.full_document_block_index_scans == 0u))) << label;
        expect(fatal(bool(counters.incremental_document_block_index_repairs == 1u))) << label;
        expect(fatal(bool(counters.full_document_symbol_derivations == 0u))) << label;
        expect(fatal(bool(counters.full_document_outline_derivations == 0u))) << label;
        expect(fatal(bool(!transaction->operations.empty()))) << label;
        expect(fatal(document_indexes_are_exact(editor.document()))) << label;
        const auto after_markdown = editor.markdown_utf8();
        const auto after = editor.selection();
        expect(fatal(bool(editor.undo()))) << label;
        expect(fatal(bool(editor.markdown_utf8() == before_markdown))) << label;
        expect(fatal(bool(editor.selection() == before))) << label;
        expect(fatal(document_indexes_are_exact(editor.document()))) << label;
        expect(fatal(bool(editor.redo()))) << label;
        expect(fatal(bool(editor.markdown_utf8() == after_markdown))) << label;
        expect(fatal(bool(editor.selection() == after))) << label;
        expect(fatal(document_indexes_are_exact(editor.document()))) << label;
    };

    for (auto markdown : {
             std::string{"- one\n- two"},
             std::string{"> - one\n> - two"},
             std::string{"- one\n- > two"}}) {
        Editor editor(markdown);
        const BlockNode* two = nullptr;
        walk_blocks(editor.document().root, [&](const BlockNode& block) {
            if (block.kind == BlockKind::Paragraph && block.inline_content.source == U"two") two = &block;
        });
        expect(fatal(bool(two != nullptr))) << markdown;
        if (two) {
            exercise(
                editor,
                TextSelection::caret({two->id, 1, TextAffinity::Downstream}),
                true,
                markdown);
        }
    }

    Editor outdent("- parent\n  - > child");
    const BlockNode* child = nullptr;
    walk_blocks(outdent.document().root, [&](const BlockNode& block) {
        if (block.kind == BlockKind::Paragraph && block.inline_content.source == U"child") child = &block;
    });
    expect(fatal(bool(child != nullptr)));
    if (child) {
        exercise(
            outdent,
            TextSelection::caret({child->id, 2, TextAffinity::Downstream}),
            false,
            "nested quoted item");
    }
};

"moves_reproject_only_order_sensitive_data_present_in_the_moved_subtree"_test = [] {
    Editor symbols("- first\n- second[^1]\n\n[^1]: note");
    const BlockNode* referenced = nullptr;
    walk_blocks(symbols.document().root, [&](const BlockNode& block) {
        if (block.inline_content.source == U"second[^1]") referenced = &block;
    });
    expect(fatal(bool(referenced != nullptr)));
    if (!referenced) return;
    symbols.set_selection(caret(*referenced, 0));
    reset_core_operation_counters();
    expect(fatal(bool(symbols.execute_document_indent_list_item(symbols.selection()).has_value())));
    auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_symbol_derivations == 0u)));
    expect(fatal(bool(counters.full_document_outline_derivations == 0u)));
    expect(fatal(bool(symbols.symbols() == build_document_symbol_index(symbols.document()))));
    reset_core_operation_counters();
    expect(fatal(bool(symbols.undo())));
    counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_symbol_derivations == 0u)));
    expect(fatal(bool(symbols.symbols() == build_document_symbol_index(symbols.document()))));
    reset_core_operation_counters();
    expect(fatal(bool(symbols.redo())));
    counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_symbol_derivations == 0u)));
    expect(fatal(bool(symbols.symbols() == build_document_symbol_index(symbols.document()))));

    Editor headings("- first\n- # second");
    const BlockNode* heading = nullptr;
    walk_blocks(headings.document().root, [&](const BlockNode& block) {
        if (block.kind == BlockKind::Heading) heading = &block;
    });
    expect(fatal(bool(heading != nullptr)));
    if (!heading) return;
    headings.set_selection(caret(*heading, 0));
    reset_core_operation_counters();
    expect(fatal(bool(headings.execute_document_indent_list_item(headings.selection()).has_value())));
    counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_symbol_derivations == 0u)));
    expect(fatal(bool(counters.full_document_outline_derivations == 1u)));
    expect(fatal(bool(headings.symbols() == build_document_symbol_index(headings.document()))));
};

}; // suite editor_command_tests
