import std;
#include "test_framework.h"
import elmd.core.editor;
import elmd.core.command;
import elmd.core.semantic_edit;
import elmd.core.parser;
import elmd.core.selection;
import elmd.core.transaction;
import elmd.core.utf;
import elmd.core.source_structure;
import elmd.core.table_edit;
import elmd.core.render_builder;
import elmd.core.render_model;

using namespace elmd;

ELMD_TEST(test_new_editor_empty) {
    Editor e;
    ELMD_CHECK(e.buffer().is_empty());
}

ELMD_TEST(test_editor_insert_text) {
    Editor e;
    e.execute_command(Command::InsertText(U"hello"));
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("hello"));
}

ELMD_TEST(test_editor_incremental_unclosed_bracket_math) {
    Editor e;
    ELMD_CHECK(e.execute_command(Command::InsertText(U"\\")) != std::nullopt);
    ELMD_CHECK(e.execute_command(Command::InsertText(U"[")) != std::nullopt);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("\\["));
    auto model = build_render_model(e.document(), e.buffer().text_utf8(), e.outline());
    ELMD_CHECK_EQ(model.blocks.size(), 1u);
    ELMD_CHECK(model.blocks[0].kind == RenderBlockKind::Math);
}

ELMD_TEST(test_editor_incremental_inline_math_keeps_exact_source_mapping) {
    Editor e;
    for (char32_t ch : std::u32string_view(U"$y=x$")) {
        ELMD_CHECK(e.execute_command(Command::InsertText(std::u32string(1, ch))) != std::nullopt);
    }
    auto model = build_render_model(e.document(), e.buffer().text_utf8(), e.outline());
    ELMD_CHECK_EQ(model.blocks.size(), 1u);
    auto item = std::find_if(model.blocks[0].inline_items.begin(), model.blocks[0].inline_items.end(), [](auto const& candidate) {
        return candidate.kind == InlineRenderItem::Kind::Math;
    });
    ELMD_CHECK(item != model.blocks[0].inline_items.end());
    ELMD_CHECK_EQ(item->text, std::u32string(U"y=x"));
    ELMD_CHECK_EQ(item->source_range.start.v, 0u);
    ELMD_CHECK_EQ(item->source_range.end.v, 5u);

    e.set_selection(Selection::caret(CharOffset(0)));
    for (std::size_t expected = 1; expected <= 5; ++expected) {
        ELMD_CHECK(e.execute_command(Command::MoveRight(false)) != std::nullopt);
        ELMD_CHECK_EQ(e.selection().active.v, expected);
    }
    for (std::size_t expected = 4; expected < 5; --expected) {
        ELMD_CHECK(e.execute_command(Command::MoveLeft(false)) != std::nullopt);
        ELMD_CHECK_EQ(e.selection().active.v, expected);
        if (expected == 0) break;
    }
}

ELMD_TEST(test_editor_undo_redo) {
    Editor e;
    e.execute_command(Command::InsertText(U"hello"));
    e.execute_command(Command::InsertText(U" world"));
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("hello world"));
    e.undo();
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("hello"));
    e.redo();
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("hello world"));
}

ELMD_TEST(test_revision_monotonic_across_undo_redo) {
    // Regression for HANDOFF deadly bug #9: revision must not reset on undo/redo.
    Editor e;
    e.execute_command(Command::InsertText(U"ab"));
    ELMD_CHECK_EQ(e.revision(), 2u); // insert bumped 1->2
    e.set_selection(Selection::caret(CharOffset(2)));
    e.execute_command(Command::InsertText(U"cd"));
    ELMD_CHECK_EQ(e.revision(), 3u); // ->3
    e.undo();
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("ab"));
    ELMD_CHECK(e.revision() == 4u); // unchanged (always++)
    e.redo();
    ELMD_CHECK(e.revision() == 5u);
}

ELMD_TEST(test_editor_toggle_strong) {
    Editor e("bold");
    e.set_selection(Selection{CharOffset(0), CharOffset(4), TextAffinity::Downstream});
    Command c; c.kind = CommandKind::ToggleStrong;
    e.execute_command(c);
    ELMD_CHECK(e.buffer().text_utf8() == "**bold**");
}

ELMD_TEST(test_editor_delete_backward) {
    Editor e("hello");
    e.set_caret(CharOffset(5));
    Command c; c.kind = CommandKind::DeleteBackward;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("hell"));
    ELMD_CHECK(e.selection().affinity == TextAffinity::Upstream);
}

ELMD_TEST(test_editor_insert_text_cursor_moves) {
    Editor e("123");
    e.set_caret(CharOffset(1));
    e.execute_command(Command::InsertText(U" "));
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("1 23"));
    ELMD_CHECK_EQ(e.selection().head().v, 2u);
}

ELMD_TEST(test_editor_insert_math_inline) {
    Editor e("x2");
    e.set_selection(Selection{CharOffset(0), CharOffset(2), TextAffinity::Downstream});
    Command c; c.kind = CommandKind::InsertMathInline;
    e.execute_command(c);
    ELMD_CHECK(e.buffer().text_utf8() == "$x2$");
}

ELMD_TEST(test_editor_insert_math_block) {
    Editor e;
    Command c; c.kind = CommandKind::InsertMathBlock;
    e.execute_command(c);
    auto s = e.buffer().text_utf8();
    ELMD_CHECK(s.find("$$") != std::string::npos);
}

ELMD_TEST(test_insert_text_transaction) {
    Editor e("hello");
    e.set_caret(CharOffset(5));
    auto t = semantic_transaction(Command::InsertText(U" world"), e.text_cps(), e.document(), Selection::caret(CharOffset(5)), 1);
    ELMD_CHECK(t && t->edits.size() == 1);
    auto s = t->apply_to(e.text_cps());
    ELMD_CHECK(cps_to_utf8(s) == "hello world");
}

ELMD_TEST(test_delete_transaction) {
    Editor e("hello world");
    auto text = e.text_cps();
    Command dc; dc.kind = CommandKind::DeleteSelection;
    Selection sel{CharOffset(5), CharOffset(11), TextAffinity::Downstream};
    auto t = semantic_transaction(dc, text, e.document(), sel, 1);
    if (t) { auto s = t->apply_to(text); ELMD_CHECK(cps_to_utf8(s) == "hello"); }
    else ELMD_CHECK(false);
}

ELMD_TEST(test_toggle_strong_transaction) {
    Editor e("hello");
    auto text = e.text_cps();
    Selection sel{CharOffset(0), CharOffset(5), TextAffinity::Downstream};
    Command tc; tc.kind = CommandKind::ToggleStrong;
    auto t = semantic_transaction(tc, text, e.document(), sel, 1);
    ELMD_CHECK(t);
    if (t) { auto s = t->apply_to(text); ELMD_CHECK(cps_to_utf8(s) == "**hello**"); }
}

ELMD_TEST(test_insert_table_contains_header) {
    Editor e;
    Command c; c.kind = CommandKind::InsertTable; c.rows = 2; c.cols = 3;
    e.execute_command(c);
    auto s = e.buffer().text_utf8();
    ELMD_CHECK(s.find("| Header |") != std::string::npos);
}

ELMD_TEST(test_table_tab_moves_to_next_cell) {
    Editor e("| A | B |\n| --- | --- |\n| C | D |\n");
    e.set_caret(CharOffset(2));
    Command c; c.kind = CommandKind::MoveTableCellNext;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.selection().head().v, 6u);
}

ELMD_TEST(test_table_insert_row_below) {
    Editor e("| A | B |\n| --- | --- |\n| C | D |\n");
    e.set_caret(CharOffset(27));
    Command c; c.kind = CommandKind::InsertTableRowBelow;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("| A   | B   |\n| --- | --- |\n| C   | D   |\n|     |     |\n"));
}

ELMD_TEST(test_table_delete_column) {
    Editor e("| A | B |\n| --- | --- |\n| C | D |\n");
    e.set_caret(CharOffset(6));
    Command c; c.kind = CommandKind::DeleteTableColumn;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("| A   |\n| --- |\n| C   |\n"));
}

ELMD_TEST(test_table_move_column_left) {
    Editor e("| A | B |\n| --- | --- |\n| C | D |\n");
    e.set_caret(CharOffset(6));
    Command c; c.kind = CommandKind::MoveTableColumnLeft;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("| B   | A   |\n| --- | --- |\n| D   | C   |\n"));
}

ELMD_TEST(test_table_column_alignment_is_preserved_by_structural_edits) {
    Editor e("| A | B |\n| --- | --- |\n| 1 | 2 |\n");
    e.set_caret(CharOffset(6));
    Command align; align.kind = CommandKind::SetTableColumnAlignment; align.table_alignment = TableAlignment::Right;
    e.execute_command(align);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("| A   | B   |\n| --- | --: |\n| 1   | 2   |\n"));
    ELMD_CHECK_EQ(e.selection().head().v, 8u);
    Command move; move.kind = CommandKind::MoveTableColumnLeft;
    e.execute_command(move);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("| B   | A   |\n| --: | --- |\n| 2   | 1   |\n"));
}

ELMD_TEST(test_normalize_table_canonicalizes_widths_without_losing_alignment) {
    Editor e("|Long|B|\n|:--|--:|\n|1|2|\n");
    e.set_caret(CharOffset(2));
    Command c; c.kind = CommandKind::NormalizeTable;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("| Long | B   |\n| :--- | --: |\n| 1    | 2   |\n"));
}

ELMD_TEST(test_table_insert_row_and_column_at_visual_boundaries) {
    Editor e("| A | B |\n| --- | --- |\n| 1 | 2 |\n");
    e.set_caret(CharOffset(2));
    Command row; row.kind = CommandKind::InsertTableRowAt; row.table_index = 1;
    e.execute_command(row);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("| A   | B   |\n| --- | --- |\n|     |     |\n| 1   | 2   |\n"));
    Command column; column.kind = CommandKind::InsertTableColumnAt; column.table_index = 1;
    e.execute_command(column);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("| A   |     | B   |\n| --- | --- | --- |\n|     |     |     |\n| 1   |     | 2   |\n"));
}

ELMD_TEST(test_table_drag_moves_row_and_column_atomically) {
    Editor e("| A | B |\n| --- | --- |\n| 1 | 2 |\n");
    e.set_caret(CharOffset(29));
    Command row; row.kind = CommandKind::MoveTableRowTo; row.table_index = 0;
    e.execute_command(row);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("| 1   | 2   |\n| --- | --- |\n| A   | B   |\n"));
    e.set_caret(CharOffset(8));
    Command column; column.kind = CommandKind::MoveTableColumnTo; column.table_index = 0;
    e.execute_command(column);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("| 2   | 1   |\n| --- | --- |\n| B   | A   |\n"));
}

ELMD_TEST(test_typing_into_empty_table_cell_updates_source_and_ast) {
    Editor e("| A | B |\n| --- | --- |\n|   | 2 |\n");
    auto table = table_source_at(e.text_cps(), 0);
    ELMD_CHECK(table.has_value());
    ELMD_CHECK(table && table->rows.size() == 3);
    if (!table || table->rows.size() != 3) return;
    ELMD_CHECK(table->rows[2].cells[0].text.empty());
    ELMD_CHECK_EQ(table->rows[2].cells[0].content_range.len(), 0u);
    auto emptyCellOffset = table->rows[2].cells[0].content_range.start;
    e.set_caret(emptyCellOffset);
    e.execute_command(Command::InsertText(U"x"));
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("| A | B |\n| --- | --- |\n| x  | 2 |\n"));
    ELMD_CHECK_EQ(e.document().blocks.size(), 1u);
    ELMD_CHECK(e.document().blocks[0].kind == BlockKind::Table);
    auto const& row = e.document().blocks[0].table_rows[0];
    ELMD_CHECK_EQ(row.cells.size(), 2u);
    ELMD_CHECK_EQ(cps_to_utf8(block_inline_text_content(row.cells[0].children)), std::string("x"));
}

ELMD_TEST(test_enter_at_end_of_table_exits_after_closing_pipe) {
    Editor e("| H1 | H2 |\n| --- | --- |\n| A | B |");
    auto table = table_source_at(e.text_cps(), 0);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    auto const& last_cell = table->rows.back().cells.back();
    e.set_caret(last_cell.content_range.end);
    Command command;
    command.kind = CommandKind::InsertNewline;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("| H1 | H2 |\n| --- | --- |\n| A | B |\n\n"));
    ELMD_CHECK_EQ(e.selection().head().v, e.text_cps().size());
}

ELMD_TEST(test_enter_at_end_of_table_reuses_trailing_newline) {
    Editor e("| H |\n| --- |\n| A |\n");
    auto table = table_source_at(e.text_cps(), 0);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    e.set_caret(table->rows.back().cells.back().content_range.end);
    Command command;
    command.kind = CommandKind::InsertNewline;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("| H |\n| --- |\n| A |\n\n"));
    ELMD_CHECK_EQ(e.selection().head().v, e.text_cps().size());
}

ELMD_TEST(test_enter_in_table_moves_to_same_column_of_next_row) {
    Editor e("| H1 | H2 |\n| --- | --- |\n| A1 | A2 |\n| B1 | B2 |");
    auto table = table_source_at(e.text_cps(), 0);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    auto source_before = e.buffer().text_utf8();
    e.set_caret(table->rows[2].cells[1].content_range.end);
    Command command;
    command.kind = CommandKind::InsertNewline;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), source_before);
    ELMD_CHECK_EQ(e.selection().head().v, table->rows[3].cells[1].content_range.start.v);
}

ELMD_TEST(test_enter_in_table_header_skips_separator_row) {
    Editor e("| H1 | H2 |\n| --- | --- |\n| A1 | A2 |");
    auto table = table_source_at(e.text_cps(), 0);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    auto source_before = e.buffer().text_utf8();
    e.set_caret(table->rows[0].cells[0].content_range.start);
    Command command;
    command.kind = CommandKind::InsertNewline;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), source_before);
    ELMD_CHECK_EQ(e.selection().head().v, table->rows[2].cells[0].content_range.start.v);
}

ELMD_TEST(test_enter_in_any_cell_of_last_table_row_exits_table) {
    Editor e("| H1 | H2 |\n| --- | --- |\n| A1 | A2 |");
    auto table = table_source_at(e.text_cps(), 0);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    e.set_caret(table->rows.back().cells[0].content_range.start);
    Command command;
    command.kind = CommandKind::InsertNewline;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("| H1 | H2 |\n| --- | --- |\n| A1 | A2 |\n\n"));
    ELMD_CHECK_EQ(e.selection().head().v, e.text_cps().size());
}

ELMD_TEST(test_empty_table_cell_has_zero_width_content_after_leading_padding) {
    Editor e("| H |\n| --- |\n|     |");
    auto table = table_source_at(e.text_cps(), 0);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    auto const& cell = table->rows.back().cells.front();
    ELMD_CHECK_EQ(cell.content_range.len(), 0u);
    ELMD_CHECK_EQ(cell.content_range.start.v, cell.content_range.end.v);
    ELMD_CHECK_EQ(cell.content_range.start.v, table->rows.back().line_range.start.v + 2);
    ELMD_CHECK(e.text_cps()[cell.content_range.start.v - 1] == U' ');
    ELMD_CHECK(e.text_cps()[cell.content_range.start.v] == U' ');
}

ELMD_TEST(test_backspace_and_delete_do_not_remove_empty_table_padding_or_pipe) {
    Editor e("| H |\n| --- |\n|     |");
    auto table = table_source_at(e.text_cps(), 0);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    auto offset = table->rows.back().cells.front().content_range.start;
    auto source = e.buffer().text_utf8();
    e.set_caret(offset);
    Command backward;
    backward.kind = CommandKind::DeleteBackward;
    ELMD_CHECK(!e.execute_command(backward).has_value());
    ELMD_CHECK_EQ(e.buffer().text_utf8(), source);
    Command forward;
    forward.kind = CommandKind::DeleteForward;
    ELMD_CHECK(!e.execute_command(forward).has_value());
    ELMD_CHECK_EQ(e.buffer().text_utf8(), source);
}

ELMD_TEST(test_table_character_delete_stays_inside_cell_content) {
    Editor e("| H |\n| --- |\n| ABC |");
    auto table = table_source_at(e.text_cps(), 0);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    auto cell = table->rows.back().cells.front();
    e.set_caret(cell.content_range.end);
    Command backward;
    backward.kind = CommandKind::DeleteBackward;
    e.execute_command(backward);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("| H |\n| --- |\n| AB |"));
    table = table_source_at(e.text_cps(), 0);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    e.set_caret(table->rows.back().cells.front().content_range.start);
    Command forward;
    forward.kind = CommandKind::DeleteForward;
    e.execute_command(forward);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("| H |\n| --- |\n| B |"));
    ELMD_CHECK(e.document().blocks[0].kind == BlockKind::Table);
}

ELMD_TEST(test_deleting_last_table_cell_character_keeps_empty_caret_stable) {
    Editor e("| H |\n| --- |\n| X |");
    auto table = table_source_at(e.text_cps(), 0);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    e.set_caret(table->rows.back().cells.front().content_range.end);
    Command command;
    command.kind = CommandKind::DeleteBackward;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("| H |\n| --- |\n|  |"));
    table = table_source_at(e.text_cps(), 0);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    auto const& empty = table->rows.back().cells.front();
    ELMD_CHECK_EQ(empty.content_range.len(), 0u);
    ELMD_CHECK_EQ(e.selection().head().v, empty.content_range.start.v);
    ELMD_CHECK(e.document().blocks[0].kind == BlockKind::Table);
}

ELMD_TEST(test_delete_from_adjacent_paragraph_does_not_consume_table_boundary) {
    Editor e("| H |\n| --- |\n| A |\n\nafter");
    auto table = table_source_at(e.text_cps(), 0);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    auto source = e.buffer().text_utf8();
    e.set_caret(CharOffset(table->range.end.v));
    Command command;
    command.kind = CommandKind::DeleteBackward;
    ELMD_CHECK(!e.execute_command(command).has_value());
    ELMD_CHECK_EQ(e.buffer().text_utf8(), source);
    ELMD_CHECK(e.document().blocks[0].kind == BlockKind::Table);
}

ELMD_TEST(test_table_cross_cell_selection_delete_preserves_structure) {
    Editor e("| H1 | H2 |\n| --- | --- |\n| A1 | A2 |");
    auto table = table_source_at(e.text_cps(), 0);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    auto const& row = table->rows.back();
    Selection selection;
    selection.anchor = row.cells[0].content_range.start;
    selection.active = row.cells[1].content_range.end;
    e.set_selection(selection);
    Command command;
    command.kind = CommandKind::DeleteSelection;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("| H1 | H2 |\n| --- | --- |\n|  |  |"));
    ELMD_CHECK(e.document().blocks[0].kind == BlockKind::Table);
}

ELMD_TEST(test_insert_toc_contains_marker) {
    Editor e;
    Command c; c.kind = CommandKind::InsertToc;
    e.execute_command(c);
    ELMD_CHECK(e.buffer().text_utf8().find("[TOC]") != std::string::npos);
}

ELMD_TEST(test_enter_in_plain_paragraph_inserts_semantic_block_break) {
    Editor e("alphaomega");
    e.set_caret(CharOffset(5));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("alpha\n\nomega"));
    ELMD_CHECK_EQ(e.selection().head().v, 7u);
    ELMD_CHECK_EQ(e.document().blocks.size(), 2u);
    auto structure = build_source_structure(e.document(), e.text_cps());
    ELMD_CHECK_EQ(structure.blocks.size(), 2u);
    ELMD_CHECK(structure.blocks[0].kind == SourceBlockKind::Semantic);
    ELMD_CHECK(structure.blocks[1].kind == SourceBlockKind::Semantic);
}

ELMD_TEST(test_enter_at_block_end_reuses_existing_separator_and_creates_one_blank) {
    Editor e("# H\n\n## Next");
    e.set_caret(CharOffset(3));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("# H\n\n\n## Next"));
    ELMD_CHECK_EQ(e.selection().head().v, 4u);
    ELMD_CHECK_EQ(e.document().blocks.size(), 2u);
    auto structure = build_source_structure(e.document(), e.text_cps());
    ELMD_CHECK_EQ(structure.blocks.size(), 3u);
    ELMD_CHECK(structure.blocks[0].kind == SourceBlockKind::Semantic);
    ELMD_CHECK(structure.blocks[1].kind == SourceBlockKind::Blank);
    ELMD_CHECK(structure.blocks[2].kind == SourceBlockKind::Semantic);
}

ELMD_TEST(test_repeated_enter_between_early_blocks_keeps_incremental_ranges_in_sync) {
    Editor e("## Sample\n\n```cpp\nx\n```\n\n## Later 1\n\n## Later 2\n\n## Later 3\n\n## Later 4\n");
    e.set_caret(CharOffset(9));
    for (std::size_t count = 1; count <= 4; ++count) {
        Command c; c.kind = CommandKind::InsertNewline;
        e.execute_command(c);
        ELMD_CHECK_EQ(e.selection().head().v, 9u + count);
        auto full = parse_text(e.revision(), e.buffer().text_utf8());
        ELMD_CHECK_EQ(e.document().blocks.size(), full.document.blocks.size());
        for (std::size_t index = 0; index < e.document().blocks.size() && index < full.document.blocks.size(); ++index) {
            ELMD_CHECK(e.document().blocks[index].kind == full.document.blocks[index].kind);
            auto* incremental_range = e.document().source_map.find_node_by_id(e.document().blocks[index].id);
            auto* full_range = full.document.source_map.find_node_by_id(full.document.blocks[index].id);
            ELMD_CHECK(incremental_range != nullptr);
            ELMD_CHECK(full_range != nullptr);
            if (incremental_range && full_range) {
                ELMD_CHECK(incremental_range->source_range.start == full_range->source_range.start);
                ELMD_CHECK(incremental_range->source_range.end == full_range->source_range.end);
                ELMD_CHECK(incremental_range->content_range.start == full_range->content_range.start);
                ELMD_CHECK(incremental_range->content_range.end == full_range->content_range.end);
            }
        }
        auto structure = build_source_structure(e.document(), e.text_cps());
        std::size_t blank_count = 0;
        for (const auto& block : structure.blocks) if (block.kind == SourceBlockKind::Blank) ++blank_count;
        ELMD_CHECK_EQ(blank_count, count);
    }
}

ELMD_TEST(test_backspace_before_opening_fence_only_removes_preceding_block_separator) {
    Editor e("before\n\n```cpp\n```\n");
    e.set_caret(CharOffset(8));
    Command c; c.kind = CommandKind::DeleteBackward;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("before\n```cpp\n```\n"));
    ELMD_CHECK_EQ(e.selection().head().v, 7u);
    ELMD_CHECK_EQ(e.document().blocks.size(), 2u);
    ELMD_CHECK(e.document().blocks[0].kind == BlockKind::Paragraph);
    ELMD_CHECK(e.document().blocks[1].kind == BlockKind::CodeBlock);
    auto* range = e.document().source_map.find_node_by_id(e.document().blocks[1].id);
    ELMD_CHECK(range != nullptr);
    if (range) {
        ELMD_CHECK_EQ(range->source_range.start.v, 7u);
        ELMD_CHECK_EQ(range->marker_ranges.size(), 2u);
    }
}

ELMD_TEST(test_enter_inside_code_block_inserts_single_newline) {
    Editor e("```\nabc\n```\n");
    e.set_caret(CharOffset(7));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("```\nabc\n\n```\n"));
    ELMD_CHECK_EQ(e.selection().head().v, 8u);
    ELMD_CHECK_EQ(e.document().blocks.size(), 1u);
}

ELMD_TEST(test_enter_inside_indented_code_block_preserves_indent) {
    Editor e("    abc\n");
    e.set_caret(CharOffset(7));
    Command command;
    command.kind = CommandKind::InsertNewline;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("    abc\n    \n"));
    ELMD_CHECK_EQ(e.selection().head().v, 12u);
    ELMD_CHECK_EQ(e.document().blocks.size(), 1u);
    ELMD_CHECK(e.document().blocks[0].kind == BlockKind::CodeBlock);
    ELMD_CHECK(e.document().blocks[0].code_indented);
}

ELMD_TEST(test_soft_break_in_plain_paragraph_inserts_single_newline) {
    Editor e("alphaomega");
    e.set_caret(CharOffset(5));
    Command c; c.kind = CommandKind::InsertSoftBreak;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("alpha\nomega"));
    ELMD_CHECK_EQ(e.selection().head().v, 6u);
    ELMD_CHECK_EQ(e.document().blocks.size(), 1u);
}

ELMD_TEST(test_enter_on_empty_paragraph_inserts_one_empty_sibling) {
    Editor e("alpha\n\n");
    e.set_caret(CharOffset(7));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("alpha\n\n\n"));
    ELMD_CHECK_EQ(e.selection().head().v, 8u);
    ELMD_CHECK_EQ(e.document().blocks.size(), 1u);
    auto structure = build_source_structure(e.document(), e.text_cps());
    ELMD_CHECK_EQ(structure.blocks.size(), 3u);
    ELMD_CHECK(structure.blocks[1].kind == SourceBlockKind::Blank);
    ELMD_CHECK(structure.blocks[2].kind == SourceBlockKind::Blank);
}

ELMD_TEST(test_backspace_on_empty_block_deletes_semantic_block) {
    Editor e("alpha\n\n");
    e.set_caret(CharOffset(7));
    Command c; c.kind = CommandKind::DeleteBackward;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("alpha\n"));
    ELMD_CHECK_EQ(e.selection().head().v, 6u);
    ELMD_CHECK_EQ(e.document().blocks.size(), 1u);
}

ELMD_TEST(test_backspace_on_consecutive_empty_block_deletes_block_span) {
    Editor e("alpha\n\n\n\n");
    e.set_caret(CharOffset(9));
    Command c; c.kind = CommandKind::DeleteBackward;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("alpha\n\n\n"));
    ELMD_CHECK_EQ(e.selection().head().v, 8u);
    ELMD_CHECK_EQ(e.document().blocks.size(), 1u);
    auto structure = build_source_structure(e.document(), e.text_cps());
    ELMD_CHECK_EQ(structure.blocks.size(), 3u);
}

ELMD_TEST(test_enter_continues_unordered_list) {
    Editor e("- alpha");
    e.set_caret(CharOffset(7));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("- alpha\n- "));
    ELMD_CHECK_EQ(e.selection().head().v, 10u);
}

ELMD_TEST(test_enter_continues_ordered_list) {
    Editor e("9. alpha");
    e.set_caret(CharOffset(8));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("9. alpha\n10. "));
    ELMD_CHECK_EQ(e.selection().head().v, 13u);
}

ELMD_TEST(test_enter_continues_task_list_unchecked) {
    Editor e("- [x] alpha");
    e.set_caret(CharOffset(11));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("- [x] alpha\n- [ ] "));
    ELMD_CHECK_EQ(e.selection().head().v, 18u);
}

ELMD_TEST(test_enter_exits_list_one_level_before_exiting_blockquote) {
    Editor e("> * alpha");
    e.set_caret(CharOffset(9));
    Command newline; newline.kind = CommandKind::InsertNewline;
    e.execute_command(newline);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> * alpha  \n> * "));
    e.execute_command(newline);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> * alpha\n> "));
    ELMD_CHECK_EQ(e.selection().head().v, 12u);
    ELMD_CHECK(e.selection().affinity == TextAffinity::Downstream);
    e.execute_command(newline);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> * alpha\n"));
    ELMD_CHECK_EQ(e.selection().head().v, 10u);
}

ELMD_TEST(test_empty_list_item_reuses_following_empty_quote_line) {
    std::string source =
        "> #### The quarterly results look great!\n"
        "> \n"
        "> * Revenue was off the chart.\n"
        "> * Profits were higher than ever.\n"
        "> \n"
        "> _Everything_ is going according to **plan**.";
    Editor e(source);
    auto profits_end = source.find("\n> \n", source.find("Profits"));
    ELMD_CHECK(profits_end != std::string::npos);
    if (profits_end == std::string::npos) return;
    e.set_caret(CharOffset(profits_end));
    Command newline; newline.kind = CommandKind::InsertNewline;
    e.execute_command(newline);
    auto continued = source;
    continued.insert(profits_end, "  \n> * ");
    ELMD_CHECK_EQ(e.buffer().text_utf8(), continued);
    e.execute_command(newline);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), source);
    ELMD_CHECK_EQ(e.selection().head().v, profits_end + 3u);
    ELMD_CHECK(e.selection().affinity == TextAffinity::Downstream);
}

ELMD_TEST(test_enter_continues_blockquote) {
    Editor e("> alpha");
    e.set_caret(CharOffset(7));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> alpha  \n> "));
    ELMD_CHECK_EQ(e.selection().head().v, 12u);
}

ELMD_TEST(test_second_enter_exits_empty_blockquote_line) {
    Editor e("> alpha");
    e.set_caret(CharOffset(7));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> alpha\n"));
    ELMD_CHECK_EQ(e.selection().head().v, 8u);
}

ELMD_TEST(test_second_enter_removes_the_empty_quote_line_before_a_following_block) {
    Editor e("> alpha\n\nafter");
    e.set_caret(CharOffset(7));
    Command newline; newline.kind = CommandKind::InsertNewline;
    e.execute_command(newline);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> alpha  \n> \n\nafter"));
    e.execute_command(newline);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> alpha\n\nafter"));
    ELMD_CHECK_EQ(e.selection().head().v, 8u);
    ELMD_CHECK(e.selection().affinity == TextAffinity::Downstream);
}

ELMD_TEST(test_enter_exits_nested_blockquotes_one_level_at_a_time) {
    Editor e("> > alpha");
    e.set_caret(CharOffset(9));
    Command newline; newline.kind = CommandKind::InsertNewline;
    e.execute_command(newline);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> > alpha  \n> > "));
    ELMD_CHECK_EQ(e.selection().head().v, 16u);
    e.execute_command(newline);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> > alpha\n> "));
    ELMD_CHECK_EQ(e.selection().head().v, 12u);
    ELMD_CHECK(e.selection().affinity == TextAffinity::Downstream);
    e.execute_command(newline);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> > alpha\n"));
    ELMD_CHECK_EQ(e.selection().head().v, 10u);
    ELMD_CHECK(e.selection().affinity == TextAffinity::Downstream);
}

ELMD_TEST(test_backspace_exits_nested_blockquotes_one_level_at_a_time) {
    Editor e("> > alpha  \n> > ");
    e.set_caret(CharOffset(16));
    Command backspace; backspace.kind = CommandKind::DeleteBackward;
    e.execute_command(backspace);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> > alpha\n> "));
    ELMD_CHECK_EQ(e.selection().head().v, 12u);
    ELMD_CHECK(e.selection().affinity == TextAffinity::Downstream);
    e.execute_command(backspace);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> > alpha"));
    ELMD_CHECK_EQ(e.selection().head().v, 9u);
}

ELMD_TEST(test_backspace_at_first_quote_content_start_removes_exactly_one_level) {
    Editor e("> > alpha");
    e.set_caret(CharOffset(4));
    Command backspace; backspace.kind = CommandKind::DeleteBackward;
    e.execute_command(backspace);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> alpha"));
    ELMD_CHECK_EQ(e.selection().head().v, 2u);
    ELMD_CHECK(e.selection().affinity == TextAffinity::Upstream);
    e.execute_command(backspace);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("alpha"));
    ELMD_CHECK_EQ(e.selection().head().v, 0u);
    ELMD_CHECK(e.selection().affinity == TextAffinity::Upstream);
}

ELMD_TEST(test_backspace_at_following_quote_line_start_joins_the_same_depth_first) {
    Editor e("> > first\n> > second");
    e.set_caret(CharOffset(14));
    Command backspace; backspace.kind = CommandKind::DeleteBackward;
    e.execute_command(backspace);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> > firstsecond"));
    ELMD_CHECK_EQ(e.selection().head().v, 9u);
    ELMD_CHECK(e.selection().affinity == TextAffinity::Upstream);
    auto line = quote_source_line_at(e.text_cps(), e.selection().head());
    ELMD_CHECK(line.has_value());
    if (line) ELMD_CHECK_EQ(line->marker_ranges.size(), 2u);
}

ELMD_TEST(test_backspace_join_removes_the_preceding_hard_break_marker) {
    Editor e("> > first  \n> > second");
    e.set_caret(CharOffset(16));
    Command backspace; backspace.kind = CommandKind::DeleteBackward;
    e.execute_command(backspace);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> > firstsecond"));
    ELMD_CHECK_EQ(e.selection().head().v, 9u);
}

ELMD_TEST(test_backspace_inside_nested_quote_content_never_touches_quote_markers) {
    Editor e("> > abcdefghijklmnopqrstuvwxyz");
    e.set_caret(CharOffset(18));
    Command backspace; backspace.kind = CommandKind::DeleteBackward;
    e.execute_command(backspace);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> > abcdefghijklmopqrstuvwxyz"));
    ELMD_CHECK_EQ(e.selection().head().v, 17u);
    ELMD_CHECK(e.selection().affinity == TextAffinity::Upstream);
    auto line = quote_source_line_at(e.text_cps(), e.selection().head());
    ELMD_CHECK(line.has_value());
    if (line) {
        ELMD_CHECK_EQ(line->marker_ranges.size(), 2u);
        ELMD_CHECK_EQ(line->content_range.start.v, 4u);
    }
}

ELMD_TEST(test_quote_source_line_model_owns_each_marker_and_content_range) {
    auto text = std::u32string(U"> > nested text\n");
    auto line = quote_source_line_at(text, CharOffset(8));
    ELMD_CHECK(line.has_value());
    if (line) {
        ELMD_CHECK_EQ(line->marker_ranges.size(), 2u);
        ELMD_CHECK_EQ(line->marker_ranges[0].start.v, 0u);
        ELMD_CHECK_EQ(line->marker_ranges[0].end.v, 2u);
        ELMD_CHECK_EQ(line->marker_ranges[1].start.v, 2u);
        ELMD_CHECK_EQ(line->marker_ranges[1].end.v, 4u);
        ELMD_CHECK_EQ(line->content_range.start.v, 4u);
        ELMD_CHECK_EQ(line->content_range.end.v, 15u);
        ELMD_CHECK(!line->empty);
    }
}

ELMD_TEST(test_blockquote_newline_with_following_block_keeps_an_editable_quote_line) {
    Editor e("> alpha\n\nafter");
    e.set_caret(CharOffset(7));
    Command newline; newline.kind = CommandKind::InsertNewline;
    e.execute_command(newline);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> alpha  \n> \n\nafter"));
    ELMD_CHECK_EQ(e.selection().head().v, 12u);
    ELMD_CHECK(e.selection().affinity == TextAffinity::Downstream);
    e.execute_command(Command::InsertText(U"11111"));
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> alpha  \n> 11111\n\nafter"));
    ELMD_CHECK_EQ(e.selection().head().v, 17u);
    auto quote_edit = markdown_newline_edit(e.text_cps(), e.selection().normalized_range());
    ELMD_CHECK(quote_edit.has_value());
    if (quote_edit) ELMD_CHECK_EQ(quote_edit->text, std::u32string(U"\n> "));
    e.execute_command(newline);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> alpha  \n> 11111  \n> \n\nafter"));
    ELMD_CHECK_EQ(e.selection().head().v, 22u);
    ELMD_CHECK(e.selection().affinity == TextAffinity::Downstream);
}

ELMD_TEST(test_backspace_removes_an_empty_blockquote_prefix_atomically) {
    Editor e("> alpha  \n> \n\nafter");
    e.set_caret(CharOffset(12));
    Command backspace; backspace.kind = CommandKind::DeleteBackward;
    e.execute_command(backspace);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> alpha\n\nafter"));
    ELMD_CHECK_EQ(e.selection().head().v, 7u);
}

ELMD_TEST(test_enter_on_empty_indented_code_line_exits_the_code_block) {
    Editor e("    alpha\n    \n\nafter");
    e.set_caret(CharOffset(14));
    Command newline; newline.kind = CommandKind::InsertNewline;
    e.execute_command(newline);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("    alpha\n\n\nafter"));
    ELMD_CHECK_EQ(e.selection().head().v, 10u);
    ELMD_CHECK_EQ(e.document().blocks.size(), 2u);
    ELMD_CHECK(e.document().blocks[0].kind == BlockKind::CodeBlock);
    ELMD_CHECK(e.document().blocks[1].kind == BlockKind::Paragraph);
}

ELMD_TEST(test_backspace_on_empty_indented_code_line_exits_the_code_block) {
    Editor e("    alpha\n    \n\nafter");
    e.set_caret(CharOffset(14));
    Command backspace; backspace.kind = CommandKind::DeleteBackward;
    e.execute_command(backspace);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("    alpha\n\nafter"));
    ELMD_CHECK_EQ(e.selection().head().v, 9u);
}

ELMD_TEST(test_enter_on_empty_list_exits_list) {
    Editor e("- ");
    e.set_caret(CharOffset(2));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string(""));
    ELMD_CHECK_EQ(e.selection().head().v, 0u);
}

ELMD_TEST(test_toggle_unordered_list) {
    Editor e("alpha");
    e.set_caret(CharOffset(5));
    Command c; c.kind = CommandKind::ToggleUnorderedList;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("- alpha"));
    ELMD_CHECK_EQ(e.selection().head().v, 7u);
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("alpha"));
    ELMD_CHECK_EQ(e.selection().head().v, 5u);
}

ELMD_TEST(test_toggle_ordered_list_replaces_unordered_marker) {
    Editor e("- alpha");
    e.set_caret(CharOffset(7));
    Command c; c.kind = CommandKind::ToggleOrderedList;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("1. alpha"));
    ELMD_CHECK_EQ(e.selection().head().v, 8u);
}

ELMD_TEST(test_toggle_task_list_replaces_ordered_marker) {
    Editor e("1. alpha");
    e.set_caret(CharOffset(8));
    Command c; c.kind = CommandKind::ToggleTaskList;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("- [ ] alpha"));
    ELMD_CHECK_EQ(e.selection().head().v, 11u);
}

ELMD_TEST(test_toggle_task_checkbox) {
    Editor e("- [ ] alpha");
    e.set_caret(CharOffset(3));
    Command c; c.kind = CommandKind::ToggleTaskCheckbox;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("- [x] alpha"));
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("- [ ] alpha"));
}

ELMD_TEST(test_toggle_ordered_list_across_selected_lines) {
    Editor e("alpha\nbeta\ngamma");
    e.set_selection(Selection{CharOffset(0), CharOffset(16), TextAffinity::Downstream});
    Command c; c.kind = CommandKind::ToggleOrderedList;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("1. alpha\n2. beta\n3. gamma"));
    ELMD_CHECK_EQ(e.document().blocks.size(), 1u);
    ELMD_CHECK(e.document().blocks[0].list_ordered);
    ELMD_CHECK_EQ(e.document().blocks[0].list_items.size(), 3u);
}

ELMD_TEST(test_toggle_task_list_across_selected_lines_and_remove) {
    Editor e("alpha\nbeta");
    e.set_selection(Selection{CharOffset(0), CharOffset(10), TextAffinity::Downstream});
    Command c; c.kind = CommandKind::ToggleTaskList;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("- [ ] alpha\n- [ ] beta"));
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("alpha\nbeta"));
}

ELMD_TEST(test_empty_inline_format_commands_insert_editable_pairs) {
    Editor e;
    Command strong; strong.kind = CommandKind::ToggleStrong;
    e.execute_command(strong);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("****"));
    ELMD_CHECK_EQ(e.selection().head().v, 2u);
    Editor math;
    Command inlineMath; inlineMath.kind = CommandKind::InsertMathInline;
    math.execute_command(inlineMath);
    ELMD_CHECK_EQ(math.buffer().text_utf8(), std::string("$$"));
    ELMD_CHECK_EQ(math.selection().head().v, 1u);
}

ELMD_TEST(test_editor_auto_pairing_uses_semantic_transaction_pipeline) {
    Editor emphasis;
    auto firstEmphasis = emphasis.execute_command(Command::InsertText(U"*"));
    ELMD_CHECK(firstEmphasis.has_value());
    ELMD_CHECK_EQ(emphasis.buffer().text_utf8(), std::string("**"));
    ELMD_CHECK_EQ(emphasis.selection().head().v, 1u);
    auto secondEmphasis = emphasis.execute_command(Command::InsertText(U"*"));
    ELMD_CHECK(secondEmphasis.has_value());
    ELMD_CHECK_EQ(emphasis.buffer().text_utf8(), std::string("****"));
    ELMD_CHECK_EQ(emphasis.selection().head().v, 2u);

    Editor strike;
    strike.execute_command(Command::InsertText(U"~"));
    strike.execute_command(Command::InsertText(U"~"));
    ELMD_CHECK_EQ(strike.buffer().text_utf8(), std::string("~~~~"));
    ELMD_CHECK_EQ(strike.selection().head().v, 2u);

    Editor math;
    math.execute_command(Command::InsertText(U"$"));
    math.execute_command(Command::InsertText(U"$"));
    ELMD_CHECK_EQ(math.buffer().text_utf8(), std::string("$$$$"));
    ELMD_CHECK_EQ(math.selection().head().v, 2u);
    ELMD_CHECK(!math.document().blocks.empty());
    ELMD_CHECK(math.document().blocks.front().kind == BlockKind::MathBlock);

    Editor fence;
    fence.execute_command(Command::InsertText(U"`"));
    fence.execute_command(Command::InsertText(U"`"));
    fence.execute_command(Command::InsertText(U"`"));
    ELMD_CHECK_EQ(fence.buffer().text_utf8(), std::string("```\n\n```"));
    ELMD_CHECK_EQ(fence.selection().head().v, 4u);
    ELMD_CHECK(!fence.document().blocks.empty());
    ELMD_CHECK(fence.document().blocks.front().kind == BlockKind::CodeBlock);

    Editor deletion;
    deletion.execute_command(Command::InsertText(U"_"));
    Command backspace;
    backspace.kind = CommandKind::DeleteBackward;
    deletion.execute_command(backspace);
    ELMD_CHECK(deletion.buffer().text_utf8().empty());
    ELMD_CHECK_EQ(deletion.selection().head().v, 0u);
}

ELMD_TEST(test_inline_format_command_removes_surrounding_markers) {
    Editor e("**bold**");
    e.set_selection(Selection{CharOffset(2), CharOffset(6), TextAffinity::Downstream});
    Command command; command.kind = CommandKind::ToggleStrong;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("bold"));
    ELMD_CHECK_EQ(e.selection().normalized_range().start.v, 0u);
    ELMD_CHECK_EQ(e.selection().normalized_range().end.v, 4u);
}

ELMD_TEST(test_toggle_blockquote_across_selected_lines) {
    Editor e("alpha\nbeta");
    e.set_selection(Selection{CharOffset(0), CharOffset(10), TextAffinity::Downstream});
    Command command; command.kind = CommandKind::ToggleBlockQuote;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> alpha\n> beta"));
    e.execute_command(command);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("alpha\nbeta"));
}

ELMD_TEST(test_clear_heading_preserves_text) {
    Editor e("### title");
    e.set_caret(CharOffset(9));
    Command command; command.kind = CommandKind::ClearHeading;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("title"));
}

ELMD_TEST(test_insert_image_uses_selection_as_alt_text) {
    Editor e("diagram");
    e.set_selection(Selection{CharOffset(0), CharOffset(7), TextAffinity::Downstream});
    Command command; command.kind = CommandKind::InsertImage; command.path = U"chart.png";
    e.execute_command(command);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("![diagram](chart.png)"));
}

ELMD_TEST(test_insert_footnote_adds_unique_definition) {
    Editor e("alpha [^1]: old");
    e.set_caret(CharOffset(5));
    Command command; command.kind = CommandKind::InsertFootnote;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("alpha[^2] [^1]: old\n\n[^2]: "));
    ELMD_CHECK(std::any_of(e.document().blocks.begin(), e.document().blocks.end(), [](auto const& block) { return block.kind == BlockKind::FootnoteDefinition; }));
}

ELMD_TEST(test_toggle_callout_wraps_and_unwraps_selected_lines) {
    Editor e("alpha\nbeta");
    e.set_selection(Selection{CharOffset(0), CharOffset(10), TextAffinity::Downstream});
    Command command; command.kind = CommandKind::ToggleCallout; command.callout_kind = U"warning";
    e.execute_command(command);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> [!WARNING]\n> alpha\n> beta"));
    ELMD_CHECK(std::any_of(e.document().blocks.begin(), e.document().blocks.end(), [](auto const& block) { return block.kind == BlockKind::Callout; }));
    e.set_caret(CharOffset(20));
    e.execute_command(command);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("alpha\nbeta"));
}

ELMD_TEST(test_large_document_incremental_edit_keeps_distant_blocks) {
    std::string source;
    for (int index = 0; index < 1500; ++index) {
        source += "## Section " + std::to_string(index) + "\n\nParagraph " + std::to_string(index) + " with **formatting** and $x_" + std::to_string(index) + "$.\n\n";
    }
    Editor editor(source);
    auto position = source.find("Paragraph 750") + std::string("Paragraph 750").size();
    editor.set_caret(CharOffset(position));
    editor.execute_command(Command::InsertText(U" updated"));
    auto result = editor.buffer().text_utf8();
    ELMD_CHECK(result.find("Paragraph 750 updated") != std::string::npos);
    ELMD_CHECK(result.find("## Section 1499") != std::string::npos);
    ELMD_CHECK(editor.outline().flat_items().size() == 1500u);
}

ELMD_TEST(test_enter_after_thematic_break_creates_blank_lines_without_duplicate_rules) {
    Editor editor("---");
    editor.set_caret(CharOffset(3));
    Command enter; enter.kind = CommandKind::InsertNewline;
    auto check_structure = [&](std::size_t blank_count) {
        auto structure = build_source_structure(editor.document(), editor.text_cps());
        auto actual = std::count_if(structure.blocks.begin(), structure.blocks.end(), [](auto const& block) {
            return block.kind == SourceBlockKind::Blank;
        });
        ELMD_CHECK_EQ(actual, blank_count);
        auto range = editor.document().source_map.find_node_by_id(editor.document().blocks[0].id);
        ELMD_CHECK(range != nullptr);
        ELMD_CHECK_EQ(range->source_range.start.v, 0u);
        ELMD_CHECK_EQ(range->source_range.end.v, 3u);
    };
    editor.execute_command(enter);
    ELMD_CHECK_EQ(editor.buffer().text_utf8(), std::string("---\n"));
    ELMD_CHECK_EQ(editor.selection().head().v, 4u);
    check_structure(1);
    editor.execute_command(enter);
    ELMD_CHECK_EQ(editor.buffer().text_utf8(), std::string("---\n\n"));
    ELMD_CHECK_EQ(editor.selection().head().v, 5u);
    check_structure(2);
    editor.execute_command(enter);
    ELMD_CHECK_EQ(editor.buffer().text_utf8(), std::string("---\n\n\n"));
    ELMD_CHECK_EQ(editor.selection().head().v, 6u);
    check_structure(3);
    auto breaks = std::count_if(editor.document().blocks.begin(), editor.document().blocks.end(), [](auto const& block) {
        return block.kind == BlockKind::ThematicBreak;
    });
    ELMD_CHECK_EQ(breaks, 1);
}

ELMD_TEST(test_arrow_navigation_skips_thematic_break_source_marker) {
    Editor editor("---");
    editor.set_caret(CharOffset(0));
    editor.execute_command(Command::MoveRight(false));
    ELMD_CHECK_EQ(editor.selection().head().v, 3u);
    editor.execute_command(Command::MoveLeft(false));
    ELMD_CHECK_EQ(editor.selection().head().v, 0u);
}
