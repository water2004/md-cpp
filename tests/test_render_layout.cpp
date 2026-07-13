#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "elmd_test.hpp"
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
using namespace boost::ut;

static RenderModel build_model(const std::string& src) {
    auto out = parse_text(1, src);
    return build_render_model(out.document, out.outline);
}


suite render_layout_tests = [] {

"list_nested_quote_keeps_structural_layout_metadata"_test = [] {
    auto model = build_model("- item\n  > quoted");
    expect(fatal(bool(model.blocks.size() == 1u)));
    expect(fatal(bool(model.blocks.front().kind == RenderBlockKind::Text)));
    auto quote = std::find_if(model.blocks.front().child_blocks.begin(), model.blocks.front().child_blocks.end(), [](auto const& child) {
        return child.kind == RenderBlockKind::Quote;
    });
    expect(fatal(bool(quote != model.blocks.front().child_blocks.end())));
    if (quote != model.blocks.front().child_blocks.end()) {
        expect(fatal(bool(quote->container_indent_columns == 2u)));
        expect(fatal(bool(quote->container_marker_owner_id.v != 0u)));
        expect(fatal(bool(!quote->child_blocks.empty())));
        auto marker = std::find_if(model.blocks.front().inline_items.begin(), model.blocks.front().inline_items.end(), [&](auto const& item) {
            return item.marker_role == MarkerRole::ListBullet
                && item.source_span.container_id == quote->container_marker_owner_id;
        });
        expect(fatal(bool(marker != model.blocks.front().inline_items.end())));
    }
    auto indent = std::find_if(model.blocks.front().inline_items.begin(), model.blocks.front().inline_items.end(), [](auto const& item) {
        return item.marker_role == MarkerRole::Structural && !item.display_text.empty()
            && std::all_of(item.display_text.begin(), item.display_text.end(), [](char32_t value) { return value == U' '; });
    });
    expect(fatal(bool(indent != model.blocks.front().inline_items.end())));
};

"test_empty_document_yields_editable_blank_model"_test = [] {
    EditorDocument doc = EditorDocument::empty(1);
    Outline o = Outline::empty(1);
    auto m = build_render_model(doc, o);
    expect(fatal(bool((m.blocks.size()) == (1u))));
    expect(fatal(bool(m.blocks[0].kind == RenderBlockKind::Blank)));
    expect(fatal(bool((m.revision) == (1ull))));
};

"builds_heading_with_marker_and_text"_test = [] {
    auto m = build_model("# Title");
    expect(fatal(bool(!m.blocks.empty())));
    for (const auto& it : m.blocks[0].inline_items)
        expect(fatal(bool(it.source_span.source_range.end >= it.source_span.source_range.start)));
    bool has_text = false;
    for (const auto& it : m.blocks[0].inline_items) if (it.kind == InlineRenderItem::Kind::Text) has_text = true;
    expect(fatal(bool(has_text)));
};

"builds_setext_heading_with_a_structural_trailing_marker"_test = [] {
    auto m = build_model("Section\n-------\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    bool headingText = false;
    bool underline = false;
    if (!m.blocks.empty()) {
        expect(fatal(bool(m.blocks[0].kind == RenderBlockKind::Text)));
        for (auto const& item : m.blocks[0].inline_items) {
            if (item.kind == InlineRenderItem::Kind::Text && item.style.heading_level && *item.style.heading_level == 2) headingText = true;
            if (item.kind == InlineRenderItem::Kind::Marker && item.marker_role == MarkerRole::Heading && item.text == U"\n-------") underline = true;
        }
    }
    expect(fatal(bool(headingText)));
    expect(fatal(bool(underline)));
};

"atx_heading_link_reaches_the_render_model_as_a_link"_test = [] {
    auto m = build_model("### [#](https://example.com)可做转义的字符\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    if (!m.blocks.empty()) expect(fatal(bool(std::any_of(m.blocks[0].inline_items.begin(), m.blocks[0].inline_items.end(), [](auto const& item) {
            return item.kind == InlineRenderItem::Kind::Link && item.href == "https://example.com";
        }))));
};

"builds_strong_with_open_and_close_markers"_test = [] {
    auto m = build_model("**bold**");
    int marker_count = 0;
    for (const auto& it : m.blocks[0].inline_items) {
        if (it.kind == InlineRenderItem::Kind::Marker && it.text == U"**") ++marker_count;
    }
    expect(fatal(bool((marker_count) == (2))));
};

"render_markers_are_exact_source_delimiters"_test = [] {
    struct Case {
        std::string source;
        std::u32string marker;
    };
    const std::vector<Case> cases{
        {"*word*", U"*"},
        {"_word_", U"_"},
        {"**word**", U"**"},
        {"__word__", U"__"},
        {"~~word~~", U"~~"},
    };
    for (auto const& test : cases) {
        auto model = build_model(test.source);
        expect(fatal(bool((model.blocks.size()) == (1u))));
        if (model.blocks.empty()) continue;
        std::vector<InlineRenderItem const*> markers;
        for (auto const& item : model.blocks.front().inline_items) {
            if (item.kind == InlineRenderItem::Kind::Marker) markers.push_back(&item);
        }
        expect(fatal(bool((markers.size()) == (2u))));
        if (markers.size() != 2) continue;
        expect(fatal(bool((markers[0]->text) == (test.marker))));
        expect(fatal(bool((markers[1]->text) == (test.marker))));
        expect(fatal(bool((markers[0]->source_span.source_range.start) == (0u))));
        expect(fatal(bool((markers[0]->source_span.source_range.end) == (test.marker.size()))));
        expect(fatal(bool((markers[1]->source_span.source_range.start) == (test.source.size() - test.marker.size()))));
        expect(fatal(bool((markers[1]->source_span.source_range.end) == (test.source.size()))));
        expect(fatal(bool(markers[0]->marker_owner.has_value())));
        expect(fatal(bool(markers[1]->marker_owner == markers[0]->marker_owner)));
    }
};

"render_nested_delimiters_keep_monotonic_exact_ranges"_test = [] {
    auto model = build_model("_outer **inner** tail_");
    expect(fatal(bool((model.blocks.size()) == (1u))));
    if (model.blocks.empty()) return;
    std::vector<InlineRenderItem const*> markers;
    for (auto const& item : model.blocks.front().inline_items) {
        if (item.kind == InlineRenderItem::Kind::Marker) markers.push_back(&item);
    }
    expect(fatal(bool((markers.size()) == (4u))));
    if (markers.size() == 4) {
        expect(fatal(bool((markers[0]->text) == (std::u32string(U"_")))));
        expect(fatal(bool((markers[0]->source_span.source_range.start) == (0u))));
        expect(fatal(bool((markers[1]->text) == (std::u32string(U"**")))));
        expect(fatal(bool((markers[1]->source_span.source_range.start) == (7u))));
        expect(fatal(bool((markers[2]->text) == (std::u32string(U"**")))));
        expect(fatal(bool((markers[2]->source_span.source_range.start) == (14u))));
        expect(fatal(bool((markers[3]->text) == (std::u32string(U"_")))));
        expect(fatal(bool((markers[3]->source_span.source_range.start) == (21u))));
    }
    auto const& items = model.blocks.front().inline_items;
    for (std::size_t index = 1; index < items.size(); ++index) {
        if (items[index - 1].source_span.container_id == items[index].source_span.container_id) {
            expect(fatal(bool(items[index - 1].source_span.source_range.end <= items[index].source_span.source_range.start)));
        }
    }
};

"builds_code_block"_test = [] {
    auto m = build_model("```rust\nfn main() {}\n```");
    expect(fatal(bool(!m.blocks.empty())));
    expect(fatal(bool(m.blocks[0].kind == RenderBlockKind::Code)));
    expect(fatal(bool(m.blocks[0].language && *m.blocks[0].language == "rust")));
    expect(fatal(bool(m.blocks[0].line_count >= 1)));
};

"builds_indented_code_block_with_local_code_source"_test = [] {
    auto m = build_model("    alpha\n    beta\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    expect(fatal(bool(m.blocks[0].kind == RenderBlockKind::Code)));
    expect(fatal(bool(m.blocks[0].code_indented)));
    expect(fatal(bool((m.blocks[0].code_text) == (std::u32string(U"alpha\nbeta\n")))));
    expect(fatal(bool((m.blocks[0].source_span.source_range.start) == (0u))));
    expect(fatal(bool((m.blocks[0].source_span.source_range.end) == (m.blocks[0].code_text.size()))));
};

"list_render_model_retains_nested_code_block_identity"_test = [] {
    auto model = build_model(
        "1.  Open the file.\n"
        "\n"
        "2.  Find the following code block on line 21:\n"
        "\n"
        "        <html>\n"
        "          <head>\n"
        "            <title>Test</title>\n"
        "          </head>\n"
        "\n"
        "3.  Update the title to match the name of your website.\n");
    expect(fatal(bool((model.blocks.size()) == (1u))));
    if (model.blocks.empty()) return;
    auto const& list = model.blocks.front();
    expect(fatal(bool(list.kind == RenderBlockKind::Text)));
    auto code = std::find_if(list.child_blocks.begin(), list.child_blocks.end(), [](auto const& child) {
        return child.kind == RenderBlockKind::Code;
    });
    expect(fatal(bool(code != list.child_blocks.end())));
    if (code != list.child_blocks.end()) {
        expect(fatal(bool(code->code_indented)));
        expect(fatal(bool((code->code_text) == (std::u32string(U"<html>\n  <head>\n    <title>Test</title>\n  </head>\n")))));
        expect(fatal(bool((code->source_span.source_range.end) == (code->code_text.size()))));
    }
    bool styled_code = false;
    for (auto const& item : list.inline_items) {
        if (item.kind == InlineRenderItem::Kind::Text && item.style.code) styled_code = true;
    }
    expect(fatal(bool(styled_code)));
};

"list_render_model_retains_nested_quote_identity_and_indent"_test = [] {
    auto m = build_model("- first\n- second\n  > quoted\n- third\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    if (m.blocks.empty()) return;
    auto const& list = m.blocks[0];
    auto quote = std::find_if(list.child_blocks.begin(), list.child_blocks.end(), [](auto const& child) {
        return child.kind == RenderBlockKind::Quote;
    });
    expect(fatal(bool(quote != list.child_blocks.end())));
    if (quote != list.child_blocks.end()) {
        expect(fatal(bool((quote->container_depth) == (1u))));
        expect(fatal(bool((quote->container_indent_columns) == (2u))));
        expect(fatal(bool(quote->container_marker_owner_id.v != 0u)));
    }
};

"nested_list_markers_use_semantic_depth"_test = [] {
    auto m = build_model("1. first\n2. second\n   - nested\n   - nested again\n3. third\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    if (m.blocks.empty()) return;
    std::vector<std::u32string> bullets;
    for (auto const& item : m.blocks[0].inline_items) {
        if (item.marker_role == MarkerRole::ListBullet) bullets.push_back(item.display_text);
    }
    expect(fatal(bool((bullets.size()) == (2u))));
    if (bullets.size() == 2) {
        expect(fatal(bool((bullets[0]) == (U"   \u2022 "))));
        expect(fatal(bool((bullets[1]) == (U"   \u2022 "))));
    }
};

"thematic_break_remains_an_atomic_render_block"_test = [] {
    auto model = build_model("---");
    expect(fatal(bool((model.blocks.size()) == (1u))));
    expect(fatal(bool(model.blocks[0].kind == RenderBlockKind::ThematicBreak)));
    expect(fatal(bool(model.blocks[0].inline_items.empty())));
    expect(fatal(bool((model.blocks[0].source_span.source_range.start) == (0u))));
    expect(fatal(bool((model.blocks[0].source_span.source_range) == (SourceRange{0, 1}))));
    expect(fatal(bool((model.blocks[0].content_span.source_range) == (SourceRange{0, 1}))));
    StubMeasurer measurer(8.0f);
    auto layout = layout_blocks(model.blocks, 800.0f, 1.0f, measurer, std::nullopt, LogicalPoint(0, 0), Outline::empty(1));
    expect(fatal(bool((layout.blocks.size()) == (1u))));
    expect(fatal(bool(layout.blocks[0].kind.kind == LayoutBlockKind::ThematicBreak)));
    expect(fatal(bool((layout.blocks[0].source_span.source_range) == (SourceRange{0, 1}))));
};

"builds_inline_math"_test = [] {
    auto m = build_model("$x^2$");
    expect(fatal(bool(!m.blocks.empty())));
    bool has_math = false;
    for (const auto& it : m.blocks[0].inline_items)
        if (it.kind == InlineRenderItem::Kind::Math) has_math = true;
    expect(fatal(bool(has_math)));
};

"inline_math_only_line_preserves_full_visual_source_range"_test = [] {
    auto m = build_model("$11111$");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    expect(fatal(bool((m.blocks[0].source_span.source_range.start) == (0u))));
    expect(fatal(bool((m.blocks[0].source_span.source_range.end) == (7u))));
    expect(fatal(bool((m.blocks[0].content_span.source_range.start) == (0u))));
    expect(fatal(bool((m.blocks[0].content_span.source_range.end) == (7u))));
    expect(fatal(bool((m.blocks[0].inline_items.size()) == (1u))));
    expect(fatal(bool(m.blocks[0].inline_items[0].kind == InlineRenderItem::Kind::Math)));
    expect(fatal(bool((m.blocks[0].inline_items[0].source_span.source_range.start) == (0u))));
    expect(fatal(bool((m.blocks[0].inline_items[0].source_span.source_range.end) == (7u))));
};

"inline_math_at_either_text_edge_preserves_paragraph_end"_test = [] {
    auto leading = build_model("$x$ tail");
    expect(fatal(bool((leading.blocks.size()) == (1u))));
    expect(fatal(bool((leading.blocks[0].content_span.source_range.start) == (0u))));
    expect(fatal(bool((leading.blocks[0].content_span.source_range.end) == (8u))));
    auto trailing = build_model("lead $x$");
    expect(fatal(bool((trailing.blocks.size()) == (1u))));
    expect(fatal(bool((trailing.blocks[0].content_span.source_range.start) == (0u))));
    expect(fatal(bool((trailing.blocks[0].content_span.source_range.end) == (8u))));
};

"builds_paren_inline_math_with_exact_source_range"_test = [] {
    auto m = build_model("\\(x\\) after");
    auto it = std::find_if(m.blocks[0].inline_items.begin(), m.blocks[0].inline_items.end(), [](auto const& item) {
        return item.kind == InlineRenderItem::Kind::Math;
    });
    expect(fatal(bool(it != m.blocks[0].inline_items.end())));
    expect(fatal(bool(it->math_delim == MathDelimiter::InlineParen)));
    expect(fatal(bool((it->source_span.source_range.start) == (0u))));
    expect(fatal(bool((it->source_span.source_range.end) == (5u))));
};

"builds_table_block"_test = [] {
    auto m = build_model("| A | B |\n|---|---|\n| 1 | 2 |\n");
    expect(fatal(bool(m.blocks[0].kind == RenderBlockKind::Table)));
    expect(fatal(bool(m.blocks[0].row_count >= 2)));
    expect(fatal(bool(m.blocks[0].column_count >= 2)));
};

"builds_safe_html_table_with_cell_ranges"_test = [] {
    auto m = build_model("<table><tr><th>A</th><th>B</th></tr><tr><td>1</td><td>2</td></tr></table>\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    expect(fatal(bool(m.blocks[0].kind == RenderBlockKind::Table)));
    expect(fatal(bool(m.blocks[0].table_header_row)));
    expect(fatal(bool((m.blocks[0].row_count) == (2u))));
    expect(fatal(bool((m.blocks[0].column_count) == (2u))));
    expect(fatal(bool((m.blocks[0].table_cell_spans.size()) == (4u))));
    for (auto const& range : m.blocks[0].table_cell_spans) expect(fatal(bool(range.source_range.end >= range.source_range.start)));
};

"builds_ordered_list_with_source_markers_and_line_breaks"_test = [] {
    auto m = build_model("9. alpha\n10. beta\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    std::u32string markers;
    for (auto const& item : m.blocks[0].inline_items) {
        if (item.kind == InlineRenderItem::Kind::Marker) markers += item.text;
    }
    expect(fatal(bool(markers.find(U"9. ") != std::u32string::npos)));
    expect(fatal(bool(markers.find(U"10. ") != std::u32string::npos)));
    expect(fatal(bool(markers.find(U"\n") != std::u32string::npos)));
};

"ordered_list_display_numbers_are_sequential"_test = [] {
    auto m = build_model("1. first\n8. second\n3. third\n");
    std::vector<std::u32string> source;
    std::vector<std::u32string> display;
    for (auto const& item : m.blocks[0].inline_items) {
        if (item.kind != InlineRenderItem::Kind::Marker || item.marker_role != MarkerRole::ListNumber) continue;
        source.push_back(item.text);
        display.push_back(item.display_text);
    }
    expect(fatal(bool((source.size()) == (3u))));
    expect(fatal(bool(source[0] == U"1. ")));
    expect(fatal(bool(source[1] == U"8. ")));
    expect(fatal(bool(source[2] == U"3. ")));
    expect(fatal(bool(display[0] == U"1. ")));
    expect(fatal(bool(display[1] == U"2. ")));
    expect(fatal(bool(display[2] == U"3. ")));
};

"unordered_list_preserves_source_markers_and_presents_bullets"_test = [] {
    auto m = build_model("- alpha\n* beta\n+ gamma\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    expect(fatal(bool((m.blocks[0].block_style.padding_left) == (20.0f))));
    std::vector<std::u32string> source_markers;
    std::vector<std::u32string> display_markers;
    for (auto const& item : m.blocks[0].inline_items) {
        if (item.kind != InlineRenderItem::Kind::Marker || item.marker_role != MarkerRole::ListBullet) continue;
        source_markers.push_back(item.text);
        display_markers.push_back(item.display_text);
        expect(fatal(bool(item.source_span.source_range.empty())));
    }
    expect(fatal(bool((source_markers.size()) == (3u))));
    expect(fatal(bool(source_markers[0] == U"- ")));
    expect(fatal(bool(source_markers[1] == U"* ")));
    expect(fatal(bool(source_markers[2] == U"+ ")));
    for (auto const& marker : display_markers) expect(fatal(bool(marker == U"\u2022 ")));
};

"list_inline_math_reaches_the_render_model_with_exact_ranges"_test = [] {
    auto m = build_model("- before $x^2$ after\n- next $y$\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    std::vector<TextSpan> math_ranges;
    for (auto const& item : m.blocks[0].inline_items) {
        if (item.kind == InlineRenderItem::Kind::Math) math_ranges.push_back(item.source_span);
    }
    expect(fatal(bool((math_ranges.size()) == (2u))));
    expect(fatal(bool((math_ranges[0].source_range.start) == (7u))));
    expect(fatal(bool((math_ranges[0].source_range.end) == (12u))));
    expect(fatal(bool((math_ranges[1].source_range.start) == (5u))));
    expect(fatal(bool((math_ranges[1].source_range.end) == (8u))));
    expect(fatal(bool(math_ranges[0].container_id != math_ranges[1].container_id)));
};

"nested_list_blocks_reach_the_render_model"_test = [] {
    auto m = build_model("- parent\n  - child\n    1. grandchild\n\n  > quoted\n\n  ![diagram](image.png)\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    std::u32string text;
    bool image = false;
    bool quote = false;
    for (auto const& item : m.blocks[0].inline_items) {
        if (item.kind == InlineRenderItem::Kind::Image) image = true;
        else if (item.kind == InlineRenderItem::Kind::Link) for (auto const& child : item.children) text += child.text;
        else text += item.text;
    }
    quote = std::any_of(m.blocks[0].child_blocks.begin(), m.blocks[0].child_blocks.end(), [](auto const& child) {
        return child.kind == RenderBlockKind::Quote;
    });
    expect(fatal(bool(text.find(U"parent") != std::u32string::npos)));
    expect(fatal(bool(text.find(U"child") != std::u32string::npos)));
    expect(fatal(bool(text.find(U"grandchild") != std::u32string::npos)));
    expect(fatal(bool(text.find(U"quoted") != std::u32string::npos)));
    expect(fatal(bool(image)));
    expect(fatal(bool(quote)));
};

"math_inside_emphasis_renders_without_inheriting_text_style"_test = [] {
    auto m = build_model("**before $x$ after** *\\(y\\)* ~~$z$~~\n- **$q$**\n");
    std::vector<InlineRenderItem> math;
    for (auto const& block : m.blocks) {
        for (auto const& item : block.inline_items) {
            if (item.kind == InlineRenderItem::Kind::Math) math.push_back(item);
        }
    }
    expect(fatal(bool((math.size()) == (4u))));
    for (auto const& item : math) {
        expect(fatal(bool(!item.style.bold)));
        expect(fatal(bool(!item.style.italic)));
    }
    expect(fatal(bool(!math[0].style.strikethrough)));
    expect(fatal(bool(!math[1].style.strikethrough)));
    expect(fatal(bool(math[2].style.strikethrough)));
    expect(fatal(bool(!math[3].style.strikethrough)));
    bool bold_text = false;
    for (auto const& item : m.blocks[0].inline_items) {
        if (item.kind == InlineRenderItem::Kind::Text && item.style.bold) bold_text = true;
    }
    expect(fatal(bool(bold_text)));
};

"table_render_cells_keep_their_own_source_ranges"_test = [] {
    auto m = build_model("| A | B |\n| :--- | ---: |\n| 1 | 2 |\n");
    expect(fatal(bool(m.blocks[0].kind == RenderBlockKind::Table)));
    expect(fatal(bool((m.blocks[0].table_cells.size()) == (4u))));
    expect(fatal(bool(!m.blocks[0].table_cells[0].empty())));
    expect(fatal(bool(!m.blocks[0].table_cells[1].empty())));
    if (!m.blocks[0].table_cells[0].empty() && !m.blocks[0].table_cells[1].empty()) {
        expect(fatal(bool((m.blocks[0].table_cells[0][0].source_span.source_range.start) == (0u))));
        expect(fatal(bool((m.blocks[0].table_cells[1][0].source_span.source_range.start) == (0u))));
        expect(fatal(bool(m.blocks[0].table_cells[0][0].source_span.container_id != m.blocks[0].table_cells[1][0].source_span.container_id)));
    }
};

"table_render_cells_preserve_inline_styles"_test = [] {
    auto m = build_model("| **bold** | plain |\n| --- | --- |\n| `code` | ~~gone~~ |\n");
    expect(fatal(bool(m.blocks[0].kind == RenderBlockKind::Table)));
    expect(fatal(bool((m.blocks[0].table_cells.size()) == (4u))));
    bool bold = false;
    bool code = false;
    bool strike = false;
    for (auto const& item : m.blocks[0].table_cells[0]) bold = bold || item.style.bold;
    for (auto const& item : m.blocks[0].table_cells[2]) code = code || item.style.code;
    for (auto const& item : m.blocks[0].table_cells[3]) strike = strike || item.style.strikethrough;
    expect(fatal(bool(bold)));
    expect(fatal(bool(code)));
    expect(fatal(bool(strike)));
};

"blockquote_render_model_contains_text_and_quote_style"_test = [] {
    auto m = build_model("> quoted text\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    expect(fatal(bool(m.blocks[0].kind == RenderBlockKind::Quote)));
    expect(fatal(bool((m.blocks[0].child_blocks.size()) == (1u))));
    expect(fatal(bool(m.blocks[0].block_style.border_left.has_value())));
    bool hasText = false;
    if (!m.blocks[0].child_blocks.empty()) {
        expect(fatal(bool(!m.blocks[0].child_blocks[0].inline_items.empty())));
        for (auto const& item : m.blocks[0].child_blocks[0].inline_items) {
            hasText = hasText || (item.kind == InlineRenderItem::Kind::Text && item.text == U"quoted text");
        }
    }
    expect(fatal(bool(hasText)));
};

"blockquote_render_model_preserves_nested_child_blocks"_test = [] {
    auto m = build_model("> paragraph\n> > nested\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    expect(fatal(bool(m.blocks[0].kind == RenderBlockKind::Quote)));
    expect(fatal(bool((m.blocks[0].child_blocks.size()) == (2u))));
    if (m.blocks[0].child_blocks.size() >= 2) {
        expect(fatal(bool(m.blocks[0].child_blocks[0].kind == RenderBlockKind::Text)));
        expect(fatal(bool(m.blocks[0].child_blocks[1].kind == RenderBlockKind::Text)));
        expect(fatal(bool((m.blocks[0].child_blocks[0].quote_depth) == (0u))));
        expect(fatal(bool((m.blocks[0].child_blocks[1].quote_depth) == (1u))));
    }
};

"nested_quote_flow_fragment_owns_only_its_visible_content"_test = [] {
    auto m = build_model("> > The Witch bade her clean the pots and kettles\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    expect(fatal(bool((m.blocks[0].child_blocks.size()) == (1u))));
    if (!m.blocks[0].child_blocks.empty()) {
        auto const& fragment = m.blocks[0].child_blocks[0];
        expect(fatal(bool((fragment.quote_depth) == (1u))));
        expect(fatal(bool((fragment.content_span.source_range.start) == (0u))));
        expect(fatal(bool(!fragment.inline_items.empty())));
        if (!fragment.inline_items.empty()) {
            expect(fatal(bool((fragment.inline_items.front().source_span.source_range.start) == (0u))));
            expect(fatal(bool(fragment.content_span.source_range.end == fragment.inline_items.back().source_span.source_range.end)));
        }
    }
};

"core_quote_layout_consumes_flat_fragment_depths"_test = [] {
    StubMeasurer measurer(8.0f);
    auto model = build_model("> outer\n> > nested\n");
    auto tree = layout_blocks(model.blocks, 600.0f, 1.0f, measurer, std::nullopt, LogicalPoint(0.0f, 0.0f), Outline::empty(1));
    expect(fatal(bool((tree.blocks.size()) == (1u))));
    expect(fatal(bool(tree.blocks[0].kind.kind == LayoutBlockKind::Quote)));
    expect(fatal(bool((tree.blocks[0].children.size()) == (2u))));
    if (tree.blocks[0].children.size() >= 2) {
        expect(fatal(bool(tree.blocks[0].children[0].kind == LayoutItem::Kind::Line)));
        expect(fatal(bool(tree.blocks[0].children[1].kind == LayoutItem::Kind::Line)));
        expect(fatal(bool(tree.blocks[0].children[1].line.rect.x > tree.blocks[0].children[0].line.rect.x)));
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
    expect(fatal(bool((m.blocks[0].child_blocks.size()) == (3u))));
    if (!m.blocks[0].child_blocks.empty()) {
        auto const& items = m.blocks[0].child_blocks[0].inline_items;
        expect(fatal(bool(!items.empty())));
        expect(fatal(bool(items.back().text == U"\n")));
        expect(fatal(bool((items.back().source_span.source_range.end) == (8u))));
    }
};

"blockquote_render_model_tracks_the_depth_of_a_trailing_empty_line"_test = [] {
    auto outerBlank = build_model("> > alpha\n> ");
    expect(fatal(bool((outerBlank.blocks.size()) == (1u))));
    expect(fatal(bool((outerBlank.blocks[0].child_blocks.size()) == (2u))));
    if (outerBlank.blocks[0].child_blocks.size() >= 2) {
        expect(fatal(bool(outerBlank.blocks[0].child_blocks[0].kind == RenderBlockKind::Text)));
        expect(fatal(bool((outerBlank.blocks[0].child_blocks[0].quote_depth) == (1u))));
        expect(fatal(bool(outerBlank.blocks[0].child_blocks[1].kind == RenderBlockKind::Blank)));
        expect(fatal(bool((outerBlank.blocks[0].child_blocks[1].quote_depth) == (0u))));
    }

    auto nestedBlank = build_model("> > alpha  \n> > ");
    expect(fatal(bool((nestedBlank.blocks.size()) == (1u))));
    expect(fatal(bool((nestedBlank.blocks[0].child_blocks.size()) == (2u))));
    if (nestedBlank.blocks[0].child_blocks.size() >= 2) {
        expect(fatal(bool(nestedBlank.blocks[0].child_blocks[0].kind == RenderBlockKind::Text)));
        expect(fatal(bool((nestedBlank.blocks[0].child_blocks[0].quote_depth) == (1u))));
        expect(fatal(bool(nestedBlank.blocks[0].child_blocks[1].kind == RenderBlockKind::Blank)));
        expect(fatal(bool((nestedBlank.blocks[0].child_blocks[1].quote_depth) == (1u))));
    }
};

"list_render_model_has_no_synthetic_trailing_newline"_test = [] {
    auto m = build_model("- one\n- two\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    expect(fatal(bool(!m.blocks[0].inline_items.empty())));
    if (!m.blocks[0].inline_items.empty()) expect(fatal(bool(m.blocks[0].inline_items.back().text != U"\n")));
};

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
    for (auto const& item : m.blocks[0].inline_items) if (item.kind == InlineRenderItem::Kind::Marker && item.marker_owner) markers.push_back(&item);
    expect(fatal(bool((markers.size()) == (2u))));
    if (markers.size() == 2) {
        expect(fatal(bool(markers[0]->text == U"<em>")));
        expect(fatal(bool(markers[1]->text == U"</em>")));
        expect(fatal(bool(markers[0]->marker_owner == markers[1]->marker_owner)));
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
    auto model = build_render_model(parsed.document, parsed.outline);
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

"render_model_preserves_link_children_and_footnote_label"_test = [] {
    auto model = build_model("See [site](https://example.com) and [^note].\n\n[^note]: source\n");
    expect(fatal(bool(!model.blocks.empty())));
    bool link = false;
    bool footnote = false;
    for (auto const& item : model.blocks.front().inline_items) {
        if (item.kind == InlineRenderItem::Kind::Link) {
            link = !item.children.empty() && item.href == "https://example.com";
        }
        if (item.kind == InlineRenderItem::Kind::Text && item.text == U"[^note]") footnote = true;
    }
    expect(fatal(bool(link)));
    expect(fatal(bool(footnote)));
};

}; // suite render_layout_tests
