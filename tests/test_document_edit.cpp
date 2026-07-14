#include <algorithm>
#include <cstddef>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "elmd_test.hpp"
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.block_tree;
import elmd.core.document;
import elmd.core.document_edit;
import elmd.core.document_symbols;
import elmd.core.document_text;
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
        if (const auto* inline_document = editable_inline_document(node))
            expect_inline_lossless(*inline_document);
    });
}

std::pair<EditorDocument, TextSelection> type_text(
    EditorDocument document,
    TextSelection selection,
    std::u32string_view text) {
    for (const auto value : text) {
        auto transaction = document_insert_text(document, selection, std::u32string(1, value));
        if (!transaction) throw std::runtime_error("typing failed");
        document = std::move(transaction->after);
        selection = transaction->selection_after;
    }
    return {std::move(document), selection};
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
    expect(fatal(bool(inline_document.source == U"a*bc")));
    expect(fatal(bool(transaction->selection_after.active.source_offset == 2u)));
    expect_inline_lossless(inline_document);
};

"source_transactions_publish_reversible_text_operations"_test = [] {
    auto document = parse_document("abc");
    const auto& paragraph = first_editable(document);
    auto transaction = document_insert_text(document, caret(paragraph, 1), U"X");
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(transaction->revision_before == document.revision)));
    expect(fatal(bool(transaction->operations.size() == 1u)));
    const auto* operation = transaction->operations.empty()
        ? nullptr
        : std::get_if<DocumentTextOperation>(&transaction->operations.front());
    expect(fatal(bool(operation != nullptr)));
    if (!operation) return;
    expect(fatal(bool(operation->forward.container_id == paragraph.id)));
    expect(fatal(bool(operation->forward.range == SourceRange{1, 1})));
    expect(fatal(bool(operation->forward.replacement == U"X")));
    expect(fatal(bool(operation->inverse.container_id == paragraph.id)));
    expect(fatal(bool(operation->inverse.range == SourceRange{1, 2})));
    expect(fatal(bool(operation->inverse.replacement.empty())));
};

"structural_transactions_publish_tree_and_source_operations"_test = [] {
    auto document = parse_document("");
    normalize_document(document);
    const auto paragraph_id = first_editable(document).id;
    const auto initial_selection = caret(first_editable(document));
    auto [heading, selection] = type_text(
        std::move(document), initial_selection, U"# ");
    expect(fatal(bool(heading.root.children.front().kind == BlockKind::Heading)));
    expect(fatal(bool(selection.active.container_id == paragraph_id)));

    auto original = parse_document("");
    normalize_document(original);
    auto marker = document_insert_text(original, caret(first_editable(original)), U"# ");
    expect(fatal(bool(marker.has_value())));
    if (!marker) return;
    const auto tree_count = std::ranges::count_if(marker->operations, [](const auto& operation) {
        return std::holds_alternative<DocumentTreeEdit>(operation);
    });
    const auto text_count = std::ranges::count_if(marker->operations, [](const auto& operation) {
        return std::holds_alternative<DocumentTextOperation>(operation);
    });
    expect(fatal(bool(tree_count == 1u)));
    // History records the actual semantic sequence: insert the marker source,
    // consume that source into block structure, then update the block payload.
    // It does not collapse the command into a before/after whole-tree diff.
    expect(fatal(bool(text_count == 2u)));
};

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
            expect(fatal(bool(root.level == test.heading_level)));
            expect(fatal(bool(root.opening_marker == test.marker)));
        } else if (root.kind == BlockKind::List || root.kind == BlockKind::TaskList) {
            expect(fatal(bool(root.children.front().marker == test.marker)));
            expect(fatal(bool(root.list_ordered == test.ordered)));
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
        auto deletion = document_delete_backward(
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
    auto deletion = document_delete_backward(
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

    auto remove_heading = document_delete_backward(
        with_content,
        TextSelection::caret({content_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(remove_heading.has_value())));
    if (!remove_heading) return;
    expect(fatal(bool(remove_heading->after.root.children.front().kind == BlockKind::BlockQuote)));
    expect(fatal(bool(remove_heading->after.root.children.front().children.front().kind == BlockKind::Paragraph)));
    expect(fatal(bool(remove_heading->after.root.children.front().children.front().inline_content.source == U"nested")));

    auto remove_quote = document_delete_backward(remove_heading->after, remove_heading->selection_after);
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
    auto deletion = document_delete_backward(
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
    auto deletion = document_delete_backward(
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
    auto entered = document_enter(opening, at_opening_end);
    expect(fatal(bool(entered.has_value())));
    if (!entered) return;
    expect(fatal(bool(entered->after.root.children.front().kind == BlockKind::CodeBlock)));
    auto const& code = entered->after.root.children.front();
    expect(fatal(bool(code.block_source.source == U"```cpp\n```")));
    expect(fatal(bool(code.block_source.tree.language == std::optional<std::string>{"cpp"})));
    expect(fatal(bool(code.block_source.tree.complete_opening)));
    expect(fatal(bool(code.block_source.tree.complete_closing)));
    expect(fatal(bool(block_source_tokens_partition(code.block_source))));
    expect(fatal(bool(flatten_block_source_tokens(code.block_source) == code.block_source.source)));
    expect(fatal(bool(entered->selection_after.active.container_id == entered->after.root.children.front().id)));
    expect(fatal(bool(entered->selection_after.active.source_offset == 7u)));

    auto second_enter = document_enter(entered->after, entered->selection_after);
    expect(fatal(bool(second_enter.has_value())));
    if (!second_enter) return;
    expect(fatal(bool(second_enter->after.root.children.front().block_source.source == U"```cpp\n\n```")));
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
        expect(fatal(bool(flatten_block_source_tokens(original) == original.source)));
        for (std::size_t offset = 0; offset <= test_case.source.size(); ++offset) {
            auto inserted = original;
            inserted.source.insert(offset, 1, U'X');
            reparse_block_source(inserted);
            expect(fatal(bool(block_source_tokens_partition(inserted))));
            expect(fatal(bool(flatten_block_source_tokens(inserted) == inserted.source)));
        }
        for (std::size_t offset = 0; offset < test_case.source.size(); ++offset) {
            auto deleted = original;
            deleted.source.erase(offset, 1);
            reparse_block_source(deleted);
            expect(fatal(bool(block_source_tokens_partition(deleted))));
            expect(fatal(bool(flatten_block_source_tokens(deleted) == deleted.source)));
        }
    }
};

"enter_continues_and_exits_lists_by_tree_context"_test = [] {
    auto bullets = parse_document("- one");
    auto bullet_id = bullets.root.children.front().children.front().children.front().id;
    auto continued = document_enter(bullets, TextSelection::caret({bullet_id, 3, TextAffinity::Downstream}));
    expect(fatal(bool(continued.has_value())));
    if (continued) {
        const auto& list = continued->after.root.children.front();
        expect(fatal(bool(list.children.size() == 2u)));
        expect(fatal(bool(list.children.back().marker == U"- ")));
        expect(fatal(bool(list.children.back().children.front().inline_content.source.empty())));
        expect(fatal(bool(continued->selection_after.active.container_id == list.children.back().children.front().id)));
    }

    auto ordered = parse_document("7) one");
    auto ordered_id = ordered.root.children.front().children.front().children.front().id;
    auto next_number = document_enter(ordered, TextSelection::caret({ordered_id, 3, TextAffinity::Downstream}));
    expect(fatal(bool(next_number.has_value())));
    if (next_number) expect(fatal(bool(next_number->after.root.children.front().children.back().marker == U"8) ")));

    auto before_ordered = parse_document("7) one");
    auto before_ordered_id = before_ordered.root.children.front().children.front().children.front().id;
    auto inserted_before = document_enter(before_ordered, TextSelection::caret({before_ordered_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(inserted_before.has_value())));
    if (inserted_before) {
        const auto& list = inserted_before->after.root.children.front();
        expect(fatal(bool(list.children.front().marker == U"7) ")));
        expect(fatal(bool(list.children.back().marker == U"8) ")));
        expect(fatal(bool(list.children.back().children.front().inline_content.source == U"one")));
    }

    auto tasks = parse_document("- [x] done");
    auto task_id = tasks.root.children.front().children.front().children.front().id;
    auto next_task = document_enter(tasks, TextSelection::caret({task_id, 4, TextAffinity::Downstream}));
    expect(fatal(bool(next_task.has_value())));
    if (next_task) {
        const auto& item = next_task->after.root.children.front().children.back();
        expect(fatal(bool(item.kind == BlockKind::TaskListItem)));
        expect(fatal(bool(!item.checked)));
        expect(fatal(bool(item.marker == U"- [ ] ")));
    }

    auto empty = parse_document("- one\n- ");
    auto empty_id = empty.root.children.front().children.back().children.front().id;
    auto exited = document_enter(empty, TextSelection::caret({empty_id, 0, TextAffinity::Downstream}));
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
    auto transaction = document_enter(document, TextSelection::caret({paragraph_id, 6, TextAffinity::Downstream}));
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

"footnote_commands_resolve_labels_and_record_source_plus_tree_edits"_test = [] {
    auto document = parse_document("body[^1]\n\n[^3]: existing");
    const auto& owner = first_editable(document);
    const auto before = range(owner, 0, 4);
    auto inserted = document_insert_footnote(document, before);
    expect(fatal(bool(inserted.has_value())));
    if (!inserted) return;

    expect(fatal(bool(inserted->operations.size() == 2u)));
    expect(fatal(bool(std::holds_alternative<DocumentTextOperation>(inserted->operations[0]))));
    expect(fatal(bool(std::holds_alternative<DocumentTreeEdit>(inserted->operations[1]))));
    expect(fatal(bool(inserted->after.root.children.size() == 3u)));
    expect(fatal(bool(inserted->after.root.children[0].inline_content.source == U"body[^2][^1]")));
    const auto& definition = inserted->after.root.children.back();
    expect(fatal(bool(definition.kind == BlockKind::FootnoteDefinition)));
    expect(fatal(bool(definition.footnote_label == "2")));
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
    auto created = document_create_footnote_definition(document, before, "missing");
    expect(fatal(bool(created.has_value())));
    if (!created) return;
    expect(fatal(bool(created->operations.size() == 1u)));
    expect(fatal(bool(created->after.root.children.size() == 2u)));
    expect(fatal(bool(created->after.root.children.back().kind == BlockKind::FootnoteDefinition)));
    expect(fatal(bool(created->after.root.children.back().footnote_label == "missing")));
    expect(fatal(bool(footnote_definition_target(created->after, "missing").has_value())));
    const auto reference = first_footnote_reference_target(created->after, "missing");
    expect(fatal(bool(reference.has_value())));
    if (reference) {
        expect(fatal(bool(reference->container_id == owner.id)));
        expect(fatal(bool(reference->source_offset == 4u)));
    }
    expect(fatal(bool(!document_create_footnote_definition(
        created->after, created->selection_after, "missing").has_value())));
    expect_document_valid(created->after);
};

"enter_edits_code_math_and_table_cell_sources"_test = [] {
    auto fenced = parse_document("```cpp\none\ntwo\n```");
    auto const code_offset = block_source_offset_for_content(fenced.root.children.front().block_source, 3);
    auto code = document_enter(fenced, TextSelection::caret({fenced.root.children.front().id, code_offset, TextAffinity::Downstream}));
    expect(fatal(bool(code.has_value())));
    if (code) {
        expect(fatal(bool(block_source_content(code->after.root.children.front().block_source) == U"one\n\ntwo\n")));
        expect(fatal(bool(code->selection_after.active.source_offset == code_offset + 1)));
    }

    auto math = parse_document("$$\na+b\n$$");
    auto const math_offset = block_source_offset_for_content(math.root.children.front().block_source, 1);
    auto math_break = document_enter(math, TextSelection::caret({math.root.children.front().id, math_offset, TextAffinity::Downstream}));
    expect(fatal(bool(math_break.has_value())));
    if (math_break) expect(fatal(bool(block_source_content(math_break->after.root.children.front().block_source) == U"a\n+b\n")));

    auto table = parse_document("| ab |\n| --- |");
    auto& cell = table.root.children.front().children.front().children.front();
    auto cell_break = document_enter(table, caret(cell, 1));
    expect(fatal(bool(cell_break.has_value())));
    if (cell_break) {
        auto const& edited = cell_break->after.root.children.front().children.front().children.front().inline_content;
        expect(fatal(bool(edited.source == U"a<br>b")));
        expect(fatal(bool(inline_contains_kind(edited, InlineCstKind::HardBreak))));
        expect(fatal(bool(serialize_markdown(cell_break->after).find("a<br>b") != std::string::npos)));

        auto second_break = document_enter(cell_break->after, cell_break->selection_after);
        expect(fatal(bool(second_break.has_value())));
        if (second_break) {
            auto const& with_empty_line = second_break->after.root.children.front().children.front().children.front().inline_content;
            expect(fatal(bool(with_empty_line.source == U"a<br><br>b")));
            auto removed = document_delete_backward(second_break->after, second_break->selection_after);
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
    auto transaction = document_enter(document, TextSelection::caret({code_id, 8, TextAffinity::Downstream}));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(transaction->after.root.children.size() == 3u)));
    expect(fatal(bool(transaction->after.root.children[0].kind == BlockKind::CodeBlock)));
    expect(fatal(bool(transaction->after.root.children[0].block_source.source == U"    one")));
    expect(fatal(bool(transaction->after.root.children[1].kind == BlockKind::Paragraph)));
    expect(fatal(bool(transaction->after.root.children[2].kind == BlockKind::CodeBlock)));
    expect(fatal(bool(transaction->after.root.children[2].block_source.source == U"    two")));
    expect(fatal(bool(transaction->selection_after.active.container_id == transaction->after.root.children[1].id)));
    expect(fatal(bool(serialize_markdown(transaction->after).find("    one\n    \n") == std::string::npos)));
};

"enter_on_empty_quote_line_exits_the_quote"_test = [] {
    auto document = parse_document("> one\n>");
    auto const& quote = document.root.children.front();
    expect(fatal(bool(quote.kind == BlockKind::BlockQuote)));
    expect(fatal(bool(quote.children.size() == 2u)));
    auto const empty_id = quote.children.back().id;
    auto transaction = document_enter(document, TextSelection::caret({empty_id, 0, TextAffinity::Downstream}));
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

    auto transaction = document_enter(document, TextSelection::caret({empty_id, 0, TextAffinity::Downstream}));
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

"callout_title_boundaries_use_tree_split_join_and_unwrap"_test = [] {
    auto document = parse_document("> [!NOTE] title\n> body");
    const auto callout_id = document.root.children.front().id;

    auto entered_at_start = document_enter(
        document,
        TextSelection::caret({callout_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(entered_at_start.has_value()))) << "enter at callout title start";
    if (entered_at_start) {
        auto const& callout = entered_at_start->after.root.children.front();
        expect(fatal(bool(!callout.callout_title.has_value())));
        expect(fatal(bool(callout.children.size() == 2u)));
        if (callout.children.size() == 2u) {
            expect(fatal(bool(callout.children[0].inline_content.source == U"title")));
            expect(fatal(bool(callout.children[1].inline_content.source == U"body")));
            expect(fatal(bool(entered_at_start->selection_after.active.container_id == callout.children[0].id)));
            expect(fatal(bool(entered_at_start->selection_after.active.source_offset == 0u)));
        }
        expect_document_valid(entered_at_start->after);
    }

    auto entered = document_enter(
        document,
        TextSelection::caret({callout_id, 2, TextAffinity::Downstream}));
    expect(fatal(bool(entered.has_value()))) << "enter callout title";
    if (entered) {
        auto const& callout = entered->after.root.children.front();
        expect(fatal(bool(callout.kind == BlockKind::Callout)));
        expect(fatal(bool(callout.callout_title.has_value())));
        expect(fatal(bool(callout.callout_title && callout.callout_title->source == U"ti")));
        expect(fatal(bool(callout.children.size() == 2u)));
        if (callout.children.size() == 2) {
            expect(fatal(bool(callout.children[0].inline_content.source == U"tle")));
            expect(fatal(bool(callout.children[1].inline_content.source == U"body")));
            expect(fatal(bool(entered->selection_after.active.container_id == callout.children[0].id)));
        }
        auto serialized = serialize_markdown(entered->after);
        expect(fatal(bool(serialized == "> [!NOTE] ti\n> tle\n>\n> body"))) << serialized;
        expect_document_valid(entered->after);
    }

    const auto body_id = document.root.children.front().children.front().id;
    auto backward = document_delete_backward(
        document,
        TextSelection::caret({body_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(backward.has_value()))) << "backspace first callout body";
    if (backward) {
        auto const& callout = backward->after.root.children.front();
        expect(fatal(bool(callout.callout_title.has_value())));
        expect(fatal(bool(callout.callout_title && callout.callout_title->source == U"titlebody")));
        expect(fatal(bool(callout.children.empty())));
        expect(fatal(bool(backward->selection_after.active.container_id == callout_id)));
        expect(fatal(bool(backward->selection_after.active.source_offset == 5u)));
        expect_document_valid(backward->after);
    }

    auto forward = document_delete_forward(
        document,
        TextSelection::caret({callout_id, 5, TextAffinity::Upstream}));
    expect(fatal(bool(forward.has_value()))) << "delete after callout title";
    if (forward) {
        auto const& callout = forward->after.root.children.front();
        expect(fatal(bool(callout.callout_title.has_value())));
        expect(fatal(bool(callout.callout_title && callout.callout_title->source == U"titlebody")));
        expect(fatal(bool(callout.children.empty())));
        expect(fatal(bool(forward->selection_after.active.container_id == callout_id)));
        expect(fatal(bool(forward->selection_after.active.source_offset == 5u)));
        expect_document_valid(forward->after);
    }

    auto unwrapped = document_delete_backward(
        document,
        TextSelection::caret({callout_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(unwrapped.has_value()))) << "backspace callout title start";
    if (unwrapped) {
        auto const& quote = unwrapped->after.root.children.front();
        expect(fatal(bool(quote.kind == BlockKind::BlockQuote)));
        expect(fatal(bool(quote.children.size() == 1u)));
        if (quote.children.size() == 1) {
            expect(fatal(bool(quote.children[0].id == callout_id)));
            expect(fatal(bool(quote.children[0].inline_content.source == U"title\nbody")));
        }
        expect(fatal(bool(serialize_markdown(unwrapped->after) == "> title\n> body")));
        expect_document_valid(unwrapped->after);
    }
};

"callout_boundaries_are_independent_of_ancestor_depth"_test = [] {
    auto nested = parse_document("- > [!NOTE] title\n  > body");
    const BlockNode* callout = nullptr;
    walk_blocks(nested.root, [&](const BlockNode& node) {
        if (!callout && node.kind == BlockKind::Callout) callout = &node;
    });
    expect(fatal(bool(callout != nullptr)));
    if (callout && callout->callout_title && !callout->children.empty()) {
        const auto callout_id = callout->id;
        const auto body_id = callout->children.front().id;
        auto joined = document_delete_backward(
            nested,
            TextSelection::caret({body_id, 0, TextAffinity::Downstream}));
        expect(fatal(bool(joined.has_value())));
        if (joined) {
            const auto* updated = find_block(joined->after.root, callout_id);
            expect(fatal(bool(updated != nullptr)));
            expect(fatal(bool(updated && updated->kind == BlockKind::Callout)));
            expect(fatal(bool(updated && updated->callout_title.has_value())));
            expect(fatal(bool(updated && updated->callout_title
                && updated->callout_title->source == U"titlebody")));
            expect(fatal(bool(updated && updated->children.empty())));
            expect(fatal(bool(joined->after.root.children.front().kind == BlockKind::List)));
            expect_document_valid(joined->after);
        }

        auto unwrapped = document_delete_backward(
            nested,
            TextSelection::caret({callout_id, 0, TextAffinity::Downstream}));
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
    auto remove_callout = document_delete_backward(
        untitled,
        TextSelection::caret({body_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(remove_callout.has_value())));
    if (!remove_callout) return;
    expect(fatal(bool(remove_callout->after.root.children.front().kind == BlockKind::BlockQuote)));
    expect(fatal(bool(remove_callout->selection_after.active.container_id == body_id)));

    auto remove_quote = document_delete_backward(remove_callout->after, remove_callout->selection_after);
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
        auto split = document_enter(document, caret(first_editable(document), 5));
        expect(fatal(bool(split.has_value()))) << markdown;
        if (!split) continue;
        const auto second_id = split->selection_after.active.container_id;
        auto inserted = document_insert_text(split->after, split->selection_after, U"second");
        expect(fatal(bool(inserted.has_value()))) << markdown;
        if (!inserted) continue;

        auto joined = document_delete_backward(
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
        auto joined = document_delete_backward(
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
        auto transaction = document_delete_backward(quote_document, caret(quote_document.root.children[1], 0));
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
        auto const code_length = code_document.root.children[0].block_source.source.size();
        auto transaction = document_delete_backward(code_document, caret(code_document.root.children[1], 0));
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
    auto transaction = document_delete_forward(document, caret(document.root.children[1], 0));
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
        auto transaction = document_delete_backward(after_image, caret(after_image.root.children[1], 0));
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
        auto transaction = document_delete_forward(before_break, caret(before_break.root.children[1], 0));
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
    auto document = parse_document("![alt](image.png)\n\n> quoted");
    expect(fatal(bool((document.root.children.size()) == (2u))));
    if (document.root.children.size() != 2u) return;
    auto const image_id = document.root.children[0].id;
    auto const quote_content_id = document.root.children[1].children.front().id;
    auto transaction = document_delete_forward(document, TextSelection::caret({image_id, 0, TextAffinity::Downstream}));
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(find_block(transaction->after.root, image_id) == nullptr)));
    expect(fatal(bool(transaction->selection_after.active.container_id == quote_content_id)));
    expect(fatal(bool((transaction->selection_after.active.source_offset) == (0u))));
    expect_document_valid(transaction->after);
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

"paste_normalizes_newlines_and_parses_clipboard_markdown"_test = [] {
    auto document = parse_document("ab");
    auto transaction = document_paste_text(document, caret(first_editable(document), 1), U"X\r\nY");
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(transaction->after.root.children.size() == 1u)));
    expect(fatal(bool(transaction->after.root.children[0].inline_content.source == U"aX\nYb")));
    expect_document_valid(transaction->after);
};

"paste_semantically_merges_fragment_head_and_anchor_tail"_test = [] {
    auto document = parse_document("ab");
    auto transaction = document_paste_text(
        document,
        caret(first_editable(document), 1),
        U"X\n\nY");
    expect(fatal(bool(transaction.has_value())));
    if (!transaction) return;
    expect(fatal(bool(transaction->after.root.children.size() == 2u)));
    expect(fatal(bool(transaction->after.root.children[0].inline_content.source == U"aX")));
    expect(fatal(bool(transaction->after.root.children[1].inline_content.source == U"Yb")));
    expect(fatal(bool(transaction->selection_after.active.container_id
        == transaction->after.root.children[1].id)));
    expect(fatal(bool(transaction->selection_after.active.source_offset == 1u)));
    expect_document_valid(transaction->after);
};

"paste_uses_literal_semantics_for_raw_blocks_and_table_cells"_test = [] {
    auto code = parse_document("```cpp\nabc\n```");
    auto code_paste = document_paste_text(
        code,
        caret(code.root.children.front(), 7),
        U"# h\r\n- item");
    expect(fatal(bool(code_paste.has_value())));
    if (code_paste) {
        expect(fatal(bool(code_paste->after.root.children.size() == 1u)));
        expect(fatal(bool(code_paste->after.root.children.front().kind == BlockKind::CodeBlock)));
        expect(fatal(bool(code_paste->after.root.children.front().block_source.source
            == U"```cpp\n# h\n- itemabc\n```")));
        expect_document_valid(code_paste->after);
    }

    auto table = parse_document("| a | b |\n| --- | --- |\n| c | d |");
    auto* first_cell = &table.root.children.front().children.front().children.front();
    auto cell_paste = document_paste_text(
        table,
        TextSelection{
            {first_cell->id, 0, TextAffinity::Downstream},
            {first_cell->id, first_cell->inline_content.source.size(), TextAffinity::Upstream}},
        U"  x\r\ny  ");
    expect(fatal(bool(cell_paste.has_value())));
    if (cell_paste) {
        const auto& cell = cell_paste->after.root.children.front().children.front().children.front();
        expect(fatal(bool(cell.inline_content.source == U"x<br>y")));
        expect_document_valid(cell_paste->after);
    }
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
