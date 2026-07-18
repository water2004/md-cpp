#include <cstddef>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "support/folia_test.hpp"
import folia.core.ast;
import folia.core.block_source;
import folia.core.block_tree;
import folia.core.command;
import folia.core.dialect;
import folia.core.document;
import folia.core.document_edit;
import folia.core.document_ids;
import folia.core.document_text;
import folia.core.editor;
import folia.core.inline_cst;
import folia.core.inline_document;
import folia.core.instrumentation;
import folia.core.input;
import folia.core.render_builder;
import folia.core.render_model;
import folia.core.serializer;
import folia.core.text_edit;
import folia.core.theme;
import folia.core.utf;

using namespace folia;
using namespace boost::ut;

#include "support/editor_test_support.hpp"

suite editor_block_semantic_tests = [] {

"structural_prefix_backspace_records_uniform_tree_operations"_test = [] {
    auto exercise = [](Editor& editor, NodeId owner, std::string_view label) {
        const auto before_markdown = editor.markdown_utf8();
        const auto before = TextSelection::caret({owner, 0, TextAffinity::Downstream});
        editor.set_selection(before);
        reset_core_operation_counters();
        auto transaction = editor.execute_document_delete_backward(editor.selection());
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

    Editor heading("# title");
    exercise(heading, heading.document().root.children.front().id, "heading");

    Editor quote("> quote");
    exercise(quote, quote.document().root.children.front().children.front().id, "quote");

    Editor callout("> [!NOTE] title\n> body");
    exercise(
        callout,
        callout.document().root.children.front().children.front().id,
        "callout title");

    Editor untitled_callout("> [!NOTE]\n> body");
    exercise(
        untitled_callout,
        untitled_callout.document().root.children.front().children.front().id,
        "untitled callout");

    for (auto markdown : {
             std::string{"- item"},
             std::string{"- one\n- two\n- three"},
             std::string{"> - item"}}) {
        Editor list(markdown);
        const BlockNode* selected = nullptr;
        walk_blocks(list.document().root, [&](const BlockNode& block) {
            if (block.kind != BlockKind::Paragraph) return;
            if (!selected || block.inline_content.source == U"two") selected = &block;
        });
        expect(fatal(bool(selected != nullptr))) << markdown;
        if (selected) exercise(list, selected->id, markdown);
    }

    Editor nested_list("- parent\n  - child");
    const BlockNode* nested_child = nullptr;
    walk_blocks(nested_list.document().root, [&](const BlockNode& block) {
        if (block.kind == BlockKind::Paragraph && block.inline_content.source == U"child") {
            nested_child = &block;
        }
    });
    expect(fatal(bool(nested_child != nullptr)));
    if (nested_child) exercise(nested_list, nested_child->id, "nested list");
};

"cross_block_selection_delete_records_source_and_pruning_operations"_test = [] {
    auto exercise = [](Editor& editor, TextSelection selection, std::string_view label) {
        const auto before_markdown = editor.markdown_utf8();
        editor.set_selection(selection);
        reset_core_operation_counters();
        auto transaction = editor.execute_document_delete_selection(editor.selection());
        expect(fatal(bool(transaction.has_value()))) << label;
        if (!transaction) return;
        const auto counters = read_core_operation_counters();
        expect(fatal(bool(counters.full_document_parses == 0u))) << label;
        expect(fatal(bool(counters.full_document_serializations == 0u))) << label;
        expect(fatal(bool(counters.full_tree_transaction_diffs == 0u))) << label;
        expect(fatal(bool(counters.full_document_text_projections == 0u))) << label;
        expect(fatal(bool(counters.full_document_block_index_scans == 0u))) << label;
        expect(fatal(bool(counters.incremental_document_block_index_repairs == 1u))) << label;
        expect(fatal(bool(!transaction->operations.empty()))) << label;
        expect(fatal(document_indexes_are_exact(editor.document()))) << label;
        const auto after_markdown = editor.markdown_utf8();
        const auto after = editor.selection();
        expect(fatal(bool(editor.undo()))) << label;
        expect(fatal(bool(editor.markdown_utf8() == before_markdown))) << label;
        expect(fatal(bool(editor.selection() == selection))) << label;
        expect(fatal(document_indexes_are_exact(editor.document()))) << label;
        expect(fatal(bool(editor.redo()))) << label;
        expect(fatal(bool(editor.markdown_utf8() == after_markdown))) << label;
        expect(fatal(bool(editor.selection() == after))) << label;
        expect(fatal(document_indexes_are_exact(editor.document()))) << label;
    };

    Editor paragraphs("alpha\n\nbeta\n\ngamma");
    exercise(
        paragraphs,
        TextSelection{
            {paragraphs.document().root.children.front().id, 2, TextAffinity::Downstream},
            {paragraphs.document().root.children.back().id, 3, TextAffinity::Downstream}},
        "paragraphs");

    Editor nested("alpha\n\n> beta\n\n- gamma\n\nomega");
    const BlockNode* first = nullptr;
    const BlockNode* last = nullptr;
    walk_blocks(nested.document().root, [&](const BlockNode& block) {
        if (block.kind != BlockKind::Paragraph) return;
        if (block.inline_content.source == U"alpha") first = &block;
        if (block.inline_content.source == U"omega") last = &block;
    });
    expect(fatal(bool(first != nullptr && last != nullptr)));
    if (first && last) {
        exercise(
            nested,
            TextSelection{
                {first->id, 2, TextAffinity::Downstream},
                {last->id, 2, TextAffinity::Downstream}},
            "nested containers");
    }

    Editor callout("> [!NOTE] title\n> body");
    const auto callout_id = callout.document().root.children.front().id;
    const auto title_id = callout.document().root.children.front().children.front().id;
    const auto body_id = callout.document().root.children.front().children[1].id;
    exercise(
        callout,
        TextSelection{
            {title_id, 2, TextAffinity::Downstream},
            {body_id, 2, TextAffinity::Downstream}},
        "callout title to body");
    const auto* remaining_callout = find_block(callout.document().root, callout_id);
    expect(fatal(bool(remaining_callout != nullptr)));
    expect(fatal(bool(remaining_callout && callout_title_block(*remaining_callout) != nullptr)));

    Editor raw("```\nalpha\n```\n\nomega");
    const auto code_id = raw.document().root.children.front().id;
    const auto paragraph_id = raw.document().root.children.back().id;
    exercise(
        raw,
        TextSelection{
            {code_id, 2, TextAffinity::Downstream},
            {paragraph_id, 2, TextAffinity::Downstream}},
        "raw source boundary");
};

"selection_replacement_composes_delete_and_insert_source_edits"_test = [] {
    Editor editor("alpha");
    const auto owner_id = first_text(editor).id;
    const TextSelection before_selection{
        {owner_id, 1, TextAffinity::Downstream},
        {owner_id, 4, TextAffinity::Upstream}};
    editor.set_selection(before_selection);
    reset_core_operation_counters();
    auto transaction = editor.execute_document_insert_text(editor.selection(), U"X");
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    const auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_parses == 0u)));
    expect(fatal(bool(counters.full_document_serializations == 0u)));
    expect(fatal(bool(counters.full_tree_transaction_diffs == 0u)));
    expect(fatal(bool(counters.inline_reparses == 2u)));
    expect(fatal(bool(transaction->operations.size() == 2u)));
    expect(fatal(bool(editor.markdown_utf8() == "aXa")));
    const auto after_selection = editor.selection();

    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.markdown_utf8() == "alpha")));
    expect(fatal(bool(editor.selection() == before_selection)));
    expect(fatal(bool(editor.redo())));
    expect(fatal(bool(editor.markdown_utf8() == "aXa")));
    expect(fatal(bool(editor.selection() == after_selection)));
};

"typed_block_markers_record_source_and_tree_transformations"_test = [] {
    struct Case {
        std::u32string marker;
        BlockKind expected;
    };
    const std::vector<Case> cases{
        {U"# ", BlockKind::Heading},
        {U"> ", BlockKind::BlockQuote},
        {U"- ", BlockKind::List},
        {U"1. ", BlockKind::List},
    };
    for (const auto& entry : cases) {
        Editor editor;
        for (std::size_t index = 0; index + 1 < entry.marker.size(); ++index) {
            expect(fatal(bool(editor.execute_document_insert_text(
                editor.selection(), std::u32string_view(entry.marker).substr(index, 1)).has_value())));
        }
        const auto before_markdown = editor.markdown_utf8();
        const auto before_selection = editor.selection();
        reset_core_operation_counters();
        auto transaction = editor.execute_document_insert_text(
            editor.selection(), std::u32string_view(entry.marker).substr(entry.marker.size() - 1, 1));
        expect(fatal(bool(transaction.has_value()))) << cps_to_utf8(entry.marker);
        if (!transaction) continue;
        const auto counters = read_core_operation_counters();
        expect(fatal(bool(counters.full_document_parses == 0u))) << cps_to_utf8(entry.marker);
        expect(fatal(bool(counters.full_document_serializations == 0u))) << cps_to_utf8(entry.marker);
        expect(fatal(bool(counters.full_tree_transaction_diffs == 0u))) << cps_to_utf8(entry.marker);
        expect(fatal(bool(editor.document().root.children.front().kind == entry.expected)))
            << cps_to_utf8(entry.marker);
        expect(fatal(bool(editor.selection().active.source_offset == 0u)))
            << cps_to_utf8(entry.marker);
        const auto after_markdown = editor.markdown_utf8();
        const auto after_selection = editor.selection();

        expect(fatal(bool(editor.undo()))) << cps_to_utf8(entry.marker);
        expect(fatal(bool(editor.markdown_utf8() == before_markdown))) << cps_to_utf8(entry.marker);
        expect(fatal(bool(editor.selection() == before_selection))) << cps_to_utf8(entry.marker);
        expect(fatal(bool(editor.redo()))) << cps_to_utf8(entry.marker);
        expect(fatal(bool(editor.markdown_utf8() == after_markdown))) << cps_to_utf8(entry.marker);
        expect(fatal(bool(editor.selection() == after_selection))) << cps_to_utf8(entry.marker);
    }
};

"typed_list_markers_coalesce_adjacent_lists_with_reversible_moves"_test = [] {
    const std::vector<std::string> cases{
        "- before\n\nafter",
        "before\n\n- after",
    };
    for (const auto& markdown : cases) {
        Editor editor(markdown);
        const auto before_markdown = editor.markdown_utf8();
        const auto paragraph = std::ranges::find_if(
            editor.document().root.children,
            [](const BlockNode& block) {
                return block.kind == BlockKind::Paragraph
                    && !block.inline_content.source.empty();
            });
        expect(fatal(bool(paragraph != editor.document().root.children.end()))) << markdown;
        if (paragraph == editor.document().root.children.end()) continue;
        const auto before_selection = TextSelection::caret(
            {paragraph->id, 0, TextAffinity::Downstream});
        editor.set_selection(before_selection);
        reset_core_operation_counters();
        auto transaction = editor.execute_document_insert_text(editor.selection(), U"- ");
        expect(fatal(bool(transaction.has_value()))) << markdown;
        if (!transaction) continue;
        const auto counters = read_core_operation_counters();
        expect(fatal(bool(counters.full_tree_transaction_diffs == 0u))) << markdown;
        expect(fatal(bool(editor.document().root.children.size() == 1u))) << markdown;
        if (editor.document().root.children.size() != 1u) continue;
        expect(fatal(bool(editor.document().root.children.front().kind == BlockKind::List))) << markdown;
        expect(fatal(bool(editor.document().root.children.front().children.size() == 2u))) << markdown;
        const auto after_markdown = editor.markdown_utf8();
        const auto after_selection = editor.selection();
        expect(fatal(bool(editor.undo()))) << markdown;
        expect(fatal(bool(editor.markdown_utf8() == before_markdown))) << markdown;
        expect(fatal(bool(editor.selection() == before_selection))) << markdown;
        expect(fatal(bool(editor.redo()))) << markdown;
        expect(fatal(bool(editor.markdown_utf8() == after_markdown))) << markdown;
        expect(fatal(bool(editor.selection() == after_selection))) << markdown;
    }
};

"typed_task_markers_split_list_segments_with_recorded_moves"_test = [] {
    Editor single;
    expect(fatal(bool(single.execute_document_insert_text(single.selection(), U"- ").has_value())));
    const auto single_before_markdown = single.markdown_utf8();
    const auto single_before = single.selection();
    reset_core_operation_counters();
    auto upgraded = single.execute_document_insert_text(single.selection(), U"[ ] ");
    expect(fatal(bool(upgraded.has_value())));
    if (!upgraded) return;
    auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_tree_transaction_diffs == 0u)));
    expect(fatal(bool(single.document().root.children.front().kind == BlockKind::TaskList)));
    expect(fatal(bool(single.selection().active.source_offset == 0u)));
    const auto single_after_markdown = single.markdown_utf8();
    const auto single_after = single.selection();
    expect(fatal(bool(single.undo())));
    expect(fatal(bool(single.markdown_utf8() == single_before_markdown)));
    expect(fatal(bool(single.selection() == single_before)));
    expect(fatal(bool(single.redo())));
    expect(fatal(bool(single.markdown_utf8() == single_after_markdown)));
    expect(fatal(bool(single.selection() == single_after)));

    Editor middle("- before\n- current\n- after");
    const auto middle_before_markdown = middle.markdown_utf8();
    const BlockNode* current = nullptr;
    walk_blocks(middle.document().root, [&](const BlockNode& block) {
        if (!current && block.kind == BlockKind::Paragraph
            && block.inline_content.source == U"current") {
            current = &block;
        }
    });
    expect(fatal(bool(current != nullptr)));
    if (!current) return;
    const auto middle_before = TextSelection::caret(
        {current->id, 0, TextAffinity::Downstream});
    middle.set_selection(middle_before);
    reset_core_operation_counters();
    auto split = middle.execute_document_insert_text(middle.selection(), U"[x] ");
    expect(fatal(bool(split.has_value())));
    if (!split) return;
    counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_tree_transaction_diffs == 0u)));
    expect(fatal(bool(middle.document().root.children.size() == 3u)));
    if (middle.document().root.children.size() == 3u) {
        expect(fatal(bool(middle.document().root.children[0].kind == BlockKind::List)));
        expect(fatal(bool(middle.document().root.children[1].kind == BlockKind::TaskList)));
        expect(fatal(bool(middle.document().root.children[2].kind == BlockKind::List)));
    }
    const auto middle_after_markdown = middle.markdown_utf8();
    const auto middle_after = middle.selection();
    expect(fatal(bool(middle.undo())));
    expect(fatal(bool(middle.markdown_utf8() == middle_before_markdown)));
    expect(fatal(bool(middle.selection() == middle_before)));
    expect(fatal(bool(middle.redo())));
    expect(fatal(bool(middle.markdown_utf8() == middle_after_markdown)));
    expect(fatal(bool(middle.selection() == middle_after)));
};

}; // suite editor_block_semantic_tests
