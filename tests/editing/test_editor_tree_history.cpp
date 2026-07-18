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

suite editor_tree_history_tests = [] {

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

    Editor quote("> one\n>\n>\n> two");
    auto* initial_quote = find_block(quote.document().root, quote.document().root.children.front().id);
    expect(fatal(bool(initial_quote != nullptr)));
    expect(fatal(bool(initial_quote && initial_quote->children.size() == 3u)));
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

    Editor footnote("body[^1]\n\n[^1]: first");
    const BlockNode* definition = nullptr;
    walk_blocks(footnote.document().root, [&](const BlockNode& block) {
        if (!definition && block.kind == BlockKind::FootnoteDefinition) definition = &block;
    });
    expect(fatal(bool(definition != nullptr)));
    if (definition && definition->children.size() == 1u) {
        auto const definition_id = definition->id;
        auto const content_id = definition->children.front().id;
        footnote.set_selection(TextSelection::caret({content_id, 5, TextAffinity::Downstream}));
        expect(fatal(bool(footnote.execute_document_enter(footnote.selection()).has_value())));

        auto const* opened_definition = find_block(footnote.document().root, definition_id);
        expect(fatal(bool(opened_definition != nullptr)));
        expect(fatal(bool(opened_definition && opened_definition->children.size() == 2u)));
        if (opened_definition && opened_definition->children.size() == 2u) {
            auto const empty_id = opened_definition->children.back().id;
            exercise(
                footnote,
                TextSelection::caret({empty_id, 0, TextAffinity::Downstream}),
                "footnote");
        }
    }

    Editor code("    one\n\n    two");
    const BlockNode* code_block = nullptr;
    walk_blocks(code.document().root, [&](const BlockNode& block) {
        if (!code_block && block.kind == BlockKind::CodeBlock && block.atomic_special().code_indented) {
            code_block = &block;
        }
    });
    expect(fatal(bool(code_block != nullptr)));
    if (code_block) {
        const auto separator = code_block->block_source.source().find(U"\n\n");
        expect(fatal(bool(separator != std::u32string::npos)));
        if (separator != std::u32string::npos) {
            exercise(
                code,
                TextSelection::caret({code_block->id, separator + 1, TextAffinity::Downstream}),
                "indented code");
        }
    }
};

"block_boundary_deletes_record_local_source_and_tree_operations"_test = [] {
    auto exercise = [](Editor& editor, TextSelection before, bool backward, std::string_view label) {
        const auto before_markdown = editor.markdown_utf8();
        editor.set_selection(before);
        reset_core_operation_counters();
        auto transaction = backward
            ? editor.execute_document_delete_backward(editor.selection())
            : editor.execute_document_delete_forward(editor.selection());
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

    Editor backward("alpha\n\nbeta");
    const auto second_id = backward.document().root.children[1].id;
    exercise(
        backward,
        TextSelection::caret({second_id, 0, TextAffinity::Downstream}),
        true,
        "backward sibling join");

    Editor forward("alpha\n\nbeta");
    const auto first_id = forward.document().root.children.front().id;
    exercise(
        forward,
        TextSelection::caret({first_id, 5, TextAffinity::Upstream}),
        false,
        "forward sibling join");

    Editor callout_backward("> [!NOTE] title\n> body");
    const auto& callout = callout_backward.document().root.children.front();
    const auto body_id = callout.children[1].id;
    exercise(
        callout_backward,
        TextSelection::caret({body_id, 0, TextAffinity::Downstream}),
        true,
        "parent inline join");

    Editor callout_forward("> [!NOTE] title\n> body");
    const auto title_id = callout_forward.document().root.children.front().children.front().id;
    exercise(
        callout_forward,
        TextSelection::caret({title_id, 5, TextAffinity::Upstream}),
        false,
        "first child join");

    Editor blank_after_block("> quoted\n\n\nnext");
    const auto blank_after_id = blank_after_block.document().root.children[1].id;
    exercise(
        blank_after_block,
        TextSelection::caret({blank_after_id, 0, TextAffinity::Downstream}),
        true,
        "blank after structural block");

    Editor blank_before_block("first\n\n\n> quoted");
    const auto blank_before_id = blank_before_block.document().root.children[1].id;
    exercise(
        blank_before_block,
        TextSelection::caret({blank_before_id, 0, TextAffinity::Downstream}),
        false,
        "blank before structural block");

    Editor atomic("---\n\nafter");
    const auto atomic_id = atomic.document().root.children.front().id;
    exercise(
        atomic,
        TextSelection::caret({atomic_id, 0, TextAffinity::Downstream}),
        false,
        "atomic removal");
};

}; // suite editor_tree_history_tests
