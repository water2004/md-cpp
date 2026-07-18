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
import elmd.core.parser;
import elmd.core.document_edit;
import elmd.core.document;
import elmd.core.document_symbols;
import elmd.core.inline_parser;
import elmd.core.render_builder;
import elmd.core.render_model;
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.block_tree;
import elmd.core.callout;
import elmd.core.text_measurer;
import elmd.core.block_layout;
import elmd.core.hit_test;
import elmd.core.selection_geometry;
import elmd.core.selection;

using namespace elmd;
using namespace boost::ut;

#include "support/render_layout_test_support.hpp"

suite render_layout_hit_test_tests = [] {

"layout_table_builds_rows_columns_and_cells"_test = [] {
    StubMeasurer ms(8.0f);
    auto m = build_model("| A | B |\n| :--- | ---: |\n| 1 | 2 |\n");
    auto t = layout_blocks(m.blocks, 800.0f, 1.0f, ms, std::nullopt, LogicalPoint(0, 0), Outline::empty(1));
    expect(fatal(bool((t.blocks.size()) == (1u))));
    expect(fatal(bool(t.blocks[0].kind.kind == LayoutBlockKind::Table)));
    expect(fatal(bool((t.blocks[0].children.size()) == (1u))));
    expect(fatal(bool(t.blocks[0].children[0].kind == LayoutItem::Kind::Table)));
    auto const& table = t.blocks[0].children[0].table;
    expect(fatal(bool((table.columns.size()) == (2u))));
    expect(fatal(bool((table.rows.size()) == (2u))));
    expect(fatal(bool((table.rows[0].cells.size()) == (2u))));
    expect(fatal(bool(table.rows[0].is_header)));
    expect(fatal(bool(table.columns[0].alignment == TableAlignment::Left)));
    expect(fatal(bool(table.columns[1].alignment == TableAlignment::Right)));
    expect(fatal(bool((table.rows[0].cells[0].source_span.source_range.start) == (0u))));
    expect(fatal(bool(table.rows[0].cells[0].source_span.container_id.v != 0)));
};

"builds_text_blocks_from_ast_paragraphs"_test = [] {
    auto trailing = build_model("Hello\n");
    expect(fatal(bool((trailing.blocks.size()) == (1u))));

    auto between = build_model("Hello\n\nWorld");
    expect(fatal(bool((between.blocks.size()) == (2u))));

    auto empty_after_break = build_model("Hello\n\n");
    expect(fatal(bool((empty_after_break.blocks.size()) == (2u))));
    expect(fatal(bool(empty_after_break.blocks[1].kind == RenderBlockKind::Blank)));
    expect(fatal(bool(empty_after_break.blocks[1].content_span.source_range.empty())));

    auto one_blank_between = build_model("Hello\n\n\nWorld");
    expect(fatal(bool((one_blank_between.blocks.size()) == (3u))));
    if (one_blank_between.blocks.size() != 3u) return;
    expect(fatal(bool(one_blank_between.blocks[0].kind == RenderBlockKind::Text)));
    expect(fatal(bool(one_blank_between.blocks[1].kind == RenderBlockKind::Blank)));
    expect(fatal(bool(one_blank_between.blocks[2].kind == RenderBlockKind::Text)));
};

"layout_blank_block_has_one_caret_line"_test = [] {
    StubMeasurer ms;
    auto m = build_model("Hello\n\n");
    const auto owner = m.blocks[1].id;
    auto t = layout_blocks(m.blocks, 800.0f, 1.0f, ms, TextPosition{owner, 0, TextAffinity::Downstream}, LogicalPoint(0, 0), Outline::empty(1));
    expect(fatal(bool((t.blocks.size()) == (2u))));
    expect(fatal(bool(t.blocks[1].kind.kind == LayoutBlockKind::Blank)));
    expect(fatal(bool((t.blocks[1].children.size()) == (1u))));
    auto caret = compute_caret_geometry(t, TextPosition{owner, 0, TextAffinity::Downstream});
    expect(fatal(bool(caret.has_value())));
};

"renders_soft_break_as_space"_test = [] {
    auto m = build_model("Hello\nWorld");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    std::u32string text;
    for (const auto& item : m.blocks[0].inline_items) {
        if (item.kind == InlineRenderItem::Kind::Text) text += item.text;
    }
    expect(fatal(bool(text == U"Hello World")));
};

"renders_heading_soft_break_as_line_break"_test = [] {
    auto m = build_model("Hello\nWorld\n---");
    expect(fatal(bool(m.blocks.size() == 1u)));
    if (m.blocks.empty()) return;
    expect(fatal(bool(m.blocks.front().text_heading_level == 2u)));
    std::u32string text;
    for (const auto& item : m.blocks.front().inline_items) {
        if (item.kind == InlineRenderItem::Kind::Text) text += item.text;
    }
    expect(fatal(bool(text == U"Hello\nWorld")));
};

"safe_block_html_becomes_rendered_content"_test = [] {
    auto m = build_model("<div>\nhello\n</div>\n");
    bool found = false;
    for (const auto& b : m.blocks) {
        if (b.kind == RenderBlockKind::Text) {
            for (auto const& item : b.inline_items) if (item.text.find(U"hello") != std::u32string::npos) found = true;
        }
    }
    expect(fatal(bool(found)));
};

"safe_inline_html_markers_share_their_node_owner"_test = [] {
    auto m = build_model("before <em>italic</em> after\n");
    std::vector<InlineRenderItem const*> markers;
    for (auto const& item : m.blocks[0].inline_items) {
        if (item.kind == InlineRenderItem::Kind::Marker && item.special().marker_owner) {
            markers.push_back(&item);
        }
    }
    expect(fatal(bool((markers.size()) == (2u))));
    if (markers.size() == 2) {
        expect(fatal(bool(markers[0]->text == U"<em>")));
        expect(fatal(bool(markers[1]->text == U"</em>")));
        expect(fatal(bool(markers[0]->special().marker_owner == markers[1]->special().marker_owner)));
    }
};

"incomplete_inline_math_remains_structural"_test = [] {
    auto out = parse_text(1, "$x + 1\n");
    expect(fatal(bool(!out.document.root.children.empty())));
    expect(fatal(bool(inline_contains_kind(out.document.root.children.front().inline_content, InlineCstKind::Incomplete))));
};

"large_document_parse_and_render_model_build_are_bounded"_test = [] {
    std::string source;
    source.reserve(512 * 1024);
    for (std::size_t index = 0; index < 1200; ++index) {
        source += "## Section " + std::to_string(index) + "\n\n";
        source += "Paragraph with **bold**, [link](https://example.com), `$code`, and $x^2$.\n\n";
        source += "- first item\n  - nested item\n- second item\n\n";
    }
    auto started = std::chrono::steady_clock::now();
    auto parsed = parse_text(1, source);
    auto model = build_render_model(
        parsed.document,
        parsed.outline,
        parsed.symbols,
        default_theme_profile());
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    expect(fatal(bool(parsed.document.root.children.size() >= 3600u)));
    expect(fatal(bool(model.blocks.size() >= parsed.document.root.children.size())));
    expect(fatal(bool(elapsed < 5.0)));
};

"layout_empty"_test = [] {
    StubMeasurer ms;
    auto t = layout_blocks({}, 800.0f, 1.0f, ms, std::nullopt, LogicalPoint(0, 0), Outline::empty(0));
    expect(fatal(bool((t.blocks.size()) == (0u))));
    expect(fatal(bool((t.total_height) == (0.0f))));
};

"layout_text_block"_test = [] {
    StubMeasurer ms;
    RenderBlock rb; rb.kind = RenderBlockKind::Text; rb.block_style = BlockStyle::paragraph();
    rb.id = NodeId{42};
    InlineRenderItem it = InlineRenderItem::plain_text(U"Hello", TextSpan{rb.id, {0, 5}});
    rb.inline_items.push_back(it);
    auto t = layout_blocks({rb}, 800.0f, 1.0f, ms, std::nullopt, LogicalPoint(0, 0), Outline::empty(0));
    expect(fatal(bool(!t.blocks.empty())));
    expect(fatal(bool(t.blocks[0].rect.height > 0)));
    expect(fatal(bool(!t.blocks[0].children.empty())));
};

"pipeline_parse_render_layout_hittest"_test = [] {
    StubMeasurer ms;
    auto m = build_model("# Title\n\nSome **bold** text.\n");
    expect(fatal(bool(!m.blocks.empty())));
    auto out = parse_text(1, "# Title\n\nSome **bold** text.\n");
    Outline ol = out.outline;
    auto t = layout_blocks(m.blocks, 800.0f, 1.0f, ms, std::nullopt, LogicalPoint(0, 0), ol);
    expect(fatal(bool(t.total_height > 0)));
    expect(fatal(bool(!t.blocks.empty())));
    LogicalPoint hit_point(20.0f, t.blocks[0].children[0].line.rect.y + 6.0f);
    auto hit = hit_test_layout_tree(t, hit_point);
    expect(fatal(bool(hit.has_value())));
    bool any_line_range = false;
    for (const auto& b : t.blocks) for (const auto& ch : b.children) if (ch.line.source_span.source_range.length() > 0) any_line_range = true;
    expect(fatal(bool(any_line_range)));
};

"hit_test_returns_block_local_position"_test = [] {
    StubMeasurer ms(6.0f);
    LayoutTree tree;
    LayoutBlock b(NodeId(0), TextSpan{NodeId(0), {10, 15}}, {LayoutBlockKind::Paragraph}, BlockStyle::paragraph());
    const auto owner = NodeId{77};
    TextLineLayout ll{TextSpan{owner, {10, 15}}};
    ll.rect = LogicalRect(0, 0, 100, 20);
    ll.baseline = 16;
    GlyphRunLayout r; r.source_span = {owner, {10, 15}};
    r.text = U"hello"; r.origin = LogicalPoint(0, 0); r.width = 30; r.marker_visibility = MarkerVisibility::Always;
    for (std::size_t i = 0; i < 5; ++i) { GlyphInfo g; g.advance = 6; g.char_index = i; r.glyphs.push_back(g); }
    ll.runs.push_back(std::move(r));
    LayoutItem li; li.kind = LayoutItem::Kind::Line; li.line = std::move(ll);
    b.children.push_back(std::move(li));
    b.rect = LogicalRect(0, 0, 100, 20);
    tree.blocks.push_back(std::move(b));
    auto hit = hit_test_layout_tree(tree, LogicalPoint(15, 10));
    expect(fatal(bool(hit.has_value())));
    if (hit) {
        expect(fatal(bool(hit->position.container_id == owner)));
        expect(fatal(bool(hit->position.source_offset >= 10)));
        expect(fatal(bool(hit->position.source_offset <= 15)));
    }
};

"multiline_code_and_math_layouts_keep_true_source_offsets"_test = [] {
    StubMeasurer ms(6.0f);
    auto code_model = build_model("```cpp\none\ntwo\n```");
    auto code_tree = layout_blocks(
        code_model.blocks,
        800.0f,
        1.0f,
        ms,
        std::nullopt,
        LogicalPoint(0, 0),
        Outline::empty(1));
    expect(fatal(bool(code_tree.blocks.size() == 1u)));
    if (!code_tree.blocks.empty()) {
        auto const& lines = code_tree.blocks.front().children;
        expect(fatal(bool(lines.size() == 2u)));
        if (lines.size() == 2u) {
            expect(fatal(bool(lines[0].line.source_span.source_range == SourceRange{7, 10})));
            expect(fatal(bool(lines[1].line.source_span.source_range == SourceRange{11, 14})));
            auto hit = hit_test_layout_tree(
                code_tree,
                LogicalPoint(20.0f, lines[1].line.rect.y + 1.0f));
            expect(fatal(bool(hit.has_value())));
            if (hit) {
                expect(fatal(bool(hit->position.container_id == lines[1].line.source_span.container_id)));
                expect(fatal(bool(hit->position.source_offset >= 11u)));
                expect(fatal(bool(hit->position.source_offset <= 14u)));
            }
        }
    }

    auto math_model = build_model("$$\na\nb\n$$");
    auto math_tree = layout_blocks(
        math_model.blocks,
        800.0f,
        1.0f,
        ms,
        std::nullopt,
        LogicalPoint(0, 0),
        Outline::empty(1));
    expect(fatal(bool(math_tree.blocks.size() == 1u)));
    if (!math_tree.blocks.empty()) {
        auto const& lines = math_tree.blocks.front().children;
        expect(fatal(bool(lines.size() == 3u)));
        if (lines.size() == 3u) {
            expect(fatal(bool(lines[0].line.source_span.source_range == SourceRange{3, 4})));
            expect(fatal(bool(lines[1].line.source_span.source_range == SourceRange{5, 6})));
            expect(fatal(bool(lines[2].line.source_span.source_range == SourceRange{7, 7})));
        }
    }
};

"block_gap_and_table_hits_resolve_to_editable_owners"_test = [] {
    LayoutTree gap_tree;
    LayoutBlock outer(
        NodeId{10},
        TextSpan{NodeId{10}, {0, 0}},
        {LayoutBlockKind::Quote},
        BlockStyle::blockquote());
    outer.rect = LogicalRect(0, 0, 100, 60);
    const auto owner = NodeId{77};
    TextLineLayout line{TextSpan{owner, {2, 6}}};
    line.rect = LogicalRect(0, 20, 100, 20);
    LayoutItem line_item;
    line_item.kind = LayoutItem::Kind::Line;
    line_item.line = std::move(line);
    outer.children.push_back(std::move(line_item));
    gap_tree.blocks.push_back(std::move(outer));

    auto before = hit_test_layout_tree(gap_tree, LogicalPoint(5, 5));
    auto after = hit_test_layout_tree(gap_tree, LogicalPoint(5, 55));
    expect(fatal(bool(before.has_value())));
    expect(fatal(bool(after.has_value())));
    if (before) {
        expect(fatal(bool(before->position.container_id == owner)));
        expect(fatal(bool(before->position.source_offset == 2u)));
    }
    if (after) {
        expect(fatal(bool(after->position.container_id == owner)));
        expect(fatal(bool(after->position.source_offset == 6u)));
    }

    StubMeasurer ms(6.0f);
    auto table_model = build_model("| A | B |\n| --- | --- |\n| 1 | 2 |");
    auto table_tree = layout_blocks(
        table_model.blocks,
        800.0f,
        1.0f,
        ms,
        std::nullopt,
        LogicalPoint(0, 0),
        Outline::empty(1));
    expect(fatal(bool(!table_tree.blocks.empty())));
    if (table_tree.blocks.empty() || table_tree.blocks.front().children.empty()) return;
    auto const& table = table_tree.blocks.front().children.front().table;
    expect(fatal(bool(!table.rows.empty() && !table.rows.front().cells.empty())));
    if (table.rows.empty() || table.rows.front().cells.empty()) return;
    auto const& cell = table.rows.front().cells.front();
    auto hit = hit_test_layout_tree(
        table_tree,
        LogicalPoint(cell.rect.x + 10.0f, cell.rect.y + 1.0f));
    expect(fatal(bool(hit.has_value())));
    if (hit) expect(fatal(bool(hit->position.container_id == cell.source_span.container_id)));
};

"stub_measurer_per_char_advances"_test = [] {
    StubMeasurer ms(8.0f);
    auto s = ms.measure(U"abc", 16.0f, InlineStyle::plain());
    expect(fatal(bool((s.glyphs.size()) == (3u))));
    expect(fatal(bool((s.width) == (24.0f))));
    expect(fatal(bool(!s.glyphs[0].is_whitespace)));
};

"stub_measurer_marks_whitespace"_test = [] {
    StubMeasurer ms;
    auto s = ms.measure(U"a b", 16.0f, InlineStyle::plain());
    expect(fatal(bool(!s.glyphs[0].is_whitespace)));
    expect(fatal(bool(s.glyphs[1].is_whitespace)));
};

"advance_total_matches_width"_test = [] {
    StubMeasurer ms(7.0f);
    auto s = ms.measure(U"hello", 16.0f, InlineStyle::plain());
    expect(fatal(bool(std::fabs(s.advance_total() - s.width) < 0.001f)));
};

"selection_geometry_runs"_test = [] {
    StubMeasurer ms;
    LayoutTree t;
    LayoutBlock b(NodeId(0), TextSpan{NodeId(0), {0, 5}}, {LayoutBlockKind::Paragraph}, BlockStyle::paragraph());
    const auto owner = NodeId{88};
    TextLineLayout ll{TextSpan{owner, {0, 5}}};
    ll.rect = LogicalRect(0, 0, 100, 20); ll.baseline = 16;
    GlyphRunLayout r; r.source_span = {owner, {0, 5}};
    r.text = U"hello"; r.origin = LogicalPoint(0, 0); r.width = 50; r.marker_visibility = MarkerVisibility::Always;
    for (std::size_t i = 0; i < 5; ++i) { GlyphInfo g; g.advance = 10; g.char_index = i; r.glyphs.push_back(g); }
    ll.runs.push_back(std::move(r));
    LayoutItem li; li.kind = LayoutItem::Kind::Line; li.line = std::move(ll);
    b.children.push_back(std::move(li));
    b.rect = LogicalRect(0, 0, 100, 20);
    t.blocks.push_back(std::move(b));
    auto sg = compute_selection_geometry(t, TextSelection{{owner, 1, TextAffinity::Downstream}, {owner, 4, TextAffinity::Downstream}});
    expect(fatal(bool(!sg.ranges.empty())));
};

"caret_geometry_at_position"_test = [] {
    StubMeasurer ms;
    LayoutTree t;
    LayoutBlock b(NodeId(0), TextSpan{NodeId(0), {0, 5}}, {LayoutBlockKind::Paragraph}, BlockStyle::paragraph());
    const auto owner = NodeId{99};
    TextLineLayout ll{TextSpan{owner, {0, 5}}};
    ll.rect = LogicalRect(0, 0, 100, 20); ll.baseline = 16;
    GlyphRunLayout r; r.source_span = {owner, {0, 5}};
    r.text = U"hello"; r.origin = LogicalPoint(0, 0); r.width = 50; r.marker_visibility = MarkerVisibility::Always;
    for (std::size_t i = 0; i < 5; ++i) { GlyphInfo g; g.advance = 10; g.char_index = i; r.glyphs.push_back(g); }
    ll.runs.push_back(std::move(r));
    LayoutItem li; li.kind = LayoutItem::Kind::Line; li.line = std::move(ll);
    b.children.push_back(std::move(li));
    b.rect = LogicalRect(0, 0, 100, 20);
    t.blocks.push_back(std::move(b));
    auto cg = compute_caret_geometry(t, TextPosition{owner, 3, TextAffinity::Downstream});
    expect(fatal(bool(cg.has_value())));
};

}; // suite render_layout_hit_test_tests
