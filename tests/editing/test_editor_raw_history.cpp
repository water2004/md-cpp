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

suite editor_raw_history_tests = [] {

"raw_marker_reclassification_preserves_arbitrary_ancestor_structure"_test = [] {
    Editor editor("> - ```cpp\n>   value\n>   ```");
    const BlockNode* code = nullptr;
    walk_blocks(editor.document().root, [&](const BlockNode& block) {
        if (!code && block.kind == BlockKind::CodeBlock) code = &block;
    });
    expect(fatal(bool(code != nullptr)));
    if (!code) return;
    const auto code_id = code->id;
    const auto opening_end = code->block_source.source().find(U'\n');
    expect(fatal(bool(opening_end != std::u32string::npos)));
    if (opening_end == std::u32string::npos) return;
    editor.set_selection({
        {code_id, 0, TextAffinity::Downstream},
        {code_id, opening_end, TextAffinity::Downstream},
    });

    reset_core_operation_counters();
    expect(fatal(bool(editor.execute_document_delete_selection(
        editor.selection()).has_value())));
    const auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_parses == 0u)));
    expect(fatal(bool(counters.full_document_serializations == 0u)));
    expect(fatal(bool(editor.document().root.children.front().kind == BlockKind::BlockQuote)));
    expect(fatal(bool(editor.document().root.children.front().children.front().kind == BlockKind::List)));
    const auto* paragraph = find_block(editor.document().root, code_id);
    expect(fatal(bool(paragraph && paragraph->kind == BlockKind::Paragraph)));
    expect(fatal(bool(editor.selection().active.container_id == code_id)));
    expect(fatal(bool(validate_document(editor.document()).empty())));

    expect(fatal(bool(editor.undo())));
    const auto* restored = find_block(editor.document().root, code_id);
    expect(fatal(bool(restored && restored->kind == BlockKind::CodeBlock)));
    expect(fatal(bool(editor.redo())));
    paragraph = find_block(editor.document().root, code_id);
    expect(fatal(bool(paragraph && paragraph->kind == BlockKind::Paragraph)));
};

"undo_restores_adjacent_math_blocks_after_cross_block_marker_deletion"_test = [] {
    Editor editor("$$\na\n$$\n$$\nb\n$$");
    expect(fatal(bool(editor.document().root.children.size() == 2u)));
    if (editor.document().root.children.size() != 2u) return;
    auto const& first = editor.document().root.children[0];
    auto const& second = editor.document().root.children[1];
    expect(fatal(bool(first.kind == BlockKind::MathBlock)));
    expect(fatal(bool(second.kind == BlockKind::MathBlock)));
    auto first_id = first.id;
    auto second_id = second.id;
    auto closing = first.block_source.source().rfind(U"$$");
    expect(fatal(bool(closing != std::u32string::npos)));
    if (closing == std::u32string::npos) return;
    auto before = editor.markdown_utf8();
    auto before_selection = TextSelection{
        {first_id, closing, TextAffinity::Downstream},
        {second_id, 2, TextAffinity::Downstream},
    };
    editor.set_selection(before_selection);
    expect(fatal(bool(editor.execute_document_delete_selection(editor.selection()).has_value())));
    expect(fatal(bool(editor.document().root.children.size() == 1u)));
    expect(fatal(bool(validate_document(editor.document()).empty())));
    auto merged = build_render_model(
        editor.document(), editor.outline(), editor.symbols(), default_theme_profile());
    expect(fatal(bool(merged.blocks.size() == 1u)));

    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.markdown_utf8() == before)));
    expect(fatal(bool(editor.selection() == before_selection)));
    expect(fatal(bool(editor.document().root.children.size() == 2u)));
    expect(fatal(bool(find_block(editor.document().root, first_id) != nullptr)));
    expect(fatal(bool(find_block(editor.document().root, second_id) != nullptr)));
    expect(fatal(bool(validate_document(editor.document()).empty())));
    auto restored = build_render_model(
        editor.document(), editor.outline(), editor.symbols(), default_theme_profile());
    expect(fatal(bool(restored.blocks.size() == 2u)));

    expect(fatal(bool(editor.redo())));
    expect(fatal(bool(editor.document().root.children.size() == 1u)));
    expect(fatal(bool(validate_document(editor.document()).empty())));
    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.markdown_utf8() == before)));
};

"raw_block_boundary_history_survives_weird_but_plausible_edits"_test = [] {
    struct Case {
        std::string markdown;
        bool reverse_selection = false;
    };
    const std::vector<Case> cases{
        {"$$\na\n$$\n$$\nb\n$$"},
        {"$$\n😀 + x\n$$\n$$\ny\n$$", true},
        {"$$\n$$\n$$\n$$"},
        {"```cpp\na\n```\n```js\nb\n```"},
        {"````cpp\na\n````\n```js\nb\n```", true},
        {"$$\na\n$$\n```cpp\nb\n```"},
        {"```cpp\na\n```\n$$\nb\n$$", true},
        {"before\n\n$$\na\n$$\n$$\nb\n$$\n\nafter"},
        {"> $$\n> a\n> $$\n>\n> $$\n> b\n> $$", true},
        {"- ```cpp\n  a\n  ```\n\n  ```js\n  b\n  ```"},
    };

    for (const auto& test_case : cases) {
        Editor editor(test_case.markdown);
        std::vector<const BlockNode*> raw_blocks;
        walk_blocks(editor.document().root, [&](const BlockNode& block) {
            if (block.kind == BlockKind::CodeBlock || block.kind == BlockKind::MathBlock) {
                raw_blocks.push_back(&block);
            }
        });
        expect(bool(raw_blocks.size() >= 2u)) << test_case.markdown;
        if (raw_blocks.size() < 2u) continue;

        const auto closing = std::ranges::find(
            raw_blocks[0]->block_source.tree().tokens,
            BlockSourceTokenKind::ClosingMarker,
            &BlockSourceToken::kind);
        const auto opening = std::ranges::find(
            raw_blocks[1]->block_source.tree().tokens,
            BlockSourceTokenKind::OpeningMarker,
            &BlockSourceToken::kind);
        expect(bool(closing != raw_blocks[0]->block_source.tree().tokens.end()))
            << test_case.markdown;
        expect(bool(opening != raw_blocks[1]->block_source.tree().tokens.end()))
            << test_case.markdown;
        if (closing == raw_blocks[0]->block_source.tree().tokens.end()
            || opening == raw_blocks[1]->block_source.tree().tokens.end()) {
            continue;
        }

        TextSelection boundary{
            {raw_blocks[0]->id, closing->source_range.start, TextAffinity::Downstream},
            {raw_blocks[1]->id, opening->source_range.end, TextAffinity::Upstream},
        };
        if (test_case.reverse_selection) std::swap(boundary.anchor, boundary.active);
        const auto before_markdown = editor.markdown_utf8();
        editor.set_selection(boundary);

        reset_core_operation_counters();
        expect(fatal(bool(editor.execute_document_delete_selection(editor.selection()).has_value())))
            << test_case.markdown;
        auto counters = read_core_operation_counters();
        expect(fatal(bool(counters.full_document_parses == 0u))) << test_case.markdown;
        expect(fatal(bool(counters.full_document_serializations == 0u))) << test_case.markdown;
        expect(fatal(bool(counters.full_tree_transaction_diffs == 0u))) << test_case.markdown;
        expect(fatal(bool(validate_document(editor.document()).empty()))) << test_case.markdown;
        const auto after_markdown = editor.markdown_utf8();
        const auto after_selection = editor.selection();
        auto changed_model = build_render_model(
            editor.document(), editor.outline(), editor.symbols(), default_theme_profile());
        expect(fatal(bool(!changed_model.blocks.empty()))) << test_case.markdown;
        Editor reloaded(after_markdown);
        expect(fatal(bool(reloaded.markdown_utf8() == after_markdown))) << test_case.markdown;
        expect(fatal(bool(validate_document(reloaded.document()).empty()))) << test_case.markdown;

        for (int cycle = 0; cycle < 3; ++cycle) {
            expect(fatal(bool(editor.undo()))) << test_case.markdown;
            expect(fatal(bool(editor.markdown_utf8() == before_markdown))) << test_case.markdown;
            expect(fatal(bool(editor.selection() == boundary))) << test_case.markdown;
            expect(fatal(bool(validate_document(editor.document()).empty()))) << test_case.markdown;
            auto restored_model = build_render_model(
                editor.document(), editor.outline(), editor.symbols(), default_theme_profile());
            expect(fatal(bool(!restored_model.blocks.empty()))) << test_case.markdown;

            expect(fatal(bool(editor.redo()))) << test_case.markdown;
            expect(fatal(bool(editor.markdown_utf8() == after_markdown))) << test_case.markdown;
            expect(fatal(bool(editor.selection() == after_selection))) << test_case.markdown;
            expect(fatal(bool(validate_document(editor.document()).empty()))) << test_case.markdown;
        }
    }
};

"code_fence_closer_deletion_and_undo_preserve_history_and_incremental_rendering"_test = [] {
    auto exercise = [&](bool delete_as_selection, bool reverse_selection) {
        Editor editor("```cpp\n11111111\n```");
        expect(fatal(bool(editor.document().root.children.size() == 1u)));
        if (editor.document().root.children.size() != 1u) return;

        const auto code_id = editor.document().root.children.front().id;
        const auto* code = find_block(editor.document().root, code_id);
        expect(fatal(bool(code && code->kind == BlockKind::CodeBlock)));
        if (!code || code->kind != BlockKind::CodeBlock) return;
        const auto closing = std::ranges::find(
            code->block_source.tree().tokens,
            BlockSourceTokenKind::ClosingMarker,
            &BlockSourceToken::kind);
        expect(fatal(bool(closing != code->block_source.tree().tokens.end())));
        if (closing == code->block_source.tree().tokens.end()) return;

        auto original_selection = delete_as_selection
            ? TextSelection{
                {code_id, closing->source_range.start, TextAffinity::Downstream},
                {code_id, closing->source_range.end, TextAffinity::Upstream}}
            : TextSelection::caret(
                {code_id, closing->source_range.end, TextAffinity::Downstream});
        if (reverse_selection) std::swap(original_selection.anchor, original_selection.active);
        editor.set_selection(original_selection);
        auto render_model = build_render_model(
            editor.document(), editor.outline(), editor.symbols(), default_theme_profile());

        auto update_render_model = [&] {
            auto change = editor.take_last_document_change();
            expect(fatal(bool(change.has_value())));
            if (!change) return;
            RenderModelUpdate update;
            update.structural = change->structural;
            update.structural_locality_known = change->structural_locality_known;
            update.structural_anchors = change->structural_anchors;
            for (const auto& operation : change->text_operations) {
                update.changed_owners.push_back(
                    (change->forward ? operation.forward : operation.inverse).container_id);
            }
            render_model = build_render_model_incremental(
                editor.document(),
                editor.outline(),
                editor.symbols(),
                default_theme_profile(),
                std::move(render_model),
                update);
            expect(fatal(bool(render_model.blocks.size() == 1u)));
            expect(fatal(bool(render_model.changed_block_indices == std::vector<std::size_t>{0})));
        };
        auto verify = [&] {
            expect(fatal(bool(validate_document(editor.document()).empty())));
            expect(fatal(bool(document_indexes_are_exact(editor.document()))));
            const auto anchor_source = editor.editable_source(editor.selection().anchor.container_id);
            const auto active_source = editor.editable_source(editor.selection().active.container_id);
            expect(fatal(bool(anchor_source.has_value())));
            expect(fatal(bool(active_source.has_value())));
            if (anchor_source) {
                expect(fatal(bool(editor.selection().anchor.source_offset <= anchor_source->size())));
            }
            if (active_source) {
                expect(fatal(bool(editor.selection().active.source_offset <= active_source->size())));
            }
            Editor reloaded(editor.markdown_utf8());
            expect(fatal(bool(reloaded.markdown_utf8() == editor.markdown_utf8())));
            expect(fatal(bool(validate_document(reloaded.document()).empty())));
        };

        const auto original_markdown = editor.markdown_utf8();
        Command backspace{.kind = CommandKind::DeleteBackward};
        const int edit_count = delete_as_selection ? 1 : 3;
        for (int index = 0; index < edit_count; ++index) {
            expect(fatal(bool(editor.execute_command(backspace))));
            update_render_model();
            verify();
        }
        const auto edited_markdown = editor.markdown_utf8();
        expect(fatal(bool(edited_markdown == "```cpp\n11111111\n")));

        for (int index = 0; index < edit_count; ++index) {
            expect(fatal(bool(editor.undo())));
            update_render_model();
            verify();
        }
        expect(fatal(bool(editor.markdown_utf8() == original_markdown)));
        expect(fatal(bool(editor.selection() == original_selection)));

        for (int index = 0; index < edit_count; ++index) {
            expect(fatal(bool(editor.redo())));
            update_render_model();
            verify();
        }
        expect(fatal(bool(editor.markdown_utf8() == edited_markdown)));
    };

    exercise(true, false);
    exercise(true, true);
    exercise(false, false);
};

"incomplete_raw_marker_edits_restore_exact_source_and_selection"_test = [] {
    const std::vector<std::string> documents{
        "```cpp\nvalue",
        "$$\nx + y",
    };

    for (const auto& markdown : documents) {
        Editor editor(markdown);
        auto const& raw = editor.document().root.children.front();
        expect(fatal(bool(raw.kind == BlockKind::CodeBlock || raw.kind == BlockKind::MathBlock)))
            << markdown;
        if (raw.kind != BlockKind::CodeBlock && raw.kind != BlockKind::MathBlock) continue;
        const auto opening = std::ranges::find(
            raw.block_source.tree().tokens,
            BlockSourceTokenKind::OpeningMarker,
            &BlockSourceToken::kind);
        expect(fatal(bool(opening != raw.block_source.tree().tokens.end()))) << markdown;
        if (opening == raw.block_source.tree().tokens.end()) continue;

        auto selection = TextSelection{
            {raw.id, opening->source_range.end, TextAffinity::Upstream},
            {raw.id, opening->source_range.start, TextAffinity::Downstream},
        };
        const auto before = editor.markdown_utf8();
        editor.set_selection(selection);
        expect(fatal(bool(editor.execute_document_delete_selection(editor.selection()).has_value())))
            << markdown;
        expect(fatal(bool(validate_document(editor.document()).empty()))) << markdown;
        const auto after = editor.markdown_utf8();
        auto model = build_render_model(
            editor.document(), editor.outline(), editor.symbols(), default_theme_profile());
        expect(fatal(bool(!model.blocks.empty()))) << markdown;
        expect(fatal(bool(editor.undo()))) << markdown;
        expect(fatal(bool(editor.markdown_utf8() == before))) << markdown;
        expect(fatal(bool(editor.selection() == selection))) << markdown;
        expect(fatal(bool(editor.redo()))) << markdown;
        expect(fatal(bool(editor.markdown_utf8() == after))) << markdown;
        expect(fatal(bool(editor.undo()))) << markdown;
        expect(fatal(bool(editor.execute_document_insert_text(editor.selection(), U"x").has_value())))
            << markdown;
        expect(fatal(bool(validate_document(editor.document()).empty()))) << markdown;
    }
};

}; // suite editor_raw_history_tests
