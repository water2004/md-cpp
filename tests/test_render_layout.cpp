import std;
#include "test_framework.h"
import elmd.core.parser;
import elmd.core.render_builder;
import elmd.core.render_model;
import elmd.core.ast;
import elmd.core.text_measurer;
import elmd.core.block_layout;
import elmd.core.hit_test;
import elmd.core.selection_geometry;
import elmd.core.selection;

using namespace elmd;

static RenderModel build_model(const std::string& src) {
    auto out = parse_text(1, src);
    return build_render_model(out.document, src, out.outline);
}

ELMD_TEST(test_empty_document_yields_empty_model) {
    MarkdownDocument doc = MarkdownDocument::empty(1);
    Outline o = Outline::empty(1);
    auto m = build_render_model(doc, "", o);
    ELMD_CHECK_EQ(m.blocks.size(), 0u);
    ELMD_CHECK_EQ(m.revision, 1ull);
}

ELMD_TEST(builds_heading_with_marker_and_text) {
    auto m = build_model("# Title");
    ELMD_CHECK(!m.blocks.empty());
    for (const auto& it : m.blocks[0].inline_items)
        ELMD_CHECK(it.source_range.end.v >= it.source_range.start.v);
    bool has_text = false;
    for (const auto& it : m.blocks[0].inline_items) if (it.kind == InlineRenderItem::Kind::Text) has_text = true;
    ELMD_CHECK(has_text);
}

ELMD_TEST(builds_strong_with_open_and_close_markers) {
    auto m = build_model("**bold**");
    int marker_count = 0;
    for (const auto& it : m.blocks[0].inline_items) {
        if (it.kind == InlineRenderItem::Kind::Marker && it.text == U"**") ++marker_count;
    }
    ELMD_CHECK_EQ(marker_count, 2);
}

ELMD_TEST(builds_code_block) {
    auto m = build_model("```rust\nfn main() {}\n```");
    ELMD_CHECK(!m.blocks.empty());
    ELMD_CHECK(m.blocks[0].kind == RenderBlockKind::Code);
    ELMD_CHECK(m.blocks[0].language && *m.blocks[0].language == "rust");
    ELMD_CHECK(m.blocks[0].line_count >= 1);
}

ELMD_TEST(builds_inline_math) {
    auto m = build_model("$x^2$");
    ELMD_CHECK(!m.blocks.empty());
    bool has_math = false;
    for (const auto& it : m.blocks[0].inline_items)
        if (it.kind == InlineRenderItem::Kind::Math) has_math = true;
    ELMD_CHECK(has_math);
}

ELMD_TEST(builds_table_block) {
    auto m = build_model("| A | B |\n|---|---|\n| 1 | 2 |\n");
    ELMD_CHECK(m.blocks[0].kind == RenderBlockKind::Table);
    ELMD_CHECK(m.blocks[0].row_count >= 2);
    ELMD_CHECK(m.blocks[0].column_count >= 2);
}

ELMD_TEST(raw_html_becomes_unsupported) {
    auto m = build_model("<div>\nhello\n</div>\n");
    bool found = false;
    for (const auto& b : m.blocks) {
        if (b.kind == RenderBlockKind::Unsupported) { found = true; if (b.raw.find("div") != std::string::npos || b.raw.find('<') != std::string::npos) {} }
    }
    ELMD_CHECK(found);
}

ELMD_TEST(diagnostics_pass_through) {
    auto out = parse_text(1, "$x + 1\n");
    auto m = build_render_model(out.document, "$x + 1\n", out.outline);
    ELMD_CHECK(!m.diagnostics.empty());
}

ELMD_TEST(layout_empty) {
    StubMeasurer ms;
    auto t = layout_blocks({}, 800.0f, 1.0f, ms, std::nullopt, LogicalPoint(0, 0), Outline::empty(0));
    ELMD_CHECK_EQ(t.blocks.size(), 0u);
    ELMD_CHECK_EQ(t.total_height, 0.0f);
}

ELMD_TEST(layout_text_block) {
    StubMeasurer ms;
    RenderBlock rb; rb.kind = RenderBlockKind::Text; rb.block_style = BlockStyle::paragraph();
    InlineRenderItem it = InlineRenderItem::plain_text(U"Hello", CharRange(CharOffset(0), CharOffset(5)));
    rb.inline_items.push_back(it);
    auto t = layout_blocks({rb}, 800.0f, 1.0f, ms, std::nullopt, LogicalPoint(0, 0), Outline::empty(0));
    ELMD_CHECK(!t.blocks.empty());
    ELMD_CHECK(t.blocks[0].rect.height > 0);
    ELMD_CHECK(!t.blocks[0].children.empty());
}

ELMD_TEST(pipeline_parse_render_layout_hittest) {
    StubMeasurer ms;
    auto m = build_model("# Title\n\nSome **bold** text.\n");
    ELMD_CHECK(!m.blocks.empty());
    auto out = parse_text(1, "# Title\n\nSome **bold** text.\n");
    Outline ol = out.outline;
    auto t = layout_blocks(m.blocks, 800.0f, 1.0f, ms, std::nullopt, LogicalPoint(0, 0), ol);
    ELMD_CHECK(t.total_height > 0);
    ELMD_CHECK(!t.blocks.empty());
    LogicalPoint hit_point(20.0f, t.blocks[0].children[0].line.rect.y + 6.0f);
    auto hit = hit_test_layout_tree(t, hit_point);
    ELMD_CHECK(hit.has_value());
    bool any_line_range = false;
    for (const auto& b : t.blocks) for (const auto& ch : b.children) if (ch.line.source_range.end.v > ch.line.source_range.start.v) any_line_range = true;
    ELMD_CHECK(any_line_range);
}

ELMD_TEST(hit_test_returns_document_offset) {
    StubMeasurer ms(6.0f);
    LayoutTree tree;
    LayoutBlock b(NodeId(0), CharRange(CharOffset(10), CharOffset(15)), {LayoutBlockKind::Paragraph}, BlockStyle::paragraph());
    TextLineLayout ll{CharRange(CharOffset(10), CharOffset(15))};
    ll.rect = LogicalRect(0, 0, 100, 20);
    ll.baseline = 16;
    GlyphRunLayout r; r.source_range = CharRange(CharOffset(10), CharOffset(15));
    r.text = U"hello"; r.origin = LogicalPoint(0, 0); r.width = 30; r.marker_visibility = MarkerVisibility::Always;
    for (std::size_t i = 0; i < 5; ++i) { GlyphInfo g; g.advance = 6; g.char_index = i; r.glyphs.push_back(g); }
    ll.runs.push_back(std::move(r));
    LayoutItem li; li.kind = LayoutItem::Kind::Line; li.line = std::move(ll);
    b.children.push_back(std::move(li));
    b.rect = LogicalRect(0, 0, 100, 20);
    tree.blocks.push_back(std::move(b));
    auto hit = hit_test_layout_tree(tree, LogicalPoint(15, 10));
    ELMD_CHECK(hit.has_value());
    if (hit) {
        ELMD_CHECK(hit->position.v >= 10);
        ELMD_CHECK(hit->position.v <= 15);
    }
}

ELMD_TEST(stub_measurer_per_char_advances) {
    StubMeasurer ms(8.0f);
    auto s = ms.measure(U"abc", 16.0f, InlineStyle::plain());
    ELMD_CHECK_EQ(s.glyphs.size(), 3u);
    ELMD_CHECK_EQ(s.width, 24.0f);
    ELMD_CHECK(!s.glyphs[0].is_whitespace);
}

ELMD_TEST(stub_measurer_marks_whitespace) {
    StubMeasurer ms;
    auto s = ms.measure(U"a b", 16.0f, InlineStyle::plain());
    ELMD_CHECK(!s.glyphs[0].is_whitespace);
    ELMD_CHECK(s.glyphs[1].is_whitespace);
}

ELMD_TEST(advance_total_matches_width) {
    StubMeasurer ms(7.0f);
    auto s = ms.measure(U"hello", 16.0f, InlineStyle::plain());
    ELMD_CHECK(std::fabs(s.advance_total() - s.width) < 0.001f);
}

ELMD_TEST(selection_geometry_runs) {
    StubMeasurer ms;
    LayoutTree t;
    LayoutBlock b(NodeId(0), CharRange(CharOffset(0), CharOffset(5)), {LayoutBlockKind::Paragraph}, BlockStyle::paragraph());
    TextLineLayout ll{CharRange(CharOffset(0), CharOffset(5))};
    ll.rect = LogicalRect(0, 0, 100, 20); ll.baseline = 16;
    GlyphRunLayout r; r.source_range = CharRange(CharOffset(0), CharOffset(5));
    r.text = U"hello"; r.origin = LogicalPoint(0, 0); r.width = 50; r.marker_visibility = MarkerVisibility::Always;
    for (std::size_t i = 0; i < 5; ++i) { GlyphInfo g; g.advance = 10; g.char_index = i; r.glyphs.push_back(g); }
    ll.runs.push_back(std::move(r));
    LayoutItem li; li.kind = LayoutItem::Kind::Line; li.line = std::move(ll);
    b.children.push_back(std::move(li));
    b.rect = LogicalRect(0, 0, 100, 20);
    t.blocks.push_back(std::move(b));
    auto sg = compute_selection_geometry(t, CharRange(CharOffset(1), CharOffset(4)));
    ELMD_CHECK(!sg.ranges.empty());
}

ELMD_TEST(caret_geometry_at_position) {
    StubMeasurer ms;
    LayoutTree t;
    LayoutBlock b(NodeId(0), CharRange(CharOffset(0), CharOffset(5)), {LayoutBlockKind::Paragraph}, BlockStyle::paragraph());
    TextLineLayout ll{CharRange(CharOffset(0), CharOffset(5))};
    ll.rect = LogicalRect(0, 0, 100, 20); ll.baseline = 16;
    GlyphRunLayout r; r.source_range = CharRange(CharOffset(0), CharOffset(5));
    r.text = U"hello"; r.origin = LogicalPoint(0, 0); r.width = 50; r.marker_visibility = MarkerVisibility::Always;
    for (std::size_t i = 0; i < 5; ++i) { GlyphInfo g; g.advance = 10; g.char_index = i; r.glyphs.push_back(g); }
    ll.runs.push_back(std::move(r));
    LayoutItem li; li.kind = LayoutItem::Kind::Line; li.line = std::move(ll);
    b.children.push_back(std::move(li));
    b.rect = LogicalRect(0, 0, 100, 20);
    t.blocks.push_back(std::move(b));
    auto cg = compute_caret_geometry(t, CharOffset(3));
    ELMD_CHECK(cg.has_value());
}
