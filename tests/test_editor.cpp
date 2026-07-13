#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "elmd_test.hpp"
import elmd.core.ast;
import elmd.core.block_tree;
import elmd.core.command;
import elmd.core.dialect;
import elmd.core.document;
import elmd.core.document_edit;
import elmd.core.document_text;
import elmd.core.editor;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.instrumentation;
import elmd.core.input;
import elmd.core.serializer;
import elmd.core.text_edit;
import elmd.core.theme;
import elmd.core.utf;

using namespace elmd;
using namespace boost::ut;

namespace {

TextSelection caret(const BlockNode& node, std::size_t offset = 0) {
    return TextSelection::caret(TextPosition{node.id, offset, TextAffinity::Downstream});
}

TextSelection range(const BlockNode& node, std::size_t start, std::size_t end) {
    return {{node.id, start, TextAffinity::Downstream}, {node.id, end, TextAffinity::Downstream}};
}

const BlockNode& first_text(const Editor& editor) {
    const BlockNode* found = nullptr;
    walk_blocks(editor.document().root, [&](const BlockNode& node) {
        if (!found && (node.kind == BlockKind::Paragraph || node.kind == BlockKind::Heading || node.kind == BlockKind::TableCell)) found = &node;
    });
    return *found;
}

} // namespace

suite editor_tests = [] {

"default_editor_has_one_authoritative_selection"_test = [] {
    Editor editor;
    expect(fatal(bool(editor.document().root.kind == BlockKind::Document)));
    expect(fatal(bool(editor.document().root.children.size() == 1u)));
    expect(fatal(bool(editor.selection().is_caret())));
    expect(fatal(bool(editor.selection().active.container_id == editor.document().root.children.front().id)));
    expect(fatal(bool(editor.selection().active.source_offset == 0u)));
};

"insert_undo_redo_restore_source_and_selection_exactly"_test = [] {
    Editor editor("abc");
    editor.set_selection(caret(first_text(editor), 1));
    const auto before_selection = editor.selection();
    expect(fatal(bool(editor.execute_document_insert_text(editor.selection(), U"X").has_value())));
    expect(fatal(bool(editor.markdown_utf8() == "aXbc")));
    const auto after_selection = editor.selection();
    expect(fatal(bool(after_selection.active.source_offset == 2u)));
    expect(fatal(bool(editor.has_undo())));
    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.markdown_utf8() == "abc")));
    expect(fatal(bool(editor.selection() == before_selection)));
    expect(fatal(bool(editor.has_redo())));
    expect(fatal(bool(editor.redo())));
    expect(fatal(bool(editor.markdown_utf8() == "aXbc")));
    expect(fatal(bool(editor.selection() == after_selection)));
};

"normal_source_edits_never_parse_or_serialize_the_full_document"_test = [] {
    Editor editor("**alpha**\n\nbeta");
    editor.set_selection(caret(first_text(editor), 3));
    reset_core_operation_counters();
    expect(fatal(bool(editor.execute_command(Command::InsertText(U"X")))));
    const auto edit = read_core_operation_counters();
    expect(fatal(bool(edit.full_document_parses == 0u)));
    expect(fatal(bool(edit.full_document_serializations == 0u)));
    expect(fatal(bool(edit.full_tree_transaction_diffs == 0u)));
    expect(fatal(bool(edit.inline_reparses == 1u)));

    reset_core_operation_counters();
    expect(fatal(bool(editor.undo())));
    const auto undo = read_core_operation_counters();
    expect(fatal(bool(undo.full_document_parses == 0u)));
    expect(fatal(bool(undo.full_document_serializations == 0u)));
    expect(fatal(bool(undo.full_tree_transaction_diffs == 0u)));
    expect(fatal(bool(undo.inline_reparses == 1u)));
};

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
    expect(fatal(bool(counters.inline_reparses == 1u)));
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
        multiline.selection(), U"X\nY");
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

"paragraph_splits_record_local_source_and_tree_edits"_test = [] {
    const std::vector<std::string> cases{"ab", "> ab"};
    for (const auto& markdown : cases) {
        Editor editor(markdown);
        const auto owner_id = first_text(editor).id;
        const auto before_selection = TextSelection::caret(
            {owner_id, 1, TextAffinity::Downstream});
        const auto before_markdown = editor.markdown_utf8();
        editor.set_selection(before_selection);
        reset_core_operation_counters();
        auto transaction = editor.execute_document_enter(editor.selection());
        expect(fatal(bool(transaction.has_value()))) << markdown;
        if (!transaction) continue;
        const auto counters = read_core_operation_counters();
        expect(fatal(bool(counters.full_document_parses == 0u))) << markdown;
        expect(fatal(bool(counters.full_document_serializations == 0u))) << markdown;
        expect(fatal(bool(counters.full_tree_transaction_diffs == 0u))) << markdown;
        expect(fatal(bool(transaction->operations.size() == 2u))) << markdown;
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

"list_item_splits_record_item_inserts_independent_of_ancestors"_test = [] {
    const std::vector<std::string> cases{
        "- ab",
        "1. ab",
        "- [ ] ab",
        "> - ab",
    };
    for (const auto& markdown : cases) {
        Editor editor(markdown);
        const auto owner_id = first_text(editor).id;
        const auto before_selection = TextSelection::caret(
            {owner_id, 1, TextAffinity::Downstream});
        const auto before_markdown = editor.markdown_utf8();
        editor.set_selection(before_selection);
        reset_core_operation_counters();
        auto transaction = editor.execute_document_enter(editor.selection());
        expect(fatal(bool(transaction.has_value()))) << markdown;
        if (!transaction) continue;
        const auto counters = read_core_operation_counters();
        expect(fatal(bool(counters.full_document_parses == 0u))) << markdown;
        expect(fatal(bool(counters.full_document_serializations == 0u))) << markdown;
        expect(fatal(bool(counters.full_tree_transaction_diffs == 0u))) << markdown;
        const BlockNode* list = nullptr;
        walk_blocks(editor.document().root, [&](const BlockNode& block) {
            if (!list && (block.kind == BlockKind::List || block.kind == BlockKind::TaskList)) {
                list = &block;
            }
        });
        expect(fatal(bool(list != nullptr))) << markdown;
        expect(fatal(bool(list && list->children.size() == 2u))) << markdown;
        const auto after_markdown = editor.markdown_utf8();
        const auto after_selection = editor.selection();

        expect(fatal(bool(editor.undo()))) << markdown;
        expect(fatal(bool(editor.markdown_utf8() == before_markdown))) << markdown;
        expect(fatal(bool(editor.selection() == before_selection))) << markdown;
        expect(fatal(bool(editor.redo()))) << markdown;
        expect(fatal(bool(editor.markdown_utf8() == after_markdown))) << markdown;
        expect(fatal(bool(editor.selection() == after_selection))) << markdown;
    }

    Editor at_end("- ab");
    const auto owner_id = first_text(at_end).id;
    const auto before = TextSelection::caret({owner_id, 2, TextAffinity::Upstream});
    at_end.set_selection(before);
    reset_core_operation_counters();
    auto transaction = at_end.execute_document_enter(at_end.selection());
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    const auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_tree_transaction_diffs == 0u)));
    const auto& list = at_end.document().root.children.front();
    expect(fatal(bool(list.kind == BlockKind::List)));
    expect(fatal(bool(list.children.size() == 2u)));
    expect(fatal(bool(list.children.back().children.front().inline_content.source.empty())));
    expect(fatal(bool(at_end.undo())));
    expect(fatal(bool(at_end.markdown_utf8() == "- ab")));
    expect(fatal(bool(at_end.selection() == before)));

    Editor with_nested_block("- ab\n\n  > quote");
    const auto nested_before_markdown = with_nested_block.markdown_utf8();
    const auto nested_owner = first_text(with_nested_block).id;
    const auto nested_before = TextSelection::caret(
        {nested_owner, 2, TextAffinity::Upstream});
    with_nested_block.set_selection(nested_before);
    reset_core_operation_counters();
    auto moved = with_nested_block.execute_document_enter(with_nested_block.selection());
    expect(fatal(bool(moved.has_value())));
    if (!moved) return;
    const auto moved_counters = read_core_operation_counters();
    expect(fatal(bool(moved_counters.full_tree_transaction_diffs == 0u)));
    const auto& moved_list = with_nested_block.document().root.children.front();
    expect(fatal(bool(moved_list.children.size() == 2u)));
    expect(fatal(bool(moved_list.children.back().children.size() == 2u)));
    expect(fatal(bool(moved_list.children.back().children.back().kind == BlockKind::BlockQuote)));
    const auto nested_after_markdown = with_nested_block.markdown_utf8();
    const auto nested_after = with_nested_block.selection();
    expect(fatal(bool(with_nested_block.undo())));
    expect(fatal(bool(with_nested_block.markdown_utf8() == nested_before_markdown)));
    expect(fatal(bool(with_nested_block.selection() == nested_before)));
    expect(fatal(bool(with_nested_block.redo())));
    expect(fatal(bool(with_nested_block.markdown_utf8() == nested_after_markdown)));
    expect(fatal(bool(with_nested_block.selection() == nested_after)));
};

"empty_block_exits_record_tree_operations_and_restore_exactly"_test = [] {
    auto exercise = [](Editor& editor, TextSelection before, std::string_view label) {
        const auto before_markdown = editor.markdown_utf8();
        editor.set_selection(before);
        reset_core_operation_counters();
        auto transaction = editor.execute_document_enter(editor.selection());
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

    Editor list("- one\n- ");
    const BlockNode* empty_item = nullptr;
    walk_blocks(list.document().root, [&](const BlockNode& block) {
        if (!empty_item && block.kind == BlockKind::Paragraph && block.inline_content.source.empty()) {
            empty_item = &block;
        }
    });
    expect(fatal(bool(empty_item != nullptr)));
    if (empty_item) {
        exercise(list, TextSelection::caret({empty_item->id, 0, TextAffinity::Downstream}), "list");
    }

    for (auto markdown : {std::string{"- \n- two"}, std::string{"- one\n- \n- three"}, std::string{"> - one\n> - "}}) {
        Editor split_list(markdown);
        const BlockNode* empty = nullptr;
        walk_blocks(split_list.document().root, [&](const BlockNode& block) {
            if (!empty && block.kind == BlockKind::Paragraph && block.inline_content.source.empty()) {
                empty = &block;
            }
        });
        expect(fatal(bool(empty != nullptr))) << markdown;
        if (empty) {
            exercise(
                split_list,
                TextSelection::caret({empty->id, 0, TextAffinity::Downstream}),
                markdown);
        }
    }

    Editor quote("> one\n>\n> two");
    auto* initial_quote = find_block(quote.document().root, quote.document().root.children.front().id);
    expect(fatal(bool(initial_quote != nullptr)));
    expect(fatal(bool(initial_quote && initial_quote->children.size() == 2u)));
    if (initial_quote && initial_quote->children.size() == 2u) {
        const auto first_id = initial_quote->children.front().id;
        const auto first_length = initial_quote->children.front().inline_content.source.size();
        quote.set_selection(TextSelection::caret({first_id, first_length, TextAffinity::Upstream}));
        expect(fatal(bool(quote.execute_document_enter(quote.selection()).has_value())));
    }
    const BlockNode* empty_quote_line = nullptr;
    walk_blocks(quote.document().root, [&](const BlockNode& block) {
        if (!empty_quote_line && block.kind == BlockKind::Paragraph && block.inline_content.source.empty()) {
            empty_quote_line = &block;
        }
    });
    expect(fatal(bool(empty_quote_line != nullptr)));
    if (empty_quote_line) {
        exercise(quote, TextSelection::caret({empty_quote_line->id, 0, TextAffinity::Downstream}), "quote");
        expect(fatal(bool(quote.document().root.children.size() == 3u)));
        if (quote.document().root.children.size() == 3u) {
            const auto& trailing = quote.document().root.children.back();
            expect(fatal(bool(trailing.kind == BlockKind::BlockQuote)));
        }
    }

    Editor nested_quote("- item\n  > ");
    const BlockNode* nested_empty = nullptr;
    walk_blocks(nested_quote.document().root, [&](const BlockNode& block) {
        if (!nested_empty && block.kind == BlockKind::Paragraph && block.inline_content.source.empty()) {
            nested_empty = &block;
        }
    });
    expect(fatal(bool(nested_empty != nullptr)));
    if (nested_empty) {
        exercise(
            nested_quote,
            TextSelection::caret({nested_empty->id, 0, TextAffinity::Downstream}),
            "nested quote");
    }

    Editor code("    one\n\n    two");
    const BlockNode* code_block = nullptr;
    walk_blocks(code.document().root, [&](const BlockNode& block) {
        if (!code_block && block.kind == BlockKind::CodeBlock && block.code_indented) code_block = &block;
    });
    expect(fatal(bool(code_block != nullptr)));
    if (code_block) {
        const auto separator = code_block->code_text.find(U"\n\n");
        expect(fatal(bool(separator != std::u32string::npos)));
        if (separator != std::u32string::npos) {
            exercise(
                code,
                TextSelection::caret({code_block->id, separator + 1, TextAffinity::Downstream}),
                "indented code");
        }
    }
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
            [](const BlockNode& block) { return block.kind == BlockKind::Paragraph; });
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

"typed_fences_use_source_payload_and_tree_operations"_test = [] {
    Editor editor;
    expect(fatal(bool(editor.execute_document_insert_text(
        editor.selection(), U"```cpp").has_value())));
    const auto opening_before_markdown = editor.markdown_utf8();
    const auto opening_before = editor.selection();
    reset_core_operation_counters();
    auto opened = editor.execute_document_enter(editor.selection());
    expect(fatal(bool(opened.has_value())));
    if (!opened) return;
    auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_parses == 0u)));
    expect(fatal(bool(counters.full_document_serializations == 0u)));
    expect(fatal(bool(counters.full_tree_transaction_diffs == 0u)));
    expect(fatal(bool(editor.document().root.children.front().kind == BlockKind::CodeBlock)));
    expect(fatal(bool(editor.document().root.children.front().opening_marker == U"```cpp\n")));
    const auto opened_markdown = editor.markdown_utf8();
    const auto opened_selection = editor.selection();

    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.markdown_utf8() == opening_before_markdown)));
    expect(fatal(bool(editor.selection() == opening_before)));
    expect(fatal(bool(editor.redo())));
    expect(fatal(bool(editor.markdown_utf8() == opened_markdown)));
    expect(fatal(bool(editor.selection() == opened_selection)));

    reset_core_operation_counters();
    auto closed = editor.execute_document_insert_text(editor.selection(), U"```");
    expect(fatal(bool(closed.has_value())));
    if (!closed) return;
    counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_parses == 0u)));
    expect(fatal(bool(counters.full_document_serializations == 0u)));
    expect(fatal(bool(counters.full_tree_transaction_diffs == 0u)));
    expect(fatal(bool(editor.document().root.children.size() == 2u)));
    expect(fatal(bool(editor.document().root.children.front().closing_marker == U"```")));
    expect(fatal(bool(editor.document().root.children.back().kind == BlockKind::Paragraph)));
    const auto closed_markdown = editor.markdown_utf8();
    const auto closed_selection = editor.selection();
    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.markdown_utf8() == opened_markdown)));
    expect(fatal(bool(editor.selection() == opened_selection)));
    expect(fatal(bool(editor.redo())));
    expect(fatal(bool(editor.markdown_utf8() == closed_markdown)));
    expect(fatal(bool(editor.selection() == closed_selection)));
};

"code_and_math_blocks_use_local_source_edit_history"_test = [] {
    const std::vector<std::string> cases{
        "```cpp\nab\n```",
        "$$\nab\n$$",
    };
    for (const auto& markdown : cases) {
        Editor editor(markdown);
        const auto owner_id = editor.document().root.children.front().id;
        const auto original_source = *document_editable_text(editor.document(), owner_id);
        const auto before_markdown = editor.markdown_utf8();
        const auto before_selection = TextSelection::caret(
            {owner_id, 1, TextAffinity::Downstream});
        editor.set_selection(before_selection);

        reset_core_operation_counters();
        auto inserted = editor.execute_document_insert_text(editor.selection(), U"X");
        expect(fatal(bool(inserted.has_value()))) << markdown;
        if (!inserted) continue;
        auto counters = read_core_operation_counters();
        expect(fatal(bool(counters.full_document_parses == 0u))) << markdown;
        expect(fatal(bool(counters.full_document_serializations == 0u))) << markdown;
        expect(fatal(bool(counters.full_tree_transaction_diffs == 0u))) << markdown;
        expect(fatal(bool(counters.inline_reparses == 0u))) << markdown;
        auto expected = original_source;
        expected.insert(1, U"X");
        const auto inserted_expected = expected;
        expect(fatal(bool(document_editable_text(editor.document(), owner_id) == expected))) << markdown;
        const auto inserted_selection = editor.selection();

        expect(fatal(bool(editor.undo()))) << markdown;
        expect(fatal(bool(editor.markdown_utf8() == before_markdown))) << markdown;
        expect(fatal(bool(editor.selection() == before_selection))) << markdown;
        expect(fatal(bool(editor.redo()))) << markdown;
        expect(fatal(bool(document_editable_text(editor.document(), owner_id) == expected))) << markdown;
        expect(fatal(bool(editor.selection() == inserted_selection))) << markdown;

        reset_core_operation_counters();
        auto entered = editor.execute_document_enter(editor.selection());
        expect(fatal(bool(entered.has_value()))) << markdown;
        if (!entered) continue;
        counters = read_core_operation_counters();
        expect(fatal(bool(counters.full_document_parses == 0u))) << markdown;
        expect(fatal(bool(counters.full_document_serializations == 0u))) << markdown;
        expect(fatal(bool(counters.full_tree_transaction_diffs == 0u))) << markdown;
        expect(fatal(bool(counters.inline_reparses == 0u))) << markdown;
        expected.insert(2, U"\n");
        expect(fatal(bool(document_editable_text(editor.document(), owner_id) == expected))) << markdown;
        expect(fatal(bool(editor.undo()))) << markdown;
        expect(fatal(bool(document_editable_text(editor.document(), owner_id)
            == inserted_expected))) << markdown;

        reset_core_operation_counters();
        auto deleted = editor.execute_document_delete_backward(editor.selection());
        expect(fatal(bool(deleted.has_value()))) << markdown;
        if (!deleted) continue;
        counters = read_core_operation_counters();
        expect(fatal(bool(counters.full_document_parses == 0u))) << markdown;
        expect(fatal(bool(counters.full_document_serializations == 0u))) << markdown;
        expect(fatal(bool(counters.full_tree_transaction_diffs == 0u))) << markdown;
        expect(fatal(bool(counters.inline_reparses == 0u))) << markdown;
        expect(fatal(bool(document_editable_text(editor.document(), owner_id)
            == original_source))) << markdown;
        expect(fatal(bool(editor.undo()))) << markdown;
        expect(fatal(bool(document_editable_text(editor.document(), owner_id)
            == inserted_expected))) << markdown;
    }
};

"callout_titles_are_inline_source_owners_with_local_history"_test = [] {
    Editor editor("> [!NOTE] _title_\n> body");
    const auto callout_id = editor.document().root.children.front().id;
    const auto* callout = find_block(editor.document().root, callout_id);
    expect(fatal(bool(callout != nullptr)));
    expect(fatal(bool(callout && callout->kind == BlockKind::Callout)));
    expect(fatal(bool(callout && callout->callout_title.has_value())));
    if (!callout || !callout->callout_title) return;
    expect(fatal(bool(callout->callout_title->source == U"_title_")));

    const auto before_selection = TextSelection::caret(
        {callout_id, 1, TextAffinity::Downstream});
    editor.set_selection(before_selection);
    reset_core_operation_counters();
    expect(fatal(bool(editor.execute_document_insert_text(editor.selection(), U"X").has_value())));
    const auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_parses == 0u)));
    expect(fatal(bool(counters.full_document_serializations == 0u)));
    expect(fatal(bool(counters.full_tree_transaction_diffs == 0u)));
    expect(fatal(bool(counters.inline_reparses == 1u)));

    callout = find_block(editor.document().root, callout_id);
    expect(fatal(bool(callout && callout->callout_title.has_value())));
    if (!callout || !callout->callout_title) return;
    expect(fatal(bool(callout->callout_title->source == U"_Xtitle_")));
    expect(fatal(bool(flatten_tokens(callout->callout_title->tree, callout->callout_title->source)
        == callout->callout_title->source)));
    const auto after_selection = editor.selection();
    expect(fatal(bool(after_selection.active == TextPosition{
        callout_id, 2, TextAffinity::Downstream})));
    expect(fatal(bool(editor.markdown_utf8() == "> [!NOTE] _Xtitle_\n> body")));

    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.selection() == before_selection)));
    expect(fatal(bool(editor.markdown_utf8() == "> [!NOTE] _title_\n> body")));
    expect(fatal(bool(editor.redo())));
    expect(fatal(bool(editor.selection() == after_selection)));
    expect(fatal(bool(editor.markdown_utf8() == "> [!NOTE] _Xtitle_\n> body")));
};

"callout_title_tree_splits_restore_exactly_through_history"_test = [] {
    Editor editor("> [!NOTE] title\n> body");
    const auto callout_id = editor.document().root.children.front().id;
    const auto before_selection = TextSelection::caret(
        {callout_id, 2, TextAffinity::Downstream});
    editor.set_selection(before_selection);
    reset_core_operation_counters();
    expect(fatal(bool(editor.execute_document_enter(editor.selection()).has_value())));
    const auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_parses == 0u)));
    expect(fatal(bool(counters.full_document_serializations == 0u)));
    expect(fatal(bool(counters.full_tree_transaction_diffs == 0u)));
    const auto after_selection = editor.selection();
    expect(fatal(bool(after_selection.active.container_id != callout_id)));
    expect(fatal(bool(editor.markdown_utf8() == "> [!NOTE] ti\n> tle\n>\n> body")));

    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.markdown_utf8() == "> [!NOTE] title\n> body")));
    expect(fatal(bool(editor.selection() == before_selection)));
    expect(fatal(bool(editor.redo())));
    expect(fatal(bool(editor.markdown_utf8() == "> [!NOTE] ti\n> tle\n>\n> body")));
    expect(fatal(bool(editor.selection() == after_selection)));
};

"inline_delete_and_format_record_their_text_edits_directly"_test = [] {
    Editor editor("alpha");
    editor.set_selection(caret(first_text(editor), 2));
    reset_core_operation_counters();
    expect(fatal(bool(editor.execute_document_delete_backward(editor.selection()).has_value())));
    const auto deletion = read_core_operation_counters();
    expect(fatal(bool(deletion.full_document_parses == 0u)));
    expect(fatal(bool(deletion.full_document_serializations == 0u)));
    expect(fatal(bool(deletion.full_tree_transaction_diffs == 0u)));
    expect(fatal(bool(deletion.inline_reparses == 1u)));

    editor.set_selection(range(first_text(editor), 0, 2));
    reset_core_operation_counters();
    expect(fatal(bool(editor.execute_document_toggle_inline_format(
        editor.selection(), InlineFormat::Strong).has_value())));
    const auto formatting = read_core_operation_counters();
    expect(fatal(bool(formatting.full_document_parses == 0u)));
    expect(fatal(bool(formatting.full_document_serializations == 0u)));
    expect(fatal(bool(formatting.full_tree_transaction_diffs == 0u)));
    expect(fatal(bool(formatting.inline_reparses == 1u)));
};

"cross_block_selected_text_uses_tree_order_without_a_global_selection_offset"_test = [] {
    Editor editor("one\n\ntwo");
    const auto first = editor.document().root.children.front().id;
    const auto second = editor.document().root.children.back().id;
    editor.set_selection({
        {first, 2, TextAffinity::Downstream},
        {second, 1, TextAffinity::Upstream}});
    expect(fatal(bool(editor.selected_text_cps() == U"e\nt")));
    editor.set_selection({
        {second, 1, TextAffinity::Upstream},
        {first, 2, TextAffinity::Downstream}});
    expect(fatal(bool(editor.selected_text_cps() == U"e\nt")));

    Editor marked("**one**");
    const auto marked_id = first_text(marked).id;
    marked.set_selection({
        {marked_id, 1, TextAffinity::Downstream},
        {marked_id, 6, TextAffinity::Upstream}});
    expect(fatal(bool(marked.selected_text_cps() == U"*one*")));
};

"format_undo_redo_preserve_original_marker_spelling"_test = [] {
    Editor editor("value");
    editor.set_selection(range(first_text(editor), 0, 5));
    expect(fatal(bool(editor.execute_document_toggle_inline_format(editor.selection(), InlineFormat::Strong).has_value())));
    expect(fatal(bool(editor.markdown_utf8() == "**value**")));
    expect(fatal(bool(inline_contains_kind(first_text(editor).inline_content, InlineCstKind::Strong))));
    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.markdown_utf8() == "value")));
    expect(fatal(bool(editor.redo())));
    expect(fatal(bool(editor.markdown_utf8() == "**value**")));
};

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

"table_navigation_changes_selection_without_creating_history"_test = [] {
    Editor editor("| A | B |\n| --- | --- |\n| 1 | 2 |");
    const auto& table = editor.document().root.children.front();
    const auto first_id = table.children.front().children.front().id;
    const auto second_id = table.children.front().children[1].id;
    editor.set_selection(TextSelection::caret({first_id, 0, TextAffinity::Downstream}));
    const auto had_undo = editor.has_undo();
    expect(fatal(bool(editor.execute_document_table_edit(editor.selection(), DocumentTableEdit::MoveCellNext).has_value())));
    expect(fatal(bool(editor.selection().active.container_id == second_id)));
    expect(fatal(bool(editor.has_undo() == had_undo)));
};

"derived_symbols_and_outline_refresh_after_structure_edits"_test = [] {
    Editor editor("title");
    editor.set_selection(caret(first_text(editor), 0));
    expect(fatal(bool(editor.execute_document_set_heading(editor.selection(), 2).has_value())));
    expect(fatal(bool(editor.document().root.children.front().kind == BlockKind::Heading)));
    expect(fatal(bool(editor.symbols().headings.size() == 1u)));
    expect(fatal(bool(editor.outline().items.size() == 1u)));
    expect(fatal(bool(editor.outline().items.front().title_plain_text == "title")));
};

"set_dialect_reloads_explicitly_and_keeps_one_selection"_test = [] {
    MarkdownDialect dialect = default_dialect();
    dialect.math.inline_dollar = false;
    Editor editor("$x$", dialect);
    expect(fatal(bool(!inline_contains_kind(first_text(editor).inline_content, InlineCstKind::InlineMath))));
    editor.set_selection(caret(first_text(editor), 1));
    expect(fatal(bool(editor.execute_document_insert_text(editor.selection(), U"y").has_value())));
    expect(fatal(bool(editor.has_undo())));
    const auto before_reload_revision = editor.revision();
    dialect.math.inline_dollar = true;
    editor.set_dialect(dialect);
    expect(fatal(bool(inline_contains_kind(first_text(editor).inline_content, InlineCstKind::InlineMath))));
    expect(fatal(bool(editor.selection().active.container_id == first_text(editor).id)));
    expect(fatal(bool(editor.revision() == before_reload_revision + 1)));
    expect(fatal(bool(!editor.has_undo())));
    expect(fatal(bool(!editor.has_redo())));
};

"input_events_translate_to_commands_without_mutating_state"_test = [] {
    Editor editor("abc");
    EditorInputEvent text;
    text.kind = EditorInputEvent::Kind::TextInput;
    text.text_input.kind = TextInputEvent::Kind::InsertText;
    text.text_input.text = U"X";
    auto command = editor.handle_input(text);
    expect(fatal(bool(command.has_value())));
    expect(fatal(bool(command->kind == CommandKind::InsertText)));
    expect(fatal(bool(command->text == U"X")));
    expect(fatal(bool(editor.markdown_utf8() == "abc")));

    EditorInputEvent key;
    key.kind = EditorInputEvent::Kind::KeyDown;
    key.key.key_code = KeyCode::Backspace;
    command = editor.handle_input(key);
    expect(fatal(bool(command && command->kind == CommandKind::DeleteBackward)));
};

"selection_validation_rejects_foreign_or_out_of_range_positions"_test = [] {
    Editor editor("abc");
    bool foreign = false;
    try { editor.set_selection(TextSelection::caret({NodeId(999999), 0, TextAffinity::Downstream})); }
    catch (const std::out_of_range&) { foreign = true; }
    expect(fatal(bool(foreign)));
    bool outside = false;
    try { editor.set_selection(caret(first_text(editor), 4)); }
    catch (const std::out_of_range&) { outside = true; }
    expect(fatal(bool(outside)));
};

"theme_and_scale_are_orthogonal_to_document_state"_test = [] {
    Editor editor("abc");
    const auto source = editor.markdown_utf8();
    editor.set_theme(Theme::Light);
    editor.set_scale_factor(1.5f);
    expect(fatal(bool(editor.theme() == Theme::Light)));
    expect(fatal(bool(editor.scale_factor() == 1.5f)));
    expect(fatal(bool(editor.markdown_utf8() == source)));
};

"all_editable_block_contexts_share_the_source_edit_history_pipeline"_test = [] {
    const std::vector<std::string> cases{
        "abc",
        "# abc",
        "- abc",
        "- [ ] abc",
        "> abc",
        "| abc |\n| --- |",
        "> [!NOTE]\n> abc",
        "[^1]: abc",
        "- > abc",
        "> - abc",
        "- [ ] > abc",
    };
    for (auto const& markdown : cases) {
        Editor editor(markdown);
        auto const id = first_text(editor).id;
        editor.set_selection(TextSelection::caret({id, 1, TextAffinity::Downstream}));
        const auto before = editor.markdown_utf8();
        reset_core_operation_counters();
        expect(fatal(bool(editor.execute_document_insert_text(editor.selection(), U"X").has_value()))) << markdown;
        const auto counters = read_core_operation_counters();
        expect(fatal(bool(counters.full_document_parses == 0u)));
        expect(fatal(bool(counters.full_document_serializations == 0u)));
        expect(fatal(bool(counters.full_tree_transaction_diffs == 0u)));
        expect(fatal(bool(counters.inline_reparses == 1u)));
        auto edited = *editor.editable_source(id);
        expect(fatal(bool(edited == U"aXbc"))) << markdown << " source=" << cps_to_utf8(edited);
        auto const* owner = find_block(editor.document().root, id);
        expect(fatal(bool(owner != nullptr)));
        if (owner) expect(fatal(bool(flatten_tokens(owner->inline_content.tree, owner->inline_content.source) == owner->inline_content.source)));
        expect(fatal(bool(editor.selection().active.container_id == id)));
        expect(fatal(bool(editor.selection().active.source_offset == 2u)));

        const auto saved = editor.markdown_utf8();
        Editor reloaded(saved);
        expect(fatal(bool(first_text(reloaded).inline_content.source == U"aXbc")));

        expect(fatal(bool(editor.undo())));
        expect(fatal(bool(editor.markdown_utf8() == before)));
        expect(fatal(bool(*editor.editable_source(id) == U"abc")));
        expect(fatal(bool(editor.redo())));
        expect(fatal(bool(*editor.editable_source(id) == U"aXbc")));
    }
};

"representative_inline_states_round_trip_through_every_editable_context"_test = [] {
    const std::vector<std::u32string> samples{
        U"abc", U"*abc*", U"_abc_", U"**abc**", U"__abc__", U"**", U"**abc",
        U"a***b***c", U"~~abc~~", U"~~abc", U"`abc`", U"`abc", U"[title](url)",
        U"[title](<url>)", U"[title](url \"name\")", U"[title](", U"![alt](url)",
        U"$abc$", U"$abc", U"\\*abc\\*", U"&amp;", U"a\\**b*",
    };

    for (const auto& sample : samples) {
        const auto original_source = U"p" + sample + U"q";
        const auto source_utf8 = cps_to_utf8(original_source);
        const std::vector<std::string> documents{
            source_utf8,
            "# " + source_utf8,
            "- " + source_utf8,
            "- [ ] " + source_utf8,
            "> " + source_utf8,
            "| " + source_utf8 + " |\n| --- |",
            "> [!NOTE]\n> " + source_utf8,
            "[^1]: " + source_utf8,
            "- > " + source_utf8,
        };

        for (const auto& markdown : documents) {
            Editor editor(markdown);
            const auto owner_id = first_text(editor).id;
            expect(fatal(bool(first_text(editor).inline_content.source == original_source)));
            const auto offset = 1 + sample.size() / 2;
            const auto before_source = editor.markdown_utf8();
            const auto before_selection = TextSelection::caret(
                {owner_id, offset, TextAffinity::Downstream});
            editor.set_selection(before_selection);

            reset_core_operation_counters();
            expect(fatal(bool(editor.execute_document_insert_text(editor.selection(), U"X").has_value())));
            const auto counters = read_core_operation_counters();
            expect(fatal(bool(counters.full_document_parses == 0u)));
            expect(fatal(bool(counters.full_document_serializations == 0u)));
            expect(fatal(bool(counters.full_tree_transaction_diffs == 0u)));
            expect(fatal(bool(counters.inline_reparses == 1u)));

            auto expected_source = original_source;
            expected_source.insert(offset, 1, U'X');
            const auto* owner = find_block(editor.document().root, owner_id);
            expect(fatal(bool(owner != nullptr)));
            if (!owner) continue;
            expect(fatal(bool(owner->inline_content.source == expected_source)));
            expect(fatal(bool(flatten_tokens(owner->inline_content.tree, owner->inline_content.source)
                == owner->inline_content.source)));
            expect(fatal(bool(editor.selection().active.container_id == owner_id)));
            expect(fatal(bool(editor.selection().active.source_offset == offset + 1)));

            const auto edited_source = editor.markdown_utf8();
            Editor reloaded(edited_source);
            expect(fatal(bool(first_text(reloaded).inline_content.source == expected_source)));

            expect(fatal(bool(editor.undo())));
            expect(fatal(bool(editor.markdown_utf8() == before_source)));
            expect(fatal(bool(editor.selection() == before_selection)));
            expect(fatal(bool(editor.redo())));
            expect(fatal(bool(editor.markdown_utf8() == edited_source)));
            expect(fatal(bool(*editor.editable_source(owner_id) == expected_source)));
        }
    }
};

}; // suite editor_tests
