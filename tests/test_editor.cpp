import std;
#include "test_framework.h"
import elmd.core.editor;
import elmd.core.command;
import elmd.core.selection;
import elmd.core.transaction;
import elmd.core.utf;

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
    auto t = to_transaction(Command::InsertText(U" world"), e.text_cps(), Selection::caret(CharOffset(5)), 1);
    ELMD_CHECK(t && t->edits.size() == 1);
    auto s = t->apply_to(e.text_cps());
    ELMD_CHECK(cps_to_utf8(s) == "hello world");
}

ELMD_TEST(test_delete_transaction) {
    Editor e("hello world");
    auto text = e.text_cps();
    Command dc; dc.kind = CommandKind::DeleteSelection;
    Selection sel{CharOffset(5), CharOffset(11), TextAffinity::Downstream};
    auto t = to_transaction(dc, text, sel, 1);
    if (t) { auto s = t->apply_to(text); ELMD_CHECK(cps_to_utf8(s) == "hello"); }
    else ELMD_CHECK(false);
}

ELMD_TEST(test_toggle_strong_transaction) {
    Editor e("hello");
    auto text = e.text_cps();
    Selection sel{CharOffset(0), CharOffset(5), TextAffinity::Downstream};
    Command tc; tc.kind = CommandKind::ToggleStrong;
    auto t = to_transaction(tc, text, sel, 1);
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

ELMD_TEST(test_insert_toc_contains_marker) {
    Editor e;
    Command c; c.kind = CommandKind::InsertToc;
    e.execute_command(c);
    ELMD_CHECK(e.buffer().text_utf8().find("[TOC]") != std::string::npos);
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
