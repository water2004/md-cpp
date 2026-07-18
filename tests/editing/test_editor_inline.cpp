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

suite editor_inline_tests = [] {

"callout_titles_are_inline_source_owners_with_local_history"_test = [] {
    Editor editor("> [!NOTE] _title_\n> body");
    const auto callout_id = editor.document().root.children.front().id;
    const auto* callout = find_block(editor.document().root, callout_id);
    expect(fatal(bool(callout != nullptr)));
    expect(fatal(bool(callout && callout->kind == BlockKind::Callout)));
    const auto* title = callout ? callout_title_block(*callout) : nullptr;
    expect(fatal(bool(title != nullptr)));
    if (!callout || !title) return;
    const auto title_id = title->id;
    expect(fatal(bool(title->inline_content.source == U"_title_")));

    const auto before_selection = TextSelection::caret(
        {title_id, 1, TextAffinity::Downstream});
    editor.set_selection(before_selection);
    reset_core_operation_counters();
    expect(fatal(bool(editor.execute_document_insert_text(editor.selection(), U"X").has_value())));
    const auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_parses == 0u)));
    expect(fatal(bool(counters.full_document_serializations == 0u)));
    expect(fatal(bool(counters.full_tree_transaction_diffs == 0u)));
    expect(fatal(bool(counters.inline_reparses == 1u)));

    callout = find_block(editor.document().root, callout_id);
    title = callout ? callout_title_block(*callout) : nullptr;
    expect(fatal(bool(title != nullptr)));
    if (!callout || !title) return;
    expect(fatal(bool(title->inline_content.source == U"_Xtitle_")));
    expect(fatal(bool(flatten_tokens(title->inline_content.tree, title->inline_content.source)
        == title->inline_content.source)));
    const auto after_selection = editor.selection();
    expect(fatal(bool(after_selection.active == TextPosition{
        title_id, 2, TextAffinity::Downstream})));
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
    const auto title_id = editor.document().root.children.front().children.front().id;
    const auto before_selection = TextSelection::caret(
        {title_id, 2, TextAffinity::Downstream});
    editor.set_selection(before_selection);
    reset_core_operation_counters();
    expect(fatal(bool(editor.execute_document_enter(editor.selection()).has_value())));
    const auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_parses == 0u)));
    expect(fatal(bool(counters.full_document_serializations == 0u)));
    expect(fatal(bool(counters.full_tree_transaction_diffs == 0u)));
    const auto after_selection = editor.selection();
    expect(fatal(bool(after_selection.active.container_id != title_id)));
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
    reset_core_operation_counters();
    expect(fatal(bool(marked.selected_text_cps() == U"*one*")));
    expect(fatal(bool(read_core_operation_counters().full_document_text_projections == 0u)));
};

"heading_adjacent_blank_line_delete_undo_and_redo_are_exact"_test = [] {
    Editor editor("# title\n\n\n## next");
    expect(fatal(bool(editor.document().root.children.size() == 3u)));
    if (editor.document().root.children.size() != 3u) return;
    const auto title_id = editor.document().root.children.front().id;
    const auto title_size = editor.document().root.children.front().inline_content.source.size();
    const auto blank_id = editor.document().root.children[1].id;
    const auto before = TextSelection::caret({blank_id, 0, TextAffinity::Downstream});
    editor.set_selection(before);

    expect(fatal(editor.execute_command(Command{.kind = CommandKind::DeleteBackward})));
    expect(fatal(bool(editor.markdown_utf8() == "# title\n## next")));
    expect(fatal(bool(editor.selection().active.container_id == title_id)));
    expect(fatal(bool(editor.selection().active.source_offset == title_size)));
    const auto after = editor.selection();

    expect(fatal(editor.undo()));
    expect(fatal(bool(editor.markdown_utf8() == "# title\n\n\n## next")));
    expect(fatal(bool(editor.selection() == before)));
    expect(fatal(editor.redo()));
    expect(fatal(bool(editor.markdown_utf8() == "# title\n## next")));
    expect(fatal(bool(editor.selection() == after)));
};

"clipboard_copy_preserves_exact_inline_source_and_recursive_block_structure"_test = [] {
    Editor marked("**one**");
    const auto marked_id = first_text(marked).id;
    marked.set_selection({
        {marked_id, 1, TextAffinity::Downstream},
        {marked_id, 6, TextAffinity::Upstream}});
    reset_core_operation_counters();
    expect(fatal(bool(marked.selected_markdown_cps() == U"*one*"))) << "same owner";
    auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_text_projections == 0u)));
    expect(fatal(bool(counters.full_document_block_index_scans == 0u)));

    Editor into_list("foo\n\n- one\n- two");
    const auto& first = into_list.document().root.children.front();
    const auto& list = into_list.document().root.children.back();
    const auto& second_item_text = list.children[1].children.front();
    into_list.set_selection({
        {first.id, 1, TextAffinity::Downstream},
        {second_item_text.id, 1, TextAffinity::Upstream}});
    expect(fatal(bool(into_list.selected_markdown_cps() == U"oo\n\n- one\n- t"))) << "into list";

    Editor out_of_list("- bar\n\nfoo");
    const auto& leading_list = out_of_list.document().root.children.front();
    const auto& item_text = leading_list.children.front().children.front();
    const auto& trailing = out_of_list.document().root.children.back();
    out_of_list.set_selection({
        {item_text.id, 2, TextAffinity::Downstream},
        {trailing.id, 1, TextAffinity::Upstream}});
    const auto out_of_list_copy = out_of_list.selected_markdown_cps();
    expect(fatal(bool(out_of_list_copy == U"- r\n\nf")))
        << "out of list: " << cps_to_utf8(out_of_list_copy);

    Editor nested("> alpha\n>\n> - beta\n>   - gamma\n\nomega");
    const auto fragments = document_text_fragments(nested.document());
    expect(fatal(bool(fragments.size() >= 4u))) << "nested fragment count";
    if (fragments.size() < 4u) return;
    const auto gamma = std::ranges::find_if(fragments, [](const auto& fragment) {
        return fragment.text == U"gamma";
    });
    expect(fatal(bool(gamma != fragments.end()))) << "nested gamma owner";
    if (gamma == fragments.end()) return;
    nested.set_selection({
        {fragments[0].container_id, 2, TextAffinity::Downstream},
        {gamma->container_id, 3, TextAffinity::Upstream}});
    reset_core_operation_counters();
    const auto copied = nested.selected_markdown_cps();
    counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_text_projections == 0u)));
    expect(fatal(bool(counters.full_document_block_index_scans == 0u)));
    expect(fatal(bool(copied.starts_with(U"> ")))) << "nested quote prefix";
    expect(fatal(bool(copied.find(U"- beta") != std::u32string::npos))) << "nested list";
    expect(fatal(bool(copied.find(U"- gam") != std::u32string::npos))) << "nested list boundary";
    expect(fatal(bool(copied.find(U"gamma") == std::u32string::npos))) << "nested truncation";
};

"cross_owner_copy_fragments_reparse_truncated_inline_and_raw_sources"_test = [] {
    Editor editor("first **bold**\n\n```cpp\nvalue\n```");
    const auto& paragraph = editor.document().root.children.front();
    const auto& code = editor.document().root.children.back();
    const TextSelection selection{
        {paragraph.id, 6, TextAffinity::Downstream},
        {code.id, 8, TextAffinity::Upstream}};
    const auto ordered = document_copy_detail::order_selection(
        editor.document(),
        selection);
    expect(fatal(bool(ordered.has_value())));
    if (!ordered) return;

    auto next_copy_id = editor.document().next_node_id;
    auto paragraph_copy = document_copy_detail::slice_block(
        paragraph,
        *ordered,
        next_copy_id);
    auto code_copy = document_copy_detail::slice_block(
        code,
        *ordered,
        next_copy_id);
    expect(fatal(bool(paragraph_copy.has_value())));
    expect(fatal(bool(code_copy.has_value())));
    if (!paragraph_copy || !code_copy) return;

    const auto& inline_document = paragraph_copy->inline_content;
    expect(fatal(bool(inline_document.source == U"**bold**")));
    expect(fatal(bool(tokens_partition_source(
        inline_document.tree,
        inline_document.source.size()))));
    expect(fatal(bool(roots_partition_source(
        inline_document.tree,
        inline_document.source.size()))));
    expect(fatal(bool(flatten_tokens(inline_document.tree, inline_document.source)
        == inline_document.source)));
    expect(fatal(bool(serialize_lossless(inline_document.tree, inline_document.source)
        == inline_document.source)));

    expect(fatal(bool(code_copy->block_source.source() == U"```cpp\nv")));
    expect(fatal(bool(block_source_tokens_partition(code_copy->block_source))));
    expect(fatal(bool(flatten_block_source_tokens(code_copy->block_source)
        == code_copy->block_source.source())));
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

"format_toggle_uses_cst_ranges_and_original_delimiters"_test = [] {
    struct Case {
        std::string source;
        InlineFormat format;
        std::size_t start;
        std::size_t end;
    };
    const std::vector<Case> cases{
        {"_value_", InlineFormat::Emphasis, 1, 6},
        {"__value__", InlineFormat::Strong, 2, 7},
        {"~~value~~", InlineFormat::Strikethrough, 2, 7},
        {"``value``", InlineFormat::Code, 2, 7},
        {"$value$", InlineFormat::Math, 1, 6},
        {"__value__", InlineFormat::Strong, 0, 9},
    };

    for (const auto& test : cases) {
        Editor editor(test.source);
        const auto before = range(first_text(editor), test.start, test.end);
        editor.set_selection(before);

        reset_core_operation_counters();
        const auto transaction = editor.execute_document_toggle_inline_format(
            editor.selection(),
            test.format);
        expect(fatal(bool(transaction.has_value()))) << test.source;
        if (!transaction) continue;
        const auto counters = read_core_operation_counters();
        expect(fatal(bool(counters.full_document_parses == 0u))) << test.source;
        expect(fatal(bool(counters.full_document_serializations == 0u))) << test.source;
        expect(fatal(bool(counters.inline_reparses == 1u))) << test.source;
        expect(fatal(bool(transaction->operations.size() == 1u))) << test.source;
        expect(fatal(bool(editor.markdown_utf8() == "value"))) << test.source;
        expect(fatal(bool(editor.selection().anchor.source_offset == 0u))) << test.source;
        expect(fatal(bool(editor.selection().active.source_offset == 5u))) << test.source;
        expect(fatal(bool(validate_document(editor.document()).empty()))) << test.source;

        const auto after = editor.selection();
        expect(fatal(bool(editor.undo()))) << test.source;
        expect(fatal(bool(editor.markdown_utf8() == test.source))) << test.source;
        expect(fatal(bool(editor.selection() == before))) << test.source;
        expect(fatal(bool(editor.redo()))) << test.source;
        expect(fatal(bool(editor.markdown_utf8() == "value"))) << test.source;
        expect(fatal(bool(editor.selection() == after))) << test.source;
    }
};

"format_toggle_at_a_caret_unwraps_the_innermost_matching_cst_node"_test = [] {
    Editor editor("**a *bc* d**");
    editor.set_selection(caret(first_text(editor), 6));
    const auto before = editor.selection();

    const auto transaction = editor.execute_document_toggle_inline_format(
        editor.selection(),
        InlineFormat::Emphasis);
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(editor.markdown_utf8() == "**a bc d**")));
    expect(fatal(bool(editor.selection().is_caret())));
    expect(fatal(bool(editor.selection().active.source_offset == 5u)));
    expect(fatal(bool(inline_contains_kind(
        first_text(editor).inline_content,
        InlineCstKind::Strong))));
    expect(fatal(bool(!inline_contains_kind(
        first_text(editor).inline_content,
        InlineCstKind::Emphasis))));

    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.markdown_utf8() == "**a *bc* d**")));
    expect(fatal(bool(editor.selection() == before)));
    expect(fatal(bool(editor.redo())));
    expect(fatal(bool(editor.markdown_utf8() == "**a bc d**")));
};

"partial_format_toggle_splits_the_original_run_without_normalizing_markers"_test = [] {
    Editor editor("__abcd__");
    const auto before = range(first_text(editor), 3, 5);
    editor.set_selection(before);

    const auto transaction = editor.execute_document_toggle_inline_format(
        editor.selection(),
        InlineFormat::Strong);
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(editor.markdown_utf8() == "__a__bc__d__")));
    expect(fatal(bool(editor.selection().anchor.source_offset == 5u)));
    expect(fatal(bool(editor.selection().active.source_offset == 7u)));
    expect(fatal(bool(validate_document(editor.document()).empty())));

    const auto after = editor.selection();
    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.markdown_utf8() == "__abcd__")));
    expect(fatal(bool(editor.selection() == before)));
    expect(fatal(bool(editor.redo())));
    expect(fatal(bool(editor.markdown_utf8() == "__a__bc__d__")));
    expect(fatal(bool(editor.selection() == after)));
};

"cst_format_toggle_is_identical_across_recursive_editable_contexts"_test = [] {
    struct Case { std::string source; std::string expected; };
    const std::vector<Case> cases{
        {"__value__", "value"},
        {"# __value__", "# value"},
        {"- __value__", "- value"},
        {"> __value__", "> value"},
        {"| __value__ |\n| --- |", "| value |\n| --- |"},
        {"> [!NOTE] __value__\n> body", "> [!NOTE] value\n> body"},
        {"> [!NOTE] title\n> __value__", "> [!NOTE] title\n> value"},
        {"[^n]: __value__", "[^n]: value"},
    };

    for (const auto& test : cases) {
        Editor editor(test.source);
        const auto fragments = document_text_fragments(editor.document());
        const auto fragment = std::ranges::find_if(fragments, [](const auto& candidate) {
            return candidate.text == U"__value__";
        });
        expect(fatal(bool(fragment != fragments.end()))) << test.source;
        if (fragment == fragments.end()) continue;
        const auto before = TextSelection{
            {fragment->container_id, 2, TextAffinity::Downstream},
            {fragment->container_id, 7, TextAffinity::Downstream},
        };
        editor.set_selection(before);

        reset_core_operation_counters();
        const auto transaction = editor.execute_document_toggle_inline_format(
            editor.selection(),
            InlineFormat::Strong);
        expect(fatal(bool(transaction.has_value()))) << test.source;
        if (!transaction) continue;
        const auto counters = read_core_operation_counters();
        expect(fatal(bool(counters.full_document_parses == 0u))) << test.source;
        expect(fatal(bool(counters.full_document_serializations == 0u))) << test.source;
        expect(fatal(bool(counters.inline_reparses == 1u))) << test.source;
        expect(fatal(bool(editor.markdown_utf8() == test.expected))) << test.source;
        expect(fatal(bool(*editor.editable_source(fragment->container_id) == U"value"))) << test.source;
        expect(fatal(bool(validate_document(editor.document()).empty()))) << test.source;

        expect(fatal(bool(editor.undo()))) << test.source;
        expect(fatal(bool(editor.markdown_utf8() == test.source))) << test.source;
        expect(fatal(bool(editor.selection() == before))) << test.source;
        expect(fatal(bool(editor.redo()))) << test.source;
        expect(fatal(bool(editor.markdown_utf8() == test.expected))) << test.source;
    }
};

"random_cst_format_toggles_are_lossless_and_exactly_reversible"_test = [] {
    std::mt19937_64 random{0xF04A7u};
    const std::u32string alphabet = U"abc *_~`[]()!$\\&;'\"😀";
    const std::vector<InlineFormat> formats{
        InlineFormat::Emphasis,
        InlineFormat::Strong,
        InlineFormat::Strikethrough,
        InlineFormat::Code,
        InlineFormat::Math,
    };
    const auto make_document = [](std::size_t context, std::u32string_view source) {
        const auto text = cps_to_utf8(source);
        switch (context) {
            case 0: return text;
            case 1: return "# " + text;
            case 2: return "- " + text;
            case 3: return "> " + text;
            case 4: return "| " + text + " |\n| --- |";
            case 5: return "> [!NOTE] " + text + "\n> body";
            case 6: return "> [!NOTE] title\n> " + text;
            default: return "[^n]: " + text;
        }
    };

    for (std::size_t iteration = 0; iteration < 240; ++iteration) {
        std::u32string original = U"p";
        const auto length = static_cast<std::size_t>(random() % 32);
        for (std::size_t index = 0; index < length; ++index) {
            original.push_back(alphabet[random() % alphabet.size()]);
        }
        original.push_back(U'q');

        Editor editor(make_document(iteration % 8, original));
        const auto fragments = document_text_fragments(editor.document());
        const auto fragment = std::ranges::find_if(fragments, [&](const auto& candidate) {
            return candidate.text == original;
        });
        expect(fatal(bool(fragment != fragments.end()))) << iteration;
        if (fragment == fragments.end()) continue;

        const auto first = static_cast<std::size_t>(random() % (original.size() + 1));
        const auto second = static_cast<std::size_t>(random() % (original.size() + 1));
        TextSelection before{
            {fragment->container_id, first, TextAffinity::Downstream},
            {fragment->container_id, second, TextAffinity::Upstream},
        };
        editor.set_selection(before);
        const auto before_markdown = editor.markdown_utf8();

        reset_core_operation_counters();
        const auto transaction = editor.execute_document_toggle_inline_format(
            editor.selection(),
            formats[random() % formats.size()]);
        expect(fatal(bool(transaction.has_value()))) << iteration;
        if (!transaction) continue;
        const auto counters = read_core_operation_counters();
        expect(fatal(bool(counters.full_document_parses == 0u))) << iteration;
        expect(fatal(bool(counters.full_document_serializations == 0u))) << iteration;
        expect(fatal(bool(counters.full_tree_transaction_diffs == 0u))) << iteration;
        expect(fatal(bool(counters.inline_reparses == 1u))) << iteration;
        expect(fatal(bool(validate_document(editor.document()).empty()))) << iteration;

        const auto source = editor.editable_source(fragment->container_id);
        expect(fatal(bool(source.has_value()))) << iteration;
        if (!source) continue;
        expect(fatal(bool(editor.selection().anchor.source_offset <= source->size()))) << iteration;
        expect(fatal(bool(editor.selection().active.source_offset <= source->size()))) << iteration;
        const auto* block = find_block(editor.document().root, fragment->container_id);
        expect(fatal(bool(block != nullptr))) << iteration;
        if (!block) continue;
        const auto* inline_document = editable_inline_document(*block);
        expect(fatal(bool(inline_document != nullptr))) << iteration;
        if (!inline_document) continue;
        expect(fatal(bool(flatten_tokens(inline_document->tree, inline_document->source)
            == inline_document->source))) << iteration;

        const auto after_markdown = editor.markdown_utf8();
        const auto after_selection = editor.selection();
        expect(fatal(bool(editor.undo()))) << iteration;
        expect(fatal(bool(editor.markdown_utf8() == before_markdown))) << iteration;
        expect(fatal(bool(editor.selection() == before))) << iteration;
        expect(fatal(bool(editor.redo()))) << iteration;
        expect(fatal(bool(editor.markdown_utf8() == after_markdown))) << iteration;
        expect(fatal(bool(editor.selection() == after_selection))) << iteration;
    }
};

}; // suite editor_inline_tests
