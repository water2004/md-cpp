import std;
#include "test_framework.h"
import elmd.core.editor;
import elmd.core.command;
import elmd.core.types;
import elmd.core.ast;
import elmd.core.source_map;
import elmd.core.parser;
import elmd.core.selection;
import elmd.core.utf;
import elmd.core.source_structure;
import elmd.core.render_builder;
import elmd.core.render_model;
import elmd.core.document_position;
import elmd.core.document_projection;
import elmd.core.document_edit;

using namespace elmd;

namespace {
struct TestTableCellProjection {
    TextRange<CharOffset> content_range;
    std::u32string text;
};

struct TestTableRowProjection {
    TextRange<CharOffset> line_range;
    std::vector<TestTableCellProjection> cells;
};

struct TestTableProjection {
    TextRange<CharOffset> range;
    std::vector<TestTableRowProjection> rows;
};

std::optional<TestTableProjection> table_projection(const Editor& editor) {
    if (editor.document().blocks.empty() || editor.document().blocks.front().kind != BlockKind::Table) return std::nullopt;
    const auto& block = editor.document().blocks.front();
    const auto* block_range = editor.document().source_map.find_node_by_id(block.id);
    if (!block_range) return std::nullopt;
    TestTableProjection result;
    result.range = block_range->source_range;
    const auto source = editor.text_cps();
    auto append_row = [&](const std::vector<TableCell>& cells, std::optional<NodeId> row_id) {
        TestTableRowProjection row;
        for (const auto& cell : cells) {
            const auto* range = editor.document().source_map.find_node_by_id(cell.id);
            if (!range) return false;
            row.cells.push_back(TestTableCellProjection{range->content_range, block_inline_text_content(cell.children)});
        }
        if (row_id) {
            const auto* range = editor.document().source_map.find_node_by_id(*row_id);
            if (!range) return false;
            row.line_range = range->source_range;
        } else if (!row.cells.empty()) {
            auto start = row.cells.front().content_range.start.v;
            while (start > 0 && source[start - 1] != U'\n') --start;
            auto end = row.cells.back().content_range.end.v;
            while (end < source.size() && source[end] != U'\n') ++end;
            row.line_range = TextRange<CharOffset>{CharOffset(start), CharOffset(end)};
        }
        result.rows.push_back(std::move(row));
        return true;
    };
    if (!append_row(block.table_header, std::nullopt)) return std::nullopt;
    result.rows.push_back(TestTableRowProjection{});
    for (const auto& row : block.table_rows) if (!append_row(row.cells, row.id)) return std::nullopt;
    return result;
}
}

ELMD_TEST(test_new_editor_empty) {
    Editor e;
    ELMD_CHECK(e.text_cps().empty());
}

ELMD_TEST(test_document_projection_does_not_reparse_incomplete_markdown) {
    EditorDocument document;
    document.revision = 7;
    BlockNode paragraph;
    paragraph.id = NodeId(1);
    paragraph.kind = BlockKind::Paragraph;
    paragraph.children.push_back(InlineNode::text_node(NodeId(2), U"\\["));
    document.blocks.push_back(std::move(paragraph));

    auto projection = project_document(document);
    ELMD_CHECK_EQ(projection.markdown, std::u32string(U"\\["));
    ELMD_CHECK(projection.source_map.find_node_by_id(NodeId(1)) != nullptr);
    auto* text_range = projection.source_map.find_node_by_id(NodeId(2));
    ELMD_CHECK(text_range != nullptr);
    if (text_range) {
        ELMD_CHECK_EQ(text_range->content_range.start.v, 0u);
        ELMD_CHECK_EQ(text_range->content_range.end.v, 2u);
    }
}

ELMD_TEST(test_document_projection_maps_empty_inline_structure_without_parser_rebinding) {
    EditorDocument document;
    document.revision = 3;
    BlockNode paragraph;
    paragraph.id = NodeId(10);
    paragraph.kind = BlockKind::Paragraph;
    InlineNode strong;
    strong.id = NodeId(11);
    strong.kind = InlineKind::Strong;
    strong.opening_marker = U"**";
    strong.closing_marker = U"**";
    paragraph.children.push_back(std::move(strong));
    document.blocks.push_back(std::move(paragraph));

    auto projection = project_document(document);
    ELMD_CHECK_EQ(projection.markdown, std::u32string(U"****"));
    auto* range = projection.source_map.find_node_by_id(NodeId(11));
    ELMD_CHECK(range != nullptr);
    if (range) {
        ELMD_CHECK_EQ(range->source_range.start.v, 0u);
        ELMD_CHECK_EQ(range->source_range.end.v, 4u);
        ELMD_CHECK_EQ(range->content_range.start.v, 2u);
        ELMD_CHECK_EQ(range->content_range.end.v, 2u);
        ELMD_CHECK_EQ(range->marker_ranges.size(), 2u);
    }
}

ELMD_TEST(test_document_projection_maps_nested_quote_and_list_nodes_directly) {
    EditorDocument document;
    document.revision = 5;
    BlockNode quote;
    quote.id = NodeId(20);
    quote.kind = BlockKind::BlockQuote;
    BlockNode quoted_paragraph;
    quoted_paragraph.id = NodeId(21);
    quoted_paragraph.kind = BlockKind::Paragraph;
    quoted_paragraph.children.push_back(InlineNode::text_node(NodeId(22), U"alpha"));
    quote.quote_children.push_back(std::move(quoted_paragraph));
    document.blocks.push_back(std::move(quote));

    BlockNode list;
    list.id = NodeId(30);
    list.kind = BlockKind::List;
    ListItem item;
    item.id = NodeId(31);
    item.marker = U"- ";
    BlockNode item_paragraph;
    item_paragraph.id = NodeId(32);
    item_paragraph.kind = BlockKind::Paragraph;
    item_paragraph.children.push_back(InlineNode::text_node(NodeId(33), U"beta"));
    item.children.push_back(std::move(item_paragraph));
    list.list_items.push_back(std::move(item));
    document.blocks.push_back(std::move(list));

    auto projection = project_document(document);
    document.source_map = projection.source_map;
    ELMD_CHECK_EQ(projection.markdown, std::u32string(U"> alpha\n\n- beta"));
    for (auto id : {20u, 21u, 22u, 30u, 31u, 32u, 33u}) {
        ELMD_CHECK(document.source_map.find_node_by_id(NodeId(id)) != nullptr);
    }
    auto quoted = document_position_from_source_offset(document, CharOffset(4));
    ELMD_CHECK(quoted.has_value());
    if (quoted) {
        ELMD_CHECK_EQ(quoted->node_id, NodeId(21));
        ELMD_CHECK_EQ(quoted->offset, 2u);
    }
    auto listed = document_position_from_source_offset(document, CharOffset(13));
    ELMD_CHECK(listed.has_value());
    if (listed) {
        ELMD_CHECK_EQ(listed->node_id, NodeId(32));
        ELMD_CHECK_EQ(listed->offset, 2u);
    }
}

ELMD_TEST(test_editor_document_enter_keeps_ast_authoritative_and_projects_markdown) {
    Editor editor("alphaomega");
    ELMD_CHECK(!editor.document().blocks.empty());
    if (editor.document().blocks.empty()) return;
    const auto original_id = editor.document().blocks.front().id;
    auto transaction = editor.execute_document_enter(DocumentSelection::caret(
        DocumentPosition{original_id, 5, TextAffinity::Downstream}));

    ELMD_CHECK(transaction);
    if (!transaction) return;
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("alpha\n\nomega"));
    ELMD_CHECK_EQ(editor.document().blocks.size(), 2u);
    ELMD_CHECK_EQ(editor.document().blocks.front().id, original_id);
    ELMD_CHECK_EQ(editor.document_selection().active.node_id, editor.document().blocks[1].id);
    ELMD_CHECK(editor.document().source_map.find_node_by_id(editor.document().blocks[1].id) != nullptr);

    ELMD_CHECK(editor.undo_document());
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("alphaomega"));
    ELMD_CHECK_EQ(editor.document().blocks.size(), 1u);
    ELMD_CHECK_EQ(editor.document().blocks.front().id, original_id);

    ELMD_CHECK(editor.redo_document());
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("alpha\n\nomega"));
    ELMD_CHECK_EQ(editor.document().blocks.size(), 2u);
}

ELMD_TEST(test_editor_newline_command_uses_document_transaction_for_top_level_paragraph) {
    Editor editor("alphaomega");
    const auto original_id = editor.document().blocks.front().id;
    editor.set_caret(CharOffset(5));
    Command newline;
    newline.kind = CommandKind::InsertNewline;

    auto result = editor.execute_command(newline);
    ELMD_CHECK(result);
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("alpha\n\nomega"));
    ELMD_CHECK_EQ(editor.document().blocks.front().id, original_id);
    ELMD_CHECK(editor.has_document_undo());

    auto undone = editor.undo();
    ELMD_CHECK(undone);
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("alphaomega"));
    ELMD_CHECK_EQ(editor.document().blocks.front().id, original_id);
}

ELMD_TEST(test_editor_document_delete_projects_and_restores_node_selection) {
    Editor editor("alphabeta");
    const auto block_id = editor.document().blocks.front().id;
    auto transaction = editor.execute_document_delete_backward(DocumentSelection::caret(
        DocumentPosition{block_id, 5, TextAffinity::Downstream}));
    ELMD_CHECK(transaction);
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("alphbeta"));
    ELMD_CHECK_EQ(editor.document().blocks.front().id, block_id);
    ELMD_CHECK(editor.undo_document());
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("alphabeta"));
    ELMD_CHECK_EQ(editor.document_selection().active.offset, 5u);
}

ELMD_TEST(test_editor_backspace_command_moves_list_item_tree_and_undo_restores_it) {
    Editor editor("- one\n- two");
    const auto second_id = editor.document().blocks[0].list_items[1].children[0].id;
    editor.set_document_selection(DocumentSelection::caret(
        DocumentPosition{second_id, 0, TextAffinity::Downstream}));
    Command backspace;
    backspace.kind = CommandKind::DeleteBackward;
    auto transaction = editor.execute_command(backspace);
    ELMD_CHECK(transaction);
    ELMD_CHECK_EQ(editor.document().blocks[0].list_items.size(), 1u);
    ELMD_CHECK_EQ(editor.document().blocks[0].list_items[0].children.size(), 2u);
    ELMD_CHECK_EQ(editor.document().blocks[0].list_items[0].children[1].id, second_id);
    ELMD_CHECK_EQ(editor.document_selection().active.node_id, second_id);
    ELMD_CHECK(editor.undo_document());
    ELMD_CHECK_EQ(editor.document().blocks[0].list_items.size(), 2u);
    ELMD_CHECK(editor.redo_document());
    ELMD_CHECK_EQ(editor.document().blocks[0].list_items.size(), 1u);
}

ELMD_TEST(test_editor_list_indent_commands_use_document_history) {
    Editor editor("- a\n- b");
    const auto second_id = editor.document().blocks[0].list_items[1].children[0].id;
    editor.set_document_selection(DocumentSelection::caret(
        DocumentPosition{second_id, 1, TextAffinity::Downstream}));
    Command indent;
    indent.kind = CommandKind::IndentListItem;
    ELMD_CHECK(editor.execute_command(indent));
    ELMD_CHECK_EQ(editor.markdown_utf8(), std::string("- a\n  - b"));
    ELMD_CHECK_EQ(editor.document().blocks[0].list_items.size(), 1u);
    ELMD_CHECK(editor.undo_document());
    ELMD_CHECK_EQ(editor.markdown_utf8(), std::string("- a\n- b"));
    ELMD_CHECK(editor.redo_document());
    Command outdent;
    outdent.kind = CommandKind::OutdentListItem;
    ELMD_CHECK(editor.execute_command(outdent));
    ELMD_CHECK_EQ(editor.markdown_utf8(), std::string("- a\n- b"));
    ELMD_CHECK_EQ(editor.document_selection().active.node_id, second_id);
    ELMD_CHECK_EQ(editor.document_selection().active.offset, 1u);
}

ELMD_TEST(test_editor_cross_container_selection_delete_uses_document_history) {
    Editor editor("> alpha\n\nomega");
    const auto quote_id = editor.document().blocks[0].quote_children[0].id;
    const auto paragraph_id = editor.document().blocks[1].id;
    DocumentSelection selection{
        DocumentPosition{quote_id, 2, TextAffinity::Downstream},
        DocumentPosition{paragraph_id, 3, TextAffinity::Downstream}};
    editor.set_document_selection(selection);
    Command command;
    command.kind = CommandKind::DeleteSelection;
    ELMD_CHECK(editor.execute_command(command));
    ELMD_CHECK_EQ(editor.markdown_utf8(), std::string("> alga"));
    ELMD_CHECK_EQ(editor.document_selection().active.node_id, quote_id);
    ELMD_CHECK_EQ(editor.document_selection().active.offset, 2u);
    ELMD_CHECK(editor.undo_document());
    ELMD_CHECK_EQ(editor.markdown_utf8(), std::string("> alpha\n\nomega"));
    ELMD_CHECK_EQ(editor.document_selection().anchor.node_id, quote_id);
    ELMD_CHECK_EQ(editor.document_selection().active.node_id, paragraph_id);
}

ELMD_TEST(test_editor_enter_at_paragraph_end_projects_empty_node_caret_anchor) {
    Editor editor("alpha");
    const auto first_id = editor.document().blocks.front().id;
    editor.set_caret(CharOffset(5));
    Command newline;
    newline.kind = CommandKind::InsertNewline;
    auto transaction = editor.execute_command(newline);

    ELMD_CHECK(transaction);
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("alpha\n\n"));
    ELMD_CHECK_EQ(editor.document().blocks.size(), 2u);
    ELMD_CHECK_EQ(editor.document().blocks.front().id, first_id);
    ELMD_CHECK(editor.document().source_map.find_node_by_id(editor.document().blocks[1].id) != nullptr);
    ELMD_CHECK_EQ(editor.selection().active.v, 7u);
}

ELMD_TEST(test_editor_document_range_delete_restores_ast_and_selection_on_undo) {
    Editor editor("alpha\n\nomega");
    const auto first_id = editor.document().blocks[0].id;
    const auto last_id = editor.document().blocks[1].id;
    DocumentSelection selection{
        DocumentPosition{first_id, 2, TextAffinity::Downstream},
        DocumentPosition{last_id, 3, TextAffinity::Downstream}};
    auto transaction = editor.execute_document_delete_selection(selection);

    ELMD_CHECK(transaction);
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("alga"));
    ELMD_CHECK_EQ(editor.document().blocks.size(), 1u);
    ELMD_CHECK_EQ(editor.document().blocks[0].id, first_id);
    ELMD_CHECK(editor.undo_document());
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("alpha\n\nomega"));
    ELMD_CHECK_EQ(editor.document().blocks.size(), 2u);
    {
        ELMD_CHECK_EQ(editor.document_selection().anchor.node_id, first_id);
        ELMD_CHECK_EQ(editor.document_selection().active.node_id, last_id);
    }
}

ELMD_TEST(test_document_position_projection_skips_inline_markers) {
    Editor editor("before **alpha** after");
    const auto block_id = editor.document().blocks.front().id;
    auto inside = document_position_from_source_offset(editor.document(), CharOffset(11));
    ELMD_CHECK(inside.has_value());
    if (!inside) return;
    ELMD_CHECK_EQ(inside->node_id, block_id);
    ELMD_CHECK_EQ(inside->offset, 9u);
    auto source = source_offset_from_document_position(editor.document(), *inside);
    ELMD_CHECK(source.has_value());
    if (source) ELMD_CHECK_EQ(source->v, 11u);
}

ELMD_TEST(test_editor_insert_text) {
    Editor e;
    e.execute_command(Command::InsertText(U"hello"));
    ELMD_CHECK_EQ(e.text_utf8(), std::string("hello"));
}

ELMD_TEST(test_editor_incremental_unclosed_bracket_math) {
    Editor e;
    ELMD_CHECK(e.execute_command(Command::InsertText(U"\\")));
    ELMD_CHECK(e.execute_command(Command::InsertText(U"[")));
    ELMD_CHECK_EQ(e.text_utf8(), std::string("\\["));
    auto model = build_render_model(e.document(), e.text_utf8(), e.outline());
    ELMD_CHECK_EQ(model.blocks.size(), 1u);
    ELMD_CHECK(model.blocks[0].kind == RenderBlockKind::Text);
    ELMD_CHECK(e.document().blocks[0].kind == BlockKind::Paragraph);
}

ELMD_TEST(test_editor_incremental_inline_math_keeps_exact_source_mapping) {
    Editor e;
    for (char32_t ch : std::u32string_view(U"$y=x$")) {
        ELMD_CHECK(e.execute_command(Command::InsertText(std::u32string(1, ch))));
    }
    auto model = build_render_model(e.document(), e.text_utf8(), e.outline());
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
        ELMD_CHECK(e.execute_command(Command::MoveRight(false)));
        ELMD_CHECK_EQ(e.selection().active.v, expected);
    }
    for (std::size_t expected = 4; expected < 5; --expected) {
        ELMD_CHECK(e.execute_command(Command::MoveLeft(false)));
        ELMD_CHECK_EQ(e.selection().active.v, expected);
        if (expected == 0) break;
    }
}

ELMD_TEST(test_inline_marker_navigation_preserves_each_serialized_caret_slot) {
    for (const auto& markdown : {std::string("_word_"), std::string("**x**"), std::string("`code`")}) {
        Editor editor(markdown);
        editor.set_caret(CharOffset(0));
        for (std::size_t expected = 1; expected <= markdown.size(); ++expected) {
            ELMD_CHECK(editor.execute_command(Command::MoveRight(false)));
            ELMD_CHECK_EQ(editor.selection().active.v, expected);
        }
        for (std::size_t expected = markdown.size(); expected > 0; --expected) {
            ELMD_CHECK(editor.execute_command(Command::MoveLeft(false)));
            ELMD_CHECK_EQ(editor.selection().active.v, expected - 1);
        }
    }
}

ELMD_TEST(test_editor_undo_redo) {
    Editor e;
    e.execute_command(Command::InsertText(U"hello"));
    e.execute_command(Command::InsertText(U" world"));
    ELMD_CHECK_EQ(e.text_utf8(), std::string("hello world"));
    e.undo();
    ELMD_CHECK_EQ(e.text_utf8(), std::string("hello"));
    e.redo();
    ELMD_CHECK_EQ(e.text_utf8(), std::string("hello world"));
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
    ELMD_CHECK_EQ(e.text_utf8(), std::string("ab"));
    ELMD_CHECK(e.revision() == 4u); // unchanged (always++)
    e.redo();
    ELMD_CHECK(e.revision() == 5u);
}

ELMD_TEST(test_editor_toggle_strong) {
    Editor e("bold");
    e.set_selection(Selection{CharOffset(0), CharOffset(4), TextAffinity::Downstream});
    Command c; c.kind = CommandKind::ToggleStrong;
    e.execute_command(c);
    ELMD_CHECK(e.text_utf8() == "**bold**");
}

ELMD_TEST(test_editor_delete_backward) {
    Editor e("hello");
    e.set_caret(CharOffset(5));
    Command c; c.kind = CommandKind::DeleteBackward;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("hell"));
    ELMD_CHECK(e.selection().affinity == TextAffinity::Upstream);
}

ELMD_TEST(test_editor_insert_text_cursor_moves) {
    Editor e("123");
    e.set_caret(CharOffset(1));
    e.execute_command(Command::InsertText(U" "));
    ELMD_CHECK_EQ(e.text_utf8(), std::string("1 23"));
    ELMD_CHECK_EQ(e.selection().head().v, 2u);
}

ELMD_TEST(test_editor_insert_math_inline) {
    Editor e("x2");
    e.set_selection(Selection{CharOffset(0), CharOffset(2), TextAffinity::Downstream});
    Command c; c.kind = CommandKind::InsertMathInline;
    e.execute_command(c);
    ELMD_CHECK(e.text_utf8() == "$x2$");
}

ELMD_TEST(test_editor_insert_math_block) {
    Editor e;
    Command c; c.kind = CommandKind::InsertMathBlock;
    e.execute_command(c);
    auto s = e.text_utf8();
    ELMD_CHECK(s.find("$$") != std::string::npos);
}

ELMD_TEST(test_atomic_block_commands_insert_document_nodes) {
    Editor math;
    Command math_command; math_command.kind = CommandKind::InsertMathBlock;
    math.execute_command(math_command);
    ELMD_CHECK(math.document().blocks.front().kind == BlockKind::MathBlock);
    ELMD_CHECK_EQ(math.document_selection().active.node_id, math.document().blocks.front().id);
    ELMD_CHECK(validate_document(math.document()).empty());
    math.undo();
    ELMD_CHECK(math.document().blocks.front().kind == BlockKind::Paragraph);

    Editor table;
    Command table_command; table_command.kind = CommandKind::InsertTable; table_command.rows = 2; table_command.cols = 3;
    table.execute_command(table_command);
    ELMD_CHECK(table.document().blocks.front().kind == BlockKind::Table);
    ELMD_CHECK_EQ(table.document().blocks.front().table_header.size(), 3u);
    ELMD_CHECK_EQ(table.document().blocks.front().table_rows.size(), 2u);
    ELMD_CHECK_EQ(table.document_selection().active.node_id, table.document().blocks.front().table_header.front().id);
    ELMD_CHECK(validate_document(table.document()).empty());
}

ELMD_TEST(test_insert_text_uses_document_transaction) {
    Editor e("hello");
    e.set_caret(CharOffset(5));
    const auto paragraph_id = e.document().blocks.front().id;
    auto transaction = e.execute_command(Command::InsertText(U" world"));
    ELMD_CHECK(transaction);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("hello world"));
    ELMD_CHECK_EQ(e.document().blocks.front().id, paragraph_id);
    ELMD_CHECK(e.has_document_undo());
    e.undo();
    ELMD_CHECK_EQ(e.text_utf8(), std::string("hello"));
}

ELMD_TEST(test_delete_transaction) {
    Editor e("hello world");
    const auto paragraph_id = e.document().blocks.front().id;
    e.set_document_selection(DocumentSelection{
        DocumentPosition{paragraph_id, 5, TextAffinity::Downstream},
        DocumentPosition{paragraph_id, 11, TextAffinity::Downstream}});
    Command command; command.kind = CommandKind::DeleteSelection;
    ELMD_CHECK(e.execute_command(command));
    ELMD_CHECK_EQ(e.markdown_utf8(), std::string("hello"));
    ELMD_CHECK(e.has_document_undo());
}

ELMD_TEST(test_paste_command_is_one_document_history_transaction) {
    Editor editor("alpha");
    editor.set_caret(CharOffset(5));
    Command paste;
    paste.kind = CommandKind::Paste;
    paste.text = U"x\r\ny";
    auto transaction = editor.execute_command(paste);
    ELMD_CHECK(transaction);
    ELMD_CHECK_EQ(editor.markdown_utf8(), std::string("alphax\n\ny"));
    ELMD_CHECK_EQ(editor.document().blocks.size(), 2u);
    ELMD_CHECK_EQ(editor.document_selection().active.node_id, editor.document().blocks[1].id);
    ELMD_CHECK(editor.has_document_undo());
    ELMD_CHECK(editor.undo_document());
    ELMD_CHECK_EQ(editor.markdown_utf8(), std::string("alpha"));
    ELMD_CHECK_EQ(editor.document().blocks.size(), 1u);
    ELMD_CHECK_EQ(editor.document_selection().active.offset, 5u);
}

ELMD_TEST(test_toggle_strong_uses_document_transaction) {
    Editor e("hello");
    const auto paragraph_id = e.document().blocks.front().id;
    e.set_selection(Selection{CharOffset(0), CharOffset(5), TextAffinity::Downstream});
    Command command; command.kind = CommandKind::ToggleStrong;
    auto transaction = e.execute_command(command);
    ELMD_CHECK(transaction);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("**hello**"));
    ELMD_CHECK_EQ(e.document().blocks.front().id, paragraph_id);
    ELMD_CHECK(e.document().blocks.front().children.front().kind == InlineKind::Strong);
    e.undo();
    ELMD_CHECK_EQ(e.text_utf8(), std::string("hello"));
}

ELMD_TEST(test_inline_format_commands_wrap_and_unwrap_inline_subtrees) {
    Editor emphasis("alpha");
    const auto paragraph_id = emphasis.document().blocks.front().id;
    emphasis.set_document_selection(DocumentSelection{
        DocumentPosition{paragraph_id, 1, TextAffinity::Downstream},
        DocumentPosition{paragraph_id, 4, TextAffinity::Downstream}});
    Command command; command.kind = CommandKind::ToggleEmphasis;
    emphasis.execute_command(command);
    ELMD_CHECK_EQ(emphasis.text_utf8(), std::string("a*lph*a"));
    ELMD_CHECK_EQ(emphasis.document().blocks.front().id, paragraph_id);
    ELMD_CHECK(emphasis.document().blocks.front().children[1].kind == InlineKind::Emphasis);
    emphasis.execute_command(command);
    ELMD_CHECK_EQ(emphasis.text_utf8(), std::string("alpha"));

    Editor code("value");
    code.set_document_selection(DocumentSelection{
        DocumentPosition{code.document().blocks.front().id, 0, TextAffinity::Downstream},
        DocumentPosition{code.document().blocks.front().id, 5, TextAffinity::Downstream}});
    command.kind = CommandKind::ToggleInlineCode;
    code.execute_command(command);
    ELMD_CHECK_EQ(code.text_utf8(), std::string("`value`"));
    ELMD_CHECK(code.document().blocks.front().children.front().kind == InlineKind::InlineCode);

    Editor math("x+y");
    math.set_document_selection(DocumentSelection{
        DocumentPosition{math.document().blocks.front().id, 0, TextAffinity::Downstream},
        DocumentPosition{math.document().blocks.front().id, 3, TextAffinity::Downstream}});
    command.kind = CommandKind::InsertMathInline;
    math.execute_command(command);
    ELMD_CHECK_EQ(math.text_utf8(), std::string("$x+y$"));
    ELMD_CHECK(math.document().blocks.front().children.front().kind == InlineKind::InlineMath);
}

ELMD_TEST(test_typing_into_empty_format_node_stays_before_closing_marker) {
    Editor editor;
    Command strong; strong.kind = CommandKind::ToggleStrong;
    editor.execute_command(strong);
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("****"));
    editor.execute_command(Command::InsertText(U"x"));
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("**x**"));
    ELMD_CHECK_EQ(editor.selection().head().v, 3u);
    ELMD_CHECK(editor.document().blocks.front().children.front().kind == InlineKind::Strong);
    editor.undo();
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("****"));
    editor.undo();
    ELMD_CHECK(editor.text_utf8().empty());
}

ELMD_TEST(test_inline_format_inside_table_cell_is_ast_native) {
    Editor editor("| H |\n| --- |\n| X |");
    const auto cell_id = editor.document().blocks.front().table_rows.front().cells.front().id;
    editor.set_document_selection(DocumentSelection{
        DocumentPosition{cell_id, 0, TextAffinity::Downstream},
        DocumentPosition{cell_id, 1, TextAffinity::Downstream}});
    Command strong; strong.kind = CommandKind::ToggleStrong;
    editor.execute_command(strong);
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("| H |\n| --- |\n| **X** |"));
    ELMD_CHECK(editor.document().blocks.front().table_rows.front().cells.front().children.front().kind == InlineKind::Strong);
    editor.undo();
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("| H |\n| --- |\n| X |"));
    ELMD_CHECK_EQ(editor.document().blocks.front().table_rows.front().cells.front().id, cell_id);
}

ELMD_TEST(test_insert_table_contains_header) {
    Editor e;
    Command c; c.kind = CommandKind::InsertTable; c.rows = 2; c.cols = 3;
    e.execute_command(c);
    auto s = e.text_utf8();
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
    ELMD_CHECK_EQ(e.text_utf8(), std::string("| A | B |\n| --- | --- |\n| C | D |\n|  |  |"));
}

ELMD_TEST(test_table_delete_column) {
    Editor e("| A | B |\n| --- | --- |\n| C | D |\n");
    e.set_caret(CharOffset(6));
    Command c; c.kind = CommandKind::DeleteTableColumn;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("| A |\n| --- |\n| C |"));
}

ELMD_TEST(test_table_move_column_left) {
    Editor e("| A | B |\n| --- | --- |\n| C | D |\n");
    e.set_caret(CharOffset(6));
    Command c; c.kind = CommandKind::MoveTableColumnLeft;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("| B | A |\n| --- | --- |\n| D | C |"));
}

ELMD_TEST(test_table_column_alignment_is_preserved_by_structural_edits) {
    Editor e("| A | B |\n| --- | --- |\n| 1 | 2 |\n");
    e.set_caret(CharOffset(6));
    Command align; align.kind = CommandKind::SetTableColumnAlignment; align.table_alignment = TableAlignment::Right;
    e.execute_command(align);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("| A | B |\n| --- | ---: |\n| 1 | 2 |"));
    ELMD_CHECK_EQ(e.selection().head().v, 6u);
    Command move; move.kind = CommandKind::MoveTableColumnLeft;
    e.execute_command(move);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("| B | A |\n| ---: | --- |\n| 2 | 1 |"));
}

ELMD_TEST(test_normalize_table_canonicalizes_widths_without_losing_alignment) {
    Editor e("|Long|B|\n|:--|--:|\n|1|2|\n");
    e.set_caret(CharOffset(2));
    Command c; c.kind = CommandKind::NormalizeTable;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("| Long | B |\n| :--- | ---: |\n| 1 | 2 |"));
}

ELMD_TEST(test_table_insert_row_and_column_at_visual_boundaries) {
    Editor e("| A | B |\n| --- | --- |\n| 1 | 2 |\n");
    e.set_caret(CharOffset(2));
    Command row; row.kind = CommandKind::InsertTableRowAt; row.table_index = 1;
    e.execute_command(row);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("| A | B |\n| --- | --- |\n|  |  |\n| 1 | 2 |"));
    Command column; column.kind = CommandKind::InsertTableColumnAt; column.table_index = 1;
    e.execute_command(column);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("| A |  | B |\n| --- | --- | --- |\n|  |  |  |\n| 1 |  | 2 |"));
}

ELMD_TEST(test_table_drag_moves_row_and_column_atomically) {
    Editor e("| A | B |\n| --- | --- |\n| 1 | 2 |\n");
    e.set_caret(CharOffset(29));
    Command row; row.kind = CommandKind::MoveTableRowTo; row.table_index = 0;
    e.execute_command(row);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("| 1 | 2 |\n| --- | --- |\n| A | B |"));
    e.set_caret(CharOffset(8));
    Command column; column.kind = CommandKind::MoveTableColumnTo; column.table_index = 0;
    e.execute_command(column);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("| 2 | 1 |\n| --- | --- |\n| B | A |"));
}

ELMD_TEST(test_table_structure_commands_preserve_ast_nodes_and_document_history) {
    Editor editor("| A | B |\n| --- | --- |\n| 1 | 2 |");
    const auto table_id = editor.document().blocks.front().id;
    const auto first_cell_id = editor.document().blocks.front().table_header.front().id;
    const auto second_cell_id = editor.document().blocks.front().table_header[1].id;
    editor.set_document_selection(DocumentSelection::caret(
        DocumentPosition{second_cell_id, 0, TextAffinity::Downstream}));
    Command move; move.kind = CommandKind::MoveTableColumnLeft;
    editor.execute_command(move);
    ELMD_CHECK_EQ(editor.document().blocks.front().id, table_id);
    ELMD_CHECK_EQ(editor.document().blocks.front().table_header[0].id, second_cell_id);
    ELMD_CHECK_EQ(editor.document().blocks.front().table_header[1].id, first_cell_id);
    ELMD_CHECK(editor.has_document_undo());
    ELMD_CHECK(validate_document(editor.document()).empty());
    editor.undo();
    ELMD_CHECK_EQ(editor.document().blocks.front().table_header[0].id, first_cell_id);
    ELMD_CHECK_EQ(editor.document().blocks.front().table_header[1].id, second_cell_id);
}

ELMD_TEST(test_typing_into_empty_table_cell_updates_source_and_ast) {
    Editor e("| A | B |\n| --- | --- |\n|   | 2 |\n");
    auto table = table_projection(e);
    ELMD_CHECK(table.has_value());
    ELMD_CHECK(table && table->rows.size() == 3);
    if (!table || table->rows.size() != 3) return;
    ELMD_CHECK(table->rows[2].cells[0].text.empty());
    ELMD_CHECK_EQ(table->rows[2].cells[0].content_range.len(), 0u);
    auto emptyCellOffset = table->rows[2].cells[0].content_range.start;
    e.set_caret(emptyCellOffset);
    e.execute_command(Command::InsertText(U"x"));
    ELMD_CHECK_EQ(e.text_utf8(), std::string("| A | B |\n| --- | --- |\n| x | 2 |\n"));
    ELMD_CHECK_EQ(e.document().blocks.size(), 2u);
    ELMD_CHECK(e.document().blocks[0].kind == BlockKind::Table);
    auto const& row = e.document().blocks[0].table_rows[0];
    ELMD_CHECK_EQ(row.cells.size(), 2u);
    ELMD_CHECK_EQ(cps_to_utf8(block_inline_text_content(row.cells[0].children)), std::string("x"));
}

ELMD_TEST(test_enter_at_end_of_table_exits_after_closing_pipe) {
    Editor e("| H1 | H2 |\n| --- | --- |\n| A | B |");
    auto table = table_projection(e);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    auto const& last_cell = table->rows.back().cells.back();
    e.set_caret(last_cell.content_range.end);
    Command command;
    command.kind = CommandKind::InsertNewline;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("| H1 | H2 |\n| --- | --- |\n| A | B |\n\n"));
    ELMD_CHECK_EQ(e.selection().head().v, e.text_cps().size());
}

ELMD_TEST(test_enter_at_end_of_table_reuses_trailing_newline) {
    Editor e("| H |\n| --- |\n| A |\n");
    auto table = table_projection(e);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    e.set_caret(table->rows.back().cells.back().content_range.end);
    Command command;
    command.kind = CommandKind::InsertNewline;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("| H |\n| --- |\n| A |\n\n"));
    ELMD_CHECK_EQ(e.selection().head().v, e.text_cps().size());
}

ELMD_TEST(test_enter_in_table_moves_to_same_column_of_next_row) {
    Editor e("| H1 | H2 |\n| --- | --- |\n| A1 | A2 |\n| B1 | B2 |");
    auto table = table_projection(e);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    auto source_before = e.text_utf8();
    e.set_caret(table->rows[2].cells[1].content_range.end);
    Command command;
    command.kind = CommandKind::InsertNewline;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.text_utf8(), source_before);
    ELMD_CHECK_EQ(e.selection().head().v, table->rows[3].cells[1].content_range.start.v);
}

ELMD_TEST(test_enter_in_table_header_skips_separator_row) {
    Editor e("| H1 | H2 |\n| --- | --- |\n| A1 | A2 |");
    auto table = table_projection(e);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    auto source_before = e.text_utf8();
    e.set_caret(table->rows[0].cells[0].content_range.start);
    Command command;
    command.kind = CommandKind::InsertNewline;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.text_utf8(), source_before);
    ELMD_CHECK_EQ(e.selection().head().v, table->rows[2].cells[0].content_range.start.v);
}

ELMD_TEST(test_enter_in_any_cell_of_last_table_row_exits_table) {
    Editor e("| H1 | H2 |\n| --- | --- |\n| A1 | A2 |");
    auto table = table_projection(e);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    e.set_caret(table->rows.back().cells[0].content_range.start);
    Command command;
    command.kind = CommandKind::InsertNewline;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("| H1 | H2 |\n| --- | --- |\n| A1 | A2 |\n\n"));
    ELMD_CHECK_EQ(e.selection().head().v, e.text_cps().size());
}

ELMD_TEST(test_empty_table_cell_has_zero_width_content_after_leading_padding) {
    Editor e("| H |\n| --- |\n|     |");
    auto table = table_projection(e);
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
    auto table = table_projection(e);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    auto offset = table->rows.back().cells.front().content_range.start;
    auto source = e.text_utf8();
    e.set_caret(offset);
    Command backward;
    backward.kind = CommandKind::DeleteBackward;
    ELMD_CHECK(!e.execute_command(backward));
    ELMD_CHECK_EQ(e.text_utf8(), source);
    Command forward;
    forward.kind = CommandKind::DeleteForward;
    ELMD_CHECK(!e.execute_command(forward));
    ELMD_CHECK_EQ(e.text_utf8(), source);
}

ELMD_TEST(test_table_character_delete_stays_inside_cell_content) {
    Editor e("| H |\n| --- |\n| ABC |");
    auto table = table_projection(e);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    auto cell = table->rows.back().cells.front();
    e.set_caret(cell.content_range.end);
    Command backward;
    backward.kind = CommandKind::DeleteBackward;
    e.execute_command(backward);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("| H |\n| --- |\n| AB |"));
    table = table_projection(e);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    e.set_caret(table->rows.back().cells.front().content_range.start);
    Command forward;
    forward.kind = CommandKind::DeleteForward;
    e.execute_command(forward);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("| H |\n| --- |\n| B |"));
    ELMD_CHECK(e.document().blocks[0].kind == BlockKind::Table);
}

ELMD_TEST(test_deleting_last_table_cell_character_keeps_empty_caret_stable) {
    Editor e("| H |\n| --- |\n| X |");
    auto table = table_projection(e);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    e.set_caret(table->rows.back().cells.front().content_range.end);
    Command command;
    command.kind = CommandKind::DeleteBackward;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("| H |\n| --- |\n|  |"));
    table = table_projection(e);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    auto const& empty = table->rows.back().cells.front();
    ELMD_CHECK_EQ(empty.content_range.len(), 0u);
    ELMD_CHECK_EQ(e.selection().head().v, empty.content_range.start.v);
    ELMD_CHECK(e.document().blocks[0].kind == BlockKind::Table);
}

ELMD_TEST(test_delete_from_adjacent_paragraph_does_not_consume_table_boundary) {
    Editor e("| H |\n| --- |\n| A |\n\nafter");
    auto table = table_projection(e);
    ELMD_CHECK(table.has_value());
    if (!table) return;
    auto source = e.text_utf8();
    e.set_caret(CharOffset(table->range.end.v));
    Command command;
    command.kind = CommandKind::DeleteBackward;
    ELMD_CHECK(!e.execute_command(command));
    ELMD_CHECK_EQ(e.text_utf8(), source);
    ELMD_CHECK(e.document().blocks[0].kind == BlockKind::Table);
}

ELMD_TEST(test_table_cross_cell_selection_delete_preserves_structure) {
    Editor e("| H1 | H2 |\n| --- | --- |\n| A1 | A2 |");
    auto table = table_projection(e);
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
    ELMD_CHECK_EQ(e.text_utf8(), std::string("| H1 | H2 |\n| --- | --- |\n|  |  |"));
    ELMD_CHECK(e.document().blocks[0].kind == BlockKind::Table);
}

ELMD_TEST(test_insert_toc_contains_marker) {
    Editor e;
    Command c; c.kind = CommandKind::InsertToc;
    e.execute_command(c);
    ELMD_CHECK(e.text_utf8().find("[TOC]") != std::string::npos);
}

ELMD_TEST(test_enter_in_plain_paragraph_inserts_semantic_block_break) {
    Editor e("alphaomega");
    e.set_caret(CharOffset(5));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("alpha\n\nomega"));
    ELMD_CHECK_EQ(e.selection().head().v, 7u);
    ELMD_CHECK_EQ(e.document().blocks.size(), 2u);
    auto structure = build_source_structure(e.document());
    ELMD_CHECK_EQ(structure.blocks.size(), 2u);
    ELMD_CHECK(structure.blocks[0].kind == SourceBlockKind::Semantic);
    ELMD_CHECK(structure.blocks[1].kind == SourceBlockKind::Semantic);
}

ELMD_TEST(test_enter_at_block_end_reuses_existing_separator_and_creates_one_blank) {
    Editor e("# H\n\n## Next");
    e.set_caret(CharOffset(3));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("# H\n\n\n## Next"));
    ELMD_CHECK_EQ(e.selection().head().v, 4u);
    ELMD_CHECK_EQ(e.document().blocks.size(), 2u);
    auto structure = build_source_structure(e.document());
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
        auto full = parse_text(e.revision(), e.text_utf8());
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
        auto structure = build_source_structure(e.document());
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
    ELMD_CHECK_EQ(e.text_utf8(), std::string("before\n```cpp\n```\n"));
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
    ELMD_CHECK_EQ(e.text_utf8(), std::string("```\nabc\n\n```\n"));
    ELMD_CHECK_EQ(e.selection().head().v, 8u);
    ELMD_CHECK_EQ(e.document().blocks.size(), 2u);
    ELMD_CHECK(e.document().blocks[0].kind == BlockKind::CodeBlock);
    ELMD_CHECK(e.document().blocks[1].kind == BlockKind::Paragraph);
    ELMD_CHECK(e.document().blocks[1].children.empty());
}

ELMD_TEST(test_enter_inside_indented_code_block_preserves_indent) {
    Editor e("    abc\n");
    e.set_caret(CharOffset(7));
    Command command;
    command.kind = CommandKind::InsertNewline;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("    abc\n    \n"));
    ELMD_CHECK_EQ(e.selection().head().v, 12u);
    ELMD_CHECK_EQ(e.document().blocks.size(), 2u);
    ELMD_CHECK(e.document().blocks[0].kind == BlockKind::CodeBlock);
    ELMD_CHECK(e.document().blocks[0].code_indented);
    ELMD_CHECK(e.document().blocks[1].kind == BlockKind::Paragraph);
    ELMD_CHECK(e.document().blocks[1].children.empty());
}

ELMD_TEST(test_soft_break_in_plain_paragraph_inserts_single_newline) {
    Editor e("alphaomega");
    e.set_caret(CharOffset(5));
    Command c; c.kind = CommandKind::InsertSoftBreak;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("alpha\nomega"));
    ELMD_CHECK_EQ(e.selection().head().v, 6u);
    ELMD_CHECK_EQ(e.document().blocks.size(), 1u);
}

ELMD_TEST(test_enter_on_empty_paragraph_inserts_one_empty_sibling) {
    Editor e("alpha\n\n");
    e.set_caret(CharOffset(7));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("alpha\n\n\n"));
    ELMD_CHECK_EQ(e.selection().head().v, 8u);
    ELMD_CHECK_EQ(e.document().blocks.size(), 1u);
    auto structure = build_source_structure(e.document());
    ELMD_CHECK_EQ(structure.blocks.size(), 3u);
    ELMD_CHECK(structure.blocks[1].kind == SourceBlockKind::Blank);
    ELMD_CHECK(structure.blocks[2].kind == SourceBlockKind::Blank);
}

ELMD_TEST(test_backspace_on_empty_block_deletes_semantic_block) {
    Editor e("alpha\n\n");
    e.set_caret(CharOffset(7));
    Command c; c.kind = CommandKind::DeleteBackward;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("alpha\n"));
    ELMD_CHECK_EQ(e.selection().head().v, 6u);
    ELMD_CHECK_EQ(e.document().blocks.size(), 1u);
}

ELMD_TEST(test_backspace_on_consecutive_empty_block_deletes_block_span) {
    Editor e("alpha\n\n\n\n");
    e.set_caret(CharOffset(9));
    Command c; c.kind = CommandKind::DeleteBackward;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("alpha\n\n\n"));
    ELMD_CHECK_EQ(e.selection().head().v, 8u);
    ELMD_CHECK_EQ(e.document().blocks.size(), 1u);
    auto structure = build_source_structure(e.document());
    ELMD_CHECK_EQ(structure.blocks.size(), 3u);
}

ELMD_TEST(test_enter_continues_unordered_list) {
    Editor e("- alpha");
    e.set_caret(CharOffset(7));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("- alpha\n- "));
    ELMD_CHECK_EQ(e.document().blocks.size(), 1u);
    ELMD_CHECK_EQ(e.document().blocks[0].list_items.size(), 2u);
    {
        ELMD_CHECK_EQ(e.document_selection().active.node_id, e.document().blocks[0].list_items[1].children[0].id);
        ELMD_CHECK_EQ(e.document_selection().active.offset, 0u);
    }
}

ELMD_TEST(test_enter_continues_ordered_list) {
    Editor e("9. alpha");
    e.set_caret(CharOffset(8));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("9. alpha\n10. "));
    ELMD_CHECK(e.document().blocks[0].list_ordered);
    ELMD_CHECK_EQ(e.document().blocks[0].list_items.size(), 2u);
    ELMD_CHECK(e.document().blocks[0].list_items[1].marker.empty());
    ELMD_CHECK_EQ(e.document_selection().active.node_id, e.document().blocks[0].list_items[1].children[0].id);
}

ELMD_TEST(test_enter_continues_task_list_unchecked) {
    Editor e("- [x] alpha");
    e.set_caret(CharOffset(11));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("- [x] alpha\n- [ ] "));
    ELMD_CHECK_EQ(e.document().blocks[0].task_items.size(), 2u);
    ELMD_CHECK(e.document().blocks[0].task_items[0].checked);
    ELMD_CHECK(!e.document().blocks[0].task_items[1].checked);
    ELMD_CHECK_EQ(e.document_selection().active.node_id, e.document().blocks[0].task_items[1].children[0].id);
}

ELMD_TEST(test_enter_exits_list_one_level_before_exiting_blockquote) {
    Editor e("> * alpha");
    e.set_caret(CharOffset(9));
    Command newline; newline.kind = CommandKind::InsertNewline;
    e.execute_command(newline);
    ELMD_CHECK_EQ(e.document().blocks.size(), 1u);
    ELMD_CHECK_EQ(e.document().blocks[0].kind, BlockKind::BlockQuote);
    ELMD_CHECK_EQ(e.document().blocks[0].quote_children[0].list_items.size(), 2u);
    e.execute_command(newline);
    ELMD_CHECK_EQ(e.document().blocks.size(), 1u);
    ELMD_CHECK_EQ(e.document().blocks[0].quote_children.size(), 2u);
    ELMD_CHECK_EQ(e.document().blocks[0].quote_children[0].kind, BlockKind::List);
    ELMD_CHECK_EQ(e.document().blocks[0].quote_children[1].kind, BlockKind::Paragraph);
    e.execute_command(newline);
    ELMD_CHECK_EQ(e.document().blocks.size(), 2u);
    ELMD_CHECK_EQ(e.document().blocks[0].kind, BlockKind::BlockQuote);
    ELMD_CHECK_EQ(e.document().blocks[1].kind, BlockKind::Paragraph);
    ELMD_CHECK(validate_document(e.document()).empty());
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
    ELMD_CHECK(validate_document(e.document()).empty());
    auto empty_item_id = e.document_selection().active.node_id;
    e.execute_command(newline);
    ELMD_CHECK(validate_document(e.document()).empty());
    ELMD_CHECK_EQ(e.document_selection().active.node_id, empty_item_id);
}

ELMD_TEST(test_enter_continues_blockquote) {
    Editor e("> alpha");
    e.set_caret(CharOffset(7));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.document().blocks.size(), 1u);
    ELMD_CHECK_EQ(e.document().blocks[0].kind, BlockKind::BlockQuote);
    ELMD_CHECK_EQ(e.document().blocks[0].quote_children.size(), 2u);
    ELMD_CHECK_EQ(block_inline_text_content(e.document().blocks[0].quote_children[0].children), std::u32string(U"alpha"));
    ELMD_CHECK_EQ(e.document_selection().active.node_id, e.document().blocks[0].quote_children[1].id);
}

ELMD_TEST(test_second_enter_exits_empty_blockquote_line) {
    Editor e("> alpha");
    e.set_caret(CharOffset(7));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    e.execute_command(c);
    ELMD_CHECK_EQ(e.document().blocks.size(), 2u);
    ELMD_CHECK_EQ(e.document().blocks[0].kind, BlockKind::BlockQuote);
    ELMD_CHECK_EQ(e.document().blocks[1].kind, BlockKind::Paragraph);
    ELMD_CHECK_EQ(e.document_selection().active.node_id, e.document().blocks[1].id);
}

ELMD_TEST(test_second_enter_removes_the_empty_quote_line_before_a_following_block) {
    Editor e("> alpha\n\nafter");
    e.set_caret(CharOffset(7));
    Command newline; newline.kind = CommandKind::InsertNewline;
    e.execute_command(newline);
    ELMD_CHECK_EQ(e.document().blocks.size(), 2u);
    ELMD_CHECK_EQ(e.document().blocks[0].quote_children.size(), 2u);
    e.execute_command(newline);
    ELMD_CHECK_EQ(e.document().blocks.size(), 3u);
    ELMD_CHECK_EQ(e.document().blocks[0].kind, BlockKind::BlockQuote);
    ELMD_CHECK_EQ(e.document().blocks[1].kind, BlockKind::Paragraph);
    ELMD_CHECK_EQ(e.document().blocks[2].kind, BlockKind::Paragraph);
    ELMD_CHECK_EQ(block_inline_text_content(e.document().blocks[2].children), std::u32string(U"after"));
}

ELMD_TEST(test_enter_exits_nested_blockquotes_one_level_at_a_time) {
    Editor e("> > alpha");
    e.set_caret(CharOffset(9));
    Command newline; newline.kind = CommandKind::InsertNewline;
    e.execute_command(newline);
    ELMD_CHECK_EQ(e.document().blocks[0].quote_children[0].quote_children.size(), 2u);
    e.execute_command(newline);
    ELMD_CHECK_EQ(e.document().blocks.size(), 1u);
    ELMD_CHECK_EQ(e.document().blocks[0].quote_children.size(), 2u);
    ELMD_CHECK_EQ(e.document().blocks[0].quote_children[0].kind, BlockKind::BlockQuote);
    ELMD_CHECK_EQ(e.document().blocks[0].quote_children[1].kind, BlockKind::Paragraph);
    e.execute_command(newline);
    ELMD_CHECK_EQ(e.document().blocks.size(), 2u);
    ELMD_CHECK_EQ(e.document().blocks[0].kind, BlockKind::BlockQuote);
    ELMD_CHECK_EQ(e.document().blocks[1].kind, BlockKind::Paragraph);
    ELMD_CHECK(validate_document(e.document()).empty());
}

ELMD_TEST(test_backspace_exits_nested_blockquotes_one_level_at_a_time) {
    Editor e("> > alpha");
    const auto paragraph_id = e.document().blocks[0].quote_children[0].quote_children[0].id;
    auto first = e.execute_document_delete_backward(DocumentSelection::caret(
        DocumentPosition{paragraph_id, 0, TextAffinity::Downstream}));
    ELMD_CHECK(first.has_value());
    ELMD_CHECK_EQ(e.document().blocks.size(), 1u);
    ELMD_CHECK_EQ(e.document().blocks[0].kind, BlockKind::BlockQuote);
    ELMD_CHECK_EQ(e.document().blocks[0].quote_children[0].id, paragraph_id);
    auto second = e.execute_document_delete_backward(e.document_selection());
    ELMD_CHECK(second.has_value());
    ELMD_CHECK_EQ(e.document().blocks.size(), 1u);
    ELMD_CHECK_EQ(e.document().blocks[0].kind, BlockKind::Paragraph);
    ELMD_CHECK_EQ(e.document().blocks[0].id, paragraph_id);
    ELMD_CHECK(e.undo_document());
    ELMD_CHECK_EQ(e.document().blocks[0].kind, BlockKind::BlockQuote);
    ELMD_CHECK(e.redo_document());
    ELMD_CHECK_EQ(e.document().blocks[0].kind, BlockKind::Paragraph);
}

ELMD_TEST(test_backspace_at_first_quote_content_start_removes_exactly_one_level) {
    Editor e("> > alpha");
    e.set_caret(CharOffset(4));
    Command backspace; backspace.kind = CommandKind::DeleteBackward;
    e.execute_command(backspace);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("> alpha"));
    ELMD_CHECK_EQ(e.document_selection().active.offset, 0u);
    e.execute_command(backspace);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("alpha"));
    ELMD_CHECK_EQ(e.document().blocks[0].kind, BlockKind::Paragraph);
    ELMD_CHECK_EQ(e.document_selection().active.node_id, e.document().blocks[0].id);
}

ELMD_TEST(test_backspace_at_following_quote_line_start_joins_the_same_depth_first) {
    Editor e("> > first\n> > second");
    e.set_caret(CharOffset(14));
    Command backspace; backspace.kind = CommandKind::DeleteBackward;
    e.execute_command(backspace);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("> > firstsecond"));
    ELMD_CHECK_EQ(e.document_selection().active.offset, 5u);
    ELMD_CHECK_EQ(e.document().blocks[0].quote_children[0].quote_children.size(), 1u);
}

ELMD_TEST(test_backspace_join_removes_the_preceding_hard_break_marker) {
    Editor e("> > first  \n> > second");
    e.set_caret(CharOffset(16));
    Command backspace; backspace.kind = CommandKind::DeleteBackward;
    e.execute_command(backspace);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("> > firstsecond"));
    ELMD_CHECK_EQ(e.document_selection().active.offset, 5u);
}

ELMD_TEST(test_backspace_inside_nested_quote_content_never_touches_quote_markers) {
    Editor e("> > abcdefghijklmnopqrstuvwxyz");
    e.set_caret(CharOffset(18));
    Command backspace; backspace.kind = CommandKind::DeleteBackward;
    e.execute_command(backspace);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("> > abcdefghijklmopqrstuvwxyz"));
    ELMD_CHECK_EQ(e.document_selection().active.offset, 13u);
}

ELMD_TEST(test_blockquote_newline_with_following_block_keeps_an_editable_quote_line) {
    Editor e("> alpha\n\nafter");
    e.set_caret(CharOffset(7));
    Command newline; newline.kind = CommandKind::InsertNewline;
    e.execute_command(newline);
    auto insertion = e.execute_document_insert_text(e.document_selection(), U"11111");
    ELMD_CHECK(insertion.has_value());
    ELMD_CHECK_EQ(e.document().blocks[0].quote_children.size(), 2u);
    ELMD_CHECK_EQ(block_inline_text_content(e.document().blocks[0].quote_children[1].children), std::u32string(U"11111"));
    auto split = e.execute_document_enter(e.document_selection());
    ELMD_CHECK(split.has_value());
    ELMD_CHECK_EQ(e.document().blocks[0].quote_children.size(), 3u);
    ELMD_CHECK_EQ(block_inline_text_content(e.document().blocks[0].quote_children[1].children), std::u32string(U"11111"));
    ELMD_CHECK(block_inline_text_content(e.document().blocks[0].quote_children[2].children).empty());
    ELMD_CHECK(validate_document(e.document()).empty());
}

ELMD_TEST(test_backspace_removes_an_empty_blockquote_prefix_atomically) {
    Editor e("> alpha  \n> \n\nafter");
    e.set_caret(CharOffset(12));
    Command backspace; backspace.kind = CommandKind::DeleteBackward;
    e.execute_command(backspace);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("> alpha\n\nafter"));
    ELMD_CHECK_EQ(e.selection().head().v, 7u);
}

ELMD_TEST(test_enter_on_empty_indented_code_line_exits_the_code_block) {
    Editor e("    alpha\n    \n\nafter");
    e.set_caret(CharOffset(14));
    Command newline; newline.kind = CommandKind::InsertNewline;
    e.execute_command(newline);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("    alpha\n\n\nafter"));
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
    ELMD_CHECK_EQ(e.text_utf8(), std::string("    alpha\n\nafter"));
    ELMD_CHECK_EQ(e.document_selection().active.node_id, e.document().blocks[1].id);
    ELMD_CHECK_EQ(e.document_selection().active.offset, 0u);
    ELMD_CHECK(e.document_selection().active.affinity == TextAffinity::Upstream);
}

ELMD_TEST(test_delete_forward_inside_indented_code_edits_code_node) {
    Editor editor("    ab\n");
    const auto code_id = editor.document().blocks[0].id;
    editor.set_caret(CharOffset(5));
    Command command;
    command.kind = CommandKind::DeleteForward;
    ELMD_CHECK(editor.execute_command(command));
    ELMD_CHECK_EQ(editor.document().blocks[0].id, code_id);
    ELMD_CHECK_EQ(editor.document().blocks[0].code_text, std::u32string(U"a\n"));
    ELMD_CHECK_EQ(editor.document_selection().active.node_id, code_id);
    ELMD_CHECK_EQ(editor.document_selection().active.offset, 1u);
}

ELMD_TEST(test_enter_on_empty_list_exits_list) {
    Editor e("- ");
    e.set_caret(CharOffset(2));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string(""));
    ELMD_CHECK_EQ(e.selection().head().v, 0u);
}

ELMD_TEST(test_toggle_unordered_list) {
    Editor e("alpha");
    e.set_caret(CharOffset(5));
    Command c; c.kind = CommandKind::ToggleUnorderedList;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("- alpha"));
    ELMD_CHECK_EQ(e.selection().head().v, 7u);
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("alpha"));
    ELMD_CHECK_EQ(e.selection().head().v, 5u);
}

ELMD_TEST(test_toggle_ordered_list_replaces_unordered_marker) {
    Editor e("- alpha");
    e.set_caret(CharOffset(7));
    Command c; c.kind = CommandKind::ToggleOrderedList;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("1. alpha"));
    ELMD_CHECK_EQ(e.selection().head().v, 8u);
}

ELMD_TEST(test_toggle_task_list_replaces_ordered_marker) {
    Editor e("1. alpha");
    e.set_caret(CharOffset(8));
    Command c; c.kind = CommandKind::ToggleTaskList;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("- [ ] alpha"));
    ELMD_CHECK_EQ(e.selection().head().v, 11u);
}

ELMD_TEST(test_toggle_task_checkbox) {
    Editor e("- [ ] alpha");
    e.set_caret(CharOffset(3));
    Command c; c.kind = CommandKind::ToggleTaskCheckbox;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("- [x] alpha"));
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("- [ ] alpha"));
}

ELMD_TEST(test_toggle_ordered_list_across_selected_lines) {
    Editor e("alpha\nbeta\ngamma");
    e.set_selection(Selection{CharOffset(0), CharOffset(16), TextAffinity::Downstream});
    Command c; c.kind = CommandKind::ToggleOrderedList;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("1. alpha\n2. beta\n3. gamma"));
    ELMD_CHECK_EQ(e.document().blocks.size(), 1u);
    ELMD_CHECK(e.document().blocks[0].list_ordered);
    ELMD_CHECK_EQ(e.document().blocks[0].list_items.size(), 3u);
}

ELMD_TEST(test_toggle_task_list_across_selected_lines_and_remove) {
    Editor e("alpha\nbeta");
    e.set_selection(Selection{CharOffset(0), CharOffset(10), TextAffinity::Downstream});
    Command c; c.kind = CommandKind::ToggleTaskList;
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("- [ ] alpha\n- [ ] beta"));
    e.execute_command(c);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("alpha\nbeta"));
}

ELMD_TEST(test_empty_inline_format_commands_insert_editable_pairs) {
    Editor e;
    Command strong; strong.kind = CommandKind::ToggleStrong;
    e.execute_command(strong);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("****"));
    ELMD_CHECK_EQ(e.selection().head().v, 2u);
    Editor math;
    Command inlineMath; inlineMath.kind = CommandKind::InsertMathInline;
    math.execute_command(inlineMath);
    ELMD_CHECK_EQ(math.text_utf8(), std::string("$$"));
    ELMD_CHECK_EQ(math.selection().head().v, 1u);
}

ELMD_TEST(test_editor_auto_pairing_uses_document_tree_transactions) {
    Editor emphasis;
    auto firstEmphasis = emphasis.execute_command(Command::InsertText(U"*"));
    ELMD_CHECK(firstEmphasis);
    ELMD_CHECK_EQ(emphasis.text_utf8(), std::string("**"));
    ELMD_CHECK_EQ(emphasis.selection().head().v, 1u);
    auto secondEmphasis = emphasis.execute_command(Command::InsertText(U"*"));
    ELMD_CHECK(secondEmphasis);
    ELMD_CHECK_EQ(emphasis.text_utf8(), std::string("****"));
    ELMD_CHECK_EQ(emphasis.selection().head().v, 2u);

    Editor strike;
    strike.execute_command(Command::InsertText(U"~"));
    strike.execute_command(Command::InsertText(U"~"));
    ELMD_CHECK_EQ(strike.text_utf8(), std::string("~~~~"));
    ELMD_CHECK_EQ(strike.selection().head().v, 2u);

    Editor math;
    math.execute_command(Command::InsertText(U"$"));
    math.execute_command(Command::InsertText(U"$"));
    ELMD_CHECK_EQ(math.text_utf8(), std::string("$$\n\n$$"));
    ELMD_CHECK_EQ(math.selection().head().v, 3u);
    ELMD_CHECK(!math.document().blocks.empty());
    ELMD_CHECK(math.document().blocks.front().kind == BlockKind::MathBlock);

    Editor fence;
    fence.execute_command(Command::InsertText(U"`"));
    fence.execute_command(Command::InsertText(U"`"));
    fence.execute_command(Command::InsertText(U"`"));
    ELMD_CHECK_EQ(fence.text_utf8(), std::string("```\n\n```"));
    ELMD_CHECK_EQ(fence.selection().head().v, 4u);
    ELMD_CHECK(!fence.document().blocks.empty());
    ELMD_CHECK(fence.document().blocks.front().kind == BlockKind::CodeBlock);

    Editor deletion;
    deletion.execute_command(Command::InsertText(U"_"));
    Command backspace;
    backspace.kind = CommandKind::DeleteBackward;
    deletion.execute_command(backspace);
    ELMD_CHECK(deletion.text_utf8().empty());
    ELMD_CHECK_EQ(deletion.selection().head().v, 0u);
}

ELMD_TEST(test_typing_inside_auto_pair_promotes_ast_inline_node) {
    Editor emphasis;
    emphasis.execute_command(Command::InsertText(U"*"));
    const auto paragraph_id = emphasis.document().blocks.front().id;
    emphasis.execute_command(Command::InsertText(U"value"));
    ELMD_CHECK_EQ(emphasis.text_utf8(), std::string("*value*"));
    ELMD_CHECK_EQ(emphasis.document().blocks.front().id, paragraph_id);
    ELMD_CHECK_EQ(emphasis.document().blocks.front().children.size(), 1u);
    ELMD_CHECK(emphasis.document().blocks.front().children.front().kind == InlineKind::Emphasis);
    ELMD_CHECK_EQ(block_inline_text_content(emphasis.document().blocks.front().children), std::u32string(U"value"));
    {
        ELMD_CHECK_EQ(emphasis.document_selection().active.node_id, paragraph_id);
        ELMD_CHECK_EQ(emphasis.document_selection().active.offset, 5u);
        ELMD_CHECK(emphasis.document_selection().active.affinity == TextAffinity::Upstream);
    }

    Editor strong;
    strong.execute_command(Command::InsertText(U"*"));
    strong.execute_command(Command::InsertText(U"*"));
    strong.execute_command(Command::InsertText(U"bold"));
    ELMD_CHECK_EQ(strong.text_utf8(), std::string("**bold**"));
    ELMD_CHECK(strong.document().blocks.front().children.front().kind == InlineKind::Strong);
}

ELMD_TEST(test_insert_text_replaces_cross_node_selection_in_document_tree) {
    Editor editor("alpha\n\nomega");
    const auto first_id = editor.document().blocks[0].id;
    const auto second_id = editor.document().blocks[1].id;
    editor.set_document_selection(DocumentSelection{
        DocumentPosition{first_id, 2, TextAffinity::Downstream},
        DocumentPosition{second_id, 3, TextAffinity::Downstream}});
    auto transaction = editor.execute_command(Command::InsertText(U"X"));
    ELMD_CHECK(transaction);
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("alXga"));
    ELMD_CHECK_EQ(editor.document().blocks.size(), 1u);
    ELMD_CHECK_EQ(editor.document().blocks.front().id, first_id);
    {
        ELMD_CHECK_EQ(editor.document_selection().active.node_id, first_id);
        ELMD_CHECK_EQ(editor.document_selection().active.offset, 3u);
    }
    editor.undo();
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("alpha\n\nomega"));
    ELMD_CHECK_EQ(editor.document().blocks[0].id, first_id);
    ELMD_CHECK_EQ(editor.document().blocks[1].id, second_id);
    ELMD_CHECK(!editor.document_selection().is_caret());
}

ELMD_TEST(test_table_text_input_and_delete_share_document_history) {
    Editor editor("| H |\n| --- |\n| X |");
    const auto cell_id = editor.document().blocks.front().table_rows.front().cells.front().id;
    editor.set_document_selection(DocumentSelection::caret(DocumentPosition{cell_id, 1, TextAffinity::Downstream}));
    editor.execute_command(Command::InsertText(U"Y"));
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("| H |\n| --- |\n| XY |"));
    Command backspace;
    backspace.kind = CommandKind::DeleteBackward;
    editor.execute_command(backspace);
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("| H |\n| --- |\n| X |"));
    editor.undo();
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("| H |\n| --- |\n| XY |"));
    editor.undo();
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("| H |\n| --- |\n| X |"));
    ELMD_CHECK_EQ(editor.document().blocks.front().table_rows.front().cells.front().id, cell_id);
}

ELMD_TEST(test_inline_format_command_removes_surrounding_markers) {
    Editor e("**bold**");
    e.set_selection(Selection{CharOffset(2), CharOffset(6), TextAffinity::Downstream});
    Command command; command.kind = CommandKind::ToggleStrong;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("bold"));
    ELMD_CHECK_EQ(e.selection().normalized_range().start.v, 0u);
    ELMD_CHECK_EQ(e.selection().normalized_range().end.v, 4u);
}

ELMD_TEST(test_toggle_blockquote_across_selected_lines) {
    Editor e("alpha\nbeta");
    e.set_selection(Selection{CharOffset(0), CharOffset(10), TextAffinity::Downstream});
    Command command; command.kind = CommandKind::ToggleBlockQuote;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("> alpha\n> beta"));
    e.execute_command(command);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("alpha\nbeta"));
}

ELMD_TEST(test_clear_heading_preserves_text) {
    Editor e("### title");
    e.set_caret(CharOffset(9));
    Command command; command.kind = CommandKind::ClearHeading;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("title"));
}

ELMD_TEST(test_heading_command_mutates_document_node_and_history) {
    Editor editor("title");
    const auto paragraph_id = editor.document().blocks.front().id;
    editor.set_document_selection(DocumentSelection::caret(
        DocumentPosition{paragraph_id, 5, TextAffinity::Downstream}));
    Command command; command.kind = CommandKind::SetHeading; command.level = 3;
    ELMD_CHECK(editor.execute_command(command));
    ELMD_CHECK(editor.document().blocks.front().kind == BlockKind::Heading);
    ELMD_CHECK_EQ(editor.document().blocks.front().id, paragraph_id);
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("### title"));
    ELMD_CHECK(editor.has_document_undo());
    editor.undo();
    ELMD_CHECK(editor.document().blocks.front().kind == BlockKind::Paragraph);
    ELMD_CHECK_EQ(editor.document().blocks.front().id, paragraph_id);
}

ELMD_TEST(test_quote_command_wraps_document_range_without_reparsing) {
    Editor editor("alpha\n\nbeta");
    const auto first_id = editor.document().blocks[0].id;
    const auto second_id = editor.document().blocks[1].id;
    editor.set_document_selection(DocumentSelection{
        DocumentPosition{first_id, 0, TextAffinity::Downstream},
        DocumentPosition{second_id, 4, TextAffinity::Downstream}});
    Command command; command.kind = CommandKind::ToggleBlockQuote;
    ELMD_CHECK(editor.execute_command(command));
    ELMD_CHECK_EQ(editor.document().blocks.size(), 1u);
    ELMD_CHECK(editor.document().blocks.front().kind == BlockKind::BlockQuote);
    ELMD_CHECK_EQ(editor.document().blocks.front().quote_children[0].id, first_id);
    ELMD_CHECK_EQ(editor.document().blocks.front().quote_children[1].id, second_id);
    editor.execute_command(command);
    ELMD_CHECK_EQ(editor.document().blocks.size(), 2u);
    ELMD_CHECK_EQ(editor.document().blocks[0].id, first_id);
    ELMD_CHECK_EQ(editor.document().blocks[1].id, second_id);
}

ELMD_TEST(test_list_commands_convert_document_container_in_place) {
    Editor editor("alpha\nbeta");
    const auto paragraph_id = editor.document().blocks.front().id;
    editor.set_document_selection(DocumentSelection{
        DocumentPosition{paragraph_id, 0, TextAffinity::Downstream},
        DocumentPosition{paragraph_id, 10, TextAffinity::Downstream}});
    Command ordered; ordered.kind = CommandKind::ToggleOrderedList;
    editor.execute_command(ordered);
    ELMD_CHECK(editor.document().blocks.front().kind == BlockKind::List);
    ELMD_CHECK(editor.document().blocks.front().list_ordered);
    ELMD_CHECK_EQ(editor.document().blocks.front().list_items.front().children.front().id, paragraph_id);
    const auto list_id = editor.document().blocks.front().id;
    Command task; task.kind = CommandKind::ToggleTaskList;
    editor.execute_command(task);
    ELMD_CHECK(editor.document().blocks.front().kind == BlockKind::TaskList);
    ELMD_CHECK_EQ(editor.document().blocks.front().id, list_id);
    ELMD_CHECK_EQ(editor.document().blocks.front().task_items.front().children.front().id, paragraph_id);
    editor.undo();
    ELMD_CHECK(editor.document().blocks.front().kind == BlockKind::List);
    ELMD_CHECK_EQ(editor.document().blocks.front().id, list_id);
}

ELMD_TEST(test_insert_image_uses_selection_as_alt_text) {
    Editor e("diagram");
    e.set_selection(Selection{CharOffset(0), CharOffset(7), TextAffinity::Downstream});
    Command command; command.kind = CommandKind::InsertImage; command.path = U"chart.png";
    e.execute_command(command);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("![diagram](chart.png)"));
}

ELMD_TEST(test_link_and_image_commands_mutate_inline_tree) {
    Editor link("label");
    const auto paragraph_id = link.document().blocks.front().id;
    link.set_document_selection(DocumentSelection{
        DocumentPosition{paragraph_id, 0, TextAffinity::Downstream},
        DocumentPosition{paragraph_id, 5, TextAffinity::Downstream}});
    Command link_command; link_command.kind = CommandKind::InsertLink; link_command.href = U"https://example.test";
    link.execute_command(link_command);
    ELMD_CHECK_EQ(link.text_utf8(), std::string("[label](https://example.test)"));
    ELMD_CHECK(link.document().blocks.front().children.front().kind == InlineKind::Link);
    ELMD_CHECK_EQ(link.document().blocks.front().id, paragraph_id);
    link.undo();
    ELMD_CHECK_EQ(link.text_utf8(), std::string("label"));

    Editor image("diagram");
    image.set_document_selection(DocumentSelection{
        DocumentPosition{image.document().blocks.front().id, 0, TextAffinity::Downstream},
        DocumentPosition{image.document().blocks.front().id, 7, TextAffinity::Downstream}});
    Command image_command; image_command.kind = CommandKind::InsertImage; image_command.path = U"chart.png";
    image.execute_command(image_command);
    ELMD_CHECK(image.document().blocks.front().children.front().kind == InlineKind::Image);
    ELMD_CHECK_EQ(image.document().blocks.front().children.front().alt, std::string("diagram"));
    ELMD_CHECK(image.has_document_undo());
}

ELMD_TEST(test_insert_footnote_adds_unique_definition) {
    Editor e("alpha\n\n[^1]: old");
    e.set_caret(CharOffset(5));
    Command command; command.kind = CommandKind::InsertFootnote;
    e.execute_command(command);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("alpha[^2]\n\n[^1]: old\n\n[^2]: "));
    ELMD_CHECK(std::any_of(e.document().blocks.begin(), e.document().blocks.end(), [](auto const& block) { return block.kind == BlockKind::FootnoteDefinition; }));
    ELMD_CHECK(e.document().blocks.front().children.back().kind == InlineKind::FootnoteRef);
    ELMD_CHECK_EQ(e.document().blocks.front().children.back().label, std::string("2"));
    ELMD_CHECK_EQ(e.document_selection().active.node_id, e.document().blocks.back().quote_children.front().id);
    ELMD_CHECK(e.has_document_undo());
}

ELMD_TEST(test_toggle_callout_wraps_and_unwraps_selected_lines) {
    Editor e("alpha\nbeta");
    e.set_selection(Selection{CharOffset(0), CharOffset(10), TextAffinity::Downstream});
    Command command; command.kind = CommandKind::ToggleCallout; command.callout_kind = U"warning";
    e.execute_command(command);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("> [!WARNING]\n> alpha\n> beta"));
    ELMD_CHECK(std::any_of(e.document().blocks.begin(), e.document().blocks.end(), [](auto const& block) { return block.kind == BlockKind::Callout; }));
    e.set_caret(CharOffset(20));
    e.execute_command(command);
    ELMD_CHECK_EQ(e.text_utf8(), std::string("alpha\nbeta"));
}

ELMD_TEST(test_callout_command_preserves_content_node_identity) {
    Editor editor("alpha");
    const auto paragraph_id = editor.document().blocks.front().id;
    editor.set_document_selection(DocumentSelection::caret(
        DocumentPosition{paragraph_id, 5, TextAffinity::Downstream}));
    Command command; command.kind = CommandKind::ToggleCallout; command.callout_kind = U"warning";
    editor.execute_command(command);
    ELMD_CHECK(editor.document().blocks.front().kind == BlockKind::Callout);
    ELMD_CHECK_EQ(editor.document().blocks.front().callout_kind, std::string("WARNING"));
    ELMD_CHECK_EQ(editor.document().blocks.front().quote_children.front().id, paragraph_id);
    editor.undo();
    ELMD_CHECK_EQ(editor.document().blocks.front().id, paragraph_id);
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
    auto result = editor.text_utf8();
    ELMD_CHECK(result.find("Paragraph 750 updated") != std::string::npos);
    ELMD_CHECK(result.find("## Section 1499") != std::string::npos);
    ELMD_CHECK(editor.outline().flat_items().size() == 1500u);
}

ELMD_TEST(test_enter_after_thematic_break_creates_blank_lines_without_duplicate_rules) {
    Editor editor("---");
    editor.set_caret(CharOffset(3));
    Command enter; enter.kind = CommandKind::InsertNewline;
    auto check_structure = [&](std::size_t blank_count) {
        auto structure = build_source_structure(editor.document());
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
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("---\n"));
    ELMD_CHECK_EQ(editor.selection().head().v, 4u);
    check_structure(1);
    editor.execute_command(enter);
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("---\n\n"));
    ELMD_CHECK_EQ(editor.selection().head().v, 5u);
    check_structure(2);
    editor.execute_command(enter);
    ELMD_CHECK_EQ(editor.text_utf8(), std::string("---\n\n\n"));
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
