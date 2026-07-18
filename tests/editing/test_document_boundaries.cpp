#include <algorithm>
#include <cstddef>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "support/folia_test.hpp"
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.document_edit;
import elmd.core.document_history;
import elmd.core.document_ids;
import elmd.core.document_operation_apply;
import elmd.core.document_symbols;
import elmd.core.document_text;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.parser;
import elmd.core.serializer;
import elmd.core.text_edit;

using namespace elmd;
using namespace boost::ut;

#include "support/document_edit_test_support.hpp"

suite document_boundary_tests = [] {

"backspace_and_delete_remove_an_explicit_blank_line"_test = [] {
    auto document = parse_document("alpha\n\n\nbeta");
    expect(fatal(bool(document.root.children.size() == 3u)));
    if (document.root.children.size() != 3u) return;
    const auto blank_id = document.root.children[1].id;
    auto backward = test_edit::document_delete_backward(document, caret(document.root.children[1], 0));
    expect(fatal(bool(backward.has_value())));
    if (backward) {
        expect(fatal(bool(backward->after.root.children.size() == 2u)));
        expect(fatal(bool(backward->after.root.children[0].inline_content.source == U"alpha")));
        expect(fatal(bool(backward->after.root.children[1].inline_content.source == U"beta")));
        expect(fatal(bool(backward->selection_after.active.source_offset == 5u)));
        expect(fatal(bool(serialize_markdown(backward->after) == "alpha\nbeta")));
    }
    auto forward = test_edit::document_delete_forward(document, caret(document.root.children[0], 5));
    expect(fatal(bool(forward.has_value())));
    if (forward) {
        expect(fatal(bool(forward->after.root.children.size() == 2u)));
        expect(fatal(bool(forward->after.root.children[0].inline_content.source == U"alpha")));
        expect(fatal(bool(forward->after.root.children[1].inline_content.source == U"beta")));
        expect(fatal(bool(elmd::find_block(forward->after.root, blank_id) == nullptr)));
        expect(fatal(bool(serialize_markdown(forward->after) == "alpha\nbeta")));
    }
};

"backspace_and_delete_join_adjacent_nonblank_blocks"_test = [] {
    auto backward_document = parse_document("# alpha\nbeta");
    expect(fatal(bool(backward_document.root.children.size() == 2u)));
    if (backward_document.root.children.size() != 2u) return;
    auto backward = test_edit::document_delete_backward(
        backward_document,
        caret(backward_document.root.children[1], 0));
    expect(fatal(bool(backward.has_value())));
    if (backward) {
        expect(fatal(bool(backward->after.root.children.size() == 1u)));
        expect(fatal(bool(backward->after.root.children[0].kind == BlockKind::Heading)));
        expect(fatal(bool(backward->after.root.children[0].inline_content.source == U"alphabeta")));
        expect(fatal(bool(serialize_markdown(backward->after) == "# alphabeta")));
    }

    auto forward_document = parse_document("# alpha\nbeta");
    const auto paragraph_id = forward_document.root.children[1].id;
    auto forward = test_edit::document_delete_forward(
        forward_document,
        caret(forward_document.root.children[0], 5));
    expect(fatal(bool(forward.has_value())));
    if (forward) {
        expect(fatal(bool(forward->after.root.children.size() == 1u)));
        expect(fatal(bool(forward->after.root.children[0].kind == BlockKind::Heading)));
        expect(fatal(bool(forward->after.root.children[0].inline_content.source == U"alphabeta")));
        expect(fatal(bool(elmd::find_block(forward->after.root, paragraph_id) == nullptr)));
        expect(fatal(bool(serialize_markdown(forward->after) == "# alphabeta")));
    }
};

"callout_title_boundaries_use_tree_split_join_and_unwrap"_test = [] {
    auto document = parse_document("> [!NOTE] title\n> body");
    const auto callout_id = document.root.children.front().id;
    const auto title_id = document.root.children.front().children.front().id;

    auto entered_at_start = test_edit::document_enter(
        document,
        TextSelection::caret({title_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(entered_at_start.has_value()))) << "enter at callout title start";
    if (entered_at_start) {
        auto const& callout = entered_at_start->after.root.children.front();
        expect(fatal(bool(callout_title_block(callout) == nullptr)));
        expect(fatal(bool(callout.children.size() == 2u)));
        if (callout.children.size() == 2u) {
            expect(fatal(bool(callout.children[0].inline_content.source == U"title")));
            expect(fatal(bool(callout.children[1].inline_content.source == U"body")));
            expect(fatal(bool(entered_at_start->selection_after.active.container_id == callout.children[0].id)));
            expect(fatal(bool(entered_at_start->selection_after.active.source_offset == 0u)));
        }
        expect_document_valid(entered_at_start->after);
    }

    auto entered = test_edit::document_enter(
        document,
        TextSelection::caret({title_id, 2, TextAffinity::Downstream}));
    expect(fatal(bool(entered.has_value()))) << "enter callout title";
    if (entered) {
        auto const& callout = entered->after.root.children.front();
        expect(fatal(bool(callout.kind == BlockKind::Callout)));
        const auto* title = callout_title_block(callout);
        expect(fatal(bool(title != nullptr)));
        expect(fatal(bool(title && title->inline_content.source == U"ti")));
        expect(fatal(bool(callout.children.size() == 3u)));
        if (callout.children.size() == 3) {
            expect(fatal(bool(callout.children[1].inline_content.source == U"tle")));
            expect(fatal(bool(callout.children[2].inline_content.source == U"body")));
            expect(fatal(bool(entered->selection_after.active.container_id == callout.children[1].id)));
        }
        auto serialized = serialize_markdown(entered->after);
        expect(fatal(bool(serialized == "> [!NOTE] ti\n> tle\n>\n> body"))) << serialized;
        expect_document_valid(entered->after);
    }

    const auto body_id = document.root.children.front().children[1].id;
    auto backward = test_edit::document_delete_backward(
        document,
        TextSelection::caret({body_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(backward.has_value()))) << "backspace first callout body";
    if (backward) {
        auto const& callout = backward->after.root.children.front();
        const auto* title = callout_title_block(callout);
        expect(fatal(bool(title != nullptr)));
        expect(fatal(bool(title && title->inline_content.source == U"titlebody")));
        expect(fatal(bool(callout.children.size() == 1u)));
        expect(fatal(bool(backward->selection_after.active.container_id == title_id)));
        expect(fatal(bool(backward->selection_after.active.source_offset == 5u)));
        expect_document_valid(backward->after);
    }

    auto forward = test_edit::document_delete_forward(
        document,
        TextSelection::caret({title_id, 5, TextAffinity::Upstream}));
    expect(fatal(bool(forward.has_value()))) << "delete after callout title";
    if (forward) {
        auto const& callout = forward->after.root.children.front();
        const auto* title = callout_title_block(callout);
        expect(fatal(bool(title != nullptr)));
        expect(fatal(bool(title && title->inline_content.source == U"titlebody")));
        expect(fatal(bool(callout.children.size() == 1u)));
        expect(fatal(bool(forward->selection_after.active.container_id == title_id)));
        expect(fatal(bool(forward->selection_after.active.source_offset == 5u)));
        expect_document_valid(forward->after);
    }

    auto unwrapped = test_edit::document_delete_backward(
        document,
        TextSelection::caret({title_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(unwrapped.has_value()))) << "backspace callout title start";
    if (unwrapped) {
        auto const& quote = unwrapped->after.root.children.front();
        expect(fatal(bool(quote.kind == BlockKind::BlockQuote)));
        expect(fatal(bool(quote.children.size() == 1u)));
        if (quote.children.size() == 1) {
            expect(fatal(bool(quote.children[0].id == title_id)));
            expect(fatal(bool(quote.children[0].inline_content.source == U"title\nbody")));
        }
        expect(fatal(bool(serialize_markdown(unwrapped->after) == "> title\n> body")));
        expect_document_valid(unwrapped->after);
    }
};

"callout_title_tree_position_is_a_document_invariant"_test = [] {
    auto misplaced = parse_document("> [!NOTE] title\n> body");
    auto& callout = misplaced.root.children.front();
    expect(fatal(bool(callout.children.size() == 2u)));
    if (callout.children.size() != 2u) return;
    std::swap(callout.children[0], callout.children[1]);
    const auto misplaced_errors = validate_document(misplaced);
    expect(fatal(bool(!misplaced_errors.empty())));

    auto orphaned = parse_document("paragraph");
    orphaned.root.children.front().kind = BlockKind::CalloutTitle;
    const auto orphaned_errors = validate_document(orphaned);
    expect(fatal(bool(!orphaned_errors.empty())));
};

"callout_boundaries_are_independent_of_ancestor_depth"_test = [] {
    auto nested = parse_document("- > [!NOTE] title\n  > body");
    const BlockNode* callout = nullptr;
    walk_blocks(nested.root, [&](const BlockNode& node) {
        if (!callout && node.kind == BlockKind::Callout) callout = &node;
    });
    expect(fatal(bool(callout != nullptr)));
    if (callout && callout_title_block(*callout) && callout->children.size() > 1) {
        const auto callout_id = callout->id;
        const auto title_id = callout->children.front().id;
        const auto body_id = callout->children[1].id;
        auto joined = test_edit::document_delete_backward(
            nested,
            TextSelection::caret({body_id, 0, TextAffinity::Downstream}));
        expect(fatal(bool(joined.has_value())));
        if (joined) {
            const auto* updated = find_block(joined->after.root, callout_id);
            expect(fatal(bool(updated != nullptr)));
            expect(fatal(bool(updated && updated->kind == BlockKind::Callout)));
            const auto* updated_title = updated ? callout_title_block(*updated) : nullptr;
            expect(fatal(bool(updated_title != nullptr)));
            expect(fatal(bool(updated_title
                && updated_title->inline_content.source == U"titlebody")));
            expect(fatal(bool(updated && updated->children.size() == 1u)));
            expect(fatal(bool(joined->after.root.children.front().kind == BlockKind::List)));
            expect_document_valid(joined->after);
        }

        auto unwrapped = test_edit::document_delete_backward(
            nested,
            TextSelection::caret({title_id, 0, TextAffinity::Downstream}));
        expect(fatal(bool(unwrapped.has_value())));
        if (unwrapped) {
            expect(fatal(bool(unwrapped->after.root.children.front().kind == BlockKind::List)));
            const BlockNode* quote = nullptr;
            walk_blocks(unwrapped->after.root, [&](const BlockNode& node) {
                if (!quote && node.kind == BlockKind::BlockQuote) quote = &node;
            });
            expect(fatal(bool(quote != nullptr)));
            expect(fatal(bool(find_block(unwrapped->after.root, callout_id) != nullptr)));
            expect_document_valid(unwrapped->after);
        }
    }

    auto untitled = parse_document("> [!NOTE]\n> body");
    const auto body_id = first_editable(untitled).id;
    auto remove_callout = test_edit::document_delete_backward(
        untitled,
        TextSelection::caret({body_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(remove_callout.has_value())));
    if (!remove_callout) return;
    expect(fatal(bool(remove_callout->after.root.children.front().kind == BlockKind::BlockQuote)));
    expect(fatal(bool(remove_callout->selection_after.active.container_id == body_id)));

    auto remove_quote = test_edit::document_delete_backward(remove_callout->after, remove_callout->selection_after);
    expect(fatal(bool(remove_quote.has_value())));
    if (!remove_quote) return;
    expect(fatal(bool(remove_quote->after.root.children.front().kind == BlockKind::Paragraph)));
    expect(fatal(bool(remove_quote->after.root.children.front().id == body_id)));
    expect(fatal(bool(remove_quote->after.root.children.front().inline_content.source == U"body")));
    expect_document_valid(remove_quote->after);
};

"backspace_joins_later_text_children_before_unwrapping_containers"_test = [] {
    const std::vector<std::pair<std::string, BlockKind>> cases{
        {"> first", BlockKind::BlockQuote},
        {"> [!NOTE]\n> first", BlockKind::Callout},
        {"[^1]: first", BlockKind::FootnoteDefinition},
    };

    for (const auto& [markdown, container_kind] : cases) {
        auto document = parse_document(markdown);
        auto const first_id = first_editable(document).id;
        auto split = test_edit::document_enter(document, caret(first_editable(document), 5));
        expect(fatal(bool(split.has_value()))) << markdown;
        if (!split) continue;
        const auto second_id = split->selection_after.active.container_id;
        auto inserted = test_edit::document_insert_text(split->after, split->selection_after, U"second");
        expect(fatal(bool(inserted.has_value()))) << markdown;
        if (!inserted) continue;

        auto joined = test_edit::document_delete_backward(
            inserted->after,
            TextSelection::caret({second_id, 0, TextAffinity::Downstream}));
        expect(fatal(bool(joined.has_value()))) << markdown;
        if (!joined) continue;

        const BlockNode* container = nullptr;
        walk_blocks(joined->after.root, [&](const BlockNode& node) {
            if (!container && node.kind == container_kind) container = &node;
        });
        expect(fatal(bool(container != nullptr))) << markdown;
        if (!container) continue;
        expect(fatal(bool(container->children.size() == 1u))) << markdown;
        expect(fatal(bool(container->children.front().id == first_id))) << markdown;
        expect(fatal(bool(container->children.front().inline_content.source == U"firstsecond"))) << markdown;
        expect(fatal(bool(joined->selection_after.active.container_id == first_id))) << markdown;
        expect(fatal(bool(joined->selection_after.active.source_offset == 5u))) << markdown;
        expect_document_valid(joined->after);
    }

    auto list_document = parse_document("- first\n\n  second");
    auto const& list = list_document.root.children.front();
    expect(fatal(bool(list.kind == BlockKind::List)));
    expect(fatal(bool(list.children.size() == 1u)));
    expect(fatal(bool(list.children.front().children.size() == 2u)));
    if (list.kind == BlockKind::List && list.children.size() == 1u
        && list.children.front().children.size() == 2u) {
        const auto first_id = list.children.front().children.front().id;
        const auto second_id = list.children.front().children.back().id;
        auto joined = test_edit::document_delete_backward(
            list_document,
            TextSelection::caret({second_id, 0, TextAffinity::Downstream}));
        expect(fatal(bool(joined.has_value())));
        if (joined) {
            auto const& updated_list = joined->after.root.children.front();
            expect(fatal(bool(updated_list.kind == BlockKind::List)));
            expect(fatal(bool(updated_list.children.front().children.size() == 1u)));
            expect(fatal(bool(updated_list.children.front().children.front().id == first_id)));
            expect(fatal(bool(updated_list.children.front().children.front().inline_content.source == U"firstsecond")));
            expect(fatal(bool(joined->selection_after.active.container_id == first_id)));
            expect(fatal(bool(joined->selection_after.active.source_offset == 5u)));
            expect_document_valid(joined->after);
        }
    }
};

"backspace_removes_blank_paragraph_after_structural_block"_test = [] {
    auto quote_document = parse_document("> quoted\n\n\nnext");
    expect(fatal(bool(quote_document.root.children.size() == 3u)));
    if (quote_document.root.children.size() == 3u) {
        auto const blank_id = quote_document.root.children[1].id;
        auto const& quote_content = quote_document.root.children[0].children.back();
        auto transaction = test_edit::document_delete_backward(quote_document, caret(quote_document.root.children[1], 0));
        expect(fatal(bool(transaction.has_value())));
        if (transaction) {
            expect(fatal(bool(transaction->after.root.children.size() == 2u)));
            expect(fatal(bool(find_block(transaction->after.root, blank_id) == nullptr)));
            expect(fatal(bool(transaction->selection_after.active.container_id == quote_content.id)));
            expect(fatal(bool(transaction->selection_after.active.source_offset == quote_content.inline_content.source.size())));
            expect_document_valid(transaction->after);
        }
    }

    auto code_document = parse_document("```cpp\ncode\n```\n\n\nnext");
    expect(fatal(bool(code_document.root.children.size() == 3u)));
    if (code_document.root.children.size() == 3u) {
        auto const blank_id = code_document.root.children[1].id;
        auto const code_id = code_document.root.children[0].id;
        auto const code_length = code_document.root.children[0].block_source.source().size();
        auto transaction = test_edit::document_delete_backward(code_document, caret(code_document.root.children[1], 0));
        expect(fatal(bool(transaction.has_value())));
        if (transaction) {
            expect(fatal(bool(transaction->after.root.children.size() == 2u)));
            expect(fatal(bool(find_block(transaction->after.root, blank_id) == nullptr)));
            expect(fatal(bool(transaction->selection_after.active.container_id == code_id)));
            expect(fatal(bool(transaction->selection_after.active.source_offset == code_length)));
            expect_document_valid(transaction->after);
        }
    }
};

"delete_removes_blank_paragraph_before_structural_block"_test = [] {
    auto document = parse_document("first\n\n\n> quoted");
    expect(fatal(bool(document.root.children.size() == 3u)));
    if (document.root.children.size() != 3u) return;
    auto const blank_id = document.root.children[1].id;
    auto const quote_content_id = document.root.children[2].children.front().id;
    auto transaction = test_edit::document_delete_forward(document, caret(document.root.children[1], 0));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(transaction->after.root.children.size() == 2u)));
    expect(fatal(bool(find_block(transaction->after.root, blank_id) == nullptr)));
    expect(fatal(bool(transaction->selection_after.active.container_id == quote_content_id)));
    expect(fatal(bool(transaction->selection_after.active.source_offset == 0u)));
    expect_document_valid(transaction->after);
};

"blank_paragraph_navigation_includes_atomic_blocks"_test = [] {
    auto after_image = parse_document("![alt](image.png)\n\n\nnext");
    expect(fatal(bool((after_image.root.children.size()) == (3u))));
    if (after_image.root.children.size() == 3u) {
        auto const image_id = after_image.root.children[0].id;
        auto const blank_id = after_image.root.children[1].id;
        auto transaction = test_edit::document_delete_backward(after_image, caret(after_image.root.children[1], 0));
        expect(fatal(bool(transaction.has_value())));
        if (transaction) {
            expect(fatal(bool(find_block(transaction->after.root, blank_id) == nullptr)));
            expect(fatal(bool(transaction->selection_after.active.container_id == image_id)));
            expect(fatal(bool((transaction->selection_after.active.source_offset) == (1u))));
            expect_document_valid(transaction->after);
        }
    }

    auto before_break = parse_document("first\n\n\n---");
    expect(fatal(bool((before_break.root.children.size()) == (3u))));
    if (before_break.root.children.size() == 3u) {
        auto const blank_id = before_break.root.children[1].id;
        auto const break_id = before_break.root.children[2].id;
        auto transaction = test_edit::document_delete_forward(before_break, caret(before_break.root.children[1], 0));
        expect(fatal(bool(transaction.has_value())));
        if (transaction) {
            expect(fatal(bool(find_block(transaction->after.root, blank_id) == nullptr)));
            expect(fatal(bool(transaction->selection_after.active.container_id == break_id)));
            expect(fatal(bool((transaction->selection_after.active.source_offset) == (0u))));
            expect_document_valid(transaction->after);
        }
    }
};

"deleting_atomic_block_targets_editable_descendant_of_neighbor"_test = [] {
    auto document = parse_document("![alt](image.png)\n> quoted");
    expect(fatal(bool((document.root.children.size()) == (2u))));
    if (document.root.children.size() != 2u) return;
    auto const image_id = document.root.children[0].id;
    auto const quote_content_id = document.root.children[1].children.front().id;
    auto transaction = test_edit::document_delete_forward(document, TextSelection::caret({image_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(find_block(transaction->after.root, image_id) == nullptr)));
    expect(fatal(bool(transaction->selection_after.active.container_id == quote_content_id)));
    expect(fatal(bool((transaction->selection_after.active.source_offset) == (0u))));
    expect_document_valid(transaction->after);
};

}; // suite document_boundary_tests
