#include <cstddef>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "elmd_test.hpp"
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
    expect(fatal(bool(counters.full_document_symbol_derivations == 1u)))
        << "one structural refresh, no command pre-scan";
    expect(fatal(bool(counters.inline_reparses == 2u))) << "edited owner plus new definition body";
    expect(fatal(bool(transaction->operations.size() == 2u))) << "operations";
    expect(fatal(bool(editor.markdown_utf8() == "body[^1]\n\n[^1]: "))) << "insert markdown";
    expect(fatal(bool(editor.symbols().footnotes.size() == 1u))) << "definition symbols";
    expect(fatal(bool(editor.symbols().footnote_references.size() == 1u))) << "reference symbols";
    const auto after = editor.selection();
    expect(fatal(bool(after.active.container_id == editor.document().root.children.back().children.front().id))) << "definition selection";
    expect(fatal(bool(editor.undo()))) << "undo";
    expect(fatal(bool(editor.markdown_utf8() == "body"))) << "undo markdown";
    expect(fatal(bool(editor.selection() == before))) << "undo selection";
    expect(fatal(bool(editor.redo()))) << "redo";
    expect(fatal(bool(editor.markdown_utf8() == "body[^1]\n\n[^1]: "))) << "redo markdown";
    expect(fatal(bool(editor.selection() == after))) << "redo selection";
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
    expect(fatal(bool(edit.full_document_node_id_scans == 0u)));
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
    expect(fatal(bool(undo.full_document_node_id_scans == 0u)));
    expect(fatal(bool(undo.full_document_block_index_scans == 0u)));
    expect(fatal(bool(undo.full_document_symbol_derivations == 0u)));
    expect(fatal(bool(undo.full_document_outline_derivations == 0u)));
    expect(fatal(bool(undo.local_symbol_derivations == 1u)));
    expect(fatal(bool(undo.inline_reparses == 1u)));
};

"opening_a_nonempty_document_builds_the_block_path_index_once"_test = [] {
    reset_core_operation_counters();
    Editor editor("alpha\n\nbeta\n\ngamma");
    const auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_block_index_scans == 1u)));
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
    expect(fatal(bool(read_core_operation_counters().full_document_node_id_scans == 0u)));
    expect(fatal(bool(read_core_operation_counters().full_document_block_index_scans == 0u)));

    reset_core_operation_counters();
    expect(fatal(editor.undo()));
    const auto undone_cursor = editor.document().next_node_id;
    expect(fatal(bool(undone_cursor > edited_cursor)));
    expect(fatal(bool(read_core_operation_counters().full_document_node_id_scans == 0u)));
    expect(fatal(bool(read_core_operation_counters().full_document_block_index_scans == 0u)));

    reset_core_operation_counters();
    expect(fatal(editor.redo()));
    expect(fatal(bool(editor.document().next_node_id > undone_cursor)));
    expect(fatal(bool(read_core_operation_counters().full_document_node_id_scans == 0u)));
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
    const auto previous_id = editor.document().root.children[block_count - 2].id;
    const auto last_id = editor.document().root.children.back().id;
    const auto previous_size = editor.document().root.children[block_count - 2]
        .inline_content.source.size();
    editor.set_selection(caret(editor.document().root.children.back(), 0));

    reset_core_operation_counters();
    expect(fatal(editor.execute_command(Command{.kind = CommandKind::MoveLeft})));
    auto counters = read_core_operation_counters();
    expect(fatal(bool(editor.selection().active.container_id == previous_id)));
    expect(fatal(bool(editor.selection().active.source_offset == previous_size)));
    expect(fatal(bool(counters.full_document_text_projections == 0u)));
    expect(fatal(bool(counters.full_document_block_index_scans == 0u)));

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
    const auto& last = editor.document().root.children.back();
    editor.set_selection(caret(last, last.inline_content.source.size() / 2));

    reset_core_operation_counters();
    expect(fatal(editor.execute_command(Command{.kind = CommandKind::InsertNewline})));
    const auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_parses == 0u)));
    expect(fatal(bool(counters.full_document_serializations == 0u)));
    expect(fatal(bool(counters.full_document_block_index_scans == 1u)));
    expect(fatal(bool(counters.full_document_symbol_derivations == 0u)));
    expect(fatal(bool(counters.full_document_outline_derivations == 0u)));
    expect(fatal(bool(counters.local_symbol_derivations == 1u)));
    expect(fatal(bool(validate_document(editor.document()).empty())));

    reset_core_operation_counters();
    expect(fatal(editor.undo()));
    auto history_counters = read_core_operation_counters();
    expect(fatal(bool(history_counters.full_document_block_index_scans == 1u)));
    expect(fatal(bool(history_counters.full_document_symbol_derivations == 0u)));
    expect(fatal(bool(history_counters.full_document_outline_derivations == 0u)));
    expect(fatal(bool(history_counters.local_symbol_derivations == 1u)));

    reset_core_operation_counters();
    expect(fatal(editor.redo()));
    history_counters = read_core_operation_counters();
    expect(fatal(bool(history_counters.full_document_block_index_scans == 1u)));
    expect(fatal(bool(history_counters.full_document_symbol_derivations == 0u)));
    expect(fatal(bool(history_counters.full_document_outline_derivations == 0u)));
    expect(fatal(bool(history_counters.local_symbol_derivations == 1u)));
};

"externally_assembled_documents_calibrate_node_ids_once"_test = [] {
    EditorDocument document;
    document.root.id = NodeId{41};
    BlockNode paragraph;
    paragraph.id = NodeId{99};
    InlineCstNode inline_node;
    inline_node.id = NodeId{149};
    paragraph.inline_content.tree.nodes.push_back(inline_node);
    InlineToken token;
    token.id = NodeId{200};
    paragraph.inline_content.tree.tokens.push_back(token);
    document.root.children.push_back(paragraph);

    reset_core_operation_counters();
    expect(fatal(bool(allocate_document_node_id(document) == NodeId{201})));
    expect(fatal(bool(allocate_document_node_id(document) == NodeId{202})));
    const auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_node_id_scans == 1u)));
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
        if (!code_block && block.kind == BlockKind::CodeBlock && block.code_indented) code_block = &block;
    });
    expect(fatal(bool(code_block != nullptr)));
    if (code_block) {
        const auto separator = code_block->block_source.source.find(U"\n\n");
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
        expect(fatal(bool(counters.full_document_block_index_scans == 1u))) << label;
        expect(fatal(bool(!transaction->operations.empty()))) << label;
        const auto after_markdown = editor.markdown_utf8();
        const auto after = editor.selection();
        expect(fatal(bool(editor.undo()))) << label;
        expect(fatal(bool(editor.markdown_utf8() == before_markdown))) << label;
        expect(fatal(bool(editor.selection() == selection))) << label;
        expect(fatal(bool(editor.redo()))) << label;
        expect(fatal(bool(editor.markdown_utf8() == after_markdown))) << label;
        expect(fatal(bool(editor.selection() == after))) << label;
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
    expect(fatal(bool(editor.document().root.children.front().block_source.source == U"```cpp\n```")));
    expect(fatal(bool(editor.document().root.children.front().block_source.tree.language
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
    expect(fatal(bool(editor.document().root.children.front().block_source.source == U"```cpp\n\n```")));
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
        expect(fatal(bool(block && block->block_source.source == U"```cpp\n```")));
        expect(fatal(bool(code.selection().active.source_offset == 7u)));
        expect(fatal(bool(code.execute_document_enter(code.selection()).has_value())));
        block = find_block(code.document().root, code.selection().active.container_id);
        expect(fatal(bool(block && block->block_source.source == U"```cpp\n\n```")));
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
        expect(fatal(bool(block && block->block_source.source == U"$$\n$$")));
        expect(fatal(bool(math.selection().active.source_offset == 3u)));
        expect(fatal(bool(math.execute_document_enter(math.selection()).has_value())));
        block = find_block(math.document().root, math.selection().active.container_id);
        expect(fatal(bool(block && block->block_source.source == U"$$\n\n$$")));
        expect(fatal(bool(math.selection().active.source_offset == 4u)));
    }

    Editor heading("title");
    expect(fatal(bool(heading.execute_document_set_heading(heading.selection(), 1).has_value())));
    expect(fatal(bool(heading.document().root.children.front().opening_marker == U"# ")));
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
    expect(fatal(bool(block && block->block_source.source == U"```js\n```")));
    expect(fatal(bool(block && block->block_source.tree.language
        == std::optional<std::string>{"js"})));
    const auto after = editor.selection();
    expect(fatal(bool(editor.undo())));
    block = find_block(editor.document().root, code_id);
    expect(fatal(bool(block && block->block_source.source == U"```cpp\n```")));
    expect(fatal(bool(editor.selection() == language)));
    expect(fatal(bool(editor.redo())));
    block = find_block(editor.document().root, code_id);
    expect(fatal(bool(block && block->block_source.source == U"```js\n```")));
    expect(fatal(bool(editor.selection() == after)));
};

"enter_in_incomplete_fenced_code_edits_local_source_without_crashing"_test = [] {
    Editor editor("```cpp\n");
    auto const& code = editor.document().root.children.front();
    expect(fatal(bool(code.kind == BlockKind::CodeBlock)));
    expect(fatal(bool(!code.block_source.tree.complete_closing)));
    editor.set_selection(caret(code, code.block_source.source.size()));
    const auto before = editor.selection();
    expect(fatal(bool(editor.execute_document_enter(editor.selection()).has_value())));
    auto const* changed = find_block(editor.document().root, code.id);
    expect(fatal(bool(changed && changed->block_source.source == U"```cpp\n\n")));
    expect(fatal(bool(changed && block_source_tokens_partition(changed->block_source))));
    expect(fatal(bool(changed && flatten_block_source_tokens(changed->block_source)
        == changed->block_source.source)));
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
    expect(fatal(bool(block && block->block_source.source == U"$$\n$$")));
    expect(fatal(bool(editor.selection().active.source_offset == 3u)));
    expect(fatal(bool(editor.execute_document_enter(editor.selection()).has_value())));
    block = find_block(editor.document().root, editor.selection().active.container_id);
    expect(fatal(bool(block && block->block_source.source == U"$$\n\n$$")));
    expect(fatal(bool(editor.selection().active.source_offset == 4u)));
};

"enter_after_a_complete_raw_block_creates_a_sibling_paragraph"_test = [] {
    for (const auto& markdown : {
            std::string{"$$\nx + y\n$$"},
            std::string{"```cpp\nvalue\n```"},
        }) {
        Editor editor(markdown);
        const auto raw_id = editor.document().root.children.front().id;
        const auto raw_size = editor.document().root.children.front().block_source.source.size();
        const auto raw_source = editor.document().root.children.front().block_source.source;
        editor.set_selection(TextSelection::caret({
            raw_id,
            raw_size,
            TextAffinity::Downstream,
        }));
        expect(fatal(bool(editor.execute_document_enter(editor.selection()).has_value()))) << markdown;
        expect(fatal(bool(editor.document().root.children.size() == 2u))) << markdown;
        expect(fatal(bool(editor.document().root.children.front().id == raw_id))) << markdown;
        expect(fatal(bool(editor.document().root.children.front().block_source.source == raw_source))) << markdown;
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
        code->block_source.source.size(),
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
        auto model = build_render_model(editor.document(), editor.outline());
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
        auto model = build_render_model(editor.document(), editor.outline());
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
            .block_source.tree.content_to_source.front();
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
        expect(fatal(bool(changed && changed->block_source.source
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

"raw_marker_reclassification_preserves_arbitrary_ancestor_structure"_test = [] {
    Editor editor("> - ```cpp\n>   value\n>   ```");
    const BlockNode* code = nullptr;
    walk_blocks(editor.document().root, [&](const BlockNode& block) {
        if (!code && block.kind == BlockKind::CodeBlock) code = &block;
    });
    expect(fatal(bool(code != nullptr)));
    if (!code) return;
    const auto code_id = code->id;
    const auto opening_end = code->block_source.source.find(U'\n');
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
    auto closing = first.block_source.source.rfind(U"$$");
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
    auto merged = build_render_model(editor.document(), editor.outline());
    expect(fatal(bool(merged.blocks.size() == 1u)));

    expect(fatal(bool(editor.undo())));
    expect(fatal(bool(editor.markdown_utf8() == before)));
    expect(fatal(bool(editor.selection() == before_selection)));
    expect(fatal(bool(editor.document().root.children.size() == 2u)));
    expect(fatal(bool(find_block(editor.document().root, first_id) != nullptr)));
    expect(fatal(bool(find_block(editor.document().root, second_id) != nullptr)));
    expect(fatal(bool(validate_document(editor.document()).empty())));
    auto restored = build_render_model(editor.document(), editor.outline());
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
            raw_blocks[0]->block_source.tree.tokens,
            BlockSourceTokenKind::ClosingMarker,
            &BlockSourceToken::kind);
        const auto opening = std::ranges::find(
            raw_blocks[1]->block_source.tree.tokens,
            BlockSourceTokenKind::OpeningMarker,
            &BlockSourceToken::kind);
        expect(bool(closing != raw_blocks[0]->block_source.tree.tokens.end()))
            << test_case.markdown;
        expect(bool(opening != raw_blocks[1]->block_source.tree.tokens.end()))
            << test_case.markdown;
        if (closing == raw_blocks[0]->block_source.tree.tokens.end()
            || opening == raw_blocks[1]->block_source.tree.tokens.end()) {
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
        auto changed_model = build_render_model(editor.document(), editor.outline());
        expect(fatal(bool(!changed_model.blocks.empty()))) << test_case.markdown;
        Editor reloaded(after_markdown);
        expect(fatal(bool(reloaded.markdown_utf8() == after_markdown))) << test_case.markdown;
        expect(fatal(bool(validate_document(reloaded.document()).empty()))) << test_case.markdown;

        for (int cycle = 0; cycle < 3; ++cycle) {
            expect(fatal(bool(editor.undo()))) << test_case.markdown;
            expect(fatal(bool(editor.markdown_utf8() == before_markdown))) << test_case.markdown;
            expect(fatal(bool(editor.selection() == boundary))) << test_case.markdown;
            expect(fatal(bool(validate_document(editor.document()).empty()))) << test_case.markdown;
            auto restored_model = build_render_model(editor.document(), editor.outline());
            expect(fatal(bool(!restored_model.blocks.empty()))) << test_case.markdown;

            expect(fatal(bool(editor.redo()))) << test_case.markdown;
            expect(fatal(bool(editor.markdown_utf8() == after_markdown))) << test_case.markdown;
            expect(fatal(bool(editor.selection() == after_selection))) << test_case.markdown;
            expect(fatal(bool(validate_document(editor.document()).empty()))) << test_case.markdown;
        }
    }
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
            raw.block_source.tree.tokens,
            BlockSourceTokenKind::OpeningMarker,
            &BlockSourceToken::kind);
        expect(fatal(bool(opening != raw.block_source.tree.tokens.end()))) << markdown;
        if (opening == raw.block_source.tree.tokens.end()) continue;

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
        auto model = build_render_model(editor.document(), editor.outline());
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

"clipboard_copy_preserves_exact_inline_source_and_recursive_block_structure"_test = [] {
    Editor marked("**one**");
    const auto marked_id = first_text(marked).id;
    marked.set_selection({
        {marked_id, 1, TextAffinity::Downstream},
        {marked_id, 6, TextAffinity::Upstream}});
    expect(fatal(bool(marked.selected_markdown_cps() == U"*one*"))) << "same owner";

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
    const auto copied = nested.selected_markdown_cps();
    expect(fatal(bool(copied.starts_with(U"> ")))) << "nested quote prefix";
    expect(fatal(bool(copied.find(U"- beta") != std::u32string::npos))) << "nested list";
    expect(fatal(bool(copied.find(U"- gam") != std::u32string::npos))) << "nested list boundary";
    expect(fatal(bool(copied.find(U"gamma") == std::u32string::npos))) << "nested truncation";
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

"table_navigation_changes_selection_without_creating_history"_test = [] {
    Editor editor("| A | B |\n| --- | --- |\n| 1 | 2 |");
    const auto& table = editor.document().root.children.front();
    const auto first_id = table.children.front().children.front().id;
    const auto second_id = table.children.front().children[1].id;
    editor.set_selection(TextSelection::caret({first_id, 0, TextAffinity::Downstream}));
    const auto had_undo = editor.has_undo();
    reset_core_operation_counters();
    expect(fatal(bool(editor.execute_document_table_edit(editor.selection(), DocumentTableEdit::MoveCellNext).has_value())));
    const auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_tree_transaction_diffs == 0u)));
    expect(fatal(bool(editor.selection().active.container_id == second_id)));
    expect(fatal(bool(editor.has_undo() == had_undo)));
};

"table_structure_edits_record_rows_columns_and_payloads"_test = [] {
    struct Case {
        DocumentTableEdit edit;
        std::size_t row;
        std::size_t column;
        TableAlignment alignment = TableAlignment::None;
        std::size_t argument = 0;
    };
    const std::vector<Case> cases{
        {DocumentTableEdit::InsertRowBelow, 1, 0},
        {DocumentTableEdit::DeleteRow, 1, 0},
        {DocumentTableEdit::MoveRowDown, 1, 0},
        {DocumentTableEdit::MoveRowTo, 1, 0, TableAlignment::None, 2},
        {DocumentTableEdit::InsertColumnRight, 1, 0},
        {DocumentTableEdit::DeleteColumn, 1, 0},
        {DocumentTableEdit::MoveColumnRight, 1, 0},
        {DocumentTableEdit::MoveColumnTo, 1, 0, TableAlignment::None, 1},
        {DocumentTableEdit::SetColumnAlignment, 1, 0, TableAlignment::Center},
        {DocumentTableEdit::Normalize, 1, 0},
    };
    for (const auto& test : cases) {
        Editor editor("| A | B |\n| --- | --- |\n| 1 | 2 |\n| 3 | 4 |");
        const auto& table = editor.document().root.children.front();
        const auto owner = table.children[test.row].children[test.column].id;
        const auto before = TextSelection::caret({owner, 0, TextAffinity::Downstream});
        const auto before_markdown = editor.markdown_utf8();
        editor.set_selection(before);
        reset_core_operation_counters();
        auto transaction = editor.execute_document_table_edit(
            editor.selection(), test.edit, test.alignment, test.argument);
        expect(fatal(bool(transaction.has_value()))) << static_cast<int>(test.edit);
        if (!transaction) continue;
        const auto counters = read_core_operation_counters();
        expect(fatal(bool(counters.full_document_parses == 0u))) << static_cast<int>(test.edit);
        expect(fatal(bool(counters.full_document_serializations == 0u))) << static_cast<int>(test.edit);
        expect(fatal(bool(counters.full_tree_transaction_diffs == 0u))) << static_cast<int>(test.edit);
        expect(fatal(bool(!transaction->operations.empty()))) << static_cast<int>(test.edit);
        const auto after_markdown = editor.markdown_utf8();
        const auto after = editor.selection();
        expect(fatal(bool(editor.undo()))) << static_cast<int>(test.edit);
        expect(fatal(bool(editor.markdown_utf8() == before_markdown))) << static_cast<int>(test.edit);
        expect(fatal(bool(editor.selection() == before))) << static_cast<int>(test.edit);
        expect(fatal(bool(editor.redo()))) << static_cast<int>(test.edit);
        expect(fatal(bool(editor.markdown_utf8() == after_markdown))) << static_cast<int>(test.edit);
        expect(fatal(bool(editor.selection() == after))) << static_cast<int>(test.edit);
    }
};

"derived_symbols_and_outline_refresh_after_structure_edits"_test = [] {
    Editor editor("title");
    editor.set_selection(caret(first_text(editor), 0));
    reset_core_operation_counters();
    expect(fatal(bool(editor.execute_document_set_heading(editor.selection(), 2).has_value())));
    const auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_symbol_derivations == 1u)));
    expect(fatal(bool(counters.full_document_outline_derivations == 1u)));
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

"enter_replaces_the_source_selection_before_applying_block_semantics"_test = [] {
    auto exercise = [](std::string markdown, std::u32string_view selected_source,
                       std::u32string_view expected_first, std::u32string_view expected_second) {
        Editor editor(std::move(markdown));
        const auto fragments = document_text_fragments(editor.document());
        auto selected = std::find_if(fragments.begin(), fragments.end(), [&](const auto& fragment) {
            return fragment.text == selected_source;
        });
        expect(fatal(bool(selected != fragments.end())));
        if (selected == fragments.end()) return;

        const auto before_markdown = editor.markdown_utf8();
        const TextSelection before{
            {selected->container_id, 1, TextAffinity::Downstream},
            {selected->container_id, 3, TextAffinity::Downstream},
        };
        editor.set_selection(before);
        reset_core_operation_counters();
        auto transaction = editor.execute_document_enter(editor.selection());
        expect(fatal(bool(transaction.has_value())));
        if (!transaction) return;
        const auto counters = read_core_operation_counters();
        expect(fatal(bool(counters.full_document_parses == 0u)));
        expect(fatal(bool(counters.full_document_serializations == 0u)));
        expect(fatal(bool(counters.full_tree_transaction_diffs == 0u)));

        const auto after_fragments = document_text_fragments(editor.document());
        auto first = std::find_if(after_fragments.begin(), after_fragments.end(), [&](const auto& fragment) {
            return fragment.text == expected_first;
        });
        auto second = std::find_if(after_fragments.begin(), after_fragments.end(), [&](const auto& fragment) {
            return fragment.text == expected_second;
        });
        expect(fatal(bool(first != after_fragments.end())));
        expect(fatal(bool(second != after_fragments.end())));
        const auto after_markdown = editor.markdown_utf8();
        const auto after_selection = editor.selection();

        expect(fatal(bool(editor.undo())));
        expect(fatal(bool(editor.markdown_utf8() == before_markdown)));
        expect(fatal(bool(editor.selection() == before)));
        expect(fatal(bool(editor.redo())));
        expect(fatal(bool(editor.markdown_utf8() == after_markdown)));
        expect(fatal(bool(editor.selection() == after_selection)));
    };

    exercise("abcd", U"abcd", U"a", U"d");
    exercise("> abcd", U"abcd", U"a", U"d");
    exercise("- abcd", U"abcd", U"a", U"d");
    exercise("> [!NOTE] abcd\n> body", U"abcd", U"a", U"d");
    exercise("| abcd |\n| --- |", U"abcd", U"a<br>d", U"a<br>d");
};

"table_line_break_is_one_semantic_delete_unit"_test = [] {
    Editor editor("| abcd |\n| --- |");
    const auto owner = first_text(editor).id;
    editor.set_selection({
        {owner, 1, TextAffinity::Downstream},
        {owner, 3, TextAffinity::Downstream},
    });
    expect(fatal(bool(editor.execute_document_enter(editor.selection()).has_value())));
    expect(fatal(bool(*editor.editable_source(owner) == U"a<br>d")));
    expect(fatal(bool(editor.selection().active.source_offset == 5u)));

    reset_core_operation_counters();
    expect(fatal(bool(editor.execute_document_delete_backward(editor.selection()).has_value())));
    const auto counters = read_core_operation_counters();
    expect(fatal(bool(counters.full_document_parses == 0u)));
    expect(fatal(bool(counters.full_document_serializations == 0u)));
    expect(fatal(bool(counters.full_tree_transaction_diffs == 0u)));
    expect(fatal(bool(counters.inline_reparses == 1u)));
    expect(fatal(bool(*editor.editable_source(owner) == U"ad")));
    expect(fatal(bool(editor.selection().active.source_offset == 1u)));
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

"random_editor_source_edits_restore_exactly_across_editable_contexts"_test = [] {
    std::mt19937_64 random{0xED1705u};
    const std::u32string alphabet = U"abc *_~`[]()!$\\&;'\"😀";
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
        const auto random_length = static_cast<std::size_t>(random() % 32);
        for (std::size_t index = 0; index < random_length; ++index) {
            original.push_back(alphabet[random() % alphabet.size()]);
        }
        original.push_back(U'q');

        const auto context = iteration % 8;
        Editor editor(make_document(context, original));
        const auto fragments = document_text_fragments(editor.document());
        auto owner = std::find_if(fragments.begin(), fragments.end(), [&](const auto& fragment) {
            return fragment.text == original;
        });
        expect(fatal(bool(owner != fragments.end()))) << iteration;
        if (owner == fragments.end()) continue;

        auto expected = original;
        const auto insert = (random() & 1u) != 0;
        const auto offset = insert
            ? static_cast<std::size_t>(random() % (original.size() + 1))
            : 1 + static_cast<std::size_t>(random() % original.size());
        const auto before = TextSelection::caret({
            owner->container_id,
            offset,
            TextAffinity::Downstream,
        });
        editor.set_selection(before);
        const auto before_markdown = editor.markdown_utf8();

        reset_core_operation_counters();
        auto transaction = insert
            ? editor.execute_document_insert_text(editor.selection(), U"X")
            : editor.execute_document_delete_backward(editor.selection());
        expect(fatal(bool(transaction.has_value()))) << iteration;
        if (!transaction) continue;
        const auto counters = read_core_operation_counters();
        expect(fatal(bool(counters.full_document_parses == 0u))) << iteration;
        expect(fatal(bool(counters.full_document_serializations == 0u))) << iteration;
        expect(fatal(bool(counters.full_tree_transaction_diffs == 0u))) << iteration;
        expect(fatal(bool(counters.inline_reparses == 1u))) << iteration;

        if (insert) expected.insert(offset, 1, U'X');
        else expected.erase(offset - 1, 1);
        const auto* block = find_block(editor.document().root, owner->container_id);
        expect(fatal(bool(block != nullptr))) << iteration;
        if (!block) continue;
        const auto* inline_document = editable_inline_document(*block);
        expect(fatal(bool(inline_document != nullptr))) << iteration;
        if (!inline_document) continue;
        expect(fatal(bool(inline_document->source == expected))) << iteration;
        expect(fatal(bool(flatten_tokens(inline_document->tree, inline_document->source)
            == inline_document->source))) << iteration;
        expect(fatal(bool(editor.selection().active.container_id == owner->container_id))) << iteration;
        expect(fatal(bool(editor.selection().active.source_offset <= expected.size()))) << iteration;

        const auto after_markdown = editor.markdown_utf8();
        const auto after_selection = editor.selection();
        Editor reloaded(after_markdown);
        const auto reloaded_fragments = document_text_fragments(reloaded.document());
        expect(fatal(bool(std::ranges::any_of(reloaded_fragments, [&](const auto& fragment) {
            return fragment.text == expected;
        })))) << iteration;

        expect(fatal(bool(editor.undo()))) << iteration;
        expect(fatal(bool(editor.markdown_utf8() == before_markdown))) << iteration;
        expect(fatal(bool(editor.selection() == before))) << iteration;
        expect(fatal(bool(editor.redo()))) << iteration;
        expect(fatal(bool(editor.markdown_utf8() == after_markdown))) << iteration;
        expect(fatal(bool(editor.selection() == after_selection))) << iteration;
        expect(fatal(bool(*editor.editable_source(owner->container_id) == expected))) << iteration;
    }
};

}; // suite editor_tests
