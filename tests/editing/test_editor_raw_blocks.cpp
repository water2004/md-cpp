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

suite editor_raw_block_tests = [] {

"typed_fences_create_auto_closed_block_source_transactions"_test = [] {
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
    expect(fatal(bool(editor.document().root.children.front().block_source.source() == U"```cpp\n```")));
    expect(fatal(bool(editor.document().root.children.front().block_source.tree().language
        == std::optional<std::string>{"cpp"})));
    expect(fatal(bool(editor.selection().active.source_offset == 7u)));
    const auto opened_markdown = editor.markdown_utf8();
    const auto opened_selection = editor.selection();

    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.markdown_utf8() == opening_before_markdown)));
    expect(fatal(bool(editor.selection() == opening_before)));
    expect(fatal(bool(editor.redo())));
    expect(fatal(bool(editor.markdown_utf8() == opened_markdown)));
    expect(fatal(bool(editor.selection() == opened_selection)));

    reset_core_operation_counters();
    auto newline = editor.execute_document_enter(editor.selection());
    expect(fatal(bool(newline.has_value())));
    if (!newline) return;
    counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_parses == 0u)));
    expect(fatal(bool(counters.full_document_serializations == 0u)));
    expect(fatal(bool(counters.full_tree_transaction_diffs == 0u)));
    expect(fatal(bool(editor.document().root.children.size() == 1u)));
    expect(fatal(bool(editor.document().root.children.front().block_source.source() == U"```cpp\n\n```")));
    expect(fatal(bool(editor.selection().active.source_offset == 8u)));
    const auto newline_markdown = editor.markdown_utf8();
    const auto newline_selection = editor.selection();
    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.markdown_utf8() == opened_markdown)));
    expect(fatal(bool(editor.selection() == opened_selection)));
    expect(fatal(bool(editor.redo())));
    expect(fatal(bool(editor.markdown_utf8() == newline_markdown)));
    expect(fatal(bool(editor.selection() == newline_selection)));
};

"typed_tilde_fence_auto_closes_and_round_trips_history"_test = [] {
    Editor editor;
    expect(fatal(bool(editor.execute_document_insert_text(
        editor.selection(), U"~~~cpp").has_value())));
    const auto before_markdown = editor.markdown_utf8();
    const auto before_selection = editor.selection();

    reset_core_operation_counters();
    expect(fatal(bool(editor.execute_document_enter(editor.selection()).has_value())));
    auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_parses == 0u)));
    expect(fatal(bool(counters.full_document_serializations == 0u)));
    expect(fatal(bool(counters.full_tree_transaction_diffs == 0u)));
    expect(fatal(bool(editor.document().root.children.size() == 1u)));
    if (editor.document().root.children.size() != 1u) return;
    const auto& code = editor.document().root.children.front();
    expect(fatal(bool(code.kind == BlockKind::CodeBlock)));
    expect(fatal(bool(code.block_source.source() == U"~~~cpp\n~~~")));
    expect(fatal(bool(code.block_source.tree().language == std::optional<std::string>{"cpp"})));
    expect(fatal(bool(editor.selection().active.source_offset == 7u)));

    const auto after_markdown = editor.markdown_utf8();
    const auto after_selection = editor.selection();
    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.markdown_utf8() == before_markdown)));
    expect(fatal(bool(editor.selection() == before_selection)));
    expect(fatal(bool(editor.redo())));
    expect(fatal(bool(editor.markdown_utf8() == after_markdown)));
    expect(fatal(bool(editor.selection() == after_selection)));
};

"toolbar_blocks_share_complete_source_construction_with_typed_rules"_test = [] {
    Editor code;
    Command insert_code;
    insert_code.kind = CommandKind::InsertCodeBlock;
    insert_code.lang = U"cpp";
    auto code_transaction = code.execute_document_insert_atomic_block(code.selection(), insert_code);
    expect(fatal(bool(code_transaction.has_value())));
    if (code_transaction) {
        expect(fatal(bool(code.document().root.children.size() == 1u)));
        auto const* block = find_block(code.document().root, code.selection().active.container_id);
        expect(fatal(bool(block && block->kind == BlockKind::CodeBlock)));
        expect(fatal(bool(block && block->block_source.source() == U"```cpp\n```")));
        expect(fatal(bool(code.selection().active.source_offset == 7u)));
        expect(fatal(bool(code.execute_document_enter(code.selection()).has_value())));
        block = find_block(code.document().root, code.selection().active.container_id);
        expect(fatal(bool(block && block->block_source.source() == U"```cpp\n\n```")));
    }

    Editor math;
    Command insert_math;
    insert_math.kind = CommandKind::InsertMathBlock;
    auto math_transaction = math.execute_document_insert_atomic_block(math.selection(), insert_math);
    expect(fatal(bool(math_transaction.has_value())));
    if (math_transaction) {
        expect(fatal(bool(math.document().root.children.size() == 1u)));
        auto const* block = find_block(math.document().root, math.selection().active.container_id);
        expect(fatal(bool(block && block->kind == BlockKind::MathBlock)));
        expect(fatal(bool(block && block->block_source.source() == U"$$\n$$")));
        expect(fatal(bool(math.selection().active.source_offset == 3u)));
        expect(fatal(bool(math.execute_document_enter(math.selection()).has_value())));
        block = find_block(math.document().root, math.selection().active.container_id);
        expect(fatal(bool(block && block->block_source.source() == U"$$\n\n$$")));
        expect(fatal(bool(math.selection().active.source_offset == 4u)));
    }

    Editor heading("title");
    expect(fatal(bool(heading.execute_document_set_heading(heading.selection(), 1).has_value())));
    expect(fatal(bool(heading.document().root.children.front().text_special().opening_marker == U"# ")));
    expect(fatal(bool(heading.markdown_utf8() == "# title")));
};

"fenced_code_info_is_editable_source_with_exact_undo_redo"_test = [] {
    Editor editor;
    Command insert_code;
    insert_code.kind = CommandKind::InsertCodeBlock;
    insert_code.lang = U"cpp";
    expect(fatal(bool(editor.execute_document_insert_atomic_block(
        editor.selection(), insert_code).has_value())));
    const auto code_id = editor.selection().active.container_id;
    const TextSelection language{
        {code_id, 3, TextAffinity::Downstream},
        {code_id, 6, TextAffinity::Downstream}};
    editor.set_selection(language);
    expect(fatal(bool(editor.execute_document_insert_text(editor.selection(), U"js").has_value())));
    auto const* block = find_block(editor.document().root, code_id);
    expect(fatal(bool(block && block->block_source.source() == U"```js\n```")));
    expect(fatal(bool(block && block->block_source.tree().language
        == std::optional<std::string>{"js"})));
    const auto after = editor.selection();
    expect(fatal(bool(editor.undo())));
    block = find_block(editor.document().root, code_id);
    expect(fatal(bool(block && block->block_source.source() == U"```cpp\n```")));
    expect(fatal(bool(editor.selection() == language)));
    expect(fatal(bool(editor.redo())));
    block = find_block(editor.document().root, code_id);
    expect(fatal(bool(block && block->block_source.source() == U"```js\n```")));
    expect(fatal(bool(editor.selection() == after)));
};

"enter_in_incomplete_fenced_code_edits_local_source_without_crashing"_test = [] {
    Editor editor("```cpp\n");
    auto const& code = editor.document().root.children.front();
    expect(fatal(bool(code.kind == BlockKind::CodeBlock)));
    expect(fatal(bool(!code.block_source.tree().complete_closing)));
    editor.set_selection(caret(code, code.block_source.source().size()));
    const auto before = editor.selection();
    expect(fatal(bool(editor.execute_document_enter(editor.selection()).has_value())));
    auto const* changed = find_block(editor.document().root, code.id);
    expect(fatal(bool(changed && changed->block_source.source() == U"```cpp\n\n")));
    expect(fatal(bool(changed && block_source_tokens_partition(changed->block_source))));
    expect(fatal(bool(changed && flatten_block_source_tokens(changed->block_source)
        == changed->block_source.source())));
    expect(fatal(bool(editor.selection().active.source_offset == 8u)));
    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.selection() == before)));
    expect(fatal(bool(editor.redo())));
};

"typed_math_delimiter_creates_editable_auto_closed_block"_test = [] {
    Editor editor;
    expect(fatal(bool(editor.execute_document_insert_text(editor.selection(), U"$$").has_value())));
    expect(fatal(bool(editor.execute_document_enter(editor.selection()).has_value())));
    auto const* block = find_block(editor.document().root, editor.selection().active.container_id);
    expect(fatal(bool(block && block->kind == BlockKind::MathBlock)));
    expect(fatal(bool(block && block->block_source.source() == U"$$\n$$")));
    expect(fatal(bool(editor.selection().active.source_offset == 3u)));
    expect(fatal(bool(editor.execute_document_enter(editor.selection()).has_value())));
    block = find_block(editor.document().root, editor.selection().active.container_id);
    expect(fatal(bool(block && block->block_source.source() == U"$$\n\n$$")));
    expect(fatal(bool(editor.selection().active.source_offset == 4u)));
};

"enter_after_a_complete_raw_block_creates_a_sibling_paragraph"_test = [] {
    for (const auto& markdown : {
            std::string{"$$\nx + y\n$$"},
            std::string{"```cpp\nvalue\n```"},
        }) {
        Editor editor(markdown);
        const auto raw_id = editor.document().root.children.front().id;
        const auto raw_size = editor.document().root.children.front().block_source.source().size();
        const auto raw_source = editor.document().root.children.front().block_source.source();
        editor.set_selection(TextSelection::caret({
            raw_id,
            raw_size,
            TextAffinity::Downstream,
        }));
        expect(fatal(bool(editor.execute_document_enter(editor.selection()).has_value()))) << markdown;
        expect(fatal(bool(editor.document().root.children.size() == 2u))) << markdown;
        expect(fatal(bool(editor.document().root.children.front().id == raw_id))) << markdown;
        expect(fatal(bool(editor.document().root.children.front().block_source.source() == raw_source))) << markdown;
        expect(fatal(bool(editor.document().root.children.back().kind == BlockKind::Paragraph))) << markdown;
        expect(fatal(bool(editor.selection().active.container_id
            == editor.document().root.children.back().id))) << markdown;
        expect(fatal(bool(editor.selection().active.source_offset == 0u))) << markdown;
    }
};

"enter_after_nested_complete_raw_block_stays_in_its_direct_container"_test = [] {
    Editor editor("> - ```cpp\n>   value\n>   ```");
    const BlockNode* code = nullptr;
    walk_blocks(editor.document().root, [&](const BlockNode& block) {
        if (!code && block.kind == BlockKind::CodeBlock) code = &block;
    });
    expect(fatal(bool(code != nullptr)));
    if (!code) return;
    editor.set_selection(TextSelection::caret({
        code->id,
        code->block_source.source().size(),
        TextAffinity::Downstream,
    }));
    expect(fatal(bool(editor.execute_document_enter(editor.selection()).has_value())));
    const auto* paragraph = find_block(
        editor.document().root,
        editor.selection().active.container_id);
    expect(fatal(bool(paragraph && paragraph->kind == BlockKind::Paragraph)));
    expect(fatal(bool(editor.document().root.children.front().kind == BlockKind::BlockQuote)));
    expect(fatal(bool(editor.document().root.children.front().children.front().kind == BlockKind::List)));
    const BlockNode* list_item = nullptr;
    walk_blocks(editor.document().root, [&](const BlockNode& block) {
        if (!list_item && block.kind == BlockKind::ListItem) list_item = &block;
    });
    expect(fatal(bool(list_item != nullptr)));
    if (!list_item) return;
    expect(fatal(bool(list_item->children.size() == 2u)));
    expect(fatal(bool(list_item->children.front().kind == BlockKind::CodeBlock)));
    expect(fatal(bool(list_item->children.back().id == editor.selection().active.container_id)));
};

"command_pipeline_exposes_typed_and_toolbar_raw_blocks_to_rendering_immediately"_test = [] {
    struct TypedCase {
        std::u32string opening;
        BlockKind block_kind;
        RenderBlockKind render_kind;
    };
    for (const auto& test_case : std::vector<TypedCase>{
            {U"```cpp", BlockKind::CodeBlock, RenderBlockKind::Code},
            {U"$$", BlockKind::MathBlock, RenderBlockKind::Math},
        }) {
        Editor editor;
        expect(fatal(bool(editor.execute_command(Command::InsertText(test_case.opening)))));
        Command enter;
        enter.kind = CommandKind::InsertNewline;
        expect(fatal(bool(editor.execute_command(enter))));
        expect(fatal(bool(editor.document().root.children.front().kind == test_case.block_kind)));
        auto model = build_render_model(
            editor.document(), editor.outline(), editor.symbols(), default_theme_profile());
        expect(fatal(bool(model.revision == editor.revision())));
        expect(fatal(bool(model.blocks.size() == 1u)));
        expect(fatal(bool(model.blocks.front().kind == test_case.render_kind)));
    }

    for (const auto command_kind : {
            CommandKind::InsertCodeBlock,
            CommandKind::InsertMathBlock,
        }) {
        Editor editor;
        Command command;
        command.kind = command_kind;
        expect(fatal(bool(editor.execute_command(command))));
        auto model = build_render_model(
            editor.document(), editor.outline(), editor.symbols(), default_theme_profile());
        expect(fatal(bool(model.blocks.size() == 1u)));
        expect(fatal(bool(model.blocks.front().kind
            == (command_kind == CommandKind::InsertCodeBlock
                ? RenderBlockKind::Code
                : RenderBlockKind::Math))));
        expect(fatal(bool(model.blocks.front().id == editor.selection().active.container_id)));
    }
};

"code_and_math_blocks_use_local_source_edit_history"_test = [] {
    const std::vector<std::string> cases{
        "```cpp\nab\n```",
        "$$\nab\n$$",
    };
    for (const auto& markdown : cases) {
        Editor editor(markdown);
        const auto owner_id = editor.document().root.children.front().id;
        const auto content_offset = editor.document().root.children.front()
            .block_source.tree().content_to_source.front();
        const auto original_source = *document_editable_text(editor.document(), owner_id);
        const auto before_markdown = editor.markdown_utf8();
        const auto before_selection = TextSelection::caret(
            {owner_id, content_offset, TextAffinity::Downstream});
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
        expected.insert(content_offset, U"X");
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
        expected.insert(content_offset + 1, U"\n");
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

"raw_marker_edits_reclassify_one_block_without_document_reparse"_test = [] {
    struct Case {
        std::string markdown;
        std::u32string opening;
        BlockKind kind;
    };
    const std::vector<Case> cases{
        {"```cpp\nvalue\n```", U"```cpp", BlockKind::CodeBlock},
        {"$$\nvalue\n$$", U"$$", BlockKind::MathBlock},
    };

    for (const auto& test_case : cases) {
        Editor editor(test_case.markdown);
        const auto owner_id = editor.document().root.children.front().id;
        const auto original_selection = TextSelection{
            {owner_id, 0, TextAffinity::Downstream},
            {owner_id, test_case.opening.size(), TextAffinity::Downstream},
        };
        editor.set_selection(original_selection);

        reset_core_operation_counters();
        expect(fatal(bool(editor.execute_document_delete_selection(
            editor.selection()).has_value()))) << test_case.markdown;
        auto counters = read_core_operation_counters();
        expect(fatal(bool(counters.full_document_parses == 0u))) << test_case.markdown;
        expect(fatal(bool(counters.full_document_serializations == 0u))) << test_case.markdown;
        expect(fatal(bool(counters.full_tree_transaction_diffs == 0u))) << test_case.markdown;
        auto const* changed = find_block(editor.document().root, owner_id);
        expect(fatal(bool(changed && changed->kind == BlockKind::Paragraph))) << test_case.markdown;
        const auto downgraded_source = utf8_to_cps(test_case.markdown)
            .substr(test_case.opening.size());
        expect(fatal(bool(changed && changed->inline_content.source
            == downgraded_source))) << test_case.markdown;
        expect(fatal(bool(editor.selection().active.container_id == owner_id))) << test_case.markdown;
        expect(fatal(bool(editor.selection().active.source_offset == 0u))) << test_case.markdown;
        expect(fatal(bool(validate_document(editor.document()).empty()))) << test_case.markdown;
        expect(fatal(bool(editor.markdown_utf8() == cps_to_utf8(downgraded_source)))) << test_case.markdown;
        Editor reloaded(editor.markdown_utf8());
        expect(fatal(bool(reloaded.markdown_utf8() == editor.markdown_utf8()))) << test_case.markdown;
        const auto downgraded_selection = editor.selection();

        reset_core_operation_counters();
        expect(fatal(bool(editor.execute_document_insert_text(
            editor.selection(), test_case.opening).has_value()))) << test_case.markdown;
        counters = read_core_operation_counters();
        expect(fatal(bool(counters.full_document_parses == 0u))) << test_case.markdown;
        expect(fatal(bool(counters.full_document_serializations == 0u))) << test_case.markdown;
        expect(fatal(bool(counters.full_tree_transaction_diffs == 0u))) << test_case.markdown;
        changed = find_block(editor.document().root, owner_id);
        expect(fatal(bool(changed && changed->kind == test_case.kind))) << test_case.markdown;
        expect(fatal(bool(changed && changed->block_source.source()
            == utf8_to_cps(test_case.markdown)))) << test_case.markdown;
        expect(fatal(bool(validate_document(editor.document()).empty()))) << test_case.markdown;
        const auto restored_selection = editor.selection();

        expect(fatal(bool(editor.undo()))) << test_case.markdown;
        changed = find_block(editor.document().root, owner_id);
        expect(fatal(bool(changed && changed->kind == BlockKind::Paragraph))) << test_case.markdown;
        expect(fatal(bool(editor.selection() == downgraded_selection))) << test_case.markdown;
        expect(fatal(bool(editor.redo()))) << test_case.markdown;
        changed = find_block(editor.document().root, owner_id);
        expect(fatal(bool(changed && changed->kind == test_case.kind))) << test_case.markdown;
        expect(fatal(bool(editor.selection() == restored_selection))) << test_case.markdown;
    }
};

}; // suite editor_raw_block_tests
