import std;
import boost.ut;
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
using namespace boost::ut;

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


suite editor_tests = [] {

"test_new_editor_empty"_test = [] {
    Editor e;
    expect(fatal(bool(e.text_cps().empty())));
};

"test_document_projection_does_not_reparse_incomplete_markdown"_test = [] {
    EditorDocument document;
    document.revision = 7;
    BlockNode paragraph;
    paragraph.id = NodeId(1);
    paragraph.kind = BlockKind::Paragraph;
    paragraph.children.push_back(InlineNode::text_node(NodeId(2), U"\\["));
    document.blocks.push_back(std::move(paragraph));

    auto projection = project_document(document);
    expect(fatal(bool((projection.markdown) == (std::u32string(U"\\[")))));
    expect(fatal(bool(projection.source_map.find_node_by_id(NodeId(1)) != nullptr)));
    auto* text_range = projection.source_map.find_node_by_id(NodeId(2));
    expect(fatal(bool(text_range != nullptr)));
    if (text_range) {
        expect(fatal(bool((text_range->content_range.start.v) == (0u))));
        expect(fatal(bool((text_range->content_range.end.v) == (2u))));
    }
};

"test_document_projection_maps_empty_inline_structure_without_parser_rebinding"_test = [] {
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
    expect(fatal(bool((projection.markdown) == (std::u32string(U"****")))));
    auto* range = projection.source_map.find_node_by_id(NodeId(11));
    expect(fatal(bool(range != nullptr)));
    if (range) {
        expect(fatal(bool((range->source_range.start.v) == (0u))));
        expect(fatal(bool((range->source_range.end.v) == (4u))));
        expect(fatal(bool((range->content_range.start.v) == (2u))));
        expect(fatal(bool((range->content_range.end.v) == (2u))));
        expect(fatal(bool((range->marker_ranges.size()) == (2u))));
    }
};

"test_document_projection_maps_nested_quote_and_list_nodes_directly"_test = [] {
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
    expect(fatal(bool((projection.markdown) == (std::u32string(U"> alpha\n\n- beta")))));
    for (auto id : {20u, 21u, 22u, 30u, 31u, 32u, 33u}) {
        expect(fatal(bool(document.source_map.find_node_by_id(NodeId(id)) != nullptr)));
    }
    auto quoted = document_position_from_source_offset(document, CharOffset(4));
    expect(fatal(bool(quoted.has_value())));
    if (quoted) {
        expect(fatal(bool((quoted->node_id) == (NodeId(21)))));
        expect(fatal(bool((quoted->offset) == (2u))));
    }
    auto listed = document_position_from_source_offset(document, CharOffset(13));
    expect(fatal(bool(listed.has_value())));
    if (listed) {
        expect(fatal(bool((listed->node_id) == (NodeId(32)))));
        expect(fatal(bool((listed->offset) == (2u))));
    }
};

"test_editor_document_enter_keeps_ast_authoritative_and_projects_markdown"_test = [] {
    Editor editor("alphaomega");
    expect(fatal(bool(!editor.document().blocks.empty())));
    if (editor.document().blocks.empty()) return;
    const auto original_id = editor.document().blocks.front().id;
    auto transaction = editor.execute_document_enter(DocumentSelection::caret(
        DocumentPosition{original_id, 5, TextAffinity::Downstream}));

    expect(fatal(bool(transaction)));
    if (!transaction) return;
    expect(fatal(bool((editor.text_utf8()) == (std::string("alpha\n\nomega")))));
    expect(fatal(bool((editor.document().blocks.size()) == (2u))));
    expect(fatal(bool((editor.document().blocks.front().id) == (original_id))));
    expect(fatal(bool((editor.document_selection().active.node_id) == (editor.document().blocks[1].id))));
    expect(fatal(bool(editor.document().source_map.find_node_by_id(editor.document().blocks[1].id) != nullptr)));

    expect(fatal(bool(editor.undo_document())));
    expect(fatal(bool((editor.text_utf8()) == (std::string("alphaomega")))));
    expect(fatal(bool((editor.document().blocks.size()) == (1u))));
    expect(fatal(bool((editor.document().blocks.front().id) == (original_id))));

    expect(fatal(bool(editor.redo_document())));
    expect(fatal(bool((editor.text_utf8()) == (std::string("alpha\n\nomega")))));
    expect(fatal(bool((editor.document().blocks.size()) == (2u))));
};

"test_editor_newline_command_uses_document_transaction_for_top_level_paragraph"_test = [] {
    Editor editor("alphaomega");
    const auto original_id = editor.document().blocks.front().id;
    editor.set_caret(CharOffset(5));
    Command newline;
    newline.kind = CommandKind::InsertNewline;

    auto result = editor.execute_command(newline);
    expect(fatal(bool(result)));
    expect(fatal(bool((editor.text_utf8()) == (std::string("alpha\n\nomega")))));
    expect(fatal(bool((editor.document().blocks.front().id) == (original_id))));
    expect(fatal(bool(editor.has_document_undo())));

    auto undone = editor.undo();
    expect(fatal(bool(undone)));
    expect(fatal(bool((editor.text_utf8()) == (std::string("alphaomega")))));
    expect(fatal(bool((editor.document().blocks.front().id) == (original_id))));
};

"test_editor_document_delete_projects_and_restores_node_selection"_test = [] {
    Editor editor("alphabeta");
    const auto block_id = editor.document().blocks.front().id;
    auto transaction = editor.execute_document_delete_backward(DocumentSelection::caret(
        DocumentPosition{block_id, 5, TextAffinity::Downstream}));
    expect(fatal(bool(transaction)));
    expect(fatal(bool((editor.text_utf8()) == (std::string("alphbeta")))));
    expect(fatal(bool((editor.document().blocks.front().id) == (block_id))));
    expect(fatal(bool(editor.undo_document())));
    expect(fatal(bool((editor.text_utf8()) == (std::string("alphabeta")))));
    expect(fatal(bool((editor.document_selection().active.offset) == (5u))));
};

"test_editor_backspace_command_moves_list_item_tree_and_undo_restores_it"_test = [] {
    Editor editor("- one\n- two");
    const auto second_id = editor.document().blocks[0].list_items[1].children[0].id;
    editor.set_document_selection(DocumentSelection::caret(
        DocumentPosition{second_id, 0, TextAffinity::Downstream}));
    Command backspace;
    backspace.kind = CommandKind::DeleteBackward;
    auto transaction = editor.execute_command(backspace);
    expect(fatal(bool(transaction)));
    expect(fatal(bool((editor.document().blocks[0].list_items.size()) == (1u))));
    expect(fatal(bool((editor.document().blocks[0].list_items[0].children.size()) == (2u))));
    expect(fatal(bool((editor.document().blocks[0].list_items[0].children[1].id) == (second_id))));
    expect(fatal(bool((editor.document_selection().active.node_id) == (second_id))));
    expect(fatal(bool(editor.undo_document())));
    expect(fatal(bool((editor.document().blocks[0].list_items.size()) == (2u))));
    expect(fatal(bool(editor.redo_document())));
    expect(fatal(bool((editor.document().blocks[0].list_items.size()) == (1u))));
};

"test_editor_list_indent_commands_use_document_history"_test = [] {
    Editor editor("- a\n- b");
    const auto second_id = editor.document().blocks[0].list_items[1].children[0].id;
    editor.set_document_selection(DocumentSelection::caret(
        DocumentPosition{second_id, 1, TextAffinity::Downstream}));
    Command indent;
    indent.kind = CommandKind::IndentListItem;
    expect(fatal(bool(editor.execute_command(indent))));
    expect(fatal(bool((editor.markdown_utf8()) == (std::string("- a\n  - b")))));
    expect(fatal(bool((editor.document().blocks[0].list_items.size()) == (1u))));
    expect(fatal(bool(editor.undo_document())));
    expect(fatal(bool((editor.markdown_utf8()) == (std::string("- a\n- b")))));
    expect(fatal(bool(editor.redo_document())));
    Command outdent;
    outdent.kind = CommandKind::OutdentListItem;
    expect(fatal(bool(editor.execute_command(outdent))));
    expect(fatal(bool((editor.markdown_utf8()) == (std::string("- a\n- b")))));
    expect(fatal(bool((editor.document_selection().active.node_id) == (second_id))));
    expect(fatal(bool((editor.document_selection().active.offset) == (1u))));
};

"test_editor_cross_container_selection_delete_uses_document_history"_test = [] {
    Editor editor("> alpha\n\nomega");
    const auto quote_id = editor.document().blocks[0].quote_children[0].id;
    const auto paragraph_id = editor.document().blocks[1].id;
    DocumentSelection selection{
        DocumentPosition{quote_id, 2, TextAffinity::Downstream},
        DocumentPosition{paragraph_id, 3, TextAffinity::Downstream}};
    editor.set_document_selection(selection);
    Command command;
    command.kind = CommandKind::DeleteSelection;
    auto changed = editor.execute_command(command);
    expect(fatal(bool(changed)));
    expect(fatal(bool((editor.markdown_utf8()) == (std::string("> alga")))));
    expect(fatal(bool((editor.document_selection().active.node_id) == (quote_id))));
    expect(fatal(bool((editor.document_selection().active.offset) == (2u))));
    expect(fatal(bool(editor.undo_document())));
    expect(fatal(bool((editor.markdown_utf8()) == (std::string("> alpha\n\nomega")))));
    expect(fatal(bool((editor.document_selection().anchor.node_id) == (quote_id))));
    expect(fatal(bool((editor.document_selection().active.node_id) == (paragraph_id))));
};

"test_editor_enter_at_paragraph_end_projects_empty_node_caret_anchor"_test = [] {
    Editor editor("alpha");
    const auto first_id = editor.document().blocks.front().id;
    editor.set_caret(CharOffset(5));
    Command newline;
    newline.kind = CommandKind::InsertNewline;
    auto transaction = editor.execute_command(newline);

    expect(fatal(bool(transaction)));
    expect(fatal(bool((editor.text_utf8()) == (std::string("alpha\n\n")))));
    expect(fatal(bool((editor.document().blocks.size()) == (2u))));
    expect(fatal(bool((editor.document().blocks.front().id) == (first_id))));
    expect(fatal(bool(editor.document().source_map.find_node_by_id(editor.document().blocks[1].id) != nullptr)));
    expect(fatal(bool((editor.selection().active.v) == (6u))));
};

"test_editor_document_range_delete_restores_ast_and_selection_on_undo"_test = [] {
    Editor editor("alpha\n\nomega");
    const auto first_id = editor.document().blocks[0].id;
    const auto last_id = editor.document().blocks[1].id;
    DocumentSelection selection{
        DocumentPosition{first_id, 2, TextAffinity::Downstream},
        DocumentPosition{last_id, 3, TextAffinity::Downstream}};
    auto transaction = editor.execute_document_delete_selection(selection);

    expect(fatal(bool(transaction)));
    expect(fatal(bool((editor.text_utf8()) == (std::string("alga")))));
    expect(fatal(bool((editor.document().blocks.size()) == (1u))));
    expect(fatal(bool((editor.document().blocks[0].id) == (first_id))));
    expect(fatal(bool(editor.undo_document())));
    expect(fatal(bool((editor.text_utf8()) == (std::string("alpha\n\nomega")))));
    expect(fatal(bool((editor.document().blocks.size()) == (2u))));
    {
        expect(fatal(bool((editor.document_selection().anchor.node_id) == (first_id))));
        expect(fatal(bool((editor.document_selection().active.node_id) == (last_id))));
    }
};

"test_document_position_projection_skips_inline_markers"_test = [] {
    Editor editor("before **alpha** after");
    const auto block_id = editor.document().blocks.front().id;
    auto inside = document_position_from_source_offset(editor.document(), CharOffset(11));
    expect(fatal(bool(inside.has_value())));
    if (!inside) return;
    expect(fatal(bool((inside->node_id) == (block_id))));
    expect(fatal(bool((inside->offset) == (9u))));
    auto source = source_offset_from_document_position(editor.document(), *inside);
    expect(fatal(bool(source.has_value())));
    if (source) expect(fatal(bool((source->v) == (11u))));
};

"test_editor_insert_text"_test = [] {
    Editor e;
    e.execute_command(Command::InsertText(U"hello"));
    expect(fatal(bool((e.text_utf8()) == (std::string("hello")))));
};

"test_editor_incremental_unclosed_bracket_math"_test = [] {
    Editor e;
    expect(fatal(bool(e.execute_command(Command::InsertText(U"\\")))));
    expect(fatal(bool(e.execute_command(Command::InsertText(U"[")))));
    expect(fatal(bool((e.text_utf8()) == (std::string("\\[")))));
    auto model = build_render_model(e.document(), e.text_utf8(), e.outline());
    expect(fatal(bool((model.blocks.size()) == (1u))));
    expect(fatal(bool(model.blocks[0].kind == RenderBlockKind::Text)));
    expect(fatal(bool(e.document().blocks[0].kind == BlockKind::Paragraph)));
};

"test_editor_incremental_inline_math_keeps_exact_source_mapping"_test = [] {
    Editor e;
    for (char32_t ch : std::u32string_view(U"$y=x$")) {
        expect(fatal(bool(e.execute_command(Command::InsertText(std::u32string(1, ch))))));
    }
    auto model = build_render_model(e.document(), e.text_utf8(), e.outline());
    expect(fatal(bool((model.blocks.size()) == (1u))));
    auto item = std::find_if(model.blocks[0].inline_items.begin(), model.blocks[0].inline_items.end(), [](auto const& candidate) {
        return candidate.kind == InlineRenderItem::Kind::Math;
    });
    expect(fatal(bool(item != model.blocks[0].inline_items.end())));
    expect(fatal(bool((item->text) == (std::u32string(U"y=x")))));
    expect(fatal(bool((item->source_range.start.v) == (0u))));
    expect(fatal(bool((item->source_range.end.v) == (5u))));

    e.set_selection(Selection::caret(CharOffset(0)));
    for (std::size_t expected = 1; expected <= 5; ++expected) {
        expect(fatal(bool(e.execute_command(Command::MoveRight(false)))));
        expect(fatal(bool((e.selection().active.v) == (expected))));
    }
    for (std::size_t expected = 4; expected < 5; --expected) {
        expect(fatal(bool(e.execute_command(Command::MoveLeft(false)))));
        expect(fatal(bool((e.selection().active.v) == (expected))));
        if (expected == 0) break;
    }
};

"test_inline_marker_navigation_preserves_each_serialized_caret_slot"_test = [] {
    for (const auto& markdown : {std::string("_word_"), std::string("**x**"), std::string("`code`")}) {
        Editor editor(markdown);
        editor.set_caret(CharOffset(0));
        for (std::size_t expected = 1; expected <= markdown.size(); ++expected) {
            expect(fatal(bool(editor.execute_command(Command::MoveRight(false)))));
            expect(fatal(bool((editor.selection().active.v) == (expected))));
        }
        for (std::size_t expected = markdown.size(); expected > 0; --expected) {
            expect(fatal(bool(editor.execute_command(Command::MoveLeft(false)))));
            expect(fatal(bool((editor.selection().active.v) == (expected - 1))));
        }
    }
};

"test_editor_undo_redo"_test = [] {
    Editor e;
    e.execute_command(Command::InsertText(U"hello"));
    e.execute_command(Command::InsertText(U" world"));
    expect(fatal(bool((e.text_utf8()) == (std::string("hello world")))));
    e.undo();
    expect(fatal(bool((e.text_utf8()) == (std::string("hello")))));
    e.redo();
    expect(fatal(bool((e.text_utf8()) == (std::string("hello world")))));
};

"test_revision_monotonic_across_undo_redo"_test = [] {
    // Regression for HANDOFF deadly bug #9: revision must not reset on undo/redo.
    Editor e;
    e.execute_command(Command::InsertText(U"ab"));
    expect(fatal(bool((e.revision()) == (2u)))); // insert bumped 1->2
    e.set_selection(Selection::caret(CharOffset(2)));
    e.execute_command(Command::InsertText(U"cd"));
    expect(fatal(bool((e.revision()) == (3u)))); // ->3
    e.undo();
    expect(fatal(bool((e.text_utf8()) == (std::string("ab")))));
    expect(fatal(bool(e.revision() == 4u))); // unchanged (always++)
    e.redo();
    expect(fatal(bool(e.revision() == 5u)));
};

"test_editor_toggle_strong"_test = [] {
    Editor e("bold");
    e.set_selection(Selection{CharOffset(0), CharOffset(4), TextAffinity::Downstream});
    Command c; c.kind = CommandKind::ToggleStrong;
    e.execute_command(c);
    expect(fatal(bool(e.text_utf8() == "**bold**")));
};

"test_editor_delete_backward"_test = [] {
    Editor e("hello");
    e.set_caret(CharOffset(5));
    Command c; c.kind = CommandKind::DeleteBackward;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("hell")))));
    expect(fatal(bool(e.selection().affinity == TextAffinity::Upstream)));
};

"test_editor_insert_text_cursor_moves"_test = [] {
    Editor e("123");
    e.set_caret(CharOffset(1));
    e.execute_command(Command::InsertText(U" "));
    expect(fatal(bool((e.text_utf8()) == (std::string("1 23")))));
    expect(fatal(bool((e.selection().head().v) == (2u))));
};

"test_editor_insert_math_inline"_test = [] {
    Editor e("x2");
    e.set_selection(Selection{CharOffset(0), CharOffset(2), TextAffinity::Downstream});
    Command c; c.kind = CommandKind::InsertMathInline;
    e.execute_command(c);
    expect(fatal(bool(e.text_utf8() == "$x2$")));
};

"test_editor_insert_math_block"_test = [] {
    Editor e;
    Command c; c.kind = CommandKind::InsertMathBlock;
    e.execute_command(c);
    auto s = e.text_utf8();
    expect(fatal(bool(s.find("$$") != std::string::npos)));
};

"test_atomic_block_commands_insert_document_nodes"_test = [] {
    Editor math;
    Command math_command; math_command.kind = CommandKind::InsertMathBlock;
    math.execute_command(math_command);
    expect(fatal(bool(math.document().blocks.front().kind == BlockKind::MathBlock)));
    expect(fatal(bool((math.document_selection().active.node_id) == (math.document().blocks.front().id))));
    expect(fatal(bool(validate_document(math.document()).empty())));
    math.undo();
    expect(fatal(bool(math.document().blocks.front().kind == BlockKind::Paragraph)));

    Editor table;
    Command table_command; table_command.kind = CommandKind::InsertTable; table_command.rows = 2; table_command.cols = 3;
    table.execute_command(table_command);
    expect(fatal(bool(table.document().blocks.front().kind == BlockKind::Table)));
    expect(fatal(bool((table.document().blocks.front().table_header.size()) == (3u))));
    expect(fatal(bool((table.document().blocks.front().table_rows.size()) == (2u))));
    expect(fatal(bool((table.document_selection().active.node_id) == (table.document().blocks.front().table_header.front().id))));
    expect(fatal(bool(validate_document(table.document()).empty())));
};

"test_insert_text_uses_document_transaction"_test = [] {
    Editor e("hello");
    e.set_caret(CharOffset(5));
    const auto paragraph_id = e.document().blocks.front().id;
    auto transaction = e.execute_command(Command::InsertText(U" world"));
    expect(fatal(bool(transaction)));
    expect(fatal(bool((e.text_utf8()) == (std::string("hello world")))));
    expect(fatal(bool((e.document().blocks.front().id) == (paragraph_id))));
    expect(fatal(bool(e.has_document_undo())));
    e.undo();
    expect(fatal(bool((e.text_utf8()) == (std::string("hello")))));
};

"test_delete_transaction"_test = [] {
    Editor e("hello world");
    const auto paragraph_id = e.document().blocks.front().id;
    e.set_document_selection(DocumentSelection{
        DocumentPosition{paragraph_id, 5, TextAffinity::Downstream},
        DocumentPosition{paragraph_id, 11, TextAffinity::Downstream}});
    Command command; command.kind = CommandKind::DeleteSelection;
    expect(fatal(bool(e.execute_command(command))));
    expect(fatal(bool((e.markdown_utf8()) == (std::string("hello")))));
    expect(fatal(bool(e.has_document_undo())));
};

"test_paste_command_is_one_document_history_transaction"_test = [] {
    Editor editor("alpha");
    editor.set_caret(CharOffset(5));
    Command paste;
    paste.kind = CommandKind::Paste;
    paste.text = U"x\r\ny";
    auto transaction = editor.execute_command(paste);
    expect(fatal(bool(transaction)));
    expect(fatal(bool((editor.markdown_utf8()) == (std::string("alphax\n\ny")))));
    expect(fatal(bool((editor.document().blocks.size()) == (2u))));
    expect(fatal(bool((editor.document_selection().active.node_id) == (editor.document().blocks[1].id))));
    expect(fatal(bool(editor.has_document_undo())));
    expect(fatal(bool(editor.undo_document())));
    expect(fatal(bool((editor.markdown_utf8()) == (std::string("alpha")))));
    expect(fatal(bool((editor.document().blocks.size()) == (1u))));
    expect(fatal(bool((editor.document_selection().active.offset) == (5u))));
};

"test_toggle_strong_uses_document_transaction"_test = [] {
    Editor e("hello");
    const auto paragraph_id = e.document().blocks.front().id;
    e.set_selection(Selection{CharOffset(0), CharOffset(5), TextAffinity::Downstream});
    Command command; command.kind = CommandKind::ToggleStrong;
    auto transaction = e.execute_command(command);
    expect(fatal(bool(transaction)));
    expect(fatal(bool((e.text_utf8()) == (std::string("**hello**")))));
    expect(fatal(bool((e.document().blocks.front().id) == (paragraph_id))));
    expect(fatal(bool(e.document().blocks.front().children.front().kind == InlineKind::Strong)));
    e.undo();
    expect(fatal(bool((e.text_utf8()) == (std::string("hello")))));
};

"test_inline_format_commands_wrap_and_unwrap_inline_subtrees"_test = [] {
    Editor emphasis("alpha");
    const auto paragraph_id = emphasis.document().blocks.front().id;
    emphasis.set_document_selection(DocumentSelection{
        DocumentPosition{paragraph_id, 1, TextAffinity::Downstream},
        DocumentPosition{paragraph_id, 4, TextAffinity::Downstream}});
    Command command; command.kind = CommandKind::ToggleEmphasis;
    emphasis.execute_command(command);
    expect(fatal(bool((emphasis.text_utf8()) == (std::string("a*lph*a")))));
    expect(fatal(bool((emphasis.document().blocks.front().id) == (paragraph_id))));
    expect(fatal(bool(emphasis.document().blocks.front().children.size() > 1)));
    expect(fatal(bool(emphasis.document().blocks.front().children[1].kind == InlineKind::Emphasis)));
    emphasis.execute_command(command);
    expect(fatal(bool((emphasis.text_utf8()) == (std::string("alpha")))));

    Editor code("value");
    code.set_document_selection(DocumentSelection{
        DocumentPosition{code.document().blocks.front().id, 0, TextAffinity::Downstream},
        DocumentPosition{code.document().blocks.front().id, 5, TextAffinity::Downstream}});
    command.kind = CommandKind::ToggleInlineCode;
    code.execute_command(command);
    expect(fatal(bool((code.text_utf8()) == (std::string("`value`")))));
    expect(fatal(bool(code.document().blocks.front().children.front().kind == InlineKind::InlineCode)));

    Editor math("x+y");
    math.set_document_selection(DocumentSelection{
        DocumentPosition{math.document().blocks.front().id, 0, TextAffinity::Downstream},
        DocumentPosition{math.document().blocks.front().id, 3, TextAffinity::Downstream}});
    command.kind = CommandKind::InsertMathInline;
    math.execute_command(command);
    expect(fatal(bool((math.text_utf8()) == (std::string("$x+y$")))));
    expect(fatal(bool(math.document().blocks.front().children.front().kind == InlineKind::InlineMath)));
};

"test_typing_into_empty_format_node_stays_before_closing_marker"_test = [] {
    Editor editor;
    Command strong; strong.kind = CommandKind::ToggleStrong;
    editor.execute_command(strong);
    expect(fatal(bool((editor.text_utf8()) == (std::string("****")))));
    editor.execute_command(Command::InsertText(U"x"));
    expect(fatal(bool((editor.text_utf8()) == (std::string("**x**")))));
    expect(fatal(bool((editor.selection().head().v) == (3u))));
    expect(fatal(bool(editor.document().blocks.front().children.front().kind == InlineKind::Strong)));
    editor.undo();
    expect(fatal(bool((editor.text_utf8()) == (std::string("****")))));
    editor.undo();
    expect(fatal(bool(editor.text_utf8().empty())));
};

"test_inline_format_inside_table_cell_is_ast_native"_test = [] {
    Editor editor("| H |\n| --- |\n| X |");
    const auto cell_id = editor.document().blocks.front().table_rows.front().cells.front().id;
    editor.set_document_selection(DocumentSelection{
        DocumentPosition{cell_id, 0, TextAffinity::Downstream},
        DocumentPosition{cell_id, 1, TextAffinity::Downstream}});
    Command strong; strong.kind = CommandKind::ToggleStrong;
    editor.execute_command(strong);
    expect(fatal(bool((editor.text_utf8()) == (std::string("| H |\n| --- |\n| **X** |")))));
    expect(fatal(bool(editor.document().blocks.front().table_rows.front().cells.front().children.front().kind == InlineKind::Strong)));
    editor.undo();
    expect(fatal(bool((editor.text_utf8()) == (std::string("| H |\n| --- |\n| X |")))));
    expect(fatal(bool((editor.document().blocks.front().table_rows.front().cells.front().id) == (cell_id))));
};

"test_insert_table_contains_header"_test = [] {
    Editor e;
    Command c; c.kind = CommandKind::InsertTable; c.rows = 2; c.cols = 3;
    e.execute_command(c);
    auto s = e.text_utf8();
    expect(fatal(bool(s.find("| Header |") != std::string::npos)));
};

"test_table_tab_moves_to_next_cell"_test = [] {
    Editor e("| A | B |\n| --- | --- |\n| C | D |\n");
    e.set_caret(CharOffset(2));
    Command c; c.kind = CommandKind::MoveTableCellNext;
    e.execute_command(c);
    expect(fatal(bool((e.selection().head().v) == (6u))));
};

"test_table_insert_row_below"_test = [] {
    Editor e("| A | B |\n| --- | --- |\n| C | D |\n");
    e.set_caret(CharOffset(27));
    Command c; c.kind = CommandKind::InsertTableRowBelow;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("| A | B |\n| --- | --- |\n| C | D |\n|  |  |\n")))));
};

"test_table_delete_column"_test = [] {
    Editor e("| A | B |\n| --- | --- |\n| C | D |\n");
    e.set_caret(CharOffset(6));
    Command c; c.kind = CommandKind::DeleteTableColumn;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("| A |\n| --- |\n| C |\n")))));
};

"test_table_move_column_left"_test = [] {
    Editor e("| A | B |\n| --- | --- |\n| C | D |\n");
    e.set_caret(CharOffset(6));
    Command c; c.kind = CommandKind::MoveTableColumnLeft;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("| B | A |\n| --- | --- |\n| D | C |\n")))));
};

"test_table_column_alignment_is_preserved_by_structural_edits"_test = [] {
    Editor e("| A | B |\n| --- | --- |\n| 1 | 2 |\n");
    e.set_caret(CharOffset(6));
    Command align; align.kind = CommandKind::SetTableColumnAlignment; align.table_alignment = TableAlignment::Right;
    e.execute_command(align);
    expect(fatal(bool((e.text_utf8()) == (std::string("| A | B |\n| --- | ---: |\n| 1 | 2 |\n")))));
    expect(fatal(bool((e.selection().head().v) == (6u))));
    Command move; move.kind = CommandKind::MoveTableColumnLeft;
    e.execute_command(move);
    expect(fatal(bool((e.text_utf8()) == (std::string("| B | A |\n| ---: | --- |\n| 2 | 1 |\n")))));
};

"test_normalize_table_canonicalizes_widths_without_losing_alignment"_test = [] {
    Editor e("|Long|B|\n|:--|--:|\n|1|2|\n");
    e.set_caret(CharOffset(2));
    Command c; c.kind = CommandKind::NormalizeTable;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("| Long | B |\n| :--- | ---: |\n| 1 | 2 |\n")))));
};

"test_table_insert_row_and_column_at_visual_boundaries"_test = [] {
    Editor e("| A | B |\n| --- | --- |\n| 1 | 2 |\n");
    e.set_caret(CharOffset(2));
    Command row; row.kind = CommandKind::InsertTableRowAt; row.table_index = 1;
    e.execute_command(row);
    expect(fatal(bool((e.text_utf8()) == (std::string("| A | B |\n| --- | --- |\n|  |  |\n| 1 | 2 |\n")))));
    Command column; column.kind = CommandKind::InsertTableColumnAt; column.table_index = 1;
    e.execute_command(column);
    expect(fatal(bool((e.text_utf8()) == (std::string("| A |  | B |\n| --- | --- | --- |\n|  |  |  |\n| 1 |  | 2 |\n")))));
};

"test_table_drag_moves_row_and_column_atomically"_test = [] {
    Editor e("| A | B |\n| --- | --- |\n| 1 | 2 |\n");
    e.set_caret(CharOffset(29));
    Command row; row.kind = CommandKind::MoveTableRowTo; row.table_index = 0;
    e.execute_command(row);
    expect(fatal(bool((e.text_utf8()) == (std::string("| 1 | 2 |\n| --- | --- |\n| A | B |\n")))));
    e.set_caret(CharOffset(8));
    Command column; column.kind = CommandKind::MoveTableColumnTo; column.table_index = 0;
    e.execute_command(column);
    expect(fatal(bool((e.text_utf8()) == (std::string("| 2 | 1 |\n| --- | --- |\n| B | A |\n")))));
};

"test_table_structure_commands_preserve_ast_nodes_and_document_history"_test = [] {
    Editor editor("| A | B |\n| --- | --- |\n| 1 | 2 |");
    const auto table_id = editor.document().blocks.front().id;
    const auto first_cell_id = editor.document().blocks.front().table_header.front().id;
    const auto second_cell_id = editor.document().blocks.front().table_header[1].id;
    editor.set_document_selection(DocumentSelection::caret(
        DocumentPosition{second_cell_id, 0, TextAffinity::Downstream}));
    Command move; move.kind = CommandKind::MoveTableColumnLeft;
    editor.execute_command(move);
    expect(fatal(bool((editor.document().blocks.front().id) == (table_id))));
    expect(fatal(bool((editor.document().blocks.front().table_header[0].id) == (second_cell_id))));
    expect(fatal(bool((editor.document().blocks.front().table_header[1].id) == (first_cell_id))));
    expect(fatal(bool(editor.has_document_undo())));
    expect(fatal(bool(validate_document(editor.document()).empty())));
    editor.undo();
    expect(fatal(bool((editor.document().blocks.front().table_header[0].id) == (first_cell_id))));
    expect(fatal(bool((editor.document().blocks.front().table_header[1].id) == (second_cell_id))));
};

"test_typing_into_empty_table_cell_updates_source_and_ast"_test = [] {
    Editor e("| A | B |\n| --- | --- |\n|   | 2 |\n");
    auto table = table_projection(e);
    expect(fatal(bool(table.has_value())));
    expect(fatal(bool(table && table->rows.size() == 3)));
    if (!table || table->rows.size() != 3) return;
    expect(fatal(bool(table->rows[2].cells[0].text.empty())));
    expect(fatal(bool((table->rows[2].cells[0].content_range.len()) == (0u))));
    auto emptyCellOffset = table->rows[2].cells[0].content_range.start;
    e.set_caret(emptyCellOffset);
    e.execute_command(Command::InsertText(U"x"));
    expect(fatal(bool((e.text_utf8()) == (std::string("| A | B |\n| --- | --- |\n| x | 2 |\n")))));
    expect(fatal(bool((e.document().blocks.size()) == (1u))));
    expect(fatal(bool(e.document().blocks[0].kind == BlockKind::Table)));
    auto const& row = e.document().blocks[0].table_rows[0];
    expect(fatal(bool((row.cells.size()) == (2u))));
    expect(fatal(bool((cps_to_utf8(block_inline_text_content(row.cells[0].children))) == (std::string("x")))));
};

"test_enter_at_end_of_table_exits_after_closing_pipe"_test = [] {
    Editor e("| H1 | H2 |\n| --- | --- |\n| A | B |");
    auto table = table_projection(e);
    expect(fatal(bool(table.has_value())));
    if (!table) return;
    auto const& last_cell = table->rows.back().cells.back();
    e.set_caret(last_cell.content_range.end);
    Command command;
    command.kind = CommandKind::InsertNewline;
    e.execute_command(command);
    expect(fatal(bool((e.text_utf8()) == (std::string("| H1 | H2 |\n| --- | --- |\n| A | B |\n\n")))));
    expect(fatal(bool((e.selection().head().v) == (e.text_cps().size() - 1))));
};

"test_enter_at_end_of_table_reuses_trailing_newline"_test = [] {
    Editor e("| H |\n| --- |\n| A |\n");
    auto table = table_projection(e);
    expect(fatal(bool(table.has_value())));
    if (!table) return;
    e.set_caret(table->rows.back().cells.back().content_range.end);
    Command command;
    command.kind = CommandKind::InsertNewline;
    e.execute_command(command);
    expect(fatal(bool((e.text_utf8()) == (std::string("| H |\n| --- |\n| A |\n\n")))));
    expect(fatal(bool((e.selection().head().v) == (e.text_cps().size() - 1))));
};

"test_enter_in_table_moves_to_same_column_of_next_row"_test = [] {
    Editor e("| H1 | H2 |\n| --- | --- |\n| A1 | A2 |\n| B1 | B2 |");
    auto table = table_projection(e);
    expect(fatal(bool(table.has_value())));
    if (!table) return;
    auto source_before = e.text_utf8();
    e.set_caret(table->rows[2].cells[1].content_range.end);
    Command command;
    command.kind = CommandKind::InsertNewline;
    e.execute_command(command);
    expect(fatal(bool((e.text_utf8()) == (source_before))));
    expect(fatal(bool((e.selection().head().v) == (table->rows[3].cells[1].content_range.start.v))));
};

"test_enter_in_table_header_skips_separator_row"_test = [] {
    Editor e("| H1 | H2 |\n| --- | --- |\n| A1 | A2 |");
    auto table = table_projection(e);
    expect(fatal(bool(table.has_value())));
    if (!table) return;
    auto source_before = e.text_utf8();
    e.set_caret(table->rows[0].cells[0].content_range.start);
    Command command;
    command.kind = CommandKind::InsertNewline;
    e.execute_command(command);
    expect(fatal(bool((e.text_utf8()) == (source_before))));
    expect(fatal(bool((e.selection().head().v) == (table->rows[2].cells[0].content_range.start.v))));
};

"test_enter_in_any_cell_of_last_table_row_exits_table"_test = [] {
    Editor e("| H1 | H2 |\n| --- | --- |\n| A1 | A2 |");
    auto table = table_projection(e);
    expect(fatal(bool(table.has_value())));
    if (!table) return;
    e.set_caret(table->rows.back().cells[0].content_range.start);
    Command command;
    command.kind = CommandKind::InsertNewline;
    e.execute_command(command);
    expect(fatal(bool((e.text_utf8()) == (std::string("| H1 | H2 |\n| --- | --- |\n| A1 | A2 |\n\n")))));
    expect(fatal(bool((e.selection().head().v) == (e.text_cps().size() - 1))));
};

"test_empty_table_cell_has_zero_width_content_after_leading_padding"_test = [] {
    Editor e("| H |\n| --- |\n|     |");
    auto table = table_projection(e);
    expect(fatal(bool(table.has_value())));
    if (!table) return;
    auto const& cell = table->rows.back().cells.front();
    expect(fatal(bool((cell.content_range.len()) == (0u))));
    expect(fatal(bool((cell.content_range.start.v) == (cell.content_range.end.v))));
    expect(fatal(bool((cell.content_range.start.v) == (table->rows.back().line_range.start.v + 2))));
    expect(fatal(bool(e.text_cps()[cell.content_range.start.v - 1] == U' ')));
    expect(fatal(bool(e.text_cps()[cell.content_range.start.v] == U' ')));
};

"test_backspace_and_delete_do_not_remove_empty_table_padding_or_pipe"_test = [] {
    Editor e("| H |\n| --- |\n|     |");
    auto table = table_projection(e);
    expect(fatal(bool(table.has_value())));
    if (!table) return;
    auto offset = table->rows.back().cells.front().content_range.start;
    auto source = e.text_utf8();
    e.set_caret(offset);
    Command backward;
    backward.kind = CommandKind::DeleteBackward;
    expect(fatal(bool(!e.execute_command(backward))));
    expect(fatal(bool((e.text_utf8()) == (source))));
    Command forward;
    forward.kind = CommandKind::DeleteForward;
    expect(fatal(bool(!e.execute_command(forward))));
    expect(fatal(bool((e.text_utf8()) == (source))));
};

"test_table_character_delete_stays_inside_cell_content"_test = [] {
    Editor e("| H |\n| --- |\n| ABC |");
    auto table = table_projection(e);
    expect(fatal(bool(table.has_value())));
    if (!table) return;
    auto cell = table->rows.back().cells.front();
    e.set_caret(cell.content_range.end);
    Command backward;
    backward.kind = CommandKind::DeleteBackward;
    e.execute_command(backward);
    expect(fatal(bool((e.text_utf8()) == (std::string("| H |\n| --- |\n| AB |")))));
    table = table_projection(e);
    expect(fatal(bool(table.has_value())));
    if (!table) return;
    e.set_caret(table->rows.back().cells.front().content_range.start);
    Command forward;
    forward.kind = CommandKind::DeleteForward;
    e.execute_command(forward);
    expect(fatal(bool((e.text_utf8()) == (std::string("| H |\n| --- |\n| B |")))));
    expect(fatal(bool(e.document().blocks[0].kind == BlockKind::Table)));
};

"test_deleting_last_table_cell_character_keeps_empty_caret_stable"_test = [] {
    Editor e("| H |\n| --- |\n| X |");
    auto table = table_projection(e);
    expect(fatal(bool(table.has_value())));
    if (!table) return;
    e.set_caret(table->rows.back().cells.front().content_range.end);
    Command command;
    command.kind = CommandKind::DeleteBackward;
    e.execute_command(command);
    expect(fatal(bool((e.text_utf8()) == (std::string("| H |\n| --- |\n|  |")))));
    table = table_projection(e);
    expect(fatal(bool(table.has_value())));
    if (!table) return;
    auto const& empty = table->rows.back().cells.front();
    expect(fatal(bool((empty.content_range.len()) == (0u))));
    expect(fatal(bool((e.selection().head().v) == (empty.content_range.start.v))));
    expect(fatal(bool(e.document().blocks[0].kind == BlockKind::Table)));
};

"test_delete_from_adjacent_paragraph_does_not_consume_table_boundary"_test = [] {
    Editor e("| H |\n| --- |\n| A |\n\nafter");
    auto table = table_projection(e);
    expect(fatal(bool(table.has_value())));
    if (!table) return;
    auto source = e.text_utf8();
    expect(fatal(bool((e.document().blocks.size()) == (2u))));
    e.set_document_selection(DocumentSelection::caret(DocumentPosition{
        e.document().blocks[1].id, 0, TextAffinity::Upstream}));
    Command command;
    command.kind = CommandKind::DeleteBackward;
    expect(fatal(bool(!e.execute_command(command))));
    expect(fatal(bool((e.text_utf8()) == (source))));
    expect(fatal(bool(e.document().blocks[0].kind == BlockKind::Table)));
};

"test_table_cross_cell_selection_delete_preserves_structure"_test = [] {
    Editor e("| H1 | H2 |\n| --- | --- |\n| A1 | A2 |");
    auto table = table_projection(e);
    expect(fatal(bool(table.has_value())));
    if (!table) return;
    auto const& row = table->rows.back();
    Selection selection;
    selection.anchor = row.cells[0].content_range.start;
    selection.active = row.cells[1].content_range.end;
    e.set_selection(selection);
    Command command;
    command.kind = CommandKind::DeleteSelection;
    e.execute_command(command);
    expect(fatal(bool((e.text_utf8()) == (std::string("| H1 | H2 |\n| --- | --- |\n|  |  |")))));
    expect(fatal(bool(e.document().blocks[0].kind == BlockKind::Table)));
};

"test_insert_toc_contains_marker"_test = [] {
    Editor e;
    Command c; c.kind = CommandKind::InsertToc;
    e.execute_command(c);
    expect(fatal(bool(e.text_utf8().find("[TOC]") != std::string::npos)));
};

"test_enter_in_plain_paragraph_inserts_semantic_block_break"_test = [] {
    Editor e("alphaomega");
    e.set_caret(CharOffset(5));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("alpha\n\nomega")))));
    expect(fatal(bool((e.selection().head().v) == (7u))));
    expect(fatal(bool((e.document().blocks.size()) == (2u))));
    auto structure = build_source_structure(e.document());
    expect(fatal(bool((structure.blocks.size()) == (2u))));
    expect(fatal(bool(structure.blocks[0].kind == SourceBlockKind::Semantic)));
    expect(fatal(bool(structure.blocks[1].kind == SourceBlockKind::Semantic)));
};

"test_enter_at_block_end_reuses_existing_separator_and_creates_one_blank"_test = [] {
    Editor e("# H\n\n## Next");
    e.set_caret(CharOffset(3));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("# H\n\n\n## Next")))));
    expect(fatal(bool((e.selection().head().v) == (5u))));
    expect(fatal(bool((e.document().blocks.size()) == (3u))));
    auto structure = build_source_structure(e.document());
    expect(fatal(bool((structure.blocks.size()) == (3u))));
    expect(fatal(bool(structure.blocks[0].kind == SourceBlockKind::Semantic)));
    expect(fatal(bool(structure.blocks[1].kind == SourceBlockKind::Blank)));
    expect(fatal(bool(structure.blocks[2].kind == SourceBlockKind::Semantic)));
};

"test_repeated_enter_between_early_blocks_keeps_incremental_ranges_in_sync"_test = [] {
    Editor e("## Sample\n\n```cpp\nx\n```\n\n## Later 1\n\n## Later 2\n\n## Later 3\n\n## Later 4\n");
    e.set_caret(CharOffset(9));
    const auto initial_structure = build_source_structure(e.document());
    const auto initial_blank_count = static_cast<std::size_t>(std::count_if(
        initial_structure.blocks.begin(), initial_structure.blocks.end(),
        [](const auto& block) { return block.kind == SourceBlockKind::Blank; }));
    for (std::size_t count = 1; count <= 4; ++count) {
        Command c; c.kind = CommandKind::InsertNewline;
        e.execute_command(c);
        expect(fatal(bool((e.selection().head().v) == (10u + count))));
        auto full = parse_text(e.revision(), e.text_utf8());
        expect(fatal(bool((e.document().blocks.size()) == (full.document.blocks.size()))));
        for (std::size_t index = 0; index < e.document().blocks.size() && index < full.document.blocks.size(); ++index) {
            expect(fatal(bool(e.document().blocks[index].kind == full.document.blocks[index].kind)));
            auto* incremental_range = e.document().source_map.find_node_by_id(e.document().blocks[index].id);
            auto* full_range = full.document.source_map.find_node_by_id(full.document.blocks[index].id);
            expect(fatal(bool(incremental_range != nullptr)));
            expect(fatal(bool(full_range != nullptr)));
            if (incremental_range && full_range) {
                expect(fatal(bool(incremental_range->source_range.start == full_range->source_range.start)));
                expect(fatal(bool(incremental_range->source_range.end == full_range->source_range.end)));
                expect(fatal(bool(incremental_range->content_range.start == full_range->content_range.start)));
                expect(fatal(bool(incremental_range->content_range.end == full_range->content_range.end)));
            }
        }
        auto structure = build_source_structure(e.document());
        std::size_t blank_count = 0;
        for (const auto& block : structure.blocks) if (block.kind == SourceBlockKind::Blank) ++blank_count;
        expect(fatal(bool((blank_count) == (initial_blank_count + count))));
    }
};

"test_backspace_before_opening_fence_does_not_mutate_ast_separator"_test = [] {
    Editor e("before\n\n```cpp\n```\n");
    e.set_caret(CharOffset(8));
    Command c; c.kind = CommandKind::DeleteBackward;
    const auto changed = e.execute_command(c);
    expect(fatal(bool(!changed)));
    expect(fatal(bool((e.text_utf8()) == (std::string("before\n\n```cpp\n```\n")))));
    expect(fatal(bool((e.selection().head().v) == (8u))));
    expect(fatal(bool((e.document().blocks.size()) == (2u))));
    expect(fatal(bool(e.document().blocks[0].kind == BlockKind::Paragraph)));
    expect(fatal(bool(e.document().blocks[1].kind == BlockKind::CodeBlock)));
    auto* range = e.document().source_map.find_node_by_id(e.document().blocks[1].id);
    expect(fatal(bool(range != nullptr)));
    if (range) {
        expect(fatal(bool((range->source_range.start.v) == (8u))));
        expect(fatal(bool((range->marker_ranges.size()) == (2u))));
    }
};

"test_enter_inside_code_block_inserts_single_newline"_test = [] {
    Editor e("```\nabc\n```\n");
    e.set_caret(CharOffset(7));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("```\nabc\n\n```\n")))));
    expect(fatal(bool((e.selection().head().v) == (8u))));
    expect(fatal(bool((e.document().blocks.size()) == (1u))));
    expect(fatal(bool(e.document().blocks[0].kind == BlockKind::CodeBlock)));
};

"test_enter_inside_indented_code_block_preserves_indent"_test = [] {
    Editor e("    abc\n");
    e.set_caret(CharOffset(7));
    Command command;
    command.kind = CommandKind::InsertNewline;
    e.execute_command(command);
    expect(fatal(bool((e.text_utf8()) == (std::string("    abc\n    \n")))));
    expect(fatal(bool((e.selection().head().v) == (12u))));
    expect(fatal(bool((e.document().blocks.size()) == (1u))));
    expect(fatal(bool(e.document().blocks[0].kind == BlockKind::CodeBlock)));
    expect(fatal(bool(e.document().blocks[0].code_indented)));
};

"test_soft_break_in_plain_paragraph_inserts_single_newline"_test = [] {
    Editor e("alphaomega");
    e.set_caret(CharOffset(5));
    Command c; c.kind = CommandKind::InsertSoftBreak;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("alpha\nomega")))));
    expect(fatal(bool((e.selection().head().v) == (6u))));
    expect(fatal(bool((e.document().blocks.size()) == (1u))));
};

"test_enter_on_empty_paragraph_inserts_one_empty_sibling"_test = [] {
    Editor e("alpha\n\n");
    e.set_caret(CharOffset(7));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("alpha\n\n\n")))));
    expect(fatal(bool((e.selection().head().v) == (7u))));
    expect(fatal(bool((e.document().blocks.size()) == (3u))));
    auto structure = build_source_structure(e.document());
    expect(fatal(bool((structure.blocks.size()) == (3u))));
    expect(fatal(bool(structure.blocks[1].kind == SourceBlockKind::Blank)));
    expect(fatal(bool(structure.blocks[2].kind == SourceBlockKind::Blank)));
};

"test_backspace_on_empty_block_deletes_semantic_block"_test = [] {
    Editor e("alpha\n\n");
    e.set_caret(CharOffset(7));
    Command c; c.kind = CommandKind::DeleteBackward;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("alpha\n")))));
    expect(fatal(bool((e.selection().head().v) == (5u))));
    expect(fatal(bool((e.document().blocks.size()) == (1u))));
};

"test_backspace_on_consecutive_empty_block_deletes_block_span"_test = [] {
    Editor e("alpha\n\n\n\n");
    e.set_caret(CharOffset(9));
    Command c; c.kind = CommandKind::DeleteBackward;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("alpha\n\n\n")))));
    expect(fatal(bool((e.selection().head().v) == (7u))));
    expect(fatal(bool((e.document().blocks.size()) == (3u))));
    auto structure = build_source_structure(e.document());
    expect(fatal(bool((structure.blocks.size()) == (3u))));
};

"test_enter_continues_unordered_list"_test = [] {
    Editor e("- alpha");
    e.set_caret(CharOffset(7));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("- alpha\n- ")))));
    expect(fatal(bool((e.document().blocks.size()) == (1u))));
    expect(fatal(bool((e.document().blocks[0].list_items.size()) == (2u))));
    {
        expect(fatal(bool((e.document_selection().active.node_id) == (e.document().blocks[0].list_items[1].children[0].id))));
        expect(fatal(bool((e.document_selection().active.offset) == (0u))));
    }
};

"test_enter_continues_ordered_list"_test = [] {
    Editor e("9. alpha");
    e.set_caret(CharOffset(8));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("9. alpha\n10. ")))));
    expect(fatal(bool(e.document().blocks[0].list_ordered)));
    expect(fatal(bool((e.document().blocks[0].list_items.size()) == (2u))));
    expect(fatal(bool(e.document().blocks[0].list_items[1].marker.empty())));
    expect(fatal(bool((e.document_selection().active.node_id) == (e.document().blocks[0].list_items[1].children[0].id))));
};

"test_enter_continues_task_list_unchecked"_test = [] {
    Editor e("- [x] alpha");
    e.set_caret(CharOffset(11));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("- [x] alpha\n- [ ] ")))));
    expect(fatal(bool((e.document().blocks[0].task_items.size()) == (2u))));
    expect(fatal(bool(e.document().blocks[0].task_items[0].checked)));
    expect(fatal(bool(!e.document().blocks[0].task_items[1].checked)));
    expect(fatal(bool((e.document_selection().active.node_id) == (e.document().blocks[0].task_items[1].children[0].id))));
};

"test_enter_exits_list_one_level_before_exiting_blockquote"_test = [] {
    Editor e("> * alpha");
    e.set_caret(CharOffset(9));
    Command newline; newline.kind = CommandKind::InsertNewline;
    e.execute_command(newline);
    expect(fatal(bool((e.document().blocks.size()) == (1u))));
    expect(fatal(bool((e.document().blocks[0].kind) == (BlockKind::BlockQuote))));
    expect(fatal(bool((e.document().blocks[0].quote_children[0].list_items.size()) == (2u))));
    e.execute_command(newline);
    expect(fatal(bool((e.document().blocks.size()) == (1u))));
    expect(fatal(bool((e.document().blocks[0].quote_children.size()) == (2u))));
    expect(fatal(bool((e.document().blocks[0].quote_children[0].kind) == (BlockKind::List))));
    expect(fatal(bool((e.document().blocks[0].quote_children[1].kind) == (BlockKind::Paragraph))));
    e.execute_command(newline);
    expect(fatal(bool((e.document().blocks.size()) == (2u))));
    expect(fatal(bool((e.document().blocks[0].kind) == (BlockKind::BlockQuote))));
    expect(fatal(bool((e.document().blocks[1].kind) == (BlockKind::Paragraph))));
    expect(fatal(bool(validate_document(e.document()).empty())));
};

"test_empty_list_item_reuses_following_empty_quote_line"_test = [] {
    std::string source =
        "> #### The quarterly results look great!\n"
        "> \n"
        "> * Revenue was off the chart.\n"
        "> * Profits were higher than ever.\n"
        "> \n"
        "> _Everything_ is going according to **plan**.";
    Editor e(source);
    auto profits_end = source.find("\n> \n", source.find("Profits"));
    expect(fatal(bool(profits_end != std::string::npos)));
    if (profits_end == std::string::npos) return;
    e.set_caret(CharOffset(profits_end));
    Command newline; newline.kind = CommandKind::InsertNewline;
    e.execute_command(newline);
    expect(fatal(bool(validate_document(e.document()).empty())));
    auto empty_item_id = e.document_selection().active.node_id;
    e.execute_command(newline);
    expect(fatal(bool(validate_document(e.document()).empty())));
    expect(fatal(bool((e.document_selection().active.node_id) == (empty_item_id))));
};

"test_enter_continues_blockquote"_test = [] {
    Editor e("> alpha");
    e.set_caret(CharOffset(7));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    expect(fatal(bool((e.document().blocks.size()) == (1u))));
    expect(fatal(bool((e.document().blocks[0].kind) == (BlockKind::BlockQuote))));
    expect(fatal(bool((e.document().blocks[0].quote_children.size()) == (2u))));
    expect(fatal(bool((block_inline_text_content(e.document().blocks[0].quote_children[0].children)) == (std::u32string(U"alpha")))));
    expect(fatal(bool((e.document_selection().active.node_id) == (e.document().blocks[0].quote_children[1].id))));
};

"test_second_enter_exits_empty_blockquote_line"_test = [] {
    Editor e("> alpha");
    e.set_caret(CharOffset(7));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    e.execute_command(c);
    expect(fatal(bool((e.document().blocks.size()) == (2u))));
    expect(fatal(bool((e.document().blocks[0].kind) == (BlockKind::BlockQuote))));
    expect(fatal(bool((e.document().blocks[1].kind) == (BlockKind::Paragraph))));
    expect(fatal(bool((e.document_selection().active.node_id) == (e.document().blocks[1].id))));
};

"test_second_enter_removes_the_empty_quote_line_before_a_following_block"_test = [] {
    Editor e("> alpha\n\nafter");
    e.set_caret(CharOffset(7));
    Command newline; newline.kind = CommandKind::InsertNewline;
    e.execute_command(newline);
    expect(fatal(bool((e.document().blocks.size()) == (2u))));
    expect(fatal(bool((e.document().blocks[0].quote_children.size()) == (2u))));
    e.execute_command(newline);
    expect(fatal(bool((e.document().blocks.size()) == (3u))));
    expect(fatal(bool((e.document().blocks[0].kind) == (BlockKind::BlockQuote))));
    expect(fatal(bool((e.document().blocks[1].kind) == (BlockKind::Paragraph))));
    expect(fatal(bool((e.document().blocks[2].kind) == (BlockKind::Paragraph))));
    expect(fatal(bool((block_inline_text_content(e.document().blocks[2].children)) == (std::u32string(U"after")))));
};

"test_enter_exits_nested_blockquotes_one_level_at_a_time"_test = [] {
    Editor e("> > alpha");
    e.set_caret(CharOffset(9));
    Command newline; newline.kind = CommandKind::InsertNewline;
    e.execute_command(newline);
    expect(fatal(bool((e.document().blocks[0].quote_children[0].quote_children.size()) == (2u))));
    e.execute_command(newline);
    expect(fatal(bool((e.document().blocks.size()) == (1u))));
    expect(fatal(bool((e.document().blocks[0].quote_children.size()) == (2u))));
    expect(fatal(bool((e.document().blocks[0].quote_children[0].kind) == (BlockKind::BlockQuote))));
    expect(fatal(bool((e.document().blocks[0].quote_children[1].kind) == (BlockKind::Paragraph))));
    e.execute_command(newline);
    expect(fatal(bool((e.document().blocks.size()) == (2u))));
    expect(fatal(bool((e.document().blocks[0].kind) == (BlockKind::BlockQuote))));
    expect(fatal(bool((e.document().blocks[1].kind) == (BlockKind::Paragraph))));
    expect(fatal(bool(validate_document(e.document()).empty())));
};

"test_backspace_exits_nested_blockquotes_one_level_at_a_time"_test = [] {
    Editor e("> > alpha");
    const auto paragraph_id = e.document().blocks[0].quote_children[0].quote_children[0].id;
    auto first = e.execute_document_delete_backward(DocumentSelection::caret(
        DocumentPosition{paragraph_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(first.has_value())));
    expect(fatal(bool((e.document().blocks.size()) == (1u))));
    expect(fatal(bool((e.document().blocks[0].kind) == (BlockKind::BlockQuote))));
    expect(fatal(bool((e.document().blocks[0].quote_children[0].id) == (paragraph_id))));
    auto second = e.execute_document_delete_backward(e.document_selection());
    expect(fatal(bool(second.has_value())));
    expect(fatal(bool((e.document().blocks.size()) == (1u))));
    expect(fatal(bool((e.document().blocks[0].kind) == (BlockKind::Paragraph))));
    expect(fatal(bool((e.document().blocks[0].id) == (paragraph_id))));
    expect(fatal(bool(e.undo_document())));
    expect(fatal(bool((e.document().blocks[0].kind) == (BlockKind::BlockQuote))));
    expect(fatal(bool(e.redo_document())));
    expect(fatal(bool((e.document().blocks[0].kind) == (BlockKind::Paragraph))));
};

"test_backspace_at_first_quote_content_start_removes_exactly_one_level"_test = [] {
    Editor e("> > alpha");
    e.set_caret(CharOffset(4));
    Command backspace; backspace.kind = CommandKind::DeleteBackward;
    e.execute_command(backspace);
    expect(fatal(bool((e.text_utf8()) == (std::string("> alpha")))));
    expect(fatal(bool((e.document_selection().active.offset) == (0u))));
    e.execute_command(backspace);
    expect(fatal(bool((e.text_utf8()) == (std::string("alpha")))));
    expect(fatal(bool((e.document().blocks[0].kind) == (BlockKind::Paragraph))));
    expect(fatal(bool((e.document_selection().active.node_id) == (e.document().blocks[0].id))));
};

"test_backspace_at_following_quote_line_start_joins_the_same_depth_first"_test = [] {
    Editor e("> > first\n> > second");
    e.set_caret(CharOffset(14));
    Command backspace; backspace.kind = CommandKind::DeleteBackward;
    e.execute_command(backspace);
    expect(fatal(bool((e.text_utf8()) == (std::string("> > firstsecond")))));
    expect(fatal(bool((e.document_selection().active.offset) == (5u))));
    expect(fatal(bool((e.document().blocks[0].quote_children[0].quote_children.size()) == (1u))));
};

"test_backspace_join_removes_the_preceding_hard_break_marker"_test = [] {
    Editor e("> > first  \n> > second");
    e.set_caret(CharOffset(16));
    Command backspace; backspace.kind = CommandKind::DeleteBackward;
    e.execute_command(backspace);
    expect(fatal(bool((e.text_utf8()) == (std::string("> > firstsecond")))));
    expect(fatal(bool((e.document_selection().active.offset) == (5u))));
};

"test_backspace_inside_nested_quote_content_never_touches_quote_markers"_test = [] {
    Editor e("> > abcdefghijklmnopqrstuvwxyz");
    e.set_caret(CharOffset(18));
    Command backspace; backspace.kind = CommandKind::DeleteBackward;
    e.execute_command(backspace);
    expect(fatal(bool((e.text_utf8()) == (std::string("> > abcdefghijklmopqrstuvwxyz")))));
    expect(fatal(bool((e.document_selection().active.offset) == (13u))));
};

"test_blockquote_newline_with_following_block_keeps_an_editable_quote_line"_test = [] {
    Editor e("> alpha\n\nafter");
    e.set_caret(CharOffset(7));
    Command newline; newline.kind = CommandKind::InsertNewline;
    e.execute_command(newline);
    auto insertion = e.execute_document_insert_text(e.document_selection(), U"11111");
    expect(fatal(bool(insertion.has_value())));
    expect(fatal(bool((e.document().blocks[0].quote_children.size()) == (2u))));
    expect(fatal(bool((block_inline_text_content(e.document().blocks[0].quote_children[1].children)) == (std::u32string(U"11111")))));
    auto split = e.execute_document_enter(e.document_selection());
    expect(fatal(bool(split.has_value())));
    expect(fatal(bool((e.document().blocks[0].quote_children.size()) == (3u))));
    expect(fatal(bool((block_inline_text_content(e.document().blocks[0].quote_children[1].children)) == (std::u32string(U"11111")))));
    expect(fatal(bool(block_inline_text_content(e.document().blocks[0].quote_children[2].children).empty())));
    expect(fatal(bool(validate_document(e.document()).empty())));
};

"test_backspace_removes_an_empty_blockquote_prefix_atomically"_test = [] {
    Editor e("> alpha  \n> \n\nafter");
    e.set_caret(CharOffset(12));
    Command backspace; backspace.kind = CommandKind::DeleteBackward;
    e.execute_command(backspace);
    expect(fatal(bool((e.text_utf8()) == (std::string("> alpha\n\nafter")))));
    expect(fatal(bool((e.selection().head().v) == (7u))));
};

"test_enter_on_empty_indented_code_line_exits_the_code_block"_test = [] {
    Editor e("    alpha\n    \n\nafter");
    e.set_caret(CharOffset(14));
    Command newline; newline.kind = CommandKind::InsertNewline;
    e.execute_command(newline);
    expect(fatal(bool((e.text_utf8()) == (std::string("    alpha\n\n\nafter")))));
    expect(fatal(bool((e.selection().head().v) == (11u))));
    expect(fatal(bool((e.document().blocks.size()) == (3u))));
    expect(fatal(bool(e.document().blocks[0].kind == BlockKind::CodeBlock)));
    expect(fatal(bool(e.document().blocks[1].kind == BlockKind::Paragraph)));
    expect(fatal(bool(e.document().blocks[1].children.empty())));
    expect(fatal(bool(e.document().blocks[2].kind == BlockKind::Paragraph)));
};

"test_backspace_on_empty_indented_code_line_exits_the_code_block"_test = [] {
    Editor e("    alpha\n    \n\nafter");
    e.set_caret(CharOffset(14));
    Command backspace; backspace.kind = CommandKind::DeleteBackward;
    e.execute_command(backspace);
    expect(fatal(bool((e.text_utf8()) == (std::string("    alpha\n\nafter")))));
    expect(fatal(bool((e.document_selection().active.node_id) == (e.document().blocks[1].id))));
    expect(fatal(bool((e.document_selection().active.offset) == (0u))));
    expect(fatal(bool(e.document_selection().active.affinity == TextAffinity::Upstream)));
};

"test_delete_forward_inside_indented_code_edits_code_node"_test = [] {
    Editor editor("    ab\n");
    const auto code_id = editor.document().blocks[0].id;
    editor.set_caret(CharOffset(5));
    Command command;
    command.kind = CommandKind::DeleteForward;
    expect(fatal(bool(editor.execute_command(command))));
    expect(fatal(bool((editor.document().blocks[0].id) == (code_id))));
    expect(fatal(bool((editor.document().blocks[0].code_text) == (std::u32string(U"a\n")))));
    expect(fatal(bool((editor.document_selection().active.node_id) == (code_id))));
    expect(fatal(bool((editor.document_selection().active.offset) == (1u))));
};

"test_enter_on_empty_list_exits_list"_test = [] {
    Editor e("- ");
    e.set_caret(CharOffset(2));
    Command c; c.kind = CommandKind::InsertNewline;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("")))));
    expect(fatal(bool((e.selection().head().v) == (0u))));
};

"test_toggle_unordered_list"_test = [] {
    Editor e("alpha");
    e.set_caret(CharOffset(5));
    Command c; c.kind = CommandKind::ToggleUnorderedList;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("- alpha")))));
    expect(fatal(bool((e.selection().head().v) == (7u))));
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("alpha")))));
    expect(fatal(bool((e.selection().head().v) == (5u))));
};

"test_toggle_ordered_list_replaces_unordered_marker"_test = [] {
    Editor e("- alpha");
    e.set_caret(CharOffset(7));
    Command c; c.kind = CommandKind::ToggleOrderedList;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("1. alpha")))));
    expect(fatal(bool((e.selection().head().v) == (8u))));
};

"test_toggle_task_list_replaces_ordered_marker"_test = [] {
    Editor e("1. alpha");
    e.set_caret(CharOffset(8));
    Command c; c.kind = CommandKind::ToggleTaskList;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("- [ ] alpha")))));
    expect(fatal(bool((e.selection().head().v) == (11u))));
};

"test_toggle_task_checkbox"_test = [] {
    Editor e("- [ ] alpha");
    e.set_caret(CharOffset(3));
    Command c; c.kind = CommandKind::ToggleTaskCheckbox;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("- [x] alpha")))));
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("- [ ] alpha")))));
};

"test_toggle_ordered_list_across_selected_lines"_test = [] {
    Editor e("alpha\nbeta\ngamma");
    e.set_selection(Selection{CharOffset(0), CharOffset(16), TextAffinity::Downstream});
    Command c; c.kind = CommandKind::ToggleOrderedList;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("1. alpha\n2. beta\n3. gamma")))));
    expect(fatal(bool((e.document().blocks.size()) == (1u))));
    expect(fatal(bool(e.document().blocks[0].list_ordered)));
    expect(fatal(bool((e.document().blocks[0].list_items.size()) == (3u))));
};

"test_toggle_task_list_across_selected_lines_and_remove"_test = [] {
    Editor e("alpha\nbeta");
    e.set_selection(Selection{CharOffset(0), CharOffset(10), TextAffinity::Downstream});
    Command c; c.kind = CommandKind::ToggleTaskList;
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("- [ ] alpha\n- [ ] beta")))));
    e.execute_command(c);
    expect(fatal(bool((e.text_utf8()) == (std::string("alpha\nbeta")))));
};

"test_empty_inline_format_commands_insert_editable_pairs"_test = [] {
    Editor e;
    Command strong; strong.kind = CommandKind::ToggleStrong;
    e.execute_command(strong);
    expect(fatal(bool((e.text_utf8()) == (std::string("****")))));
    expect(fatal(bool((e.selection().head().v) == (2u))));
    Editor math;
    Command inlineMath; inlineMath.kind = CommandKind::InsertMathInline;
    math.execute_command(inlineMath);
    expect(fatal(bool((math.text_utf8()) == (std::string("$$")))));
    expect(fatal(bool((math.selection().head().v) == (1u))));
};

"test_editor_auto_pairing_uses_document_tree_transactions"_test = [] {
    Editor emphasis;
    auto firstEmphasis = emphasis.execute_command(Command::InsertText(U"*"));
    expect(fatal(bool(firstEmphasis)));
    expect(fatal(bool((emphasis.text_utf8()) == (std::string("**")))));
    expect(fatal(bool((emphasis.selection().head().v) == (1u))));
    auto secondEmphasis = emphasis.execute_command(Command::InsertText(U"*"));
    expect(fatal(bool(secondEmphasis)));
    expect(fatal(bool((emphasis.text_utf8()) == (std::string("****")))));
    expect(fatal(bool((emphasis.selection().head().v) == (2u))));

    Editor strike;
    strike.execute_command(Command::InsertText(U"~"));
    strike.execute_command(Command::InsertText(U"~"));
    expect(fatal(bool((strike.text_utf8()) == (std::string("~~~~")))));
    expect(fatal(bool((strike.selection().head().v) == (2u))));

    Editor math;
    math.execute_command(Command::InsertText(U"$"));
    math.execute_command(Command::InsertText(U"$"));
    expect(fatal(bool((math.text_utf8()) == (std::string("$$\n\n$$")))));
    expect(fatal(bool((math.selection().head().v) == (3u))));
    expect(fatal(bool(!math.document().blocks.empty())));
    expect(fatal(bool(math.document().blocks.front().kind == BlockKind::MathBlock)));

    Editor fence;
    fence.execute_command(Command::InsertText(U"`"));
    fence.execute_command(Command::InsertText(U"`"));
    fence.execute_command(Command::InsertText(U"`"));
    expect(fatal(bool((fence.text_utf8()) == (std::string("```\n```")))));
    expect(fatal(bool((fence.selection().head().v) == (4u))));
    expect(fatal(bool(!fence.document().blocks.empty())));
    expect(fatal(bool(fence.document().blocks.front().kind == BlockKind::CodeBlock)));

    Editor deletion;
    deletion.execute_command(Command::InsertText(U"_"));
    Command backspace;
    backspace.kind = CommandKind::DeleteBackward;
    deletion.execute_command(backspace);
    expect(fatal(bool(deletion.text_utf8().empty())));
    expect(fatal(bool((deletion.selection().head().v) == (0u))));
};

"test_typing_inside_auto_pair_promotes_ast_inline_node"_test = [] {
    Editor emphasis;
    emphasis.execute_command(Command::InsertText(U"*"));
    const auto paragraph_id = emphasis.document().blocks.front().id;
    emphasis.execute_command(Command::InsertText(U"value"));
    expect(fatal(bool((emphasis.text_utf8()) == (std::string("*value*")))));
    expect(fatal(bool((emphasis.document().blocks.front().id) == (paragraph_id))));
    expect(fatal(bool((emphasis.document().blocks.front().children.size()) == (1u))));
    expect(fatal(bool(emphasis.document().blocks.front().children.front().kind == InlineKind::Emphasis)));
    expect(fatal(bool((block_inline_text_content(emphasis.document().blocks.front().children)) == (std::u32string(U"value")))));
    {
        expect(fatal(bool((emphasis.document_selection().active.node_id) == (paragraph_id))));
        expect(fatal(bool((emphasis.document_selection().active.offset) == (5u))));
        expect(fatal(bool(emphasis.document_selection().active.affinity == TextAffinity::Upstream)));
    }

    Editor strong;
    strong.execute_command(Command::InsertText(U"*"));
    strong.execute_command(Command::InsertText(U"*"));
    strong.execute_command(Command::InsertText(U"bold"));
    expect(fatal(bool((strong.text_utf8()) == (std::string("**bold**")))));
    expect(fatal(bool(strong.document().blocks.front().children.front().kind == InlineKind::Strong)));
};

"test_insert_text_replaces_cross_node_selection_in_document_tree"_test = [] {
    Editor editor("alpha\n\nomega");
    const auto first_id = editor.document().blocks[0].id;
    const auto second_id = editor.document().blocks[1].id;
    editor.set_document_selection(DocumentSelection{
        DocumentPosition{first_id, 2, TextAffinity::Downstream},
        DocumentPosition{second_id, 3, TextAffinity::Downstream}});
    auto transaction = editor.execute_command(Command::InsertText(U"X"));
    expect(fatal(bool(transaction)));
    expect(fatal(bool((editor.text_utf8()) == (std::string("alXga")))));
    expect(fatal(bool((editor.document().blocks.size()) == (1u))));
    expect(fatal(bool((editor.document().blocks.front().id) == (first_id))));
    {
        expect(fatal(bool((editor.document_selection().active.node_id) == (first_id))));
        expect(fatal(bool((editor.document_selection().active.offset) == (3u))));
    }
    editor.undo();
    expect(fatal(bool((editor.text_utf8()) == (std::string("alpha\n\nomega")))));
    expect(fatal(bool((editor.document().blocks[0].id) == (first_id))));
    expect(fatal(bool((editor.document().blocks[1].id) == (second_id))));
    expect(fatal(bool(!editor.document_selection().is_caret())));
};

"test_table_text_input_and_delete_share_document_history"_test = [] {
    Editor editor("| H |\n| --- |\n| X |");
    const auto cell_id = editor.document().blocks.front().table_rows.front().cells.front().id;
    editor.set_document_selection(DocumentSelection::caret(DocumentPosition{cell_id, 1, TextAffinity::Downstream}));
    editor.execute_command(Command::InsertText(U"Y"));
    expect(fatal(bool((editor.text_utf8()) == (std::string("| H |\n| --- |\n| XY |")))));
    Command backspace;
    backspace.kind = CommandKind::DeleteBackward;
    editor.execute_command(backspace);
    expect(fatal(bool((editor.text_utf8()) == (std::string("| H |\n| --- |\n| X |")))));
    editor.undo();
    expect(fatal(bool((editor.text_utf8()) == (std::string("| H |\n| --- |\n| XY |")))));
    editor.undo();
    expect(fatal(bool((editor.text_utf8()) == (std::string("| H |\n| --- |\n| X |")))));
    expect(fatal(bool((editor.document().blocks.front().table_rows.front().cells.front().id) == (cell_id))));
};

"test_inline_format_command_removes_surrounding_markers"_test = [] {
    Editor e("**bold**");
    e.set_selection(Selection{CharOffset(2), CharOffset(6), TextAffinity::Downstream});
    Command command; command.kind = CommandKind::ToggleStrong;
    e.execute_command(command);
    expect(fatal(bool((e.text_utf8()) == (std::string("bold")))));
    expect(fatal(bool((e.selection().normalized_range().start.v) == (0u))));
    expect(fatal(bool((e.selection().normalized_range().end.v) == (4u))));
};

"test_toggle_blockquote_across_selected_lines"_test = [] {
    Editor e("alpha\nbeta");
    e.set_selection(Selection{CharOffset(0), CharOffset(10), TextAffinity::Downstream});
    Command command; command.kind = CommandKind::ToggleBlockQuote;
    e.execute_command(command);
    expect(fatal(bool((e.text_utf8()) == (std::string("> alpha\n> beta")))));
    e.execute_command(command);
    expect(fatal(bool((e.text_utf8()) == (std::string("alpha\nbeta")))));
};

"test_clear_heading_preserves_text"_test = [] {
    Editor e("### title");
    e.set_caret(CharOffset(9));
    Command command; command.kind = CommandKind::ClearHeading;
    e.execute_command(command);
    expect(fatal(bool((e.text_utf8()) == (std::string("title")))));
};

"test_heading_command_mutates_document_node_and_history"_test = [] {
    Editor editor("title");
    const auto paragraph_id = editor.document().blocks.front().id;
    editor.set_document_selection(DocumentSelection::caret(
        DocumentPosition{paragraph_id, 5, TextAffinity::Downstream}));
    Command command; command.kind = CommandKind::SetHeading; command.level = 3;
    expect(fatal(bool(editor.execute_command(command))));
    expect(fatal(bool(editor.document().blocks.front().kind == BlockKind::Heading)));
    expect(fatal(bool((editor.document().blocks.front().id) == (paragraph_id))));
    expect(fatal(bool((editor.text_utf8()) == (std::string("### title")))));
    expect(fatal(bool(editor.has_document_undo())));
    editor.undo();
    expect(fatal(bool(editor.document().blocks.front().kind == BlockKind::Paragraph)));
    expect(fatal(bool((editor.document().blocks.front().id) == (paragraph_id))));
};

"test_quote_command_wraps_document_range_without_reparsing"_test = [] {
    Editor editor("alpha\n\nbeta");
    const auto first_id = editor.document().blocks[0].id;
    const auto second_id = editor.document().blocks[1].id;
    editor.set_document_selection(DocumentSelection{
        DocumentPosition{first_id, 0, TextAffinity::Downstream},
        DocumentPosition{second_id, 4, TextAffinity::Downstream}});
    Command command; command.kind = CommandKind::ToggleBlockQuote;
    expect(fatal(bool(editor.execute_command(command))));
    expect(fatal(bool((editor.document().blocks.size()) == (1u))));
    expect(fatal(bool(editor.document().blocks.front().kind == BlockKind::BlockQuote)));
    expect(fatal(bool((editor.document().blocks.front().quote_children[0].id) == (first_id))));
    expect(fatal(bool((editor.document().blocks.front().quote_children[1].id) == (second_id))));
    editor.execute_command(command);
    expect(fatal(bool((editor.document().blocks.size()) == (2u))));
    expect(fatal(bool((editor.document().blocks[0].id) == (first_id))));
    expect(fatal(bool((editor.document().blocks[1].id) == (second_id))));
};

"test_list_commands_convert_document_container_in_place"_test = [] {
    Editor editor("alpha\nbeta");
    const auto paragraph_id = editor.document().blocks.front().id;
    editor.set_document_selection(DocumentSelection{
        DocumentPosition{paragraph_id, 0, TextAffinity::Downstream},
        DocumentPosition{paragraph_id, 10, TextAffinity::Downstream}});
    Command ordered; ordered.kind = CommandKind::ToggleOrderedList;
    editor.execute_command(ordered);
    expect(fatal(bool(editor.document().blocks.front().kind == BlockKind::List)));
    expect(fatal(bool(editor.document().blocks.front().list_ordered)));
    expect(fatal(bool((editor.document().blocks.front().list_items.front().children.front().id) == (paragraph_id))));
    const auto list_id = editor.document().blocks.front().id;
    Command task; task.kind = CommandKind::ToggleTaskList;
    editor.execute_command(task);
    expect(fatal(bool(editor.document().blocks.front().kind == BlockKind::TaskList)));
    expect(fatal(bool((editor.document().blocks.front().id) == (list_id))));
    expect(fatal(bool((editor.document().blocks.front().task_items.front().children.front().id) == (paragraph_id))));
    editor.undo();
    expect(fatal(bool(editor.document().blocks.front().kind == BlockKind::List)));
    expect(fatal(bool((editor.document().blocks.front().id) == (list_id))));
};

"test_insert_image_uses_selection_as_alt_text"_test = [] {
    Editor e("diagram");
    e.set_selection(Selection{CharOffset(0), CharOffset(7), TextAffinity::Downstream});
    Command command; command.kind = CommandKind::InsertImage; command.path = U"chart.png";
    e.execute_command(command);
    expect(fatal(bool((e.text_utf8()) == (std::string("![diagram](chart.png)")))));
};

"test_link_and_image_commands_mutate_inline_tree"_test = [] {
    Editor link("label");
    const auto paragraph_id = link.document().blocks.front().id;
    link.set_document_selection(DocumentSelection{
        DocumentPosition{paragraph_id, 0, TextAffinity::Downstream},
        DocumentPosition{paragraph_id, 5, TextAffinity::Downstream}});
    Command link_command; link_command.kind = CommandKind::InsertLink; link_command.href = U"https://example.test";
    link.execute_command(link_command);
    expect(fatal(bool((link.text_utf8()) == (std::string("[label](https://example.test)")))));
    expect(fatal(bool(link.document().blocks.front().children.front().kind == InlineKind::Link)));
    expect(fatal(bool((link.document().blocks.front().id) == (paragraph_id))));
    link.undo();
    expect(fatal(bool((link.text_utf8()) == (std::string("label")))));

    Editor image("diagram");
    image.set_document_selection(DocumentSelection{
        DocumentPosition{image.document().blocks.front().id, 0, TextAffinity::Downstream},
        DocumentPosition{image.document().blocks.front().id, 7, TextAffinity::Downstream}});
    Command image_command; image_command.kind = CommandKind::InsertImage; image_command.path = U"chart.png";
    image.execute_command(image_command);
    expect(fatal(bool(image.document().blocks.front().children.front().kind == InlineKind::Image)));
    expect(fatal(bool((image.document().blocks.front().children.front().alt) == (std::string("diagram")))));
    expect(fatal(bool(image.has_document_undo())));
};

"test_insert_footnote_adds_unique_definition"_test = [] {
    Editor e("alpha\n\n[^1]: old");
    e.set_caret(CharOffset(5));
    Command command; command.kind = CommandKind::InsertFootnote;
    e.execute_command(command);
    expect(fatal(bool((e.text_utf8()) == (std::string("alpha[^2]\n\n[^1]: old\n\n[^2]: ")))));
    expect(fatal(bool(std::any_of(e.document().blocks.begin(), e.document().blocks.end(), [](auto const& block) { return block.kind == BlockKind::FootnoteDefinition; }))));
    expect(fatal(bool(e.document().blocks.front().children.back().kind == InlineKind::FootnoteRef)));
    expect(fatal(bool((e.document().blocks.front().children.back().label) == (std::string("2")))));
    expect(fatal(bool((e.document_selection().active.node_id) == (e.document().blocks.back().quote_children.front().id))));
    expect(fatal(bool(e.has_document_undo())));
};

"test_toggle_callout_wraps_and_unwraps_selected_lines"_test = [] {
    Editor e("alpha\nbeta");
    e.set_selection(Selection{CharOffset(0), CharOffset(10), TextAffinity::Downstream});
    Command command; command.kind = CommandKind::ToggleCallout; command.callout_kind = U"warning";
    e.execute_command(command);
    expect(fatal(bool((e.text_utf8()) == (std::string("> [!WARNING]\n> alpha\n> beta")))));
    expect(fatal(bool(std::any_of(e.document().blocks.begin(), e.document().blocks.end(), [](auto const& block) { return block.kind == BlockKind::Callout; }))));
    e.set_caret(CharOffset(20));
    e.execute_command(command);
    expect(fatal(bool((e.text_utf8()) == (std::string("alpha\nbeta")))));
};

"test_callout_command_preserves_content_node_identity"_test = [] {
    Editor editor("alpha");
    const auto paragraph_id = editor.document().blocks.front().id;
    editor.set_document_selection(DocumentSelection::caret(
        DocumentPosition{paragraph_id, 5, TextAffinity::Downstream}));
    Command command; command.kind = CommandKind::ToggleCallout; command.callout_kind = U"warning";
    editor.execute_command(command);
    expect(fatal(bool(editor.document().blocks.front().kind == BlockKind::Callout)));
    expect(fatal(bool((editor.document().blocks.front().callout_kind) == (std::string("WARNING")))));
    expect(fatal(bool((editor.document().blocks.front().quote_children.front().id) == (paragraph_id))));
    editor.undo();
    expect(fatal(bool((editor.document().blocks.front().id) == (paragraph_id))));
};

"test_large_document_incremental_edit_keeps_distant_blocks"_test = [] {
    std::string source;
    for (int index = 0; index < 1500; ++index) {
        source += "## Section " + std::to_string(index) + "\n\nParagraph " + std::to_string(index) + " with **formatting** and $x_" + std::to_string(index) + "$.\n\n";
    }
    Editor editor(source);
    auto position = source.find("Paragraph 750") + std::string("Paragraph 750").size();
    editor.set_caret(CharOffset(position));
    editor.execute_command(Command::InsertText(U" updated"));
    auto result = editor.text_utf8();
    expect(fatal(bool(result.find("Paragraph 750 updated") != std::string::npos)));
    expect(fatal(bool(result.find("## Section 1499") != std::string::npos)));
    expect(fatal(bool(editor.outline().flat_items().size() == 1500u)));
};

"test_enter_after_thematic_break_creates_blank_lines_without_duplicate_rules"_test = [] {
    Editor editor("---");
    editor.set_caret(CharOffset(3));
    Command enter; enter.kind = CommandKind::InsertNewline;
    auto check_structure = [&](std::size_t blank_count) {
        auto structure = build_source_structure(editor.document());
        auto actual = std::count_if(structure.blocks.begin(), structure.blocks.end(), [](auto const& block) {
            return block.kind == SourceBlockKind::Blank;
        });
        expect(fatal(bool((actual) == (blank_count))));
        auto range = editor.document().source_map.find_node_by_id(editor.document().blocks[0].id);
        expect(fatal(bool(range != nullptr)));
        expect(fatal(bool((range->source_range.start.v) == (0u))));
        expect(fatal(bool((range->source_range.end.v) == (4u))));
    };
    editor.execute_command(enter);
    expect(fatal(bool((editor.text_utf8()) == (std::string("---\n\n")))));
    expect(fatal(bool((editor.selection().head().v) == (4u))));
    check_structure(1);
    editor.execute_command(enter);
    expect(fatal(bool((editor.text_utf8()) == (std::string("---\n\n\n")))));
    expect(fatal(bool((editor.selection().head().v) == (5u))));
    check_structure(2);
    editor.execute_command(enter);
    expect(fatal(bool((editor.text_utf8()) == (std::string("---\n\n\n\n")))));
    expect(fatal(bool((editor.selection().head().v) == (6u))));
    check_structure(3);
    auto breaks = std::count_if(editor.document().blocks.begin(), editor.document().blocks.end(), [](auto const& block) {
        return block.kind == BlockKind::ThematicBreak;
    });
    expect(fatal(bool((breaks) == (1))));
};

"test_arrow_navigation_skips_thematic_break_source_marker"_test = [] {
    Editor editor("---");
    editor.set_caret(CharOffset(0));
    editor.execute_command(Command::MoveRight(false));
    expect(fatal(bool((editor.selection().head().v) == (3u))));
    editor.execute_command(Command::MoveLeft(false));
    expect(fatal(bool((editor.selection().head().v) == (0u))));
};

}; // suite editor_tests
