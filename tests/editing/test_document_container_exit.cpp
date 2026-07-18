#include <algorithm>
#include <cstddef>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "support/folia_test.hpp"
import folia.core.ast;
import folia.core.block_source;
import folia.core.block_tree;
import folia.core.document;
import folia.core.document_edit;
import folia.core.document_history;
import folia.core.document_ids;
import folia.core.document_operation_apply;
import folia.core.document_symbols;
import folia.core.document_text;
import folia.core.inline_cst;
import folia.core.inline_document;
import folia.core.parser;
import folia.core.serializer;
import folia.core.text_edit;

using namespace folia;
using namespace boost::ut;

#include "support/document_edit_test_support.hpp"

suite document_container_exit_tests = [] {

"enter_edits_code_math_and_table_cell_sources"_test = [] {
    auto fenced = parse_document("```cpp\none\ntwo\n```");
    auto const code_offset = block_source_offset_for_content(fenced.root.children.front().block_source, 3);
    auto code = test_edit::document_enter(fenced, TextSelection::caret({fenced.root.children.front().id, code_offset, TextAffinity::Downstream}));
    expect(fatal(bool(code.has_value())));
    if (code) {
        expect(fatal(bool(block_source_content(code->after.root.children.front().block_source) == U"one\n\ntwo\n")));
        expect(fatal(bool(code->selection_after.active.source_offset == code_offset + 1)));
    }

    auto math = parse_document("$$\na+b\n$$");
    auto const math_offset = block_source_offset_for_content(math.root.children.front().block_source, 1);
    auto math_break = test_edit::document_enter(math, TextSelection::caret({math.root.children.front().id, math_offset, TextAffinity::Downstream}));
    expect(fatal(bool(math_break.has_value())));
    if (math_break) expect(fatal(bool(block_source_content(math_break->after.root.children.front().block_source) == U"a\n+b\n")));

    auto table = parse_document("| ab |\n| --- |");
    auto& cell = table.root.children.front().children.front().children.front();
    auto cell_break = test_edit::document_enter(table, caret(cell, 1));
    expect(fatal(bool(cell_break.has_value())));
    if (cell_break) {
        auto const& edited = cell_break->after.root.children.front().children.front().children.front().inline_content;
        expect(fatal(bool(edited.source == U"a<br>b")));
        expect(fatal(bool(contains_html_tag(edited.tree.nodes, "br"))));
        expect(fatal(bool(serialize_markdown(cell_break->after).find("a<br>b") != std::string::npos)));

        auto second_break = test_edit::document_enter(cell_break->after, cell_break->selection_after);
        expect(fatal(bool(second_break.has_value())));
        if (second_break) {
            auto const& with_empty_line = second_break->after.root.children.front().children.front().children.front().inline_content;
            expect(fatal(bool(with_empty_line.source == U"a<br><br>b")));
            auto removed = test_edit::document_delete_backward(second_break->after, second_break->selection_after);
            expect(fatal(bool(removed.has_value())));
            if (removed) {
                auto const& joined = removed->after.root.children.front().children.front().children.front().inline_content;
                expect(fatal(bool(joined.source == U"a<br>b")));
                expect(fatal(bool(removed->selection_after.active.source_offset == 5u)));
            }
        }
    }
};

"enter_on_empty_indented_code_line_exits_the_block"_test = [] {
    auto document = parse_document("    one\n\n    two");
    auto const code_id = document.root.children.front().id;
    auto transaction = test_edit::document_enter(document, TextSelection::caret({code_id, 8, TextAffinity::Downstream}));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(transaction->after.root.children.size() == 3u)));
    expect(fatal(bool(transaction->after.root.children[0].kind == BlockKind::CodeBlock)));
    expect(fatal(bool(transaction->after.root.children[0].block_source.source() == U"    one")));
    expect(fatal(bool(transaction->after.root.children[1].kind == BlockKind::Paragraph)));
    expect(fatal(bool(transaction->after.root.children[2].kind == BlockKind::CodeBlock)));
    expect(fatal(bool(transaction->after.root.children[2].block_source.source() == U"    two")));
    expect(fatal(bool(transaction->selection_after.active.container_id == transaction->after.root.children[1].id)));
    expect(fatal(bool(serialize_markdown(transaction->after).find("    one\n    \n") == std::string::npos)));
};

"enter_on_empty_quote_line_exits_the_quote"_test = [] {
    auto document = parse_document("> one\n>");
    auto const& quote = document.root.children.front();
    expect(fatal(bool(quote.kind == BlockKind::BlockQuote)));
    expect(fatal(bool(quote.children.size() == 2u)));
    auto const empty_id = quote.children.back().id;
    auto transaction = test_edit::document_enter(document, TextSelection::caret({empty_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(transaction->after.root.children.size() == 2u)));
    expect(fatal(bool(transaction->after.root.children[0].kind == BlockKind::BlockQuote)));
    expect(fatal(bool(transaction->after.root.children[0].children.size() == 1u)));
    expect(fatal(bool(transaction->after.root.children[1].kind == BlockKind::Paragraph)));
    expect(fatal(bool(transaction->after.root.children[1].id != empty_id)));
    expect(fatal(bool(transaction->selection_after.active.container_id == transaction->after.root.children[1].id)));
    expect(fatal(bool(find_block(transaction->after.root, empty_id) == nullptr)));
};

"enter_on_trailing_empty_callout_line_exits_at_any_ancestor_depth"_test = [] {
    for (const auto& source : std::vector<std::string>{
             "> [!TIP]\n> one\n>",
             "- item\n  > [!WARNING]\n  > one\n  > ",
         }) {
        auto document = parse_document(source);
        const BlockNode* callout = nullptr;
        walk_blocks(document.root, [&](const BlockNode& node) {
            if (!callout && node.kind == BlockKind::Callout) callout = &node;
        });
        expect(fatal(bool(callout != nullptr))) << source;
        if (!callout || callout->children.size() < 2) continue;
        const auto callout_id = callout->id;
        const auto empty_id = callout->children.back().id;
        auto transaction = test_edit::document_enter(
            document,
            TextSelection::caret({empty_id, 0, TextAffinity::Downstream}));
        expect(fatal(bool(transaction.has_value()))) << source;
        if (!transaction) continue;
        expect(fatal(bool(find_block(transaction->after.root, empty_id) == nullptr))) << source;
        const auto* updated = find_block(transaction->after.root, callout_id);
        expect(fatal(bool(updated != nullptr && updated->kind == BlockKind::Callout))) << source;
        expect(fatal(bool(updated != nullptr && updated->children.size() == 1u))) << source;
        const auto* selected = find_block(
            transaction->after.root,
            transaction->selection_after.active.container_id);
        expect(fatal(bool(selected != nullptr && selected->kind == BlockKind::Paragraph))) << source;
        expect(fatal(bool(selected != nullptr && selected->inline_content.source.empty()))) << source;
        expect_document_valid(transaction->after);
    }
};

"enter_on_middle_empty_callout_line_splits_without_copying_the_title"_test = [] {
    auto document = parse_document("> [!IMPORTANT] title\n> first\n>\n>\n> last");
    auto const& callout = document.root.children.front();
    expect(fatal(bool(callout.kind == BlockKind::Callout)));
    expect(fatal(bool(callout.children.size() == 4u)));
    if (callout.children.size() != 4) return;
    auto const empty_id = callout.children[2].id;
    auto transaction = test_edit::document_enter(
        document,
        TextSelection::caret({empty_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(transaction->after.root.children.size() == 3u)));
    if (transaction->after.root.children.size() != 3) return;
    auto const& leading = transaction->after.root.children[0];
    auto const& outside = transaction->after.root.children[1];
    auto const& trailing = transaction->after.root.children[2];
    expect(fatal(bool(leading.kind == BlockKind::Callout && callout_title_block(leading) != nullptr)));
    expect(fatal(bool(outside.kind == BlockKind::Paragraph && outside.inline_content.source.empty())));
    expect(fatal(bool(trailing.kind == BlockKind::Callout && callout_title_block(trailing) == nullptr)));
    expect(fatal(bool(trailing.container_special().callout_kind == "IMPORTANT")));
    expect(fatal(bool(trailing.children.size() == 1u)));
    expect(fatal(bool(transaction->selection_after.active.container_id == outside.id)));
    expect_document_valid(transaction->after);
};

"enter_on_trailing_empty_footnote_line_exits_the_definition"_test = [] {
    auto document = parse_document("body[^1]\n\n[^1]: first");
    expect(fatal(bool(document.root.children.size() == 2u)));
    if (document.root.children.size() != 2u) return;
    auto const& definition = document.root.children[1];
    expect(fatal(bool(definition.kind == BlockKind::FootnoteDefinition)));
    expect(fatal(bool(definition.children.size() == 1u)));
    if (definition.children.size() != 1u) return;

    auto const first_id = definition.children.front().id;
    auto opened = test_edit::document_enter(
        document,
        TextSelection::caret({first_id, 5, TextAffinity::Downstream}));
    expect(fatal(bool(opened.has_value())));
    if (!opened) return;

    auto const* opened_definition = find_block(opened->after.root, definition.id);
    expect(fatal(bool(opened_definition != nullptr)));
    expect(fatal(bool(opened_definition && opened_definition->children.size() == 2u)));
    if (!opened_definition || opened_definition->children.size() != 2u) return;
    auto const empty_id = opened_definition->children.back().id;
    expect(fatal(bool(opened_definition->children.back().inline_content.source.empty())));

    auto exited = test_edit::document_enter(
        opened->after,
        TextSelection::caret({empty_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(exited.has_value())));
    if (!exited) return;

    expect(fatal(bool(exited->after.root.children.size() == 3u)));
    if (exited->after.root.children.size() != 3u) return;
    expect(fatal(bool(exited->after.root.children[1].kind == BlockKind::FootnoteDefinition)));
    expect(fatal(bool(exited->after.root.children[1].children.size() == 1u)));
    expect(fatal(bool(exited->after.root.children[2].kind == BlockKind::Paragraph)));
    expect(fatal(bool(exited->after.root.children[2].inline_content.source.empty())));
    expect(fatal(bool(exited->selection_after.active.container_id == exited->after.root.children[2].id)));
    expect(fatal(bool(find_block(exited->after.root, empty_id) == nullptr)));
    expect_document_valid(exited->after);
};

"enter_exits_empty_quote_inside_arbitrary_ancestors"_test = [] {
    auto document = parse_document("- item\n  > ");
    expect(fatal(bool((document.root.children.size()) == (1u))));
    if (document.root.children.empty()) return;
    auto const& list = document.root.children.front();
    expect(fatal(bool(list.kind == BlockKind::List)));
    expect(fatal(bool(!list.children.empty())));
    if (list.children.empty()) return;
    auto const& item = list.children.front();
    auto quote = std::find_if(item.children.begin(), item.children.end(), [](auto const& child) {
        return child.kind == BlockKind::BlockQuote;
    });
    expect(fatal(bool(quote != item.children.end())));
    if (quote == item.children.end() || quote->children.empty()) return;
    auto const empty_id = quote->children.front().id;

    auto transaction = test_edit::document_enter(document, TextSelection::caret({empty_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(find_block(transaction->after.root, empty_id) == nullptr)));
    auto const* selected = find_block(transaction->after.root, transaction->selection_after.active.container_id);
    expect(fatal(bool(selected != nullptr)));
    if (selected) {
        expect(fatal(bool(selected->kind == BlockKind::Paragraph)));
        expect(fatal(bool(selected->inline_content.source.empty())));
    }
    auto const* updated_list = find_block(transaction->after.root, list.id);
    expect(fatal(bool(updated_list != nullptr)));
    expect(fatal(bool(updated_list && !updated_list->children.empty())));
    if (updated_list && !updated_list->children.empty()) {
        expect(fatal(bool(std::none_of(updated_list->children.front().children.begin(), updated_list->children.front().children.end(), [](auto const& child) {
            return child.kind == BlockKind::BlockQuote;
        }))));
    }
    expect_document_valid(transaction->after);
};

}; // suite document_container_exit_tests
