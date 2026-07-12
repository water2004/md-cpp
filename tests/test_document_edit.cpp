import std;
import boost.ut;
import elmd.core.ast;
import elmd.core.document;
import elmd.core.document_edit;
import elmd.core.document_position;
import elmd.core.document_projection;
import elmd.core.types;
import elmd.core.parser;
import elmd.core.serializer;

using namespace elmd;
using namespace boost::ut;


suite document_edit_marker_tests = [] {

"test_backspace_on_emphasis_marker_unwraps_ast_node"_test = [] {
    auto document = parse_text(1, "_word_").document;
    auto selection = DocumentSelection::caret(*document_position_from_source_offset(document, CharOffset(1)));
    auto transaction = document_delete_backward(document, selection);
    expect(fatal(bool(transaction.has_value())));
    expect(fatal(bool(transaction && serialize_markdown(transaction->after) == "word")));
    expect(fatal(bool(transaction && transaction->after.blocks.front().children.front().kind == InlineKind::Text)));
};

"test_delete_on_emphasis_marker_unwraps_ast_node"_test = [] {
    auto document = parse_text(1, "_word_").document;
    auto selection = DocumentSelection::caret(*document_position_from_source_offset(document, CharOffset(0)));
    auto transaction = document_delete_forward(document, selection);
    expect(fatal(bool(transaction.has_value())));
    expect(fatal(bool(transaction && serialize_markdown(transaction->after) == "word")));
};

}; // suite document_edit_marker_tests

namespace {

BlockNode paragraph(std::uint64_t block_id, std::uint64_t text_id, std::u32string text) {
    BlockNode block;
    block.id = NodeId(block_id);
    block.kind = BlockKind::Paragraph;
    if (!text.empty()) block.children.push_back(InlineNode::text_node(NodeId(text_id), std::move(text)));
    return block;
}

EditorDocument document_with(BlockVec blocks) {
    EditorDocument document;
    document.revision = 1;
    document.blocks = std::move(blocks);
    return document;
}

DocumentSelection caret(std::uint64_t node_id, std::size_t offset = 0) {
    return DocumentSelection::caret(DocumentPosition{NodeId(node_id), offset, TextAffinity::Downstream});
}

};

suite document_edit_tests = [] {

"test_document_enter_splits_top_level_paragraph_by_node_position"_test = [] {
    auto document = document_with({paragraph(1, 2, U"hello world")});
    auto transaction = document_enter(document, caret(1, 5));
    expect(fatal(bool(transaction.has_value())));
    expect(fatal(bool((transaction->after.blocks.size()) == (2u))));
    expect(fatal(bool((block_inline_text_content(transaction->after.blocks[0].children)) == (std::u32string(U"hello")))));
    expect(fatal(bool((block_inline_text_content(transaction->after.blocks[1].children)) == (std::u32string(U" world")))));
    expect(fatal(bool(transaction->selection_after.active.node_id == transaction->after.blocks[1].id)));
    expect(fatal(bool((transaction->selection_after.active.offset) == (0u))));
    expect(fatal(bool(validate_document(transaction->after).empty())));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("hello\n\n world")))));
};

"test_document_enter_preserves_inline_structure_when_splitting"_test = [] {
    auto parsed = parse_text(1, "hello **world**");
    auto paragraph_id = parsed.document.blocks[0].id;
    auto transaction = document_enter(parsed.document, DocumentSelection::caret(DocumentPosition{paragraph_id, 8, TextAffinity::Downstream}));
    expect(fatal(bool(transaction.has_value())));
    expect(fatal(bool((transaction->after.blocks.size()) == (2u))));
    expect(fatal(bool(transaction->after.blocks[0].children.back().kind == InlineKind::Strong)));
    expect(fatal(bool(transaction->after.blocks[1].children.front().kind == InlineKind::Strong)));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("hello **wo**\n\n**rld**")))));
    auto reparsed = parse_text(2, serialize_markdown(transaction->after));
    expect(fatal(bool((reparsed.document.blocks.size()) == (2u))));
    expect(fatal(bool(reparsed.document.blocks[0].children.back().kind == InlineKind::Strong)));
    expect(fatal(bool(reparsed.document.blocks[1].children.front().kind == InlineKind::Strong)));
};

"test_document_enter_splits_nonempty_list_item_structurally"_test = [] {
    BlockNode list;
    list.id = NodeId(1);
    list.kind = BlockKind::List;
    ListItem item;
    item.id = NodeId(2);
    item.marker = U"- ";
    item.children.push_back(paragraph(3, 4, U"first"));
    list.list_items.push_back(std::move(item));
    auto transaction = document_enter(document_with({std::move(list)}), caret(3, 2));
    expect(fatal(bool(transaction.has_value())));
    const auto& after_list = transaction->after.blocks[0];
    expect(fatal(bool((after_list.list_items.size()) == (2u))));
    expect(fatal(bool((block_inline_text_content(after_list.list_items[0].children[0].children)) == (std::u32string(U"fi")))));
    expect(fatal(bool((block_inline_text_content(after_list.list_items[1].children[0].children)) == (std::u32string(U"rst")))));
    expect(fatal(bool(validate_document(transaction->after).empty())));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("- fi\n- rst")))));
};

"test_document_enter_empty_only_list_item_exits_list"_test = [] {
    BlockNode list;
    list.id = NodeId(1);
    list.kind = BlockKind::List;
    ListItem item;
    item.id = NodeId(2);
    item.marker = U"- ";
    item.children.push_back(paragraph(3, 4, U""));
    list.list_items.push_back(std::move(item));
    auto transaction = document_enter(document_with({std::move(list)}), caret(3));
    expect(fatal(bool(transaction.has_value())));
    expect(fatal(bool((transaction->after.blocks.size()) == (1u))));
    expect(fatal(bool(transaction->after.blocks[0].kind == BlockKind::Paragraph)));
    expect(fatal(bool(transaction->selection_after.active.node_id == transaction->after.blocks[0].id)));
    expect(fatal(bool(validate_document(transaction->after).empty())));
};

"test_document_enter_empty_middle_list_item_splits_container"_test = [] {
    BlockNode list;
    list.id = NodeId(1);
    list.kind = BlockKind::List;
    for (std::uint64_t index = 0; index < 3; ++index) {
        ListItem item;
        item.id = NodeId(2 + index * 3);
        item.marker = U"- ";
        item.children.push_back(paragraph(3 + index * 3, 4 + index * 3, index == 1 ? U"" : (index == 0 ? U"one" : U"three")));
        list.list_items.push_back(std::move(item));
    }
    auto transaction = document_enter(document_with({std::move(list)}), caret(6));
    expect(fatal(bool(transaction.has_value())));
    expect(fatal(bool((transaction->after.blocks.size()) == (3u))));
    expect(fatal(bool(transaction->after.blocks[0].kind == BlockKind::List)));
    expect(fatal(bool(transaction->after.blocks[1].kind == BlockKind::Paragraph)));
    expect(fatal(bool(transaction->after.blocks[2].kind == BlockKind::List)));
    expect(fatal(bool((transaction->after.blocks[0].list_items.size()) == (1u))));
    expect(fatal(bool((transaction->after.blocks[2].list_items.size()) == (1u))));
    expect(fatal(bool(validate_document(transaction->after).empty())));
};

"test_document_enter_empty_quote_paragraph_exits_by_tree_position"_test = [] {
    BlockNode quote;
    quote.id = NodeId(1);
    quote.kind = BlockKind::BlockQuote;
    quote.quote_children.push_back(paragraph(2, 3, U"before"));
    quote.quote_children.push_back(paragraph(4, 5, U""));
    quote.quote_children.push_back(paragraph(6, 7, U"after"));
    auto transaction = document_enter(document_with({std::move(quote)}), caret(4));
    expect(fatal(bool(transaction.has_value())));
    expect(fatal(bool((transaction->after.blocks.size()) == (3u))));
    expect(fatal(bool(transaction->after.blocks[0].kind == BlockKind::BlockQuote)));
    expect(fatal(bool(transaction->after.blocks[1].kind == BlockKind::Paragraph)));
    expect(fatal(bool(transaction->after.blocks[2].kind == BlockKind::BlockQuote)));
    expect(fatal(bool((transaction->after.blocks[0].quote_children.size()) == (1u))));
    expect(fatal(bool((transaction->after.blocks[2].quote_children.size()) == (1u))));
    expect(fatal(bool(validate_document(transaction->after).empty())));
};

"test_document_history_restores_document_and_node_selection"_test = [] {
    auto document = document_with({paragraph(1, 2, U"hello")});
    auto transaction = document_enter(document, caret(1, 2));
    expect(fatal(bool(transaction.has_value())));
    DocumentHistory history;
    history.push(*transaction);
    auto undone = history.undo();
    expect(fatal(bool(undone.has_value())));
    expect(fatal(bool((serialize_markdown(undone->first)) == (std::string("hello")))));
    expect(fatal(bool(undone->second.active.node_id == NodeId(1))));
    expect(fatal(bool((undone->second.active.offset) == (2u))));
    auto redone = history.redo();
    expect(fatal(bool(redone.has_value())));
    expect(fatal(bool((serialize_markdown(redone->first)) == (std::string("he\n\nllo")))));
    expect(fatal(bool(redone->second.active.node_id == transaction->selection_after.active.node_id)));
    expect(fatal(bool((redone->second.active.offset) == (0u))));
};

"test_enter_splits_task_item_and_resets_new_checkbox"_test = [] {
    BlockNode task_list;
    task_list.id = NodeId(1);
    task_list.kind = BlockKind::TaskList;
    TaskListItem item;
    item.id = NodeId(2);
    item.checked = true;
    item.marker = U"- [x] ";
    item.children.push_back(paragraph(3, 4, U"alphabeta"));
    task_list.task_items.push_back(std::move(item));

    auto transaction = document_enter(document_with({task_list}), caret(3, 5));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool((transaction->after.blocks[0].task_items.size()) == (2u))));
    expect(fatal(bool(transaction->after.blocks[0].task_items[0].checked)));
    expect(fatal(bool(!transaction->after.blocks[0].task_items[1].checked)));
    expect(fatal(bool(transaction->after.blocks[0].task_items[1].marker.empty())));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("- [x] alpha\n- [ ] beta")))));
};

"test_enter_empty_only_task_item_exits_task_list"_test = [] {
    BlockNode task_list;
    task_list.id = NodeId(1);
    task_list.kind = BlockKind::TaskList;
    TaskListItem item;
    item.id = NodeId(2);
    item.children.push_back(paragraph(3, 0, U""));
    task_list.task_items.push_back(std::move(item));

    auto transaction = document_enter(document_with({task_list}), caret(3));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool((transaction->after.blocks.size()) == (1u))));
    expect(fatal(bool((transaction->after.blocks[0].kind) == (BlockKind::Paragraph))));
    expect(fatal(bool(validate_document(transaction->after).empty())));
};

"test_insert_text_preserves_inline_container_identity"_test = [] {
    BlockNode block;
    block.id = NodeId(1);
    block.kind = BlockKind::Paragraph;
    InlineNode strong;
    strong.id = NodeId(2);
    strong.kind = InlineKind::Strong;
    strong.children.push_back(InlineNode::text_node(NodeId(3), U"alphabeta"));
    block.children.push_back(strong);

    auto transaction = document_insert_text(document_with({block}), caret(1, 5), U"-");
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool((transaction->after.blocks[0].children[0].id) == (NodeId(2)))));
    expect(fatal(bool((transaction->after.blocks[0].children[0].children[0].id) == (NodeId(3)))));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("**alpha-beta**")))));
    expect(fatal(bool((transaction->selection_after.active.offset) == (6u))));
};

"test_delete_backward_preserves_inline_container_identity"_test = [] {
    BlockNode block;
    block.id = NodeId(1);
    block.kind = BlockKind::Paragraph;
    InlineNode emphasis;
    emphasis.id = NodeId(2);
    emphasis.kind = InlineKind::Emphasis;
    emphasis.children.push_back(InlineNode::text_node(NodeId(3), U"alpha"));
    block.children.push_back(emphasis);

    auto transaction = document_delete_backward(document_with({block}), caret(1, 5));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool((transaction->after.blocks[0].children[0].id) == (NodeId(2)))));
    expect(fatal(bool((transaction->after.blocks[0].children[0].children[0].id) == (NodeId(3)))));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("*alph*")))));
    expect(fatal(bool((transaction->selection_after.active.offset) == (4u))));
};

"test_backspace_removes_adjacent_autopair_from_ast_text_leaf"_test = [] {
    auto parsed = parse_text(1, "__");
    expect(fatal(bool((parsed.document.blocks.size()) == (1u))));
    auto paragraph_id = parsed.document.blocks[0].id;
    auto transaction = document_delete_backward(parsed.document, DocumentSelection::caret(
        DocumentPosition{paragraph_id, 1, TextAffinity::Downstream}));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(transaction->after.blocks[0].children.empty())));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string{}))));
    expect(fatal(bool((transaction->selection_after.active.node_id) == (paragraph_id))));
    expect(fatal(bool((transaction->selection_after.active.offset) == (0u))));
};

"test_delete_forward_preserves_inline_container_identity"_test = [] {
    auto document = document_with({paragraph(1, 2, U"alpha")});
    auto transaction = document_delete_forward(document, caret(1, 1));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool((transaction->after.blocks[0].children[0].id) == (NodeId(2)))));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("apha")))));
    expect(fatal(bool((transaction->selection_after.active.offset) == (1u))));
};

"test_backspace_at_top_level_paragraph_start_merges_ast_siblings"_test = [] {
    auto document = document_with({paragraph(1, 2, U"alpha"), paragraph(3, 4, U"beta")});
    auto transaction = document_delete_backward(document, caret(3, 0));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool((transaction->after.blocks.size()) == (1u))));
    expect(fatal(bool((transaction->after.blocks[0].id) == (NodeId(1)))));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("alphabeta")))));
    expect(fatal(bool((transaction->selection_after.active.node_id) == (NodeId(1)))));
    expect(fatal(bool((transaction->selection_after.active.offset) == (5u))));
};

"test_delete_at_top_level_paragraph_end_merges_ast_siblings"_test = [] {
    auto document = document_with({paragraph(1, 2, U"alpha"), paragraph(3, 4, U"beta")});
    auto transaction = document_delete_forward(document, caret(1, 5));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool((transaction->after.blocks.size()) == (1u))));
    expect(fatal(bool((transaction->after.blocks[0].id) == (NodeId(1)))));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("alphabeta")))));
    expect(fatal(bool((transaction->selection_after.active.offset) == (5u))));
};

"test_backspace_unwraps_only_quote_paragraph_one_level"_test = [] {
    BlockNode quote;
    quote.id = NodeId(1);
    quote.kind = BlockKind::BlockQuote;
    quote.quote_children.push_back(paragraph(2, 3, U"quoted"));
    auto transaction = document_delete_backward(document_with({quote}), caret(2));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool((transaction->after.blocks.size()) == (1u))));
    expect(fatal(bool((transaction->after.blocks[0].kind) == (BlockKind::Paragraph))));
    expect(fatal(bool((transaction->after.blocks[0].id) == (NodeId(2)))));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("quoted")))));
    expect(fatal(bool((transaction->selection_after.active.node_id) == (NodeId(2)))));
    expect(fatal(bool(validate_document(transaction->after).empty())));
};

"test_backspace_unwraps_nested_quote_exactly_one_level"_test = [] {
    BlockNode inner;
    inner.id = NodeId(2);
    inner.kind = BlockKind::BlockQuote;
    inner.quote_children.push_back(paragraph(3, 4, U"nested"));
    BlockNode outer;
    outer.id = NodeId(1);
    outer.kind = BlockKind::BlockQuote;
    outer.quote_children.push_back(std::move(inner));
    auto transaction = document_delete_backward(document_with({outer}), caret(3));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool((transaction->after.blocks.size()) == (1u))));
    expect(fatal(bool((transaction->after.blocks[0].kind) == (BlockKind::BlockQuote))));
    expect(fatal(bool((transaction->after.blocks[0].quote_children.size()) == (1u))));
    expect(fatal(bool((transaction->after.blocks[0].quote_children[0].kind) == (BlockKind::Paragraph))));
    expect(fatal(bool((transaction->after.blocks[0].quote_children[0].id) == (NodeId(3)))));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("> nested")))));
};

"test_backspace_lifts_first_list_item_and_preserves_remaining_items"_test = [] {
    BlockNode list;
    list.id = NodeId(1);
    list.kind = BlockKind::List;
    for (std::uint64_t index = 0; index < 3; ++index) {
        ListItem item;
        item.id = NodeId(2 + index * 3);
        item.marker = U"- ";
        item.children.push_back(paragraph(3 + index * 3, 4 + index * 3, index == 0 ? U"one" : index == 1 ? U"two" : U"three"));
        list.list_items.push_back(std::move(item));
    }
    auto transaction = document_delete_backward(document_with({list}), caret(3));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool((transaction->after.blocks.size()) == (2u))));
    expect(fatal(bool((transaction->after.blocks[0].kind) == (BlockKind::Paragraph))));
    expect(fatal(bool((transaction->after.blocks[0].id) == (NodeId(3)))));
    expect(fatal(bool((transaction->after.blocks[1].kind) == (BlockKind::List))));
    expect(fatal(bool((transaction->after.blocks[1].list_items.size()) == (2u))));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("one\n\n- two\n- three")))));
};

"test_backspace_moves_middle_list_item_subtree_into_previous_item"_test = [] {
    BlockNode nested;
    nested.id = NodeId(20);
    nested.kind = BlockKind::List;
    ListItem nested_item;
    nested_item.id = NodeId(21);
    nested_item.marker = U"- ";
    nested_item.children.push_back(paragraph(22, 23, U"nested"));
    nested.list_items.push_back(std::move(nested_item));
    BlockNode list;
    list.id = NodeId(1);
    list.kind = BlockKind::List;
    ListItem first;
    first.id = NodeId(2);
    first.marker = U"- ";
    first.children.push_back(paragraph(3, 4, U"one"));
    ListItem second;
    second.id = NodeId(5);
    second.marker = U"- ";
    second.children.push_back(paragraph(6, 7, U"two"));
    second.children.push_back(std::move(nested));
    ListItem third;
    third.id = NodeId(8);
    third.marker = U"- ";
    third.children.push_back(paragraph(9, 10, U"three"));
    list.list_items = {std::move(first), std::move(second), std::move(third)};
    auto transaction = document_delete_backward(document_with({list}), caret(6));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    const auto& result = transaction->after.blocks[0];
    expect(fatal(bool((result.list_items.size()) == (2u))));
    expect(fatal(bool((result.list_items[0].children.size()) == (3u))));
    expect(fatal(bool((result.list_items[0].children[1].id) == (NodeId(6)))));
    expect(fatal(bool((result.list_items[0].children[2].id) == (NodeId(20)))));
    expect(fatal(bool((transaction->selection_after.active.node_id) == (NodeId(6)))));
    expect(fatal(bool(validate_document(transaction->after).empty())));
};

"test_delete_forward_merges_next_list_item_with_its_nested_subtree"_test = [] {
    BlockNode nested;
    nested.id = NodeId(20);
    nested.kind = BlockKind::List;
    ListItem nested_item;
    nested_item.id = NodeId(21);
    nested_item.marker = U"- ";
    nested_item.children.push_back(paragraph(22, 23, U"D"));
    nested.list_items.push_back(std::move(nested_item));
    BlockNode list;
    list.id = NodeId(1);
    list.kind = BlockKind::List;
    ListItem first;
    first.id = NodeId(2);
    first.marker = U"- ";
    first.children.push_back(paragraph(3, 4, U"a"));
    ListItem second;
    second.id = NodeId(5);
    second.marker = U"- ";
    second.children.push_back(paragraph(6, 7, U"C"));
    second.children.push_back(std::move(nested));
    list.list_items = {std::move(first), std::move(second)};
    auto transaction = document_delete_forward(document_with({list}), caret(3, 1));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    const auto& result = transaction->after.blocks[0];
    expect(fatal(bool((result.list_items.size()) == (1u))));
    expect(fatal(bool((result.list_items[0].children.size()) == (2u))));
    expect(fatal(bool((block_inline_text_content(result.list_items[0].children[0].children)) == (std::u32string(U"aC")))));
    expect(fatal(bool((result.list_items[0].children[1].id) == (NodeId(20)))));
    expect(fatal(bool((transaction->selection_after.active.node_id) == (NodeId(3)))));
    expect(fatal(bool((transaction->selection_after.active.offset) == (1u))));
    expect(fatal(bool(validate_document(transaction->after).empty())));
};

"test_backspace_moves_task_item_subtree_into_previous_item"_test = [] {
    BlockNode task_list;
    task_list.id = NodeId(1);
    task_list.kind = BlockKind::TaskList;
    TaskListItem first;
    first.id = NodeId(2);
    first.checked = true;
    first.children.push_back(paragraph(3, 4, U"done"));
    TaskListItem second;
    second.id = NodeId(5);
    second.children.push_back(paragraph(6, 7, U"next"));
    task_list.task_items = {std::move(first), std::move(second)};
    auto transaction = document_delete_backward(document_with({task_list}), caret(6));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    const auto& result = transaction->after.blocks[0];
    expect(fatal(bool((result.task_items.size()) == (1u))));
    expect(fatal(bool((result.task_items[0].children.size()) == (2u))));
    expect(fatal(bool((result.task_items[0].children[1].id) == (NodeId(6)))));
    expect(fatal(bool((transaction->selection_after.active.node_id) == (NodeId(6)))));
    expect(fatal(bool(validate_document(transaction->after).empty())));
};

"test_delete_selection_inside_inline_tree_preserves_container"_test = [] {
    BlockNode block;
    block.id = NodeId(1);
    block.kind = BlockKind::Paragraph;
    InlineNode strong;
    strong.id = NodeId(2);
    strong.kind = InlineKind::Strong;
    strong.children.push_back(InlineNode::text_node(NodeId(3), U"alphabet"));
    block.children.push_back(strong);
    DocumentSelection selection{
        DocumentPosition{NodeId(1), 2, TextAffinity::Downstream},
        DocumentPosition{NodeId(1), 6, TextAffinity::Downstream}};

    auto transaction = document_delete_selection(document_with({block}), selection);
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool((transaction->after.blocks[0].children[0].id) == (NodeId(2)))));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("**alet**")))));
    expect(fatal(bool((transaction->selection_after.active.offset) == (2u))));
};

"test_delete_selection_across_top_level_paragraphs_merges_tree_range"_test = [] {
    auto document = document_with({
        paragraph(1, 2, U"alpha"),
        paragraph(3, 4, U"middle"),
        paragraph(5, 6, U"omega")});
    DocumentSelection selection{
        DocumentPosition{NodeId(1), 2, TextAffinity::Downstream},
        DocumentPosition{NodeId(5), 3, TextAffinity::Downstream}};

    auto transaction = document_delete_selection(document, selection);
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool((transaction->after.blocks.size()) == (1u))));
    expect(fatal(bool((transaction->after.blocks[0].id) == (NodeId(1)))));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("alga")))));
    expect(fatal(bool((transaction->selection_after.active.node_id) == (NodeId(1)))));
    expect(fatal(bool((transaction->selection_after.active.offset) == (2u))));
};

"test_indent_list_item_moves_whole_item_under_previous_sibling"_test = [] {
    auto parsed = parse_text(1, "- a\n- b");
    const auto second_paragraph = parsed.document.blocks[0].list_items[1].children[0].id;
    auto transaction = document_indent_list_item(parsed.document, caret(second_paragraph.v, 1));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    const auto& list = transaction->after.blocks[0];
    expect(fatal(bool((list.list_items.size()) == (1u))));
    expect(fatal(bool((list.list_items[0].children.size()) == (2u))));
    expect(fatal(bool(list.list_items[0].children[1].kind == BlockKind::List)));
    expect(fatal(bool((list.list_items[0].children[1].list_items.size()) == (1u))));
    expect(fatal(bool((list.list_items[0].children[1].list_items[0].children[0].id) == (second_paragraph))));
    expect(fatal(bool((transaction->selection_after.active.node_id) == (second_paragraph))));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("- a\n  - b")))));
    expect(fatal(bool(validate_document(transaction->after).empty())));
};

"test_outdent_list_item_restores_outer_sibling_and_selection"_test = [] {
    auto parsed = parse_text(1, "- a\n  - b");
    const auto nested_paragraph = parsed.document.blocks[0].list_items[0].children[1].list_items[0].children[0].id;
    auto transaction = document_outdent_list_item(parsed.document, caret(nested_paragraph.v, 1));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    const auto& list = transaction->after.blocks[0];
    expect(fatal(bool((list.list_items.size()) == (2u))));
    expect(fatal(bool((list.list_items[1].children[0].id) == (nested_paragraph))));
    expect(fatal(bool((transaction->selection_after.active.node_id) == (nested_paragraph))));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("- a\n- b")))));
    expect(fatal(bool(validate_document(transaction->after).empty())));
};

"test_outdent_list_item_keeps_following_nested_siblings_under_lifted_item"_test = [] {
    auto parsed = parse_text(1, "- a\n  - b\n  - c\n- d");
    const auto nested_paragraph = parsed.document.blocks[0].list_items[0].children[1].list_items[0].children[0].id;
    auto transaction = document_outdent_list_item(parsed.document, caret(nested_paragraph.v, 0));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    const auto& list = transaction->after.blocks[0];
    expect(fatal(bool((list.list_items.size()) == (3u))));
    expect(fatal(bool((list.list_items[1].children.size()) == (2u))));
    expect(fatal(bool(list.list_items[1].children[1].kind == BlockKind::List)));
    expect(fatal(bool((block_inline_text_content(list.list_items[1].children[1].list_items[0].children[0].children)) == (std::u32string(U"c")))));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("- a\n- b\n  - c\n- d")))));
    expect(fatal(bool(validate_document(transaction->after).empty())));
};

"test_indent_first_list_item_is_noop"_test = [] {
    auto parsed = parse_text(1, "- a\n- b");
    const auto first_paragraph = parsed.document.blocks[0].list_items[0].children[0].id;
    expect(fatal(bool(!document_indent_list_item(parsed.document, caret(first_paragraph.v, 0)).has_value())));
};

"test_outdent_replacement_promotes_content_before_first_nested_list"_test = [] {
    auto parsed = parse_text(1, "- - A\n  - B");
    auto& nested = parsed.document.blocks[0].list_items[0].children[0];
    const auto paragraph_id = nested.list_items[1].children[0].id;
    auto transaction = document_outdent_list_item(parsed.document, caret(paragraph_id.v, 1));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    const auto& outer_item = transaction->after.blocks[0].list_items[0];
    expect(fatal(bool((outer_item.children.size()) == (2u))));
    expect(fatal(bool((outer_item.children[0].id) == (paragraph_id))));
    expect(fatal(bool(outer_item.children[1].kind == BlockKind::List)));
    expect(fatal(bool((transaction->selection_after.active.node_id) == (paragraph_id))));
    expect(fatal(bool((transaction->selection_after.active.offset) == (1u))));
    expect(fatal(bool(validate_document(transaction->after).empty())));
};

"test_task_list_indent_and_outdent_preserve_checked_item"_test = [] {
    auto parsed = parse_text(1, "- [ ] a\n- [x] b");
    const auto paragraph_id = parsed.document.blocks[0].task_items[1].children[0].id;
    auto indented = document_indent_list_item(parsed.document, caret(paragraph_id.v, 1));
    expect(fatal(bool(indented.has_value())));
    if (!indented) return;
    const auto& nested = indented->after.blocks[0].task_items[0].children[1];
    expect(fatal(bool(nested.kind == BlockKind::TaskList)));
    expect(fatal(bool((nested.task_items.size()) == (1u))));
    expect(fatal(bool(nested.task_items[0].checked)));
    auto outdented = document_outdent_list_item(indented->after, caret(paragraph_id.v, 1));
    expect(fatal(bool(outdented.has_value())));
    if (!outdented) return;
    expect(fatal(bool((outdented->after.blocks[0].task_items.size()) == (2u))));
    expect(fatal(bool(outdented->after.blocks[0].task_items[1].checked)));
    expect(fatal(bool((outdented->selection_after.active.node_id) == (paragraph_id))));
    expect(fatal(bool(validate_document(outdented->after).empty())));
};

"test_delete_selection_across_list_items_merges_into_first_item"_test = [] {
    auto parsed = parse_text(1, "- alpha\n- beta\n- gamma");
    const auto first_id = parsed.document.blocks[0].list_items[0].children[0].id;
    const auto second_id = parsed.document.blocks[0].list_items[1].children[0].id;
    DocumentSelection selection{
        DocumentPosition{first_id, 2, TextAffinity::Downstream},
        DocumentPosition{second_id, 2, TextAffinity::Downstream}};
    auto transaction = document_delete_selection(parsed.document, selection);
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    const auto& list = transaction->after.blocks[0];
    expect(fatal(bool((list.list_items.size()) == (2u))));
    expect(fatal(bool((list.list_items[0].children[0].id) == (first_id))));
    expect(fatal(bool((block_inline_text_content(list.list_items[0].children[0].children)) == (std::u32string(U"alta")))));
    expect(fatal(bool((block_inline_text_content(list.list_items[1].children[0].children)) == (std::u32string(U"gamma")))));
    expect(fatal(bool((transaction->selection_after.active.node_id) == (first_id))));
    expect(fatal(bool((transaction->selection_after.active.offset) == (2u))));
    expect(fatal(bool(validate_document(transaction->after).empty())));
};

"test_delete_selection_across_list_items_moves_last_item_subtree"_test = [] {
    auto parsed = parse_text(1, "- alpha\n- beta\n  - child\n- gamma");
    const auto first_id = parsed.document.blocks[0].list_items[0].children[0].id;
    const auto second_id = parsed.document.blocks[0].list_items[1].children[0].id;
    DocumentSelection selection{
        DocumentPosition{first_id, 2, TextAffinity::Downstream},
        DocumentPosition{second_id, 2, TextAffinity::Downstream}};
    auto transaction = document_delete_selection(parsed.document, selection);
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    const auto& list = transaction->after.blocks[0];
    expect(fatal(bool((list.list_items.size()) == (2u))));
    expect(fatal(bool((list.list_items[0].children.size()) == (2u))));
    expect(fatal(bool((block_inline_text_content(list.list_items[0].children[0].children)) == (std::u32string(U"alta")))));
    expect(fatal(bool(list.list_items[0].children[1].kind == BlockKind::List)));
    expect(fatal(bool((block_inline_text_content(list.list_items[0].children[1].list_items[0].children[0].children)) == (std::u32string(U"child")))));
    expect(fatal(bool((block_inline_text_content(list.list_items[1].children[0].children)) == (std::u32string(U"gamma")))));
    expect(fatal(bool(validate_document(transaction->after).empty())));
};

"test_delete_selection_from_quote_into_following_paragraph_keeps_first_context"_test = [] {
    auto parsed = parse_text(1, "> alpha\n\nomega");
    const auto quote_id = parsed.document.blocks[0].quote_children[0].id;
    const auto paragraph_id = parsed.document.blocks[1].id;
    DocumentSelection selection{
        DocumentPosition{quote_id, 2, TextAffinity::Downstream},
        DocumentPosition{paragraph_id, 3, TextAffinity::Downstream}};
    auto transaction = document_delete_selection(parsed.document, selection);
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool((transaction->after.blocks.size()) == (1u))));
    expect(fatal(bool(transaction->after.blocks[0].kind == BlockKind::BlockQuote)));
    expect(fatal(bool((transaction->after.blocks[0].quote_children[0].id) == (quote_id))));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("> alga")))));
    expect(fatal(bool((transaction->selection_after.active.node_id) == (quote_id))));
    expect(fatal(bool(validate_document(transaction->after).empty())));
};

"test_delete_selection_into_nested_list_preserves_content_after_endpoint"_test = [] {
    auto parsed = parse_text(1, "start\n\n- one\n- two\n\nafter");
    const auto first_id = parsed.document.blocks[0].id;
    const auto nested_id = parsed.document.blocks[1].list_items[0].children[0].id;
    DocumentSelection selection{
        DocumentPosition{first_id, 2, TextAffinity::Downstream},
        DocumentPosition{nested_id, 2, TextAffinity::Downstream}};
    auto transaction = document_delete_selection(parsed.document, selection);
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool((transaction->after.blocks.size()) == (3u))));
    expect(fatal(bool((transaction->after.blocks[0].id) == (first_id))));
    expect(fatal(bool((block_inline_text_content(transaction->after.blocks[0].children)) == (std::u32string(U"ste")))));
    expect(fatal(bool(transaction->after.blocks[1].kind == BlockKind::List)));
    expect(fatal(bool((transaction->after.blocks[1].list_items.size()) == (1u))));
    expect(fatal(bool((block_inline_text_content(transaction->after.blocks[1].list_items[0].children[0].children)) == (std::u32string(U"two")))));
    expect(fatal(bool((block_inline_text_content(transaction->after.blocks[2].children)) == (std::u32string(U"after")))));
    expect(fatal(bool(validate_document(transaction->after).empty())));
};

"test_delete_selection_removes_atomic_blocks_between_endpoints"_test = [] {
    auto parsed = parse_text(1, "alpha\n\n---\n\nomega");
    const auto first_id = parsed.document.blocks[0].id;
    const auto last_id = parsed.document.blocks[2].id;
    DocumentSelection selection{
        DocumentPosition{first_id, 2, TextAffinity::Downstream},
        DocumentPosition{last_id, 3, TextAffinity::Downstream}};
    auto transaction = document_delete_selection(parsed.document, selection);
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool((transaction->after.blocks.size()) == (1u))));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("alga")))));
    expect(fatal(bool(validate_document(transaction->after).empty())));
};

"test_delete_selection_normalizes_reverse_document_order"_test = [] {
    auto parsed = parse_text(1, "alpha\n\nomega");
    const auto first_id = parsed.document.blocks[0].id;
    const auto last_id = parsed.document.blocks[1].id;
    DocumentSelection selection{
        DocumentPosition{last_id, 3, TextAffinity::Upstream},
        DocumentPosition{first_id, 2, TextAffinity::Downstream}};
    auto transaction = document_delete_selection(parsed.document, selection);
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("alga")))));
    expect(fatal(bool((transaction->selection_after.active.node_id) == (first_id))));
    expect(fatal(bool((transaction->selection_after.active.offset) == (2u))));
};

"test_delete_selection_across_task_items_moves_trailing_blocks"_test = [] {
    auto parsed = parse_text(1, "- [ ] alpha\n- [x] beta\n      - child");
    const auto first_id = parsed.document.blocks[0].task_items[0].children[0].id;
    const auto second_id = parsed.document.blocks[0].task_items[1].children[0].id;
    DocumentSelection selection{
        DocumentPosition{first_id, 2, TextAffinity::Downstream},
        DocumentPosition{second_id, 2, TextAffinity::Downstream}};
    auto transaction = document_delete_selection(parsed.document, selection);
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    const auto& task_list = transaction->after.blocks[0];
    expect(fatal(bool((task_list.task_items.size()) == (1u))));
    expect(fatal(bool((block_inline_text_content(task_list.task_items[0].children[0].children)) == (std::u32string(U"alta")))));
    expect(fatal(bool((task_list.task_items[0].children.size()) == (2u))));
    expect(fatal(bool(validate_document(transaction->after).empty())));
};

"test_document_soft_break_inserts_inline_node"_test = [] {
    auto document = document_with({paragraph(1, 2, U"alphaomega")});
    auto transaction = document_insert_soft_break(document, caret(1, 5));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool((transaction->after.blocks[0].children.size()) == (3u))));
    expect(fatal(bool(transaction->after.blocks[0].children[1].kind == InlineKind::SoftBreak)));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("alpha\nomega")))));
    expect(fatal(bool((transaction->selection_after.active.node_id) == (NodeId(1)))));
    expect(fatal(bool((transaction->selection_after.active.offset) == (6u))));
    expect(fatal(bool(validate_document(transaction->after).empty())));
};

"test_document_horizontal_navigation_crosses_nodes"_test = [] {
    auto document = document_with({paragraph(1, 2, U"alpha"), paragraph(3, 4, U"beta")});
    auto right = document_move_selection(document, caret(1, 5), DocumentMove::Right, false);
    expect(fatal(bool(right.has_value())));
    if (!right) return;
    expect(fatal(bool((right->active.node_id) == (NodeId(3)))));
    expect(fatal(bool((right->active.offset) == (0u))));
    auto left = document_move_selection(document, *right, DocumentMove::Left, false);
    expect(fatal(bool(left.has_value())));
    if (!left) return;
    expect(fatal(bool((left->active.node_id) == (NodeId(1)))));
    expect(fatal(bool((left->active.offset) == (5u))));
};

"test_document_vertical_navigation_uses_node_local_lines"_test = [] {
    BlockNode code;
    code.id = NodeId(1);
    code.kind = BlockKind::CodeBlock;
    code.code_text = U"abcd\nxy";
    auto document = document_with({std::move(code)});
    auto down = document_move_selection(document, caret(1, 3), DocumentMove::Down, false);
    expect(fatal(bool(down.has_value())));
    if (!down) return;
    expect(fatal(bool((down->active.offset) == (7u))));
    auto up = document_move_selection(document, *down, DocumentMove::Up, false);
    expect(fatal(bool(up.has_value())));
    if (up) expect(fatal(bool((up->active.offset) == (2u))));
};

"test_document_select_all_binds_first_and_last_nodes"_test = [] {
    auto document = document_with({paragraph(1, 2, U"alpha"), paragraph(3, 4, U"beta")});
    auto selection = document_select_all(document);
    expect(fatal(bool(selection.has_value())));
    if (!selection) return;
    expect(fatal(bool((selection->anchor.node_id) == (NodeId(1)))));
    expect(fatal(bool((selection->anchor.offset) == (0u))));
    expect(fatal(bool((selection->active.node_id) == (NodeId(3)))));
    expect(fatal(bool((selection->active.offset) == (4u))));
};

"test_document_backspace_deletes_atomic_block_from_tree"_test = [] {
    auto document = parse_text(1, "---").document;
    auto id = document.blocks.front().id;
    auto transaction = document_delete_backward(
        document,
        DocumentSelection::caret(DocumentPosition{id, 1, TextAffinity::Downstream}));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool((transaction->after.blocks.size()) == (1u))));
    expect(fatal(bool(transaction->after.blocks.front().kind == BlockKind::Paragraph)));
    expect(fatal(bool((transaction->selection_after.active.node_id) == (transaction->after.blocks.front().id))));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string{}))));
};

"test_document_delete_forward_removes_atomic_block_and_keeps_adjacent_position"_test = [] {
    BlockNode rule;
    rule.id = NodeId(3);
    rule.kind = BlockKind::ThematicBreak;
    auto document = document_with({paragraph(1, 2, U"alpha"), std::move(rule)});
    auto paragraph_id = document.blocks.front().id;
    auto rule_id = document.blocks.back().id;
    auto transaction = document_delete_forward(
        document,
        DocumentSelection::caret(DocumentPosition{rule_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool((transaction->after.blocks.size()) == (1u))));
    expect(fatal(bool(transaction->after.blocks.front().kind == BlockKind::Paragraph)));
    expect(fatal(bool((transaction->selection_after.active.node_id) == (paragraph_id))));
    expect(fatal(bool((transaction->selection_after.active.offset) == (5u))));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("alpha")))));
};

"test_document_paste_builds_block_structure_in_one_transaction"_test = [] {
    auto document = document_with({paragraph(1, 2, U"alpha")});
    auto transaction = document_paste_text(document, caret(1, 5), U"x\r\ny");
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(transaction->reason == DocumentTransactionReason::Paste)));
    expect(fatal(bool((transaction->after.blocks.size()) == (2u))));
    expect(fatal(bool((block_inline_text_content(transaction->after.blocks[0].children)) == (std::u32string(U"alphax")))));
    expect(fatal(bool((block_inline_text_content(transaction->after.blocks[1].children)) == (std::u32string(U"y")))));
    expect(fatal(bool((transaction->selection_after.active.node_id) == (transaction->after.blocks[1].id))));
    expect(fatal(bool((transaction->selection_after.active.offset) == (1u))));
    expect(fatal(bool((serialize_markdown(transaction->after)) == (std::string("alphax\n\ny")))));
};

}; // suite document_edit_tests
