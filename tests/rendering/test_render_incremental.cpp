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

suite render_incremental_tests = [] {

"render_model_presentation_keys_are_stable_and_content_sensitive"_test = [] {
    auto first = build_model("alpha\n\nbeta\n");
    auto repeated = build_model("alpha\n\nbeta\n");
    auto changed = build_model("alpha\n\ngamma\n");

    expect(fatal(bool(first.blocks.size() == 2u)));
    expect(fatal(bool(repeated.blocks.size() == first.blocks.size())));
    expect(fatal(bool(changed.blocks.size() == first.blocks.size())));
    if (first.blocks.size() != 2 || repeated.blocks.size() != 2 || changed.blocks.size() != 2) return;

    expect(fatal(bool(first.blocks[0].presentation_key != 0u)));
    expect(fatal(bool(first.blocks[0].presentation_key == repeated.blocks[0].presentation_key)));
    expect(fatal(bool(first.blocks[1].presentation_key == repeated.blocks[1].presentation_key)));
    expect(fatal(bool(first.blocks[0].presentation_key == changed.blocks[0].presentation_key)));
    expect(fatal(bool(first.blocks[1].presentation_key != changed.blocks[1].presentation_key)));
};

"render_model_presentation_keys_cover_nested_content"_test = [] {
    auto first = build_model("> outer\n> - nested\n");
    auto changed = build_model("> outer\n> - changed\n");
    expect(fatal(bool(first.blocks.size() == 1u)));
    expect(fatal(bool(changed.blocks.size() == 1u)));
    if (first.blocks.empty() || changed.blocks.empty()) return;
    expect(fatal(bool(first.blocks.front().presentation_key != changed.blocks.front().presentation_key)));
};

"render_model_caches_editable_document_order"_test = [] {
    auto model = build_model("# title\n\nbody\n");
    expect(fatal(bool(model.editable_index.size() == model.editable_order.size())));
    for (std::size_t index = 0; index < model.editable_order.size(); ++index) {
        auto found = model.editable_index.find(model.editable_order[index].v);
        expect(fatal(bool(found != model.editable_index.end())));
        if (found != model.editable_index.end()) expect(fatal(bool(found->second == index)));
    }
};

"incremental_render_model_matches_a_full_rebuild"_test = [] {
    auto before = parse_text(1, "alpha\n\nbeta with **bold**\n\n> nested\n");
    auto previous = build_render_model(
        before.document,
        before.outline,
        before.symbols,
        default_theme_profile());
    auto after = parse_text(1, "alpha\n\nbeta with **bold**\n\n> changed\n");
    auto symbols = build_document_symbol_index(after.document);
    NodeId changed_owner{};
    walk_blocks(after.document.root, [&](BlockNode const& block) {
        if (block.inline_content.source == U"changed") changed_owner = block.id;
    });
    expect(fatal(bool(changed_owner.v != 0u)));
    RenderModelUpdate update;
    update.changed_owners.push_back(changed_owner);
    reset_core_operation_counters();
    auto incremental = build_render_model_incremental(
        after.document,
        after.outline,
        symbols,
        default_theme_profile(),
        std::move(previous),
        update);
    auto const incremental_counters = read_core_operation_counters();
    auto full = build_render_model(after.document, after.outline, symbols, default_theme_profile());
    expect(fatal(bool(incremental.blocks.size() == full.blocks.size())));
    if (incremental.blocks.size() != full.blocks.size()) return;
    for (std::size_t index = 0; index < full.blocks.size(); ++index) {
        expect(fatal(bool(incremental.blocks[index].source_key == full.blocks[index].source_key)));
        expect(fatal(bool(incremental.blocks[index].presentation_key == full.blocks[index].presentation_key)));
    }
    expect(fatal(bool(incremental.editable_order == full.editable_order)));
    expect(fatal(bool(incremental.editable_index == full.editable_index)));
    expect(fatal(bool(incremental.editable_top_level == full.editable_top_level)));
    expect(fatal(bool(incremental.rebuilt_block_count == 1u)));
    expect(fatal(incremental.incremental_update));
    expect(fatal(bool(incremental.changed_block_indices == std::vector<std::size_t>{2u})));
    expect(fatal(bool(incremental_counters.render_source_key_derivations == 1u)));
    expect(fatal(bool(incremental.reused_block_count + incremental.rebuilt_block_count
        == incremental.blocks.size())));
};

"virtualized_render_model_materializes_and_releases_only_the_requested_window"_test = [] {
    auto parsed = parse_text(
        1,
        "# title\n\n"
        "> quote\n> - nested **item**\n\n"
        "| A | B |\n| --- | --- |\n| 1 | 2 |\n\n"
        "```cpp\nauto answer = 42;\n```\n\n"
        "tail $x + y$\n");
    auto model = build_virtualized_render_model(
        parsed.document,
        parsed.outline,
        parsed.symbols,
        default_theme_profile());
    expect(fatal(model.virtualized));
    expect(fatal(bool(model.blocks.size() == parsed.document.root.children.size())));
    expect(fatal(bool(model.materialized_block_indices.empty())));
    expect(fatal(bool(std::ranges::all_of(model.blocks, [](auto const& block) {
        return !block.materialized
            && block.inline_items.empty()
            && block.child_blocks.empty()
            && block.source_key == 0u
            && block.presentation_key != 0u;
    }))));
    expect(fatal(bool(model.blocks.front().text_heading_level == 1u)));
    expect(fatal(bool(model.blocks.front().estimated_characters >= 5u)));

    materialize_render_model_range(
        model,
        parsed.document,
        parsed.symbols,
        default_theme_profile(),
        1,
        4);
    expect(fatal(bool(model.materialized_block_indices
        == std::unordered_set<std::size_t>{1u, 2u, 3u})));
    expect(fatal(bool(!model.blocks[0].materialized)));
    expect(fatal(bool(model.blocks.size() >= 4u)));
    if (model.blocks.size() < 4u) return;
    for (auto index : {1u, 2u, 3u}) {
        expect(fatal(model.blocks[index].materialized));
        expect(fatal(bool(model.blocks[index].source_key != 0u)));
    }
    expect(fatal(bool(!model.blocks[1].inline_items.empty())));
    expect(fatal(bool(!model.blocks[1].child_blocks.empty())));
    expect(fatal(bool(!model.blocks[2].special().table_cells.empty())));
    expect(fatal(bool(!model.blocks[3].special().code_text.empty())));

    auto retained_key = model.blocks[2].presentation_key;
    release_render_model_blocks_outside(
        model,
        parsed.document,
        default_theme_profile(),
        2,
        3);
    expect(fatal(bool(model.materialized_block_indices
        == std::unordered_set<std::size_t>{2u})));
    expect(fatal(bool(!model.blocks[1].materialized && model.blocks[1].inline_items.empty())));
    expect(fatal(model.blocks[2].materialized));
    expect(fatal(bool(model.blocks[2].presentation_key == retained_key)));
    expect(fatal(bool(!model.blocks[3].materialized && model.blocks[3].special().code_text.empty())));
};

"generated_container_prefixes_anchor_carets_to_the_source_boundary"_test = [] {
    StubMeasurer measurer(8.0f);
    for (auto const& markdown : std::vector<std::string>{"> quoted\n", "> - nested\n"}) {
        auto model = build_model(markdown);
        expect(fatal(bool(!model.blocks.empty())));
        if (model.blocks.empty()) continue;
        auto const* leaf = first_render_leaf(model.blocks.front());
        expect(fatal(bool(leaf != nullptr)));
        if (!leaf) continue;
        const auto owner = leaf->id;
        auto tree = layout_blocks(
            model.blocks,
            600.0f,
            1.0f,
            measurer,
            TextPosition{owner, 0, TextAffinity::Upstream},
            LogicalPoint(0.0f, 0.0f),
            Outline::empty(1));

        const GlyphRunLayout* source_run = nullptr;
        std::vector<std::pair<const GlyphRunLayout*, const TextLineLayout*>> prefixes;
        for (auto const& block : tree.blocks) {
            for (auto const& child : block.children) {
                if (child.kind != LayoutItem::Kind::Line) continue;
                for (auto const& run : child.line.runs) {
                    if (run.source_span.container_id != owner
                        || !run.source_span.source_range.covers(0)) continue;
                    if (run.generated_boundary_affinity == TextAffinity::Downstream) {
                        prefixes.push_back({&run, &child.line});
                    } else if (!run.generated_boundary_affinity) {
                        source_run = &run;
                    }
                }
            }
        }
        expect(fatal(bool(source_run != nullptr)));
        expect(fatal(bool(!prefixes.empty())));
        if (!source_run || prefixes.empty()) continue;

        auto upstream = compute_caret_geometry(
            tree,
            TextPosition{owner, 0, TextAffinity::Upstream});
        auto downstream = compute_caret_geometry(
            tree,
            TextPosition{owner, 0, TextAffinity::Downstream});
        expect(fatal(bool(upstream.has_value())));
        expect(fatal(bool(downstream.has_value())));
        if (upstream && downstream) {
            expect(fatal(bool(std::fabs(upstream->rect.x - source_run->origin.x) < 0.001f)));
            expect(fatal(bool(std::fabs(downstream->rect.x - source_run->origin.x) < 0.001f)));
        }

        for (auto const& [prefix, line] : prefixes) {
            if (prefix->width <= 0.0f) continue;
            auto hit = hit_test_layout_tree(
                tree,
                LogicalPoint(prefix->origin.x + prefix->width * 0.5f, line->rect.y + 1.0f));
            expect(fatal(bool(hit.has_value())));
            if (!hit) continue;
            expect(fatal(bool(hit->position.container_id == owner)));
            expect(fatal(bool(hit->position.source_offset == 0u)));
            expect(fatal(bool(hit->position.affinity == TextAffinity::Downstream)));
        }
    }
};

"blockquote_multiline_text_ranges_remain_monotonic"_test = [] {
    auto m = build_model("> first line\n> second line\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    expect(fatal(bool((m.blocks[0].child_blocks.size()) == (1u))));
    if (!m.blocks[0].child_blocks.empty()) {
        auto const& items = m.blocks[0].child_blocks[0].inline_items;
        expect(fatal(bool((items.size()) == (3u))));
        if (items.size() >= 3) {
            expect(fatal(bool((items[0].source_span.source_range.start) == (0u))));
            expect(fatal(bool((items[1].source_span.source_range.start) == (10u))));
            expect(fatal(bool((items[1].source_span.source_range.end) == (11u))));
            expect(fatal(bool(items[2].source_span.source_range.start >= items[1].source_span.source_range.end)));
        }
    }
};

"blockquote_hard_break_keeps_its_exact_inline_source_range"_test = [] {
    auto m = build_model("> alpha  \n> beta\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    expect(fatal(bool((m.blocks[0].child_blocks.size()) == (1u))));
    if (!m.blocks[0].child_blocks.empty()) {
        auto const& items = m.blocks[0].child_blocks[0].inline_items;
        auto hard = std::find_if(items.begin(), items.end(), [](auto const& item) { return item.kind == InlineRenderItem::Kind::Text && item.text == U"\n"; });
        expect(fatal(bool(hard != items.end())));
        if (hard != items.end()) {
            expect(fatal(bool((hard->source_span.source_range.start) == (5u))));
            expect(fatal(bool((hard->source_span.source_range.end) == (8u))));
        }
    }
};

"trailing_blockquote_hard_break_ends_at_inline_source_boundary"_test = [] {
    auto m = build_model("> alpha  \n> \n\nafter");
    expect(fatal(bool((m.blocks.size()) == (2u))));
    expect(fatal(bool(!m.blocks[0].child_blocks.empty())));
    if (!m.blocks[0].child_blocks.empty()) {
        auto const& items = m.blocks[0].child_blocks.front().inline_items;
        expect(fatal(bool(!items.empty())));
        expect(fatal(bool(items.back().text == U"\n")));
        expect(fatal(bool((items.back().source_span.source_range.end) == (8u))));
    }
};

"blockquote_render_model_tracks_empty_lines_in_the_structural_tree"_test = [] {
    auto outerBlank = build_model("> > alpha\n> ");
    expect(fatal(bool((outerBlank.blocks.size()) == (1u))));
    expect(fatal(bool((outerBlank.blocks[0].child_blocks.size()) == (2u))));
    if (outerBlank.blocks[0].child_blocks.size() >= 2) {
        expect(fatal(bool(outerBlank.blocks[0].child_blocks[0].kind == RenderBlockKind::Quote)));
        expect(fatal(bool((outerBlank.blocks[0].child_blocks[0].flow_local_indent_columns) == (2u))));
        expect(fatal(bool(outerBlank.blocks[0].child_blocks[1].kind == RenderBlockKind::Blank)));
        expect(fatal(bool((outerBlank.blocks[0].child_blocks[1].flow_local_indent_columns) == (2u))));
    }

    auto nestedBlank = build_model("> > alpha  \n> > ");
    expect(fatal(bool((nestedBlank.blocks.size()) == (1u))));
    expect(fatal(bool((nestedBlank.blocks[0].child_blocks.size()) == (1u))));
    if (!nestedBlank.blocks[0].child_blocks.empty()) {
        auto const& nested = nestedBlank.blocks[0].child_blocks[0];
        expect(fatal(bool(nested.kind == RenderBlockKind::Quote)));
        expect(fatal(bool((nested.child_blocks.size()) == (2u))));
        if (nested.child_blocks.size() >= 2) {
            expect(fatal(bool(nested.child_blocks[0].kind == RenderBlockKind::Text)));
            expect(fatal(bool((nested.child_blocks[0].flow_local_indent_columns) == (2u))));
            expect(fatal(bool(nested.child_blocks[1].kind == RenderBlockKind::Blank)));
            expect(fatal(bool((nested.child_blocks[1].flow_local_indent_columns) == (2u))));
        }
    }
};

"list_render_model_has_no_synthetic_trailing_newline"_test = [] {
    auto m = build_model("- one\n- two\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    expect(fatal(bool(!m.blocks[0].inline_items.empty())));
    if (!m.blocks[0].inline_items.empty()) expect(fatal(bool(m.blocks[0].inline_items.back().text != U"\n")));
};

}; // suite render_incremental_tests
