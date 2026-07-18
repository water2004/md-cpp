#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "support/folia_test.hpp"
import folia.core.parser;
import folia.core.document_edit;
import folia.core.document;
import folia.core.document_symbols;
import folia.core.inline_parser;
import folia.core.render_builder;
import folia.core.render_model;
import folia.core.ast;
import folia.core.block_source;
import folia.core.block_tree;
import folia.core.callout;
import folia.core.text_measurer;
import folia.core.block_layout;
import folia.core.hit_test;
import folia.core.selection_geometry;
import folia.core.selection;

using namespace folia;
using namespace boost::ut;

#include "support/render_layout_test_support.hpp"

suite render_recursive_flow_tests = [] {

"unified_flow_composes_list_quote_list_and_code"_test = [] {
    std::uint64_t next_id = 1;
    auto outer_text = make_render_text_block(BlockKind::Paragraph, U"outer", next_id);
    auto quoted_text = make_render_text_block(BlockKind::Paragraph, U"quoted", next_id);
    auto inner_text = make_render_text_block(BlockKind::Paragraph, U"inner", next_id);
    auto code = make_render_block(BlockKind::CodeBlock, next_id);
    code.block_source = make_block_source(U"    int x;\n", BlockSourceKind::IndentedCode);
    code.ensure_atomic_special().code_indented = true;
    auto code_id = code.id;

    auto inner_item = make_render_block(BlockKind::ListItem, next_id);
    inner_item.ensure_item_special().marker = U"1. ";
    inner_item.children.push_back(std::move(inner_text));
    inner_item.children.push_back(std::move(code));
    auto inner_list = make_render_block(BlockKind::List, next_id);
    inner_list.ensure_list_special().ordered = true;
    inner_list.children.push_back(std::move(inner_item));
    auto quote = make_render_block(BlockKind::BlockQuote, next_id);
    quote.children.push_back(std::move(quoted_text));
    quote.children.push_back(std::move(inner_list));
    auto quote_id = quote.id;
    auto outer_item = make_render_block(BlockKind::ListItem, next_id);
    outer_item.ensure_item_special().marker = U"- ";
    outer_item.children.push_back(std::move(outer_text));
    outer_item.children.push_back(std::move(quote));
    auto list = make_render_block(BlockKind::List, next_id);
    list.children.push_back(std::move(outer_item));

    auto model = build_model(std::move(list), next_id);
    expect(fatal(bool((model.blocks.size()) == (1u))));
    if (model.blocks.empty()) return;
    auto const* rendered_quote = find_render_block(model.blocks[0], quote_id);
    auto const* rendered_code = find_render_block(model.blocks[0], code_id);
    expect(fatal(bool(rendered_quote != nullptr)));
    expect(fatal(bool(rendered_code != nullptr)));
    if (!rendered_quote || !rendered_code) return;
    expect(fatal(bool(rendered_quote->kind == RenderBlockKind::Quote)));
    expect(fatal(bool(flow_indent_for(model.blocks[0], rendered_quote->id) == 2u)));
    expect(fatal(bool(flow_indent_for(model.blocks[0], rendered_code->id) == 7u)));

    std::u32string flattened_code;
    std::size_t code_indents = 0;
    for (auto const& item : model.blocks[0].inline_items) {
        if (item.source_span.container_id != code_id) continue;
        if (item.kind == InlineRenderItem::Kind::Text && item.style.code) flattened_code += item.text;
        if (item.special().marker_role == MarkerRole::Structural
            && !item.special().display_text.empty()) {
            ++code_indents;
            expect(fatal(bool((item.special().display_text.size()) == (9u))));
        }
    }
    expect(fatal(bool(flattened_code == U"int x;")));
    expect(fatal(bool((code_indents) == (1u))));
};

"nested_code_flow_omits_only_the_terminal_physical_ending"_test = [] {
    struct Case {
        std::u32string source;
        std::u32string displayed_code;
        std::size_t displayed_lines;
    };
    const std::vector<Case> cases{
        {U"```cpp\nvalue\n```", U"value", 1u},
        {U"```cpp\r\nvalue\r\n```", U"value", 1u},
        {U"```cpp\rvalue\r```", U"value", 1u},
        {U"```cpp\nvalue\n\n```", U"value\n", 2u},
        {U"```cpp\r\nvalue\r\n\r\n```", U"value\r\n", 2u},
    };
    for (const auto& item : cases) {
        std::uint64_t next_id = 1;
        auto code = make_render_block(BlockKind::CodeBlock, next_id);
        code.block_source = make_block_source(item.source, BlockSourceKind::FencedCode);
        auto code_id = code.id;
        auto quote = make_render_block(BlockKind::BlockQuote, next_id);
        quote.children.push_back(std::move(code));

        auto model = build_model(std::move(quote), next_id);
        expect(fatal(bool(model.blocks.size() == 1u)));
        if (model.blocks.empty()) continue;
        std::u32string displayed_code;
        std::size_t code_indents = 0;
        for (const auto& inline_item : model.blocks.front().inline_items) {
            if (inline_item.source_span.container_id != code_id) continue;
            if (inline_item.kind == InlineRenderItem::Kind::Text && inline_item.style.code)
                displayed_code += inline_item.text;
            if (inline_item.special().marker_role == MarkerRole::Structural
                && !inline_item.special().display_text.empty()) {
                ++code_indents;
            }
        }
        expect(fatal(bool(displayed_code == item.displayed_code)));
        expect(fatal(bool(code_indents == item.displayed_lines)));
    }
};

"unified_flow_composes_task_callout_footnote_quote_code_and_blank"_test = [] {
    std::uint64_t next_id = 1;
    auto task_text = make_render_text_block(BlockKind::Paragraph, U"task", next_id);
    auto code = make_render_block(BlockKind::CodeBlock, next_id);
    code.block_source = make_block_source(U"```cpp\nreturn 0;\n```", BlockSourceKind::FencedCode);
    auto code_id = code.id;
    auto blank = make_render_text_block(BlockKind::Paragraph, U"", next_id);
    auto blank_id = blank.id;

    auto quote = make_render_block(BlockKind::BlockQuote, next_id);
    quote.children.push_back(std::move(code));
    quote.children.push_back(std::move(blank));
    auto quote_id = quote.id;
    auto footnote = make_render_block(BlockKind::FootnoteDefinition, next_id);
    footnote.ensure_container_special().footnote_label = "n";
    footnote.children.push_back(std::move(quote));
    auto footnote_id = footnote.id;
    auto callout = make_render_block(BlockKind::Callout, next_id);
    callout.ensure_container_special().callout_kind = "NOTE";
    callout.children.push_back(std::move(footnote));
    auto callout_id = callout.id;
    auto task_item = make_render_block(BlockKind::TaskListItem, next_id);
    task_item.ensure_item_special().marker = U"- [ ] ";
    task_item.children.push_back(std::move(task_text));
    task_item.children.push_back(std::move(callout));
    auto tasks = make_render_block(BlockKind::TaskList, next_id);
    tasks.children.push_back(std::move(task_item));

    auto model = build_model(std::move(tasks), next_id);
    expect(fatal(bool((model.blocks.size()) == (1u))));
    if (model.blocks.empty()) return;
    auto const* rendered_callout = find_render_block(model.blocks[0], callout_id);
    auto const* rendered_footnote = find_render_block(model.blocks[0], footnote_id);
    auto const* rendered_quote = find_render_block(model.blocks[0], quote_id);
    auto const* rendered_code = find_render_block(model.blocks[0], code_id);
    auto const* rendered_blank = find_render_block(model.blocks[0], blank_id);
    expect(fatal(bool(rendered_callout && rendered_footnote && rendered_quote && rendered_code && rendered_blank)));
    if (!rendered_callout || !rendered_footnote || !rendered_quote || !rendered_code || !rendered_blank) return;
    expect(fatal(bool(flow_indent_for(model.blocks[0], rendered_callout->id) == 6u)));
    expect(fatal(bool(flow_indent_for(model.blocks[0], rendered_footnote->id) == 8u)));
    expect(fatal(bool(flow_indent_for(model.blocks[0], rendered_quote->id) == 11u)));
    expect(fatal(bool(flow_indent_for(model.blocks[0], rendered_code->id) == 13u)));
    expect(fatal(bool(flow_indent_for(model.blocks[0], rendered_blank->id) == 13u)));

    auto blank_indent = std::find_if(model.blocks[0].inline_items.begin(), model.blocks[0].inline_items.end(), [&](auto const& item) {
        return item.source_span.container_id == blank_id
            && item.special().marker_role == MarkerRole::Structural
            && item.special().display_text == std::u32string(13, U' ');
    });
    expect(fatal(bool(blank_indent != model.blocks[0].inline_items.end())));
};

"callout_markers_share_three_visual_categories_and_render_a_generated_label"_test = [] {
    struct Case {
        std::string marker;
        CalloutVisualKind visual;
        std::u32string label;
    };
    for (const auto& test : std::vector<Case>{
             {"NOTE", CalloutVisualKind::Note, U"Note"},
             {"IMPORTANT", CalloutVisualKind::Note, U"Important"},
             {"TIP", CalloutVisualKind::Tip, U"Tip"},
             {"WARNING", CalloutVisualKind::Warning, U"Warning"},
             {"CAUTION", CalloutVisualKind::Warning, U"Caution"},
         }) {
        expect(fatal(bool(callout_visual_kind(test.marker) == test.visual))) << test.marker;
        expect(fatal(bool(callout_display_label(test.marker) == test.label))) << test.marker;
        auto model = build_model("> [!" + test.marker + "]\n> body");
        expect(fatal(bool(model.blocks.size() == 1u))) << test.marker;
        if (model.blocks.empty()) continue;
        expect(fatal(bool(model.blocks.front().kind == RenderBlockKind::Callout))) << test.marker;
        expect(fatal(bool(model.blocks.front().special().callout_kind == test.marker))) << test.marker;
        const auto label = std::ranges::find_if(model.blocks.front().inline_items, [&](auto const& item) {
            return item.kind == InlineRenderItem::Kind::Marker
                && item.source_span.source_range.empty()
                && item.special().display_text == test.label;
        });
        expect(fatal(bool(label != model.blocks.front().inline_items.end()))) << test.marker;
        if (label != model.blocks.front().inline_items.end()) expect(fatal(bool(label->style.bold)));
    }
};

"unified_flow_accumulates_arbitrary_container_depth"_test = [] {
    std::uint64_t next_id = 1;
    auto leaf = make_render_text_block(BlockKind::Paragraph, U"", next_id);
    auto leaf_id = leaf.id;
    BlockNode chain = std::move(leaf);
    constexpr std::size_t depth = 12;
    for (std::size_t index = 0; index < depth; ++index) {
        auto kind = index % 3 == 0 ? BlockKind::BlockQuote
            : index % 3 == 1 ? BlockKind::Callout
            : BlockKind::FootnoteDefinition;
        auto parent = make_render_block(kind, next_id);
        parent.children.push_back(std::move(chain));
        chain = std::move(parent);
    }
    auto model = build_model(std::move(chain), next_id);
    expect(fatal(bool((model.blocks.size()) == (1u))));
    if (model.blocks.empty()) return;

    auto const* cursor = &model.blocks[0];
    std::size_t accumulated_indent = 0;
    for (std::size_t level = 0; level < depth; ++level) {
        accumulated_indent += cursor->flow_local_indent_columns;
        expect(fatal(bool((accumulated_indent) == (level * 2u))));
        expect(fatal(bool((cursor->child_blocks.size()) == (1u))));
        if (cursor->child_blocks.empty()) return;
        cursor = &cursor->child_blocks[0];
    }
    expect(fatal(bool(cursor->id == leaf_id)));
    accumulated_indent += cursor->flow_local_indent_columns;
    expect(fatal(bool((accumulated_indent) == (depth * 2u))));
    auto leaf_indent = std::find_if(model.blocks[0].inline_items.begin(), model.blocks[0].inline_items.end(), [&](auto const& item) {
        return item.source_span.container_id == leaf_id
            && item.special().display_text == std::u32string(depth * 2u, U' ');
    });
    expect(fatal(bool(leaf_indent != model.blocks[0].inline_items.end())));
};

"recursive_flow_anchors_each_nested_container_to_its_own_subtree"_test = [] {
    auto model = build_model("> outer\n> > middle\n> > > inner\n");
    expect(fatal(bool(model.blocks.size() == 1u)));
    if (model.blocks.empty()) return;

    std::vector<const RenderBlock*> quotes;
    auto collect = [&](auto& self, const RenderBlock& block) -> void {
        if (block.kind == RenderBlockKind::Quote) quotes.push_back(&block);
        for (auto const& child : block.child_blocks) self(self, child);
    };
    collect(collect, model.blocks.front());
    expect(fatal(bool(quotes.size() == 3u)));
    if (quotes.size() != 3u) return;

    for (std::size_t index = 0; index < quotes.size(); ++index) {
        auto const* leaf = first_render_leaf(*quotes[index]);
        expect(fatal(bool(leaf != nullptr)));
        if (!leaf) return;
        expect(fatal(bool(quotes[index]->flow_anchor_owner_id == leaf->source_span.container_id)));
        expect(fatal(bool(flow_indent_for(model.blocks.front(), quotes[index]->id) == index * 2u)));
    }
    expect(fatal(bool(quotes[0]->flow_anchor_owner_id != quotes[1]->flow_anchor_owner_id)));
    expect(fatal(bool(quotes[1]->flow_anchor_owner_id != quotes[2]->flow_anchor_owner_id)));
};

"recursive_flow_anchors_each_list_item_and_nested_container_independently"_test = [] {
    auto model = build_model("- first\n- second\n  > quoted\n- third\n");
    expect(fatal(bool(model.blocks.size() == 1u)));
    if (model.blocks.empty()) return;
    auto const& list = model.blocks.front();
    expect(fatal(bool(list.child_blocks.size() == 3u)));
    if (list.child_blocks.size() != 3u) return;

    std::vector<NodeId> item_anchors;
    for (auto const& item : list.child_blocks) {
        auto const* leaf = first_render_leaf(item);
        expect(fatal(bool(leaf != nullptr)));
        if (!leaf) return;
        expect(fatal(bool(item.flow_anchor_owner_id == leaf->source_span.container_id)));
        item_anchors.push_back(item.flow_anchor_owner_id);
    }
    expect(fatal(bool(item_anchors[0] != item_anchors[1])));
    expect(fatal(bool(item_anchors[1] != item_anchors[2])));

    auto const* quote = find_render_block(list, RenderBlockKind::Quote);
    expect(fatal(bool(quote != nullptr)));
    if (!quote) return;
    auto const* quoted_leaf = first_render_leaf(*quote);
    expect(fatal(bool(quoted_leaf != nullptr)));
    if (!quoted_leaf) return;
    expect(fatal(bool(quote->flow_anchor_owner_id == quoted_leaf->source_span.container_id)));
    expect(fatal(bool(quote->flow_anchor_owner_id != item_anchors[0])));
    expect(fatal(bool(flow_indent_for(list, quote->id) == 2u)));
};

"nested_quote_flow_tree_owns_its_visible_content"_test = [] {
    auto m = build_model("> > The Witch bade her clean the pots and kettles\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    expect(fatal(bool((m.blocks[0].child_blocks.size()) == (1u))));
    if (!m.blocks[0].child_blocks.empty()) {
        auto const& nested = m.blocks[0].child_blocks[0];
        expect(fatal(bool(nested.kind == RenderBlockKind::Quote)));
        expect(fatal(bool((nested.flow_local_indent_columns) == (2u))));
        expect(fatal(bool((nested.child_blocks.size()) == (1u))));
        if (!nested.child_blocks.empty()) {
            auto const& fragment = nested.child_blocks[0];
            expect(fatal(bool((fragment.flow_local_indent_columns) == (2u))));
            expect(fatal(bool((fragment.content_span.source_range.start) == (0u))));
            expect(fatal(bool(!fragment.inline_items.empty())));
            if (!fragment.inline_items.empty()) {
                expect(fatal(bool((fragment.inline_items.front().source_span.source_range.start) == (0u))));
                expect(fatal(bool(fragment.content_span.source_range.end == fragment.inline_items.back().source_span.source_range.end)));
            }
        }
    }
};

"core_quote_layout_consumes_unified_flow"_test = [] {
    StubMeasurer measurer(8.0f);
    auto model = build_model("> outer\n> > nested\n");
    auto tree = layout_blocks(model.blocks, 600.0f, 1.0f, measurer, std::nullopt, LogicalPoint(0.0f, 0.0f), Outline::empty(1));
    expect(fatal(bool((tree.blocks.size()) == (1u))));
    expect(fatal(bool(tree.blocks[0].kind.kind == LayoutBlockKind::Quote)));
    expect(fatal(bool(tree.blocks[0].children.size() >= 2u)));
    if (tree.blocks[0].children.size() >= 2) {
        expect(fatal(bool(tree.blocks[0].children[0].kind == LayoutItem::Kind::Line)));
        expect(fatal(bool(tree.blocks[0].children[1].kind == LayoutItem::Kind::Line)));
        expect(fatal(bool(!tree.blocks[0].children[0].line.runs.empty())));
        expect(fatal(bool(!tree.blocks[0].children[1].line.runs.empty())));
        if (!tree.blocks[0].children[0].line.runs.empty() && !tree.blocks[0].children[1].line.runs.empty())
            expect(fatal(bool(tree.blocks[0].children[1].line.runs.front().width > tree.blocks[0].children[0].line.runs.front().width)));
    }
};

"callout_title_is_the_first_editable_owner_in_unified_flow"_test = [] {
    auto parsed = parse_text(1, "> [!NOTE] _title_\n> body\n");
    expect(fatal(bool(!parsed.document.root.children.empty())));
    if (parsed.document.root.children.empty()) return;
    auto const& callout = parsed.document.root.children.front();
    expect(fatal(bool(callout.kind == BlockKind::Callout)));
    const auto* title = callout_title_block(callout);
    expect(fatal(bool(title != nullptr)));
    expect(fatal(bool(callout.children.size() >= 2u)));
    if (!title || callout.children.size() < 2) return;
    auto const title_id = title->id;
    auto const body_id = callout.children[1].id;

    auto model = build_render_model(
        parsed.document,
        parsed.outline,
        parsed.symbols,
        default_theme_profile());
    expect(fatal(bool(model.editable_order.size() >= 2u)));
    if (model.editable_order.size() < 2) return;
    expect(fatal(bool(model.editable_order[0] == title_id)));
    expect(fatal(bool(model.editable_order[1] == body_id)));
    expect(fatal(bool(!model.blocks.empty())));
    if (model.blocks.empty()) return;
    expect(fatal(bool(model.blocks.front().flow_anchor_owner_id == title_id)));
    expect(fatal(bool(model.blocks.front().source_span.container_id == callout.id)));
    expect(fatal(bool(model.blocks.front().source_span.source_range.empty())));
    const auto title_source = std::ranges::any_of(
        model.blocks.front().inline_items,
        [&](auto const& item) {
            return item.source_span.container_id == title_id
                && !item.source_span.source_range.empty();
        });
    expect(fatal(bool(title_source)));
};

}; // suite render_recursive_flow_tests
