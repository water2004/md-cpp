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

suite document_input_tests = [] {

"typed_block_markers_apply_local_tree_rules"_test = [] {
    struct Case {
        std::u32string input;
        BlockKind root_kind;
        BlockKind content_kind;
        std::u32string marker;
        std::uint8_t heading_level = 0;
        bool ordered = false;
    };
    const std::vector<Case> cases{
        {U"## ", BlockKind::Heading, BlockKind::Heading, U"## ", 2, false},
        {U"> ", BlockKind::BlockQuote, BlockKind::Paragraph, U"> ", 0, false},
        {U"- ", BlockKind::List, BlockKind::Paragraph, U"- ", 0, false},
        {U"7) ", BlockKind::List, BlockKind::Paragraph, U"7) ", 0, true},
        {U"- [x] ", BlockKind::TaskList, BlockKind::Paragraph, U"- [x] ", 0, false},
    };
    for (const auto& test : cases) {
        auto document = parse_document("");
        normalize_document(document);
        auto selection = caret(first_editable(document));
        auto [after, result] = type_text(std::move(document), selection, test.input);
        const auto& root = after.root.children.front();
        expect(fatal(bool(root.kind == test.root_kind)));
        const BlockNode* content = &root;
        if (root.kind == BlockKind::BlockQuote) content = &root.children.front();
        if (root.kind == BlockKind::List || root.kind == BlockKind::TaskList) content = &root.children.front().children.front();
        expect(fatal(bool(content->kind == test.content_kind)));
        expect(fatal(bool(content->inline_content.source.empty())));
        expect(fatal(bool(result.active.container_id == content->id)));
        expect(fatal(bool(result.active.source_offset == 0u)));
        if (root.kind == BlockKind::Heading) {
            expect(fatal(bool(root.text_special().level == test.heading_level)));
            expect(fatal(bool(root.text_special().opening_marker == test.marker)));
        } else if (root.kind == BlockKind::List || root.kind == BlockKind::TaskList) {
            expect(fatal(bool(root.children.front().item_special().marker == test.marker)));
            expect(fatal(bool(root.list_special().ordered == test.ordered)));
        }
        expect_document_valid(after);
    }
};

"backspace_at_downstream_block_start_removes_one_structural_prefix"_test = [] {
    const std::vector<std::u32string> markers{U"# ", U"> ", U"- ", U"1. ", U"- [ ] "};
    for (const auto& marker : markers) {
        auto document = parse_document("");
        normalize_document(document);
        auto [structured, at_content_start] = type_text(
            std::move(document), caret(first_editable(document)), marker);
        auto [with_content, after_content] = type_text(
            std::move(structured), at_content_start, U"content");
        auto deletion = test_edit::document_delete_backward(
            with_content,
            TextSelection::caret({
                after_content.active.container_id,
                0,
                TextAffinity::Downstream}));
        expect(fatal(bool(deletion.has_value())));
        if (!deletion) continue;
        expect(fatal(bool(deletion->after.root.children.size() == 1u)));
        expect(fatal(bool(deletion->after.root.children.front().kind == BlockKind::Paragraph)));
        expect(fatal(bool(deletion->after.root.children.front().inline_content.source == U"content")));
        expect(fatal(bool(deletion->selection_after.active.container_id
            == deletion->after.root.children.front().id)));
        expect(fatal(bool(deletion->selection_after.active.source_offset == 0u)));
        expect(fatal(bool(deletion->selection_after.active.affinity == TextAffinity::Downstream)));
        expect_document_valid(deletion->after);
    }
};

"block_start_backspace_semantics_do_not_depend_on_visual_affinity"_test = [] {
    auto document = parse_document("> content");
    const auto owner_id = first_editable(document).id;
    auto deletion = test_edit::document_delete_backward(
        document,
        TextSelection::caret({owner_id, 0, TextAffinity::Upstream}));
    expect(fatal(bool(deletion.has_value())));
    if (!deletion) return;
    expect(fatal(bool(deletion->after.root.children.size() == 1u)));
    expect(fatal(bool(deletion->after.root.children.front().kind == BlockKind::Paragraph)));
    expect(fatal(bool(deletion->after.root.children.front().id == owner_id)));
    expect(fatal(bool(deletion->selection_after.active.affinity == TextAffinity::Downstream)));
    expect_document_valid(deletion->after);
};

"block_start_backspace_peels_nested_prefixes_from_inside_out"_test = [] {
    auto document = parse_document("");
    normalize_document(document);
    auto [quoted, quoted_start] = type_text(
        std::move(document), caret(first_editable(document)), U"> ");
    auto [heading, heading_start] = type_text(
        std::move(quoted), quoted_start, U"# ");
    auto [with_content, after_content] = type_text(
        std::move(heading), heading_start, U"nested");
    const auto content_id = after_content.active.container_id;

    auto remove_heading = test_edit::document_delete_backward(
        with_content,
        TextSelection::caret({content_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(remove_heading.has_value())));
    if (!remove_heading) return;
    expect(fatal(bool(remove_heading->after.root.children.front().kind == BlockKind::BlockQuote)));
    expect(fatal(bool(remove_heading->after.root.children.front().children.front().kind == BlockKind::Paragraph)));
    expect(fatal(bool(remove_heading->after.root.children.front().children.front().inline_content.source == U"nested")));

    auto remove_quote = test_edit::document_delete_backward(remove_heading->after, remove_heading->selection_after);
    expect(fatal(bool(remove_quote.has_value())));
    if (!remove_quote) return;
    expect(fatal(bool(remove_quote->after.root.children.size() == 1u)));
    expect(fatal(bool(remove_quote->after.root.children.front().kind == BlockKind::Paragraph)));
    expect(fatal(bool(remove_quote->after.root.children.front().id == content_id)));
    expect(fatal(bool(remove_quote->after.root.children.front().inline_content.source == U"nested")));
    expect_document_valid(remove_quote->after);
};

"backspace_unlists_only_the_direct_first_block_and_preserves_siblings"_test = [] {
    auto document = parse_document("- one\n- two\n- three");
    std::vector<NodeId> editable;
    walk_blocks(document.root, [&](const BlockNode& node) {
        if (node.kind == BlockKind::Paragraph) editable.push_back(node.id);
    });
    expect(fatal(bool(editable.size() == 3u)));
    if (editable.size() != 3) return;
    auto deletion = test_edit::document_delete_backward(
        document,
        TextSelection::caret({editable[1], 0, TextAffinity::Downstream}));
    expect(fatal(bool(deletion.has_value())));
    if (!deletion) return;
    expect(fatal(bool(deletion->after.root.children.size() == 3u)));
    expect(fatal(bool(deletion->after.root.children[0].kind == BlockKind::List)));
    expect(fatal(bool(deletion->after.root.children[1].kind == BlockKind::Paragraph)));
    expect(fatal(bool(deletion->after.root.children[1].id == editable[1])));
    expect(fatal(bool(deletion->after.root.children[1].inline_content.source == U"two")));
    expect(fatal(bool(deletion->after.root.children[2].kind == BlockKind::List)));
    expect(fatal(bool(deletion->after.root.children[0].children.size() == 1u)));
    expect(fatal(bool(deletion->after.root.children[2].children.size() == 1u)));
    expect_document_valid(deletion->after);
};

"backspace_outdents_a_nested_list_item_by_one_tree_level"_test = [] {
    auto document = parse_document("- outer\n  - inner");
    const BlockNode* inner = nullptr;
    walk_blocks(document.root, [&](const BlockNode& node) {
        if (node.kind == BlockKind::Paragraph && node.inline_content.source == U"inner") inner = &node;
    });
    expect(fatal(bool(inner != nullptr)));
    if (!inner) return;
    const auto inner_id = inner->id;
    auto deletion = test_edit::document_delete_backward(
        document,
        TextSelection::caret({inner_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(deletion.has_value())));
    if (!deletion) return;
    const auto& list = deletion->after.root.children.front();
    expect(fatal(bool(list.kind == BlockKind::List)));
    expect(fatal(bool(list.children.size() == 2u)));
    expect(fatal(bool(list.children[0].children.size() == 1u)));
    expect(fatal(bool(list.children[1].children.front().id == inner_id)));
    expect(fatal(bool(list.children[1].children.front().inline_content.source == U"inner")));
    expect_document_valid(deletion->after);
};

"typed_fence_enters_auto_closed_code_without_document_reparse"_test = [] {
    auto document = parse_document("");
    normalize_document(document);
    auto [opening, at_opening_end] = type_text(std::move(document), caret(first_editable(document)), U"```cpp");
    auto entered = test_edit::document_enter(opening, at_opening_end);
    expect(fatal(bool(entered.has_value())));
    if (!entered) return;
    expect(fatal(bool(entered->after.root.children.front().kind == BlockKind::CodeBlock)));
    auto const& code = entered->after.root.children.front();
    expect(fatal(bool(code.block_source.source() == U"```cpp\n```")));
    expect(fatal(bool(code.block_source.tree().language == std::optional<std::string>{"cpp"})));
    expect(fatal(bool(code.block_source.tree().complete_opening)));
    expect(fatal(bool(code.block_source.tree().complete_closing)));
    expect(fatal(bool(block_source_tokens_partition(code.block_source))));
    expect(fatal(bool(flatten_block_source_tokens(code.block_source) == code.block_source.source())));
    expect(fatal(bool(entered->selection_after.active.container_id == entered->after.root.children.front().id)));
    expect(fatal(bool(entered->selection_after.active.source_offset == 7u)));

    auto second_enter = test_edit::document_enter(entered->after, entered->selection_after);
    expect(fatal(bool(second_enter.has_value())));
    if (!second_enter) return;
    expect(fatal(bool(second_enter->after.root.children.front().block_source.source() == U"```cpp\n\n```")));
    expect(fatal(bool(second_enter->selection_after.active.source_offset == 8u)));
    expect(fatal(bool(serialize_markdown(second_enter->after) == "```cpp\n\n```")));
    expect_document_valid(second_enter->after);
};

"raw_block_source_projection_is_lossless_through_incomplete_character_edits"_test = [] {
    struct Case {
        BlockSourceKind kind;
        std::u32string source;
    };
    const std::vector<Case> cases{
        {BlockSourceKind::FencedCode, U"```cpp"},
        {BlockSourceKind::FencedCode, U"```cpp\n"},
        {BlockSourceKind::FencedCode, U"~~~js\nvalue\n~~"},
        {BlockSourceKind::DollarMath, U"$"},
        {BlockSourceKind::DollarMath, U"$$"},
        {BlockSourceKind::DollarMath, U"$$\n"},
        {BlockSourceKind::BracketMath, U"\\["},
        {BlockSourceKind::IndentedCode, U"    a\n  b\n"},
    };
    for (const auto& test_case : cases) {
        auto original = make_block_source(test_case.source, test_case.kind);
        expect(fatal(bool(block_source_tokens_partition(original))));
        expect(fatal(bool(flatten_block_source_tokens(original) == original.source())));
        for (std::size_t offset = 0; offset <= test_case.source.size(); ++offset) {
            auto inserted = original;
            inserted.source().insert(offset, 1, U'X');
            reparse_block_source(inserted);
            expect(fatal(bool(block_source_tokens_partition(inserted))));
            expect(fatal(bool(flatten_block_source_tokens(inserted) == inserted.source())));
        }
        for (std::size_t offset = 0; offset < test_case.source.size(); ++offset) {
            auto deleted = original;
            deleted.source().erase(offset, 1);
            reparse_block_source(deleted);
            expect(fatal(bool(block_source_tokens_partition(deleted))));
            expect(fatal(bool(flatten_block_source_tokens(deleted) == deleted.source())));
        }
    }
};

"enter_continues_and_exits_lists_by_tree_context"_test = [] {
    auto bullets = parse_document("- one");
    auto bullet_id = bullets.root.children.front().children.front().children.front().id;
    auto continued = test_edit::document_enter(bullets, TextSelection::caret({bullet_id, 3, TextAffinity::Downstream}));
    expect(fatal(bool(continued.has_value())));
    if (continued) {
        const auto& list = continued->after.root.children.front();
        expect(fatal(bool(list.children.size() == 2u)));
        expect(fatal(bool(list.children.back().item_special().marker == U"- ")));
        expect(fatal(bool(list.children.back().children.front().inline_content.source.empty())));
        expect(fatal(bool(continued->selection_after.active.container_id == list.children.back().children.front().id)));
    }

    auto ordered = parse_document("7) one");
    auto ordered_id = ordered.root.children.front().children.front().children.front().id;
    auto next_number = test_edit::document_enter(ordered, TextSelection::caret({ordered_id, 3, TextAffinity::Downstream}));
    expect(fatal(bool(next_number.has_value())));
    if (next_number) expect(fatal(bool(
        next_number->after.root.children.front().children.back().item_special().marker == U"8) ")));

    auto before_ordered = parse_document("7) one");
    auto before_ordered_id = before_ordered.root.children.front().children.front().children.front().id;
    auto inserted_before = test_edit::document_enter(before_ordered, TextSelection::caret({before_ordered_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(inserted_before.has_value())));
    if (inserted_before) {
        const auto& list = inserted_before->after.root.children.front();
        expect(fatal(bool(list.children.front().item_special().marker == U"7) ")));
        expect(fatal(bool(list.children.back().item_special().marker == U"8) ")));
        expect(fatal(bool(list.children.back().children.front().inline_content.source == U"one")));
    }

    auto tasks = parse_document("- [x] done");
    auto task_id = tasks.root.children.front().children.front().children.front().id;
    auto next_task = test_edit::document_enter(tasks, TextSelection::caret({task_id, 4, TextAffinity::Downstream}));
    expect(fatal(bool(next_task.has_value())));
    if (next_task) {
        const auto& item = next_task->after.root.children.front().children.back();
        expect(fatal(bool(item.kind == BlockKind::TaskListItem)));
        expect(fatal(bool(!item.item_special().checked)));
        expect(fatal(bool(item.item_special().marker == U"- [ ] ")));
    }

    auto empty = parse_document("- one\n- ");
    auto empty_id = empty.root.children.front().children.back().children.front().id;
    auto exited = test_edit::document_enter(empty, TextSelection::caret({empty_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(exited.has_value())));
    if (exited) {
        expect(fatal(bool(exited->after.root.children.size() == 2u)));
        expect(fatal(bool(exited->after.root.children.front().kind == BlockKind::List)));
        expect(fatal(bool(exited->after.root.children.front().children.size() == 1u)));
        expect(fatal(bool(exited->after.root.children.back().kind == BlockKind::Paragraph)));
        expect(fatal(bool(exited->selection_after.active.container_id == exited->after.root.children.back().id)));
    }
};

"enter_inside_nested_list_container_respects_nearest_owner"_test = [] {
    auto document = parse_document("- > quoted");
    auto& list = document.root.children.front();
    auto& quote = list.children.front().children.front();
    expect(fatal(bool(quote.kind == BlockKind::BlockQuote)));
    auto paragraph_id = quote.children.front().id;
    auto transaction = test_edit::document_enter(document, TextSelection::caret({paragraph_id, 6, TextAffinity::Downstream}));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    const auto& updated_list = transaction->after.root.children.front();
    expect(fatal(bool(updated_list.children.size() == 1u)));
    const auto& updated_quote = updated_list.children.front().children.front();
    expect(fatal(bool(updated_quote.kind == BlockKind::BlockQuote)));
    expect(fatal(bool(updated_quote.children.size() == 2u)));
    expect_document_valid(transaction->after);
};

"backspace_inside_markers_reparses_one_inline_document"_test = [] {
    auto document = parse_document("**alpha**");
    const auto& paragraph = first_editable(document);
    auto transaction = test_edit::document_delete_backward(document, caret(paragraph, 1));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(first_editable(transaction->after).inline_content.source == U"*alpha**")));
    expect(fatal(bool(transaction->selection_after.active.source_offset == 0u)));
    expect_document_valid(transaction->after);
};

"delete_forward_preserves_unclosed_source_verbatim"_test = [] {
    auto document = parse_document("[title](url)");
    const auto& paragraph = first_editable(document);
    auto transaction = test_edit::document_delete_forward(document, caret(paragraph, 11));
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
        auto wrapped = test_edit::document_toggle_inline_format(document, range(paragraph, 0, 5), test.format);
        expect(fatal(bool(wrapped.has_value())));
        if (!wrapped) continue;
        const auto& inline_document = first_editable(wrapped->after).inline_content;
        expect(fatal(bool(inline_document.source == test.expected)));
        expect(fatal(bool(inline_contains_kind(inline_document, test.kind))));
        auto unwrapped = test_edit::document_toggle_inline_format(wrapped->after, wrapped->selection_after, test.format);
        expect(fatal(bool(unwrapped.has_value())));
        if (unwrapped) expect(fatal(bool(first_editable(unwrapped->after).inline_content.source == U"value")));
    }
};

"link_and_image_commands_are_source_edits"_test = [] {
    auto link_document = parse_document("title");
    const auto& link_paragraph = first_editable(link_document);
    auto link = test_edit::document_insert_link(link_document, range(link_paragraph, 0, 5), "url", "name");
    expect(fatal(bool(link.has_value())));
    if (link) {
        const auto& inline_document = first_editable(link->after).inline_content;
        expect(fatal(bool(inline_document.source == U"[title](url \"name\")")));
        expect(fatal(bool(inline_contains_kind(inline_document, InlineCstKind::Link))));
    }
    auto image_document = parse_document("diagram");
    const auto& image_paragraph = first_editable(image_document);
    auto image = test_edit::document_insert_image(image_document, range(image_paragraph, 0, 7), "image.png", "diagram");
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
    auto transaction = test_edit::document_enter(document, caret(first_editable(document), 5));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(transaction->after.root.children.size() == 2u)));
    expect(fatal(bool(transaction->after.root.children[0].id == original_id)));
    expect(fatal(bool(transaction->after.root.children[0].inline_content.source == U"**alp")));
    expect(fatal(bool(transaction->after.root.children[1].inline_content.source == U"ha**")));
    expect(fatal(bool(transaction->selection_after.active.container_id == transaction->after.root.children[1].id)));
    expect_document_valid(transaction->after);
};

"footnote_commands_resolve_labels_and_record_source_plus_tree_edits"_test = [] {
    auto document = parse_document("body[^1]\n\n[^3]: existing");
    const auto& owner = first_editable(document);
    const auto before = range(owner, 0, 4);
    auto inserted = test_edit::document_insert_footnote(document, before);
    expect(fatal(bool(inserted.has_value())));
    if (!inserted) return;

    expect(fatal(bool(inserted->operations.size() == 2u)));
    expect(fatal(bool(std::holds_alternative<DocumentTextOperation>(inserted->operations[0]))));
    expect(fatal(bool(std::holds_alternative<DocumentTreeEdit>(inserted->operations[1]))));
    expect(fatal(bool(inserted->after.root.children.size() == 3u)));
    expect(fatal(bool(inserted->after.root.children[0].inline_content.source == U"body[^2][^1]")));
    const auto& definition = inserted->after.root.children.back();
    expect(fatal(bool(definition.kind == BlockKind::FootnoteDefinition)));
    expect(fatal(bool(definition.container_special().footnote_label == "2")));
    expect(fatal(bool(definition.children.size() == 1u)));
    expect(fatal(bool(inserted->selection_after.active.container_id == definition.children.front().id)));
    expect(fatal(bool(inserted->selection_after.active.source_offset == 0u)));
    expect(fatal(bool(serialize_markdown(inserted->after) ==
        "body[^2][^1]\n\n[^3]: existing\n\n[^2]: ")));

    const auto symbols = build_document_symbol_index(inserted->after);
    expect(fatal(bool(symbols.footnotes.size() == 2u)));
    expect(fatal(bool(symbols.footnote_references.size() == 2u)));
    const auto resolution = resolve_footnote_reference(
        symbols,
        {owner.id, 6, TextAffinity::Downstream});
    expect(fatal(bool(resolution.has_value())));
    if (resolution) {
        expect(fatal(bool(resolution->label == "2")));
        expect(fatal(bool(resolution->definition.has_value())));
    }
    expect_document_valid(inserted->after);
};

"missing_footnote_definitions_are_created_semantically_without_duplicates"_test = [] {
    auto document = parse_document("body[^missing]");
    const auto& owner = first_editable(document);
    const auto before = caret(owner, owner.inline_content.source.size());
    auto created = test_edit::document_create_footnote_definition(document, before, "missing");
    expect(fatal(bool(created.has_value())));
    if (!created) return;
    expect(fatal(bool(created->operations.size() == 1u)));
    expect(fatal(bool(created->after.root.children.size() == 2u)));
    expect(fatal(bool(created->after.root.children.back().kind == BlockKind::FootnoteDefinition)));
    expect(fatal(bool(created->after.root.children.back().container_special().footnote_label == "missing")));
    const auto symbols = build_document_symbol_index(created->after);
    expect(fatal(bool(footnote_definition_target(
        created->after, symbols, "missing").has_value())));
    const auto reference = first_footnote_reference_target(
        symbols, "missing");
    expect(fatal(bool(reference.has_value())));
    if (reference) {
        expect(fatal(bool(reference->container_id == owner.id)));
        expect(fatal(bool(reference->source_offset == 4u)));
    }
    expect(fatal(bool(!test_edit::document_create_footnote_definition(
        created->after, created->selection_after, "missing").has_value())));
    expect_document_valid(created->after);
};

}; // suite document_input_tests
