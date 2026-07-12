#include <algorithm>
#include <cstddef>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "elmd_test.hpp"
import elmd.core.ast;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.document_edit;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.parser;
import elmd.core.serializer;
import elmd.core.text_edit;

using namespace elmd;
using namespace boost::ut;

namespace {

EditorDocument parse_document(std::string source) {
    return parse_text(1, source).document;
}

BlockNode& first_editable(EditorDocument& document) {
    BlockNode* found = nullptr;
    walk_blocks(document.root, [&](BlockNode& node) {
        if (!found && (node.kind == BlockKind::Paragraph || node.kind == BlockKind::Heading || node.kind == BlockKind::TableCell)) found = &node;
    });
    return *found;
}

const BlockNode& first_editable(const EditorDocument& document) {
    const BlockNode* found = nullptr;
    walk_blocks(document.root, [&](const BlockNode& node) {
        if (!found && (node.kind == BlockKind::Paragraph || node.kind == BlockKind::Heading || node.kind == BlockKind::TableCell)) found = &node;
    });
    return *found;
}

TextSelection caret(const BlockNode& node, std::size_t offset = 0) {
    return TextSelection::caret(TextPosition{node.id, offset, TextAffinity::Downstream});
}

TextSelection range(const BlockNode& node, std::size_t start, std::size_t end) {
    return TextSelection{
        TextPosition{node.id, start, TextAffinity::Downstream},
        TextPosition{node.id, end, TextAffinity::Downstream}};
}

void expect_inline_lossless(const InlineDocument& inline_document) {
    expect(fatal(bool(tokens_partition_source(inline_document.tree, inline_document.source.size()))));
    expect(fatal(bool(roots_partition_source(inline_document.tree, inline_document.source.size()))));
    expect(fatal(bool(flatten_tokens(inline_document.tree, inline_document.source) == inline_document.source)));
    expect(fatal(bool(serialize_lossless(inline_document.tree, inline_document.source) == inline_document.source)));
}

void expect_document_valid(const EditorDocument& document) {
    expect(fatal(bool(validate_document(document).empty())));
    walk_blocks(document.root, [&](const BlockNode& node) {
        if (node.kind == BlockKind::Paragraph || node.kind == BlockKind::Heading || node.kind == BlockKind::TableCell) {
            expect_inline_lossless(node.inline_content);
        }
    });
}

} // namespace

suite document_edit_tests = [] {

"insert_text_edits_only_the_target_inline_source"_test = [] {
    auto document = parse_document("alpha\n\nbeta");
    const auto first_id = document.root.children[0].id;
    const auto second_before = document.root.children[1].inline_content;
    auto transaction = document_insert_text(document, caret(document.root.children[0], 2), U"X");
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(transaction->after.root.children[0].inline_content.source == U"alXpha")));
    expect(fatal(bool(transaction->after.root.children[1].inline_content.source == second_before.source)));
    expect(fatal(bool(transaction->after.root.children[0].id == first_id)));
    expect(fatal(bool(transaction->selection_after.active.source_offset == 3u)));
    expect_document_valid(transaction->after);
};

"selection_replacement_uses_source_offsets_including_markers"_test = [] {
    auto document = parse_document("**alpha**");
    const auto& paragraph = first_editable(document);
    auto transaction = document_insert_text(document, range(paragraph, 2, 7), U"beta");
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(first_editable(transaction->after).inline_content.source == U"**beta**")));
    expect(fatal(bool(transaction->selection_after.active.source_offset == 6u)));
    expect_document_valid(transaction->after);
};

"marker_input_keeps_an_editable_incomplete_cst"_test = [] {
    auto document = parse_document("abc");
    const auto& paragraph = first_editable(document);
    auto transaction = document_insert_text(document, caret(paragraph, 1), U"*");
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    const auto& inline_document = first_editable(transaction->after).inline_content;
    expect(fatal(bool(inline_document.source == U"a**bc")));
    expect(fatal(bool(transaction->selection_after.active.source_offset == 2u)));
    expect_inline_lossless(inline_document);
};

"backspace_inside_markers_reparses_one_inline_document"_test = [] {
    auto document = parse_document("**alpha**");
    const auto& paragraph = first_editable(document);
    auto transaction = document_delete_backward(document, caret(paragraph, 1));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(first_editable(transaction->after).inline_content.source == U"*alpha**")));
    expect(fatal(bool(transaction->selection_after.active.source_offset == 0u)));
    expect_document_valid(transaction->after);
};

"delete_forward_preserves_unclosed_source_verbatim"_test = [] {
    auto document = parse_document("[title](url)");
    const auto& paragraph = first_editable(document);
    auto transaction = document_delete_forward(document, caret(paragraph, 11));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(first_editable(transaction->after).inline_content.source == U"[title](url")));
    expect_document_valid(transaction->after);
};

"format_commands_wrap_and_unwrap_exact_source"_test = [] {
    struct Case { InlineFormat format; std::u32string expected; InlineCstKind kind; };
    const std::vector<Case> cases{
        {InlineFormat::Emphasis, U"*value*", InlineCstKind::Emphasis},
        {InlineFormat::Strong, U"**value**", InlineCstKind::Strong},
        {InlineFormat::Strikethrough, U"~~value~~", InlineCstKind::Strikethrough},
        {InlineFormat::Code, U"`value`", InlineCstKind::CodeSpan},
        {InlineFormat::Math, U"$value$", InlineCstKind::InlineMath},
    };
    for (const auto& test : cases) {
        auto document = parse_document("value");
        const auto& paragraph = first_editable(document);
        auto wrapped = document_toggle_inline_format(document, range(paragraph, 0, 5), test.format);
        expect(fatal(bool(wrapped.has_value())));
        if (!wrapped) continue;
        const auto& inline_document = first_editable(wrapped->after).inline_content;
        expect(fatal(bool(inline_document.source == test.expected)));
        expect(fatal(bool(inline_contains_kind(inline_document, test.kind))));
        auto unwrapped = document_toggle_inline_format(wrapped->after, wrapped->selection_after, test.format);
        expect(fatal(bool(unwrapped.has_value())));
        if (unwrapped) expect(fatal(bool(first_editable(unwrapped->after).inline_content.source == U"value")));
    }
};

"link_and_image_commands_are_source_edits"_test = [] {
    auto link_document = parse_document("title");
    const auto& link_paragraph = first_editable(link_document);
    auto link = document_insert_link(link_document, range(link_paragraph, 0, 5), "url", "name");
    expect(fatal(bool(link.has_value())));
    if (link) {
        const auto& inline_document = first_editable(link->after).inline_content;
        expect(fatal(bool(inline_document.source == U"[title](url \"name\")")));
        expect(fatal(bool(inline_contains_kind(inline_document, InlineCstKind::Link))));
    }
    auto image_document = parse_document("diagram");
    const auto& image_paragraph = first_editable(image_document);
    auto image = document_insert_image(image_document, range(image_paragraph, 0, 7), "image.png", "diagram");
    expect(fatal(bool(image.has_value())));
    if (image) {
        const auto& inline_document = first_editable(image->after).inline_content;
        expect(fatal(bool(inline_document.source == U"![diagram](image.png)")));
        expect(fatal(bool(inline_contains_kind(inline_document, InlineCstKind::Image))));
    }
};

"enter_splits_source_without_serializing_the_document"_test = [] {
    auto document = parse_document("**alpha**");
    const auto original_id = first_editable(document).id;
    auto transaction = document_enter(document, caret(first_editable(document), 5));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(transaction->after.root.children.size() == 2u)));
    expect(fatal(bool(transaction->after.root.children[0].id == original_id)));
    expect(fatal(bool(transaction->after.root.children[0].inline_content.source == U"**alp")));
    expect(fatal(bool(transaction->after.root.children[1].inline_content.source == U"ha**")));
    expect(fatal(bool(transaction->selection_after.active.container_id == transaction->after.root.children[1].id)));
    expect_document_valid(transaction->after);
};

"backspace_and_delete_join_adjacent_blocks"_test = [] {
    auto document = parse_document("alpha\n\nbeta");
    const auto second_id = document.root.children[1].id;
    auto backward = document_delete_backward(document, caret(document.root.children[1], 0));
    expect(fatal(bool(backward.has_value())));
    if (backward) {
        expect(fatal(bool(backward->after.root.children.size() == 1u)));
        expect(fatal(bool(backward->after.root.children[0].inline_content.source == U"alphabeta")));
        expect(fatal(bool(backward->selection_after.active.source_offset == 5u)));
    }
    auto forward = document_delete_forward(document, caret(document.root.children[0], 5));
    expect(fatal(bool(forward.has_value())));
    if (forward) {
        expect(fatal(bool(forward->after.root.children.size() == 1u)));
        expect(fatal(bool(forward->after.root.children[0].inline_content.source == U"alphabeta")));
        expect(fatal(bool(elmd::find_block(forward->after.root, second_id) == nullptr)));
    }
};

"cross_block_selection_deletes_structure_and_merges_sources"_test = [] {
    auto document = parse_document("alpha\n\nbeta\n\ngamma");
    TextSelection selection{
        {document.root.children[0].id, 2, TextAffinity::Downstream},
        {document.root.children[2].id, 3, TextAffinity::Downstream}};
    auto transaction = document_delete_selection(document, selection);
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(transaction->after.root.children.size() == 1u)));
    expect(fatal(bool(transaction->after.root.children.front().inline_content.source == U"alma")));
    expect(fatal(bool(transaction->selection_after.active.source_offset == 2u)));
    expect_document_valid(transaction->after);
};

"paste_normalizes_newlines_and_builds_blocks"_test = [] {
    auto document = parse_document("ab");
    auto transaction = document_paste_text(document, caret(first_editable(document), 1), U"X\r\nY");
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(transaction->after.root.children.size() == 2u)));
    expect(fatal(bool(transaction->after.root.children[0].inline_content.source == U"aX")));
    expect(fatal(bool(transaction->after.root.children[1].inline_content.source == U"Yb")));
    expect_document_valid(transaction->after);
};

"paragraph_heading_conversion_preserves_inline_source"_test = [] {
    auto document = parse_document("__title__");
    auto heading = document_set_heading(document, caret(first_editable(document), 3), 3);
    expect(fatal(bool(heading.has_value())));
    if (!heading) return;
    expect(fatal(bool(heading->after.root.children.front().kind == BlockKind::Heading)));
    expect(fatal(bool(heading->after.root.children.front().level == 3u)));
    expect(fatal(bool(heading->after.root.children.front().inline_content.source == U"__title__")));
    auto paragraph = document_set_heading(heading->after, heading->selection_after, 0);
    expect(fatal(bool(paragraph.has_value())));
    if (paragraph) expect(fatal(bool(paragraph->after.root.children.front().kind == BlockKind::Paragraph)));
};

"quote_callout_and_list_commands_use_unified_children"_test = [] {
    auto document = parse_document("one\n\ntwo");
    TextSelection both{{document.root.children[0].id, 0, TextAffinity::Downstream}, {document.root.children[1].id, 3, TextAffinity::Downstream}};
    auto quote = document_toggle_block_quote(document, both);
    expect(fatal(bool(quote.has_value())));
    if (quote) {
        expect(fatal(bool(quote->after.root.children.front().kind == BlockKind::BlockQuote)));
        expect(fatal(bool(quote->after.root.children.front().children.size() == 2u)));
    }
    auto list = document_toggle_list(document, both, ListStyle::Bullet);
    expect(fatal(bool(list.has_value())));
    if (list) {
        const auto& root_list = list->after.root.children.front();
        expect(fatal(bool(root_list.kind == BlockKind::List)));
        expect(fatal(bool(root_list.children.size() == 2u)));
        expect(fatal(bool(root_list.children.front().kind == BlockKind::ListItem)));
        expect(fatal(bool(root_list.children.front().children.front().inline_content.source == U"one")));
    }
    auto callout = document_toggle_callout(document, both, "NOTE");
    expect(fatal(bool(callout.has_value())));
    if (callout) {
        expect(fatal(bool(callout->after.root.children.front().kind == BlockKind::Callout)));
        expect(fatal(bool(callout->after.root.children.front().children.size() == 2u)));
    }
};

"list_indent_and_outdent_move_existing_nodes"_test = [] {
    auto document = parse_document("- one\n- two\n");
    auto& list = document.root.children.front();
    const auto second_paragraph_id = list.children[1].children.front().id;
    auto indented = document_indent_list_item(document, TextSelection::caret({second_paragraph_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(indented.has_value())));
    if (!indented) return;
    const auto& after_list = indented->after.root.children.front();
    expect(fatal(bool(after_list.children.size() == 1u)));
    expect(fatal(bool(after_list.children.front().children.back().kind == BlockKind::List)));
    auto outdented = document_outdent_list_item(indented->after, indented->selection_after);
    expect(fatal(bool(outdented.has_value())));
    if (outdented) expect(fatal(bool(outdented->after.root.children.front().children.size() == 2u)));
};

"table_commands_operate_on_table_row_and_cell_nodes"_test = [] {
    auto document = parse_document("anchor");
    auto table = make_table_block(document, 1, 2);
    const auto first_cell_id = table.children.front().children.front().id;
    auto inserted = document_insert_atomic_block(document, caret(first_editable(document), 0), std::move(table));
    expect(fatal(bool(inserted.has_value())));
    if (!inserted) return;
    TextSelection in_cell = TextSelection::caret({first_cell_id, 0, TextAffinity::Downstream});
    auto column = document_edit_table(inserted->after, in_cell, DocumentTableEdit::InsertColumnRight);
    expect(fatal(bool(column.has_value())));
    if (!column) return;
    const auto& table_after_column = column->after.root.children[1];
    expect(fatal(bool(table_after_column.children.front().children.size() == 3u)));
    expect(fatal(bool(table_after_column.children[1].children.size() == 3u)));
    auto row = document_edit_table(column->after, in_cell, DocumentTableEdit::InsertRowBelow);
    expect(fatal(bool(row.has_value())));
    if (row) expect(fatal(bool(row->after.root.children[1].children.size() == 3u)));
    auto aligned = document_edit_table(column->after, in_cell, DocumentTableEdit::SetColumnAlignment, TableAlignment::Center);
    expect(fatal(bool(aligned.has_value())));
    if (aligned) expect(fatal(bool(aligned->after.root.children[1].table_aligns.front() == TableAlignment::Center)));
};

"random_source_edits_preserve_selection_and_losslessness"_test = [] {
    std::mt19937_64 random(0x5eed);
    auto document = parse_document("");
    normalize_document(document);
    auto position = caret(first_editable(document), 0);
    for (std::size_t step = 0; step < 500; ++step) {
        const bool insert = first_editable(document).inline_content.source.empty() || (random() & 1u) == 0;
        if (insert) {
            const char32_t value = U'a' + static_cast<char32_t>(random() % 26);
            auto transaction = document_insert_text(document, position, std::u32string(1, value));
            expect(fatal(bool(transaction.has_value())));
            if (!transaction) break;
            document = std::move(transaction->after);
            position = transaction->selection_after;
        } else {
            auto transaction = document_delete_backward(document, position);
            expect(fatal(bool(transaction.has_value())));
            if (!transaction) break;
            document = std::move(transaction->after);
            position = transaction->selection_after;
        }
        const auto& inline_document = first_editable(document).inline_content;
        expect(fatal(bool(position.active.source_offset <= inline_document.source.size())));
        expect_document_valid(document);
    }
};

"save_reload_after_edits_is_lossless"_test = [] {
    auto document = parse_document("*abc*\n\n[title](<url>)");
    auto transaction = document_insert_text(document, caret(document.root.children[0], 3), U"X");
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    const auto saved = serialize_markdown(transaction->after);
    const auto reloaded = parse_document(saved);
    expect(fatal(bool(serialize_markdown(reloaded) == saved)));
    expect_document_valid(reloaded);
};

}; // suite document_edit_tests
