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

suite editor_history_tests = [] {

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

"inline_html_edit_undo_redo_preserves_the_syntax_island_exactly"_test = [] {
    constexpr std::string_view source =
        "before <span style=\"color:#123456\"><strong>*literal*</strong></span> after";
    Editor editor{std::string(source)};
    const auto literal = source.find("literal");
    expect(fatal(bool(literal != std::string_view::npos)));
    if (literal == std::string_view::npos) return;

    const auto before = caret(first_text(editor), literal + 3u);
    editor.set_selection(before);
    reset_core_operation_counters();
    expect(bool(editor.execute_document_insert_text(
        editor.selection(), U"X").has_value())) << "insert";
    const auto counters = read_core_operation_counters();
    expect(bool(counters.full_document_parses == 0u)) << "no full parse";
    expect(bool(counters.full_document_serializations == 0u)) << "no full serialize";
    const auto edited = std::string(source.substr(0, literal + 3u))
        + "X"
        + std::string(source.substr(literal + 3u));
    expect(bool(editor.markdown_utf8() == edited)) << "edited source";
    expect(bool(inline_contains_kind(
        first_text(editor).inline_content,
        InlineCstKind::HtmlElement))) << "html element";
    expect(bool(!inline_contains_kind(
        first_text(editor).inline_content,
        InlineCstKind::Emphasis))) << "markdown disabled inside html";
    expect(bool(flatten_tokens(
        first_text(editor).inline_content.tree,
        first_text(editor).inline_content.source)
        == first_text(editor).inline_content.source)) << "lossless flatten";
    const auto after = editor.selection();

    expect(bool(editor.undo())) << "undo";
    expect(bool(editor.markdown_utf8() == source)) << "undo source";
    expect(bool(editor.selection() == before)) << "undo selection";
    expect(bool(editor.redo())) << "redo";
    expect(bool(editor.markdown_utf8() == edited)) << "redo source";
    expect(bool(editor.selection() == after)) << "redo selection";
};

"ordinary_block_separators_preserve_selection_source_and_history_exactly"_test = [] {
    struct Case {
        std::string source;
        std::string edited;
        BlockKind first_kind;
    };
    const std::vector<Case> cases{
        {"first\n\nsecond", "first\n\nsXecond", BlockKind::Paragraph},
        {"# first\n\nsecond", "# first\n\nsXecond", BlockKind::Heading},
        {"first\r\n\r\nsecond", "first\r\n\r\nsXecond", BlockKind::Paragraph},
        {"# first\r\n\r\nsecond", "# first\r\n\r\nsXecond", BlockKind::Heading},
    };

    for (const auto& entry : cases) {
        Editor editor(entry.source);
        expect(fatal(bool(editor.document().root.children.size() == 2u))) << entry.source;
        if (editor.document().root.children.size() != 2u) continue;
        const auto& first = editor.document().root.children[0];
        const auto& second = editor.document().root.children[1];
        expect(fatal(bool(first.kind == entry.first_kind))) << entry.source;
        expect(fatal(bool(second.kind == BlockKind::Paragraph))) << entry.source;
        expect(fatal(bool(first.inline_content.source == U"first"))) << entry.source;
        expect(fatal(bool(second.inline_content.source == U"second"))) << entry.source;

        const auto before = TextSelection::caret(
            {second.id, 1u, TextAffinity::Downstream});
        editor.set_selection(before);
        expect(fatal(bool(editor.execute_document_insert_text(
            editor.selection(), U"X").has_value()))) << entry.source;
        expect(fatal(bool(editor.markdown_utf8() == entry.edited))) << entry.source;
        expect(fatal(bool(editor.document().root.children.size() == 2u))) << entry.source;
        expect(fatal(bool(*editor.editable_source(second.id) == U"sXecond"))) << entry.source;
        const auto after = editor.selection();
        expect(fatal(bool(after.active.container_id == second.id))) << entry.source;
        expect(fatal(bool(after.active.source_offset == 2u))) << entry.source;

        Editor reloaded(editor.markdown_utf8());
        expect(fatal(bool(reloaded.document().root.children.size() == 2u))) << entry.source;
        if (reloaded.document().root.children.size() == 2u) {
            expect(fatal(bool(reloaded.document().root.children[1].inline_content.source
                == U"sXecond"))) << entry.source;
        }

        expect(fatal(bool(editor.undo()))) << entry.source;
        expect(fatal(bool(editor.markdown_utf8() == entry.source))) << entry.source;
        expect(fatal(bool(editor.selection() == before))) << entry.source;
        expect(fatal(bool(editor.redo()))) << entry.source;
        expect(fatal(bool(editor.markdown_utf8() == entry.edited))) << entry.source;
        expect(fatal(bool(editor.selection() == after))) << entry.source;
    }
};

"editor_reports_local_text_changes_and_structural_fallbacks"_test = [] {
    Editor editor("abc");
    editor.set_selection(caret(first_text(editor), 1));
    expect(fatal(editor.execute_command(Command::InsertText(U"X"))));
    auto inserted = editor.take_last_document_change();
    expect(fatal(bool(inserted.has_value())));
    if (!inserted) return;
    expect(fatal(!inserted->structural));
    expect(fatal(inserted->forward));
    expect(fatal(bool(inserted->text_operations.size() == 1u)));
    expect(fatal(bool(inserted->text_operations.front().forward.replacement == U"X")));

    expect(fatal(editor.undo()));
    auto undone = editor.take_last_document_change();
    expect(fatal(bool(undone.has_value())));
    if (!undone) return;
    expect(fatal(!undone->structural));
    expect(fatal(!undone->forward));
    expect(fatal(bool(undone->revision_before > undone->revision_after)));

    expect(fatal(editor.redo()));
    auto redone = editor.take_last_document_change();
    expect(fatal(bool(redone.has_value())));
    if (!redone) return;
    expect(fatal(redone->forward));
    editor.set_selection(caret(first_text(editor), 2));
    expect(fatal(editor.execute_command(Command{.kind = CommandKind::InsertNewline})));
    auto split = editor.take_last_document_change();
    expect(fatal(bool(split.has_value())));
    expect(fatal(bool(split && split->structural)));
};

"footnote_insert_is_one_exact_source_and_tree_history_transaction"_test = [] {
    Editor editor("body");
    const auto before = range(first_text(editor), 0, 4);
    editor.set_selection(before);
    Command command;
    command.kind = CommandKind::InsertFootnote;
    reset_core_operation_counters();
    const auto transaction = editor.execute_document_insert_footnote(editor.selection(), command);
    expect(fatal(bool(transaction.has_value()))) << "transaction";
    if (!transaction) return;
    const auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_parses == 0u))) << "full parse";
    expect(fatal(bool(counters.full_document_serializations == 0u))) << "full serialize";
    expect(fatal(bool(counters.full_tree_transaction_diffs == 0u))) << "tree diff";
    expect(fatal(bool(counters.full_document_block_index_scans == 0u)))
        << "footnote insert full block index";
    expect(fatal(bool(counters.incremental_document_block_index_repairs == 1u)))
        << "footnote insert incremental block index";
    expect(fatal(bool(counters.full_document_symbol_derivations == 0u)))
        << "inserted definition and edited reference stay contribution-local";
    expect(fatal(bool(counters.inline_reparses == 2u))) << "edited owner plus new definition body";
    expect(fatal(bool(transaction->operations.size() == 2u))) << "operations";
    expect(fatal(document_indexes_are_exact(editor.document()))) << "insert indexes";
    expect(fatal(bool(editor.markdown_utf8() == "body[^1]\n\n[^1]: "))) << "insert markdown";
    expect(fatal(bool(editor.symbols().footnotes.size() == 1u))) << "definition symbols";
    expect(fatal(bool(editor.symbols().footnote_references.size() == 1u))) << "reference symbols";
    const auto after = editor.selection();
    expect(fatal(bool(after.active.container_id == editor.document().root.children.back().children.front().id))) << "definition selection";
    expect(fatal(bool(editor.undo()))) << "undo";
    expect(fatal(bool(editor.markdown_utf8() == "body"))) << "undo markdown";
    expect(fatal(bool(editor.selection() == before))) << "undo selection";
    expect(fatal(bool(editor.symbols().footnotes.empty()))) << "undo definition symbols";
    expect(fatal(bool(editor.symbols().footnote_references.empty()))) << "undo reference symbols";
    expect(fatal(document_indexes_are_exact(editor.document()))) << "undo indexes";
    expect(fatal(bool(editor.redo()))) << "redo";
    expect(fatal(bool(editor.markdown_utf8() == "body[^1]\n\n[^1]: "))) << "redo markdown";
    expect(fatal(bool(editor.selection() == after))) << "redo selection";
    expect(fatal(bool(editor.symbols().footnotes.size() == 1u))) << "redo definition symbols";
    expect(fatal(bool(editor.symbols().footnote_references.size() == 1u))) << "redo reference symbols";
    expect(fatal(document_indexes_are_exact(editor.document()))) << "redo indexes";
    expect(fatal(bool(validate_document(editor.document()).empty()))) << "redo document invariants";
};

"missing_footnote_definition_command_is_one_semantic_undoable_edit"_test = [] {
    Editor editor("body[^missing]");
    auto before = editor.selection();
    Command command;
    command.kind = CommandKind::CreateFootnoteDefinition;
    command.footnote_label = "missing";
    expect(fatal(editor.execute_command(command)));
    expect(editor.markdown_utf8() == "body[^missing]\n\n[^missing]: ");
    expect(fatal(editor.undo()));
    expect(editor.markdown_utf8() == "body[^missing]");
    expect(editor.selection() == before);
};

"large_document_footnote_queries_use_the_cached_symbol_and_block_indexes"_test = [] {
    std::string markdown = "first[^note]";
    constexpr std::size_t block_count = 2048;
    for (std::size_t index = 1; index < block_count; ++index) {
        markdown += "\n\nparagraph " + std::to_string(index);
    }
    markdown += "\n\n[^note]: preview body";
    Editor editor(markdown);

    reset_core_operation_counters();
    const auto definition = footnote_definition_target(
        editor.document(), editor.symbols(), "note");
    const auto reference = first_footnote_reference_target(
        editor.symbols(), "note");
    const auto preview = footnote_preview(
        editor.document(), editor.symbols(), "note", 240);
    const auto counters = read_core_operation_counters();

    expect(fatal(bool(definition.has_value())));
    expect(fatal(bool(reference.has_value())));
    expect(fatal(bool(preview == "preview body")));
    expect(fatal(bool(counters.full_document_symbol_derivations == 0u)));
    expect(fatal(bool(counters.full_document_block_index_scans == 0u)));
    expect(fatal(bool(counters.full_document_text_projections == 0u)));
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
    expect(fatal(bool(edit.full_document_block_index_scans == 0u)));
    expect(fatal(bool(edit.full_document_symbol_derivations == 0u)));
    expect(fatal(bool(edit.full_document_outline_derivations == 0u)));
    expect(fatal(bool(edit.local_symbol_derivations == 1u)));
    expect(fatal(bool(edit.inline_reparses == 1u)));

    reset_core_operation_counters();
    expect(fatal(bool(editor.undo())));
    const auto undo = read_core_operation_counters();
    expect(fatal(bool(undo.full_document_parses == 0u)));
    expect(fatal(bool(undo.full_document_serializations == 0u)));
    expect(fatal(bool(undo.full_tree_transaction_diffs == 0u)));
    expect(fatal(bool(undo.full_document_block_index_scans == 0u)));
    expect(fatal(bool(undo.full_document_symbol_derivations == 0u)));
    expect(fatal(bool(undo.full_document_outline_derivations == 0u)));
    expect(fatal(bool(undo.local_symbol_derivations == 1u)));
    expect(fatal(bool(undo.inline_reparses == 1u)));
};

"opening_a_nonempty_document_builds_the_compact_block_locator_index_once"_test = [] {
    reset_core_operation_counters();
    Editor editor("alpha\n\nbeta\n\ngamma");
    const auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_block_index_scans == 1u)));
    expect(fatal(bool(counters.incremental_document_block_index_repairs == 0u)));
    expect(fatal(bool(find_document_block(
        editor.document(), editor.document().root.children.back().id) != nullptr)));
};

"local_symbol_contributions_refresh_only_the_edited_owner"_test = [] {
    Editor prose("# title\n\nalpha\n\nbeta\n\ngamma");
    const auto& paragraph = prose.document().root.children.back();
    auto const initial_outline_content_revision = prose.outline().content_revision;
    prose.set_selection(caret(paragraph, paragraph.inline_content.source.size()));
    reset_core_operation_counters();
    expect(fatal(prose.execute_command(Command::InsertText(U"!"))));
    auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.local_symbol_derivations == 1u)));
    expect(fatal(bool(counters.full_document_symbol_derivations == 0u)));
    expect(fatal(bool(counters.full_document_outline_derivations == 0u)));
    expect(fatal(bool(prose.outline().flat_items().front()->title_plain_text == "title")));
    expect(fatal(bool(prose.outline().content_revision == initial_outline_content_revision)));

    const auto& heading = prose.document().root.children.front();
    prose.set_selection(caret(heading, heading.inline_content.source.size()));
    reset_core_operation_counters();
    expect(fatal(prose.execute_command(Command::InsertText(U"!"))));
    counters = read_core_operation_counters();
    expect(fatal(bool(counters.local_symbol_derivations == 1u)));
    expect(fatal(bool(counters.full_document_symbol_derivations == 0u)));
    expect(fatal(bool(counters.full_document_outline_derivations == 1u)));
    expect(fatal(bool(prose.outline().flat_items().front()->title_plain_text == "title!")));
    expect(fatal(bool(prose.outline().content_revision == prose.revision())));

    Editor link("[x](a)\n\nplain");
    const auto& link_owner = link.document().root.children.front();
    link.set_selection(range(link_owner, 4, 5));
    reset_core_operation_counters();
    expect(fatal(link.execute_command(Command::InsertText(U"b"))));
    counters = read_core_operation_counters();
    expect(fatal(bool(counters.local_symbol_derivations == 1u)));
    expect(fatal(bool(counters.full_document_symbol_derivations == 0u)));
    expect(fatal(bool(counters.full_document_outline_derivations == 0u)));
    expect(fatal(bool(link.symbols().links.size() == 1u)));
    expect(fatal(bool(link.symbols().links.front().href == "b")));
    expect(fatal(link.undo()));
    expect(fatal(bool(link.symbols().links.front().href == "a")));
};

"node_ids_are_document_owned_monotonic_and_normal_edits_never_scan_the_tree"_test = [] {
    Editor editor("abc");
    const auto parsed_cursor = editor.document().next_node_id;
    expect(fatal(bool(parsed_cursor > 0u)));

    editor.set_selection(caret(first_text(editor), 1));
    reset_core_operation_counters();
    expect(fatal(editor.execute_command(Command::InsertText(U"X"))));
    const auto edited_cursor = editor.document().next_node_id;
    expect(fatal(bool(edited_cursor > parsed_cursor)));
    expect(fatal(bool(read_core_operation_counters().full_document_block_index_scans == 0u)));

    reset_core_operation_counters();
    expect(fatal(editor.undo()));
    const auto undone_cursor = editor.document().next_node_id;
    expect(fatal(bool(undone_cursor > edited_cursor)));
    expect(fatal(bool(read_core_operation_counters().full_document_block_index_scans == 0u)));

    reset_core_operation_counters();
    expect(fatal(editor.redo()));
    expect(fatal(bool(editor.document().next_node_id > undone_cursor)));
    expect(fatal(bool(read_core_operation_counters().full_document_block_index_scans == 0u)));
};

"large_document_text_edits_use_the_validated_block_path_index"_test = [] {
    std::string markdown;
    constexpr std::size_t block_count = 2048;
    for (std::size_t index = 0; index < block_count; ++index) {
        if (index != 0) markdown += "\n\n";
        markdown += "paragraph " + std::to_string(index);
    }

    Editor editor(markdown);
    const auto& last = editor.document().root.children.back();
    editor.set_selection(caret(last, last.inline_content.source.size()));

    reset_core_operation_counters();
    expect(fatal(editor.execute_command(Command::InsertText(U"!"))));
    auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_block_index_scans == 0u)));
    expect(fatal(bool(counters.full_document_parses == 0u)));
    expect(fatal(bool(counters.full_document_serializations == 0u)));

    reset_core_operation_counters();
    expect(fatal(editor.undo()));
    counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_block_index_scans == 0u)));

    reset_core_operation_counters();
    expect(fatal(editor.redo()));
    counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_block_index_scans == 0u)));

    expect(fatal(editor.execute_command(Command{.kind = CommandKind::InsertNewline})));
    const auto inserted = editor.selection().active.container_id;
    expect(fatal(bool(find_document_block(editor.document(), inserted) != nullptr)));

    reset_core_operation_counters();
    expect(fatal(editor.execute_command(Command::InsertText(U"new block"))));
    counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_block_index_scans == 0u)));
    expect(fatal(bool(counters.full_document_parses == 0u)));
    expect(fatal(bool(counters.full_document_serializations == 0u)));
};

"large_document_navigation_uses_the_editable_tree_order_index"_test = [] {
    std::string markdown;
    constexpr std::size_t block_count = 2048;
    for (std::size_t index = 0; index < block_count; ++index) {
        if (index != 0) markdown += "\n\n";
        markdown += "paragraph " + std::to_string(index);
    }

    Editor editor(markdown);
    expect(fatal(bool(editor.document().root.children.size() == block_count)));
    if (editor.document().root.children.size() != block_count) return;
    auto const& previous = editor.document().root.children[block_count - 2u];
    auto const& last = editor.document().root.children.back();
    const auto previous_id = previous.id;
    const auto last_id = last.id;
    const auto previous_size = previous.inline_content.source.size();
    editor.set_selection(caret(last, 0));

    reset_core_operation_counters();
    expect(fatal(editor.execute_command(Command{.kind = CommandKind::MoveLeft})));
    auto counters = read_core_operation_counters();
    expect(fatal(bool(editor.selection().active.container_id == previous_id)));
    expect(fatal(bool(editor.selection().active.source_offset == previous_size)));

    reset_core_operation_counters();
    expect(fatal(editor.execute_command(Command{.kind = CommandKind::MoveRight})));
    counters = read_core_operation_counters();
    expect(fatal(bool(editor.selection().active.container_id == last_id)));
    expect(fatal(bool(editor.selection().active.source_offset == 0u)));
    expect(fatal(bool(counters.full_document_text_projections == 0u)));
    expect(fatal(bool(counters.full_document_block_index_scans == 0u)));

    editor.set_selection(caret(
        editor.document().root.children.back(),
        editor.document().root.children.back().inline_content.source.size()));
    expect(fatal(editor.execute_command(Command{.kind = CommandKind::InsertNewline})));
    const auto inserted_id = editor.selection().active.container_id;
    expect(fatal(bool(inserted_id != last_id)));

    reset_core_operation_counters();
    expect(fatal(editor.execute_command(Command{.kind = CommandKind::MoveLeft})));
    counters = read_core_operation_counters();
    expect(fatal(bool(editor.selection().active.container_id == last_id)));
    expect(fatal(bool(counters.full_document_text_projections == 0u)));
    expect(fatal(bool(counters.full_document_block_index_scans == 0u)));

    reset_core_operation_counters();
    expect(fatal(editor.execute_command(Command{.kind = CommandKind::SelectAll})));
    counters = read_core_operation_counters();
    expect(fatal(bool(editor.selection().anchor.container_id
        == editor.document().root.children.front().id)));
    expect(fatal(bool(editor.selection().active.container_id == inserted_id)));
    expect(fatal(bool(counters.full_document_text_projections == 0u)));
    expect(fatal(bool(counters.full_document_block_index_scans == 0u)));
};

"large_document_plain_block_splits_skip_unrelated_symbol_and_outline_rebuilds"_test = [] {
    std::string markdown;
    constexpr std::size_t block_count = 2048;
    for (std::size_t index = 0; index < block_count; ++index) {
        if (index != 0) markdown += "\n\n";
        markdown += "paragraph " + std::to_string(index);
    }
    Editor editor(markdown);
    auto render_model = build_render_model(
        editor.document(),
        editor.outline(),
        editor.symbols(),
        default_theme_profile());
    const auto initial_render_block_count = render_model.blocks.size();
    auto update_render_model = [&](const EditorDocumentChange& change) {
        RenderModelUpdate update;
        update.structural = change.structural;
        update.structural_locality_known = change.structural_locality_known;
        update.structural_anchors = change.structural_anchors;
        for (const auto& operation : change.text_operations) {
            update.changed_owners.push_back(
                (change.forward ? operation.forward : operation.inverse).container_id);
        }
        reset_core_operation_counters();
        render_model = build_render_model_incremental(
            editor.document(),
            editor.outline(),
            editor.symbols(),
            default_theme_profile(),
            std::move(render_model),
            update);
        return read_core_operation_counters();
    };
    const auto& last = editor.document().root.children.back();
    editor.set_selection(caret(last, last.inline_content.source.size() / 2));

    reset_core_operation_counters();
    expect(fatal(editor.execute_command(Command{.kind = CommandKind::InsertNewline})));
    const auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_parses == 0u)));
    expect(fatal(bool(counters.full_document_serializations == 0u)));
    expect(fatal(bool(counters.full_document_block_index_scans == 0u)));
    expect(fatal(bool(counters.incremental_document_block_index_repairs == 1u)));
    expect(fatal(bool(counters.full_document_symbol_derivations == 0u)));
    expect(fatal(bool(counters.full_document_outline_derivations == 0u)));
    expect(fatal(bool(counters.local_symbol_derivations == 1u)));
    expect(fatal(bool(validate_document(editor.document()).empty())));
    expect(fatal(document_indexes_are_exact(editor.document())));

    auto change = editor.take_last_document_change();
    expect(fatal(bool(change.has_value())));
    if (!change) return;
    const auto render_counters = update_render_model(*change);
    expect(fatal(bool(render_counters.render_source_key_derivations == 2u)));
    expect(fatal(bool(render_model.rebuilt_block_count == 2u)));
    expect(fatal(bool(render_model.reused_block_count == initial_render_block_count - 1u)));

    reset_core_operation_counters();
    expect(fatal(editor.undo()));
    auto history_counters = read_core_operation_counters();
    expect(fatal(bool(history_counters.full_document_block_index_scans == 0u)));
    expect(fatal(bool(history_counters.incremental_document_block_index_repairs == 1u)));
    expect(fatal(bool(history_counters.full_document_symbol_derivations == 0u)));
    expect(fatal(bool(history_counters.full_document_outline_derivations == 0u)));
    expect(fatal(bool(history_counters.local_symbol_derivations == 1u)));
    expect(fatal(document_indexes_are_exact(editor.document())));
    change = editor.take_last_document_change();
    expect(fatal(bool(change.has_value())));
    if (!change) return;
    auto history_render_counters = update_render_model(*change);
    expect(fatal(bool(history_render_counters.render_source_key_derivations == 1u)));
    expect(fatal(bool(render_model.rebuilt_block_count == 1u)));
    expect(fatal(bool(render_model.reused_block_count == initial_render_block_count - 1u)));

    reset_core_operation_counters();
    expect(fatal(editor.redo()));
    history_counters = read_core_operation_counters();
    expect(fatal(bool(history_counters.full_document_block_index_scans == 0u)));
    expect(fatal(bool(history_counters.incremental_document_block_index_repairs == 1u)));
    expect(fatal(bool(history_counters.full_document_symbol_derivations == 0u)));
    expect(fatal(bool(history_counters.full_document_outline_derivations == 0u)));
    expect(fatal(bool(history_counters.local_symbol_derivations == 1u)));
    expect(fatal(document_indexes_are_exact(editor.document())));
    change = editor.take_last_document_change();
    expect(fatal(bool(change.has_value())));
    if (!change) return;
    history_render_counters = update_render_model(*change);
    expect(fatal(bool(history_render_counters.render_source_key_derivations == 2u)));
    expect(fatal(bool(render_model.rebuilt_block_count == 2u)));
    expect(fatal(bool(render_model.reused_block_count == initial_render_block_count - 1u)));
};

"nested_structural_edits_invalidate_only_the_stable_top_level_render_block"_test = [] {
    Editor editor("- first\n- second\n\noutside");
    auto render_model = build_render_model(
        editor.document(),
        editor.outline(),
        editor.symbols(),
        default_theme_profile());
    expect(fatal(bool(render_model.blocks.size() == 2u)));

    const auto owner = first_text(editor).id;
    editor.set_selection(TextSelection::caret({owner, 2, TextAffinity::Downstream}));
    expect(fatal(editor.execute_command(Command{.kind = CommandKind::InsertNewline})));
    auto change = editor.take_last_document_change();
    expect(fatal(bool(change.has_value())));
    if (!change) return;
    expect(fatal(bool(change->structural)));
    expect(fatal(bool(change->structural_locality_known)));

    auto update_model = [&](const EditorDocumentChange& current) {
        RenderModelUpdate update;
        update.structural = current.structural;
        update.structural_locality_known = current.structural_locality_known;
        update.structural_anchors = current.structural_anchors;
        for (const auto& operation : current.text_operations) {
            update.changed_owners.push_back(
                (current.forward ? operation.forward : operation.inverse).container_id);
        }
        reset_core_operation_counters();
        render_model = build_render_model_incremental(
            editor.document(),
            editor.outline(),
            editor.symbols(),
            default_theme_profile(),
            std::move(render_model),
            update);
        return read_core_operation_counters();
    };

    auto counters = update_model(*change);
    expect(fatal(bool(counters.render_source_key_derivations == 1u)));
    expect(fatal(bool(render_model.incremental_update)));
    expect(fatal(bool(render_model.changed_block_indices == std::vector<std::size_t>{0})));
    expect(fatal(bool(render_model.rebuilt_block_count == 1u)));
    expect(fatal(bool(render_model.reused_block_count == 1u)));
    auto full = build_render_model(
        editor.document(),
        editor.outline(),
        editor.symbols(),
        default_theme_profile());
    expect(fatal(bool(render_model.blocks.size() == full.blocks.size())));
    for (std::size_t index = 0; index < full.blocks.size(); ++index) {
        expect(fatal(bool(render_model.blocks[index].presentation_key
            == full.blocks[index].presentation_key)));
    }

    expect(fatal(editor.undo()));
    change = editor.take_last_document_change();
    expect(fatal(bool(change.has_value())));
    if (!change) return;
    counters = update_model(*change);
    expect(fatal(bool(counters.render_source_key_derivations == 1u)));
    expect(fatal(bool(render_model.incremental_update)));
    expect(fatal(bool(render_model.changed_block_indices == std::vector<std::size_t>{0})));
    expect(fatal(bool(render_model.rebuilt_block_count == 1u)));
    expect(fatal(bool(render_model.reused_block_count == 1u)));
};

"tree_moves_keep_render_invalidation_local_to_their_stable_top_level"_test = [] {
    Editor editor("- one\n- two\n\noutside");
    auto model = build_render_model(
        editor.document(),
        editor.outline(),
        editor.symbols(),
        default_theme_profile());
    const BlockNode* two = nullptr;
    walk_blocks(editor.document().root, [&](const BlockNode& block) {
        if (block.inline_content.source == U"two") two = &block;
    });
    expect(fatal(bool(two != nullptr)));
    if (!two) return;
    editor.set_selection(caret(*two, 0));
    expect(fatal(bool(editor.execute_document_indent_list_item(editor.selection()).has_value())));
    auto change = editor.take_last_document_change();
    expect(fatal(bool(change.has_value())));
    if (!change) return;
    expect(fatal(bool(change->structural_locality_known)));
    expect(fatal(bool(!change->moved_roots.empty())));

    RenderModelUpdate update;
    update.structural = change->structural;
    update.structural_locality_known = change->structural_locality_known;
    update.structural_anchors = change->structural_anchors;
    reset_core_operation_counters();
    model = build_render_model_incremental(
        editor.document(),
        editor.outline(),
        editor.symbols(),
        default_theme_profile(),
        std::move(model),
        update);
    const auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.render_source_key_derivations == 1u)));
    expect(fatal(bool(model.incremental_update)));
    expect(fatal(bool(model.changed_block_indices == std::vector<std::size_t>{0})));
    expect(fatal(bool(model.rebuilt_block_count == 1u)));
    expect(fatal(bool(model.reused_block_count == 1u)));
};

"externally_imported_blocks_reserve_node_ids_explicitly"_test = [] {
    Editor donor("external");
    BlockNode paragraph = donor.document().root.children.front();
    paragraph.id = NodeId{99};
    expect(fatal(bool(!paragraph.inline_content.tree.nodes.empty())));
    if (paragraph.inline_content.tree.nodes.empty()) return;
    paragraph.inline_content.tree.nodes.front().id = NodeId{149};

    EditorDocument invalid = EditorDocument::empty(1);
    invalid.root.children.clear();
    invalid.root.children.push_back(paragraph);
    expect(fatal(bool(!validate_document(invalid).empty())));

    EditorDocument document = EditorDocument::empty(1);
    document.root.children.clear();
    reserve_document_node_ids(document, paragraph);
    document.root.children.push_back(paragraph);
    rebuild_document_block_index(document);

    expect(fatal(bool(allocate_document_node_id(document) == NodeId{150})));
    expect(fatal(bool(allocate_document_node_id(document) == NodeId{151})));
    expect(fatal(bool(validate_document(document).empty())));
};

}; // suite editor_history_tests
