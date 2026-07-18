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

suite editor_paste_tests = [] {

"paste_composes_recorded_source_operations_without_a_final_tree_diff"_test = [] {
    Editor editor("ab");
    const auto before_selection = caret(first_text(editor), 1);
    editor.set_selection(before_selection);
    reset_core_operation_counters();
    auto transaction = editor.execute_document_paste_text(editor.selection(), U"X");
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    const auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_parses == 0u)));
    expect(fatal(bool(counters.full_document_serializations == 0u)));
    expect(fatal(bool(counters.full_tree_transaction_diffs == 0u)));
    expect(fatal(bool(counters.inline_reparses == 3u)));
    expect(fatal(bool(transaction->operations.size() == 1u)));
    expect(fatal(bool(std::holds_alternative<DocumentTextOperation>(transaction->operations.front()))));
    expect(fatal(bool(editor.markdown_utf8() == "aXb")));
    const auto after_selection = editor.selection();

    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.markdown_utf8() == "ab")));
    expect(fatal(bool(editor.selection() == before_selection)));
    expect(fatal(bool(editor.redo())));
    expect(fatal(bool(editor.markdown_utf8() == "aXb")));
    expect(fatal(bool(editor.selection() == after_selection)));

    Editor multiline("ab");
    const auto multiline_before = caret(first_text(multiline), 1);
    multiline.set_selection(multiline_before);
    reset_core_operation_counters();
    auto multiline_transaction = multiline.execute_document_paste_text(
        multiline.selection(), U"X\n\nY");
    expect(fatal(bool(multiline_transaction.has_value())));
    if (!multiline_transaction) return;
    const auto multiline_counters = read_core_operation_counters();
    expect(fatal(bool(multiline_counters.full_document_parses == 0u)));
    expect(fatal(bool(multiline_counters.full_document_serializations == 0u)));
    expect(fatal(bool(multiline_counters.full_tree_transaction_diffs == 0u)));
    expect(fatal(bool(multiline.markdown_utf8() == "aX\n\nYb")));
    const auto multiline_after = multiline.selection();
    expect(fatal(bool(multiline.undo())));
    expect(fatal(bool(multiline.markdown_utf8() == "ab")));
    expect(fatal(bool(multiline.selection() == multiline_before)));
    expect(fatal(bool(multiline.redo())));
    expect(fatal(bool(multiline.markdown_utf8() == "aX\n\nYb")));
    expect(fatal(bool(multiline.selection() == multiline_after)));
};

"paste_parses_single_line_markdown_and_splices_structural_blocks"_test = [] {
    const std::vector<std::pair<std::u32string, BlockKind>> cases{
        {U"# Title", BlockKind::Heading},
        {U"- item", BlockKind::List},
        {U"> quote", BlockKind::BlockQuote},
        {U"$$\nx + y\n$$", BlockKind::MathBlock},
    };
    for (const auto& [source, expected_kind] : cases) {
        Editor editor;
        editor.set_selection(caret(first_text(editor), 0));
        auto transaction = editor.execute_document_paste_text(editor.selection(), source);
        expect(fatal(bool(transaction.has_value())));
        if (!transaction) continue;
        expect(fatal(bool(editor.document().root.children.size() == 1u)));
        expect(fatal(bool(editor.document().root.children.front().kind == expected_kind)));
        expect(fatal(bool(validate_document(editor.document()).empty())));
        const auto after = editor.markdown_utf8();
        expect(fatal(bool(editor.undo())));
        expect(fatal(bool(editor.markdown_utf8().empty())));
        expect(fatal(bool(editor.redo())));
        expect(fatal(bool(editor.markdown_utf8() == after)));
    }

    Editor quote("ab");
    quote.set_selection(caret(first_text(quote), 1));
    expect(fatal(bool(quote.execute_document_paste_text(
        quote.selection(), U"> q").has_value())));
    expect(fatal(bool(quote.markdown_utf8() == "a\n\n> qb")));
    expect(fatal(bool(quote.selection().active.source_offset == 1u)));
};

"paste_merges_compatible_lists_at_the_semantic_item_position"_test = [] {
    Editor bullet("- a");
    auto bullet_owner = first_text(bullet);
    bullet.set_selection(caret(bullet_owner, 1));
    expect(fatal(bool(bullet.execute_document_paste_text(
        bullet.selection(), U"- x\n- y").has_value())));
    const auto& bullet_list = bullet.document().root.children.front();
    expect(fatal(bool(bullet_list.kind == BlockKind::List)));
    expect(fatal(bool(bullet_list.children.size() == 2u)));
    expect(fatal(bool(bullet_list.children[0].children[0].inline_content.source == U"ax")));
    expect(fatal(bool(bullet_list.children[1].children[0].inline_content.source == U"y")));
    expect(fatal(bool(bullet.markdown_utf8() == "- ax\n- y")));
    const auto bullet_after = bullet.selection();
    expect(fatal(bool(bullet.undo())));
    expect(fatal(bool(bullet.markdown_utf8() == "- a")));
    expect(fatal(bool(bullet.redo())));
    expect(fatal(bool(bullet.markdown_utf8() == "- ax\n- y")));
    expect(fatal(bool(bullet.selection() == bullet_after)));

    Editor task("- [ ] a");
    task.set_selection(caret(first_text(task), 1));
    expect(fatal(bool(task.execute_document_paste_text(
        task.selection(), U"- [ ] x\n- [ ] y").has_value())));
    const auto& task_list = task.document().root.children.front();
    expect(fatal(bool(task_list.kind == BlockKind::TaskList)));
    expect(fatal(bool(task_list.children.size() == 3u)));
    expect(fatal(bool(task_list.children[0].children[0].inline_content.source == U"a")));
    expect(fatal(bool(task_list.children[1].children[0].inline_content.source == U"x")));
    expect(fatal(bool(task_list.children[2].children[0].inline_content.source == U"y")));

    Editor nested("> - a");
    nested.set_selection(caret(first_text(nested), 1));
    expect(fatal(bool(nested.execute_document_paste_text(
        nested.selection(), U"- x\n- y").has_value())));
    const auto& nested_list = nested.document().root.children.front().children.front();
    expect(fatal(bool(nested_list.kind == BlockKind::List)));
    expect(fatal(bool(nested_list.children.size() == 2u)));
    expect(fatal(bool(nested_list.children[0].children[0].inline_content.source == U"ax")));
    expect(fatal(bool(nested_list.children[1].children[0].inline_content.source == U"y")));
};

"structural_paste_uses_the_nearest_recursive_container_context"_test = [] {
    Editor callout("> [!NOTE] title\n> body");
    const auto callout_id = callout.document().root.children.front().id;
    const auto title_id = callout.document().root.children.front().children.front().id;
    callout.set_selection(TextSelection::caret({
        title_id,
        0,
        TextAffinity::Downstream}));
    expect(fatal(bool(callout.execute_document_paste_text(
        callout.selection(), U"> nested").has_value()))) << "paste transaction";
    const auto& result = callout.document().root.children.front();
    expect(fatal(bool(result.kind == BlockKind::Callout))) << callout.markdown_utf8();
    expect(fatal(bool(callout_title_block(result) == nullptr))) << callout.markdown_utf8();
    expect(fatal(bool(result.children.size() == 2u))) << callout.markdown_utf8();
    expect(fatal(bool(result.children.front().kind == BlockKind::BlockQuote))) << callout.markdown_utf8();
    expect(fatal(bool(result.children.front().children.front().inline_content.source
        == U"nestedtitle"))) << callout.markdown_utf8();
    expect(fatal(bool(result.children.back().inline_content.source == U"body"))) << callout.markdown_utf8();
    expect(fatal(bool(validate_document(callout.document()).empty()))) << callout.markdown_utf8();
    expect(fatal(bool(callout.undo())));
    expect(fatal(bool(callout.markdown_utf8() == "> [!NOTE] title\n> body")));
    expect(fatal(bool(callout.redo())));
    expect(fatal(bool(validate_document(callout.document()).empty())));
};

"paste_uses_inline_cst_context_for_a_link_destination"_test = [] {
    Editor editor("[my text]()");
    const auto owner = first_text(editor);
    editor.set_selection(caret(owner, 10));
    expect(fatal(bool(editor.execute_document_paste_text(
        editor.selection(),
        U"[Some Page](https://example.com/page)").has_value())));
    expect(fatal(bool(editor.markdown_utf8()
        == "[my text](https://example.com/page)")));
    expect(fatal(bool(editor.selection().active.source_offset == 34u)));
    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.markdown_utf8() == "[my text]()")));
    expect(fatal(bool(editor.redo())));
    expect(fatal(bool(editor.markdown_utf8()
        == "[my text](https://example.com/page)")));
};

"clipboard_copy_and_paste_round_trip_structural_fragments"_test = [] {
    Editor source("foo\n\n- one\n- two");
    const auto& paragraph = source.document().root.children.front();
    const auto& list = source.document().root.children.back();
    const auto& second_item = list.children[1].children.front();
    source.set_selection({
        {paragraph.id, 1, TextAffinity::Downstream},
        {second_item.id, 1, TextAffinity::Upstream}});
    const auto fragment = source.selected_markdown_cps();
    expect(fatal(bool(fragment == U"oo\n\n- one\n- t")));

    Editor target;
    target.set_selection(caret(first_text(target), 0));
    expect(fatal(bool(target.execute_document_paste_text(
        target.selection(), fragment).has_value())));
    expect(fatal(bool(target.markdown_cps() == fragment)));
    expect(fatal(bool(target.document().root.children.size() == 2u)));
    if (target.document().root.children.size() != 2u) return;
    expect(fatal(bool(target.document().root.children.back().kind == BlockKind::List)));
    expect(fatal(bool(validate_document(target.document()).empty())));

    Editor table_source("| a | b |\n| --- | --- |\n| c | d |");
    const auto table_fragments = document_text_fragments(table_source.document());
    expect(fatal(bool(table_fragments.size() == 4u)));
    if (table_fragments.size() != 4u) return;
    table_source.set_selection({
        {table_fragments.front().container_id, 0, TextAffinity::Downstream},
        {table_fragments.back().container_id, 1, TextAffinity::Upstream}});
    const auto table_fragment = table_source.selected_markdown_cps();
    Editor table_target;
    table_target.set_selection(caret(first_text(table_target), 0));
    expect(fatal(bool(table_target.execute_document_paste_text(
        table_target.selection(), table_fragment).has_value())));
    expect(fatal(bool(table_target.document().root.children.size() == 1u)));
    expect(fatal(bool(table_target.document().root.children.front().kind == BlockKind::Table)));
    expect(fatal(bool(validate_document(table_target.document()).empty())));
};

"paste_over_cross_block_selection_is_one_exact_history_transaction"_test = [] {
    Editor editor("alpha\n\nbeta\n\ngamma");
    expect(fatal(bool(editor.document().root.children.size() == 3u)));
    if (editor.document().root.children.size() != 3u) return;
    const auto before = TextSelection{
        {editor.document().root.children[0].id, 2, TextAffinity::Downstream},
        {editor.document().root.children[2].id, 3, TextAffinity::Upstream}};
    editor.set_selection(before);
    expect(fatal(bool(editor.execute_document_paste_text(
        editor.selection(), U"> q\n>\n> - x").has_value())));
    expect(fatal(bool(editor.markdown_utf8() == "al\n\n> q\n>\n> - xma")));
    const auto after = editor.selection();
    expect(fatal(bool(validate_document(editor.document()).empty())));
    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.markdown_utf8() == "alpha\n\nbeta\n\ngamma")));
    expect(fatal(bool(editor.selection() == before)));
    expect(fatal(bool(editor.redo())));
    expect(fatal(bool(editor.markdown_utf8() == "al\n\n> q\n>\n> - xma")));
    expect(fatal(bool(editor.selection() == after)));
};

}; // suite editor_paste_tests
