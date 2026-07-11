import std;
#include "test_framework.h"
import elmd.core.ast;
import elmd.core.document;
import elmd.core.document_edit;
import elmd.core.document_position;
import elmd.core.parser;
import elmd.core.serializer;

using namespace elmd;

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

}

ELMD_TEST(test_document_enter_splits_top_level_paragraph_by_node_position) {
    auto document = document_with({paragraph(1, 2, U"hello world")});
    auto transaction = document_enter(document, caret(1, 5));
    ELMD_CHECK(transaction.has_value());
    ELMD_CHECK_EQ(transaction->after.blocks.size(), 2u);
    ELMD_CHECK_EQ(block_inline_text_content(transaction->after.blocks[0].children), std::u32string(U"hello"));
    ELMD_CHECK_EQ(block_inline_text_content(transaction->after.blocks[1].children), std::u32string(U" world"));
    ELMD_CHECK(transaction->selection_after.active.node_id == transaction->after.blocks[1].id);
    ELMD_CHECK_EQ(transaction->selection_after.active.offset, 0u);
    ELMD_CHECK(validate_document(transaction->after).empty());
    ELMD_CHECK_EQ(serialize_markdown(transaction->after), std::string("hello\n\n world"));
}

ELMD_TEST(test_document_enter_preserves_inline_structure_when_splitting) {
    auto parsed = parse_text(1, "hello **world**");
    auto paragraph_id = parsed.document.blocks[0].id;
    auto transaction = document_enter(parsed.document, DocumentSelection::caret(DocumentPosition{paragraph_id, 8, TextAffinity::Downstream}));
    ELMD_CHECK(transaction.has_value());
    ELMD_CHECK_EQ(transaction->after.blocks.size(), 2u);
    ELMD_CHECK(transaction->after.blocks[0].children.back().kind == InlineKind::Strong);
    ELMD_CHECK(transaction->after.blocks[1].children.front().kind == InlineKind::Strong);
    ELMD_CHECK_EQ(serialize_markdown(transaction->after), std::string("hello **wo**\n\n**rld**"));
    auto reparsed = parse_text(2, serialize_markdown(transaction->after));
    ELMD_CHECK_EQ(reparsed.document.blocks.size(), 2u);
    ELMD_CHECK(reparsed.document.blocks[0].children.back().kind == InlineKind::Strong);
    ELMD_CHECK(reparsed.document.blocks[1].children.front().kind == InlineKind::Strong);
}

ELMD_TEST(test_document_enter_splits_nonempty_list_item_structurally) {
    BlockNode list;
    list.id = NodeId(1);
    list.kind = BlockKind::List;
    ListItem item;
    item.id = NodeId(2);
    item.marker = U"- ";
    item.children.push_back(paragraph(3, 4, U"first"));
    list.list_items.push_back(std::move(item));
    auto transaction = document_enter(document_with({std::move(list)}), caret(3, 2));
    ELMD_CHECK(transaction.has_value());
    const auto& after_list = transaction->after.blocks[0];
    ELMD_CHECK_EQ(after_list.list_items.size(), 2u);
    ELMD_CHECK_EQ(block_inline_text_content(after_list.list_items[0].children[0].children), std::u32string(U"fi"));
    ELMD_CHECK_EQ(block_inline_text_content(after_list.list_items[1].children[0].children), std::u32string(U"rst"));
    ELMD_CHECK(validate_document(transaction->after).empty());
    ELMD_CHECK_EQ(serialize_markdown(transaction->after), std::string("- fi\n- rst"));
}

ELMD_TEST(test_document_enter_empty_only_list_item_exits_list) {
    BlockNode list;
    list.id = NodeId(1);
    list.kind = BlockKind::List;
    ListItem item;
    item.id = NodeId(2);
    item.marker = U"- ";
    item.children.push_back(paragraph(3, 4, U""));
    list.list_items.push_back(std::move(item));
    auto transaction = document_enter(document_with({std::move(list)}), caret(3));
    ELMD_CHECK(transaction.has_value());
    ELMD_CHECK_EQ(transaction->after.blocks.size(), 1u);
    ELMD_CHECK(transaction->after.blocks[0].kind == BlockKind::Paragraph);
    ELMD_CHECK(transaction->selection_after.active.node_id == transaction->after.blocks[0].id);
    ELMD_CHECK(validate_document(transaction->after).empty());
}

ELMD_TEST(test_document_enter_empty_middle_list_item_splits_container) {
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
    ELMD_CHECK(transaction.has_value());
    ELMD_CHECK_EQ(transaction->after.blocks.size(), 3u);
    ELMD_CHECK(transaction->after.blocks[0].kind == BlockKind::List);
    ELMD_CHECK(transaction->after.blocks[1].kind == BlockKind::Paragraph);
    ELMD_CHECK(transaction->after.blocks[2].kind == BlockKind::List);
    ELMD_CHECK_EQ(transaction->after.blocks[0].list_items.size(), 1u);
    ELMD_CHECK_EQ(transaction->after.blocks[2].list_items.size(), 1u);
    ELMD_CHECK(validate_document(transaction->after).empty());
}

ELMD_TEST(test_document_enter_empty_quote_paragraph_exits_by_tree_position) {
    BlockNode quote;
    quote.id = NodeId(1);
    quote.kind = BlockKind::BlockQuote;
    quote.quote_children.push_back(paragraph(2, 3, U"before"));
    quote.quote_children.push_back(paragraph(4, 5, U""));
    quote.quote_children.push_back(paragraph(6, 7, U"after"));
    auto transaction = document_enter(document_with({std::move(quote)}), caret(4));
    ELMD_CHECK(transaction.has_value());
    ELMD_CHECK_EQ(transaction->after.blocks.size(), 3u);
    ELMD_CHECK(transaction->after.blocks[0].kind == BlockKind::BlockQuote);
    ELMD_CHECK(transaction->after.blocks[1].kind == BlockKind::Paragraph);
    ELMD_CHECK(transaction->after.blocks[2].kind == BlockKind::BlockQuote);
    ELMD_CHECK_EQ(transaction->after.blocks[0].quote_children.size(), 1u);
    ELMD_CHECK_EQ(transaction->after.blocks[2].quote_children.size(), 1u);
    ELMD_CHECK(validate_document(transaction->after).empty());
}

ELMD_TEST(test_document_history_restores_document_and_node_selection) {
    auto document = document_with({paragraph(1, 2, U"hello")});
    auto transaction = document_enter(document, caret(1, 2));
    ELMD_CHECK(transaction.has_value());
    DocumentHistory history;
    history.push(*transaction);
    auto undone = history.undo();
    ELMD_CHECK(undone.has_value());
    ELMD_CHECK_EQ(serialize_markdown(undone->first), std::string("hello"));
    ELMD_CHECK(undone->second.active.node_id == NodeId(1));
    ELMD_CHECK_EQ(undone->second.active.offset, 2u);
    auto redone = history.redo();
    ELMD_CHECK(redone.has_value());
    ELMD_CHECK_EQ(serialize_markdown(redone->first), std::string("he\n\nllo"));
    ELMD_CHECK(redone->second.active.node_id == transaction->selection_after.active.node_id);
    ELMD_CHECK_EQ(redone->second.active.offset, 0u);
}

ELMD_TEST(test_enter_splits_task_item_and_resets_new_checkbox) {
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
    ELMD_CHECK(transaction.has_value());
    if (!transaction) return;
    ELMD_CHECK_EQ(transaction->after.blocks[0].task_items.size(), 2u);
    ELMD_CHECK(transaction->after.blocks[0].task_items[0].checked);
    ELMD_CHECK(!transaction->after.blocks[0].task_items[1].checked);
    ELMD_CHECK(transaction->after.blocks[0].task_items[1].marker.empty());
    ELMD_CHECK_EQ(serialize_markdown(transaction->after), std::string("- [x] alpha\n- [ ] beta"));
}

ELMD_TEST(test_enter_empty_only_task_item_exits_task_list) {
    BlockNode task_list;
    task_list.id = NodeId(1);
    task_list.kind = BlockKind::TaskList;
    TaskListItem item;
    item.id = NodeId(2);
    item.children.push_back(paragraph(3, 0, U""));
    task_list.task_items.push_back(std::move(item));

    auto transaction = document_enter(document_with({task_list}), caret(3));
    ELMD_CHECK(transaction.has_value());
    if (!transaction) return;
    ELMD_CHECK_EQ(transaction->after.blocks.size(), 1u);
    ELMD_CHECK_EQ(transaction->after.blocks[0].kind, BlockKind::Paragraph);
    ELMD_CHECK(validate_document(transaction->after).empty());
}

ELMD_TEST(test_insert_text_preserves_inline_container_identity) {
    BlockNode block;
    block.id = NodeId(1);
    block.kind = BlockKind::Paragraph;
    InlineNode strong;
    strong.id = NodeId(2);
    strong.kind = InlineKind::Strong;
    strong.children.push_back(InlineNode::text_node(NodeId(3), U"alphabeta"));
    block.children.push_back(strong);

    auto transaction = document_insert_text(document_with({block}), caret(1, 5), U"-");
    ELMD_CHECK(transaction.has_value());
    if (!transaction) return;
    ELMD_CHECK_EQ(transaction->after.blocks[0].children[0].id, NodeId(2));
    ELMD_CHECK_EQ(transaction->after.blocks[0].children[0].children[0].id, NodeId(3));
    ELMD_CHECK_EQ(serialize_markdown(transaction->after), std::string("**alpha-beta**"));
    ELMD_CHECK_EQ(transaction->selection_after.active.offset, 6u);
}

ELMD_TEST(test_delete_backward_preserves_inline_container_identity) {
    BlockNode block;
    block.id = NodeId(1);
    block.kind = BlockKind::Paragraph;
    InlineNode emphasis;
    emphasis.id = NodeId(2);
    emphasis.kind = InlineKind::Emphasis;
    emphasis.children.push_back(InlineNode::text_node(NodeId(3), U"alpha"));
    block.children.push_back(emphasis);

    auto transaction = document_delete_backward(document_with({block}), caret(1, 5));
    ELMD_CHECK(transaction.has_value());
    if (!transaction) return;
    ELMD_CHECK_EQ(transaction->after.blocks[0].children[0].id, NodeId(2));
    ELMD_CHECK_EQ(transaction->after.blocks[0].children[0].children[0].id, NodeId(3));
    ELMD_CHECK_EQ(serialize_markdown(transaction->after), std::string("*alph*"));
    ELMD_CHECK_EQ(transaction->selection_after.active.offset, 4u);
}

ELMD_TEST(test_delete_forward_preserves_inline_container_identity) {
    auto document = document_with({paragraph(1, 2, U"alpha")});
    auto transaction = document_delete_forward(document, caret(1, 1));
    ELMD_CHECK(transaction.has_value());
    if (!transaction) return;
    ELMD_CHECK_EQ(transaction->after.blocks[0].children[0].id, NodeId(2));
    ELMD_CHECK_EQ(serialize_markdown(transaction->after), std::string("apha"));
    ELMD_CHECK_EQ(transaction->selection_after.active.offset, 1u);
}

ELMD_TEST(test_backspace_at_top_level_paragraph_start_merges_ast_siblings) {
    auto document = document_with({paragraph(1, 2, U"alpha"), paragraph(3, 4, U"beta")});
    auto transaction = document_delete_backward(document, caret(3, 0));
    ELMD_CHECK(transaction.has_value());
    if (!transaction) return;
    ELMD_CHECK_EQ(transaction->after.blocks.size(), 1u);
    ELMD_CHECK_EQ(transaction->after.blocks[0].id, NodeId(1));
    ELMD_CHECK_EQ(serialize_markdown(transaction->after), std::string("alphabeta"));
    ELMD_CHECK_EQ(transaction->selection_after.active.node_id, NodeId(1));
    ELMD_CHECK_EQ(transaction->selection_after.active.offset, 5u);
}

ELMD_TEST(test_delete_at_top_level_paragraph_end_merges_ast_siblings) {
    auto document = document_with({paragraph(1, 2, U"alpha"), paragraph(3, 4, U"beta")});
    auto transaction = document_delete_forward(document, caret(1, 5));
    ELMD_CHECK(transaction.has_value());
    if (!transaction) return;
    ELMD_CHECK_EQ(transaction->after.blocks.size(), 1u);
    ELMD_CHECK_EQ(transaction->after.blocks[0].id, NodeId(1));
    ELMD_CHECK_EQ(serialize_markdown(transaction->after), std::string("alphabeta"));
    ELMD_CHECK_EQ(transaction->selection_after.active.offset, 5u);
}
