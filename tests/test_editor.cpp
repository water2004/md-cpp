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

ELMD_TEST(test_enter_continues_blockquote) {
    Editor e("> alpha");
    e.set_caret(CharOffset(7));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.buffer().text_utf8(), std::string("> alpha\n> "));
    ELMD_CHECK_EQ(e.selection().head().v, 10u);
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
