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

suite render_model_content_tests = [] {

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
    expect(fatal(bool(it->special().math_delim == MathDelimiter::InlineParen)));
    expect(fatal(bool((it->source_span.source_range.start) == (0u))));
    expect(fatal(bool((it->source_span.source_range.end) == (5u))));
};

"builds_table_block"_test = [] {
    auto m = build_model("| A | B |\n|---|---|\n| 1 | 2 |\n");
    expect(fatal(bool(m.blocks[0].kind == RenderBlockKind::Table)));
    expect(fatal(bool(m.blocks[0].special().row_count >= 2)));
    expect(fatal(bool(m.blocks[0].special().column_count >= 2)));
};

"builds_safe_html_table_with_cell_ranges"_test = [] {
    auto m = build_model("<table><tr><th>A</th><th>B</th></tr><tr><td>1</td><td>2</td></tr></table>\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    expect(fatal(bool(m.blocks[0].kind == RenderBlockKind::Table)));
    expect(fatal(bool(m.blocks[0].special().table_header_row)));
    expect(fatal(bool((m.blocks[0].special().row_count) == (2u))));
    expect(fatal(bool((m.blocks[0].special().column_count) == (2u))));
    expect(fatal(bool((m.blocks[0].special().table_cell_spans.size()) == (4u))));
    for (auto const& range : m.blocks[0].special().table_cell_spans) expect(fatal(bool(range.source_range.end >= range.source_range.start)));
};

"mixed_list_flow_does_not_promote_plain_text_to_nested_heading_style"_test = [] {
    auto model = build_model("1. item\n   ## nested heading\n   body\n");
    expect(fatal(bool(model.blocks.size() == 1u)));
    if (model.blocks.empty()) return;
    auto const& list = model.blocks.front();
    expect(fatal(bool(list.text_heading_level == 0u)));
    expect(fatal(bool(std::ranges::any_of(list.inline_items, [](auto const& item) {
        return item.style.heading_level && *item.style.heading_level == 2u;
    }))));
    expect(fatal(bool(std::ranges::any_of(list.inline_items, [](auto const& item) {
        auto const& text = item.special().display_text.empty() ? item.text : item.special().display_text;
        return !text.empty() && !item.style.heading_level;
    }))));
};

"identifier_underscores_stay_plain_and_following_heading_stays_top_level"_test = [] {
    auto model = build_model(
        "## 3.1 gtsi\n\n"
        "1. 位选通 0x1_fffff_ffff 共33bit\n"
        "   trig_out(FS17);\n"
        "## 3.2 next\n");
    expect(fatal(bool(model.blocks.size() == 3u)));
    if (model.blocks.size() != 3) return;
    expect(fatal(bool(model.blocks[0].text_heading_level == 2u)));
    expect(fatal(bool(model.blocks[1].text_heading_level == 0u)));
    expect(fatal(bool(model.blocks[2].text_heading_level == 2u)));
    expect(fatal(bool(std::ranges::none_of(model.blocks[1].inline_items, [](auto const& item) {
        return item.style.bold || item.style.italic || item.style.heading_level.has_value();
    }))));
};

"register_html_table_reaches_native_table_render_model"_test = [] {
    const std::string source =
        "<table><tr><td>寄存器</td><td>位宽/bit</td><td>助记符</td><td>汇编用法</td><td>类型</td></tr>"
        "<tr><td>0</td><td>32</td><td>%A0</td><td>临时寄存器 0</td><td>通用寄存器</td></tr>"
        "<tr><td>1</td><td>32</td><td>%A1</td><td>临时寄存器 1</td><td>通用寄存器</td></tr>"
        "<tr><td>2</td><td>32</td><td>%A2</td><td>临时寄存器 2</td><td>通用寄存器</td></tr>"
        "<tr><td>3</td><td>32</td><td>%A3</td><td>临时寄存器 3</td><td>通用寄存器</td></tr>"
        "<tr><td>4</td><td>32</td><td>%A4</td><td>临时寄存器 4</td><td>通用寄存器</td></tr>"
        "<tr><td>5</td><td>32</td><td>%A5</td><td>临时寄存器 5</td><td>通用寄存器</td></tr>"
        "<tr><td>6</td><td>32</td><td>%A6</td><td>临时寄存器 6</td><td>通用寄存器</td></tr>"
        "<tr><td>7</td><td>32</td><td>%A7</td><td>临时寄存器 7</td><td>通用寄存器</td></tr></table>";
    auto model = build_model(source);
    expect(fatal(model.blocks.size() == 1u));
    if (model.blocks.empty()) return;
    const auto& table = model.blocks.front();
    expect(fatal(table.kind == RenderBlockKind::Table));
    expect(fatal(table.special().row_count == 9u));
    expect(fatal(table.special().column_count == 5u));
    expect(fatal(table.special().table_cells.size() == 45u));
    expect(fatal(table.special().table_cell_spans.size() == 45u));
    expect(!table.special().table_header_row);

    auto afterParagraph = build_model("preceding paragraph\n" + source);
    expect(fatal(afterParagraph.blocks.size() == 2u));
    if (afterParagraph.blocks.size() == 2u) {
        expect(fatal(afterParagraph.blocks[0].kind == RenderBlockKind::Text));
        expect(fatal(afterParagraph.blocks[1].kind == RenderBlockKind::Table));
        expect(fatal(afterParagraph.blocks[1].special().row_count == 9u));
    }
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
        if (item.kind != InlineRenderItem::Kind::Marker
            || item.special().marker_role != MarkerRole::ListNumber) continue;
        source.push_back(item.text);
        display.push_back(item.special().display_text);
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
        if (item.kind != InlineRenderItem::Kind::Marker
            || item.special().marker_role != MarkerRole::ListBullet) continue;
        source_markers.push_back(item.text);
        display_markers.push_back(item.special().display_text);
        expect(fatal(bool(item.source_span.source_range.empty())));
    }
    expect(fatal(bool((source_markers.size()) == (3u))));
    expect(fatal(bool(source_markers[0] == U"- ")));
    expect(fatal(bool(source_markers[1] == U"* ")));
    expect(fatal(bool(source_markers[2] == U"+ ")));
    for (auto const& marker : display_markers) expect(fatal(bool(marker == U"\u2022 ")));
};

"task_list_markers_expose_checkbox_state_without_losing_source_spelling"_test = [] {
    auto model = build_model("- [ ] pending\n- [x] complete\n- [X] complete too\n");
    expect(fatal(bool(model.blocks.size() == 1u)));
    if (model.blocks.empty()) return;
    std::vector<InlineRenderItem const*> markers;
    for (auto const& item : model.blocks.front().inline_items) {
        if (item.kind == InlineRenderItem::Kind::Marker
            && item.special().marker_role == MarkerRole::TaskCheckbox) markers.push_back(&item);
    }
    expect(fatal(bool(markers.size() == 3u)));
    if (markers.size() != 3u) return;
    expect(fatal(bool(markers[0]->text == U"- [ ] ")));
    expect(fatal(bool(markers[1]->text == U"- [x] ")));
    expect(fatal(bool(markers[2]->text == U"- [X] ")));
    expect(fatal(bool(!markers[0]->special().task_checked)));
    expect(fatal(bool(markers[1]->special().task_checked)));
    expect(fatal(bool(markers[2]->special().task_checked)));
    for (auto const* marker : markers) {
        expect(fatal(bool(marker->source_span.source_range.empty())));
        expect(fatal(bool(marker->special().generated_boundary_affinity == TextAffinity::Downstream)));
    }
};

"nested_task_lists_keep_checkbox_projection_state_in_the_unified_flow"_test = [] {
    auto model = build_model("> - [x] parent\n>   - [ ] child\n");
    expect(fatal(bool(model.blocks.size() == 1u)));
    if (model.blocks.empty()) return;
    std::vector<bool> states;
    for (auto const& item : model.blocks.front().inline_items) {
        if (item.kind == InlineRenderItem::Kind::Marker
            && item.special().marker_role == MarkerRole::TaskCheckbox) {
            states.push_back(item.special().task_checked);
        }
    }
    expect(fatal(bool(states.size() == 2u)));
    if (states.size() == 2u) {
        expect(fatal(bool(states[0])));
        expect(fatal(bool(!states[1])));
    }
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
    bool block_image = false;
    bool quote = false;
    for (auto const& item : m.blocks[0].inline_items) {
        if (item.kind == InlineRenderItem::Kind::Image) {
            image = true;
            block_image = item.special().semantic().block_image;
        }
        else if (item.kind == InlineRenderItem::Kind::Link) {
            for (auto const& child : item.special().semantic().children) text += child.text;
        }
        else text += item.text;
    }
    quote = find_render_block(m.blocks[0], RenderBlockKind::Quote) != nullptr;
    expect(fatal(bool(text.find(U"parent") != std::u32string::npos)));
    expect(fatal(bool(text.find(U"child") != std::u32string::npos)));
    expect(fatal(bool(text.find(U"grandchild") != std::u32string::npos)));
    expect(fatal(bool(text.find(U"quoted") != std::u32string::npos)));
    expect(fatal(bool(image)));
    expect(fatal(bool(block_image)));
    expect(fatal(bool(quote)));
};

"inline_and_block_images_keep_distinct_flow_alignment"_test = [] {
    auto inline_model = build_model("before ![inline](inline.png) after");
    expect(fatal(bool(inline_model.blocks.size() == 1u)));
    auto inline_image = std::ranges::find(
        inline_model.blocks.front().inline_items,
        InlineRenderItem::Kind::Image,
        &InlineRenderItem::kind);
    expect(fatal(bool(inline_image != inline_model.blocks.front().inline_items.end())));
    if (inline_image != inline_model.blocks.front().inline_items.end())
        expect(fatal(bool(!inline_image->special().semantic().block_image)));

    auto nested_model = build_model("> - ![block](block.gif)");
    expect(fatal(bool(nested_model.blocks.size() == 1u)));
    auto nested_image = std::ranges::find(
        nested_model.blocks.front().inline_items,
        InlineRenderItem::Kind::Image,
        &InlineRenderItem::kind);
    expect(fatal(bool(nested_image != nested_model.blocks.front().inline_items.end())));
    if (nested_image != nested_model.blocks.front().inline_items.end())
        expect(fatal(bool(nested_image->special().semantic().block_image)));
};

"linked_html_images_keep_text_only_link_decoration"_test = [] {
    auto model = build_model(
        "<p align=\"center\">\n"
        "  <a href=\"https://example.com\">\n"
        "    <img src=\"badge.svg\">\n"
        "  </a>\n"
        "</p>\n\n"
        "[linked text](https://example.com)\n");

    bool linked_image = false;
    bool decorated_text = false;
    bool decorated_whitespace = false;
    auto visit_items = [&](auto&& self, const std::vector<InlineRenderItem>& items, bool inside_link) -> void {
        for (auto const& item : items) {
            if (item.kind == InlineRenderItem::Kind::Image && inside_link) {
                linked_image = true;
                expect(bool(!item.style.link));
            }
            if (item.kind == InlineRenderItem::Kind::Text && inside_link) {
                const auto whitespace = !item.text.empty() && std::ranges::all_of(item.text, [](char32_t ch) {
                    return ch == U' ' || ch == U'\t' || ch == U'\n' || ch == U'\r';
                });
                if (whitespace && item.style.link) decorated_whitespace = true;
                if (!whitespace && item.style.link) decorated_text = true;
            }
            if (item.kind == InlineRenderItem::Kind::Link) {
                self(self, item.special().semantic().children, true);
            }
        }
    };
    auto visit_block = [&](auto&& self, const RenderBlock& block) -> void {
        visit_items(visit_items, block.inline_items, false);
        for (auto const& child : block.child_blocks) self(self, child);
    };
    for (auto const& block : model.blocks) visit_block(visit_block, block);

    expect(fatal(bool(linked_image))) << "linked image reaches the render model";
    expect(fatal(bool(decorated_text))) << "link text keeps its decoration";
    expect(fatal(bool(!decorated_whitespace))) << "link indentation remains undecorated";
};

"html_paragraph_alignment_and_formatting_whitespace_project_semantically"_test = [] {
    auto text_model = build_model(
        "<p align=\"center\">\n"
        "  面向高并发场景的现代化外置登录与材质平台\n"
        "</p>");
    expect(fatal(text_model.blocks.size() == 1u));
    if (text_model.blocks.empty()) return;
    expect(fatal(text_model.blocks.front().block_style.text_alignment
        == std::optional<TextAlignment>{TextAlignment::Center}));
    expect(fatal(text_model.blocks.front().inline_items.size() == 1u));
    if (!text_model.blocks.front().inline_items.empty()) {
        expect(fatal(text_model.blocks.front().inline_items.front().text
            == U"面向高并发场景的现代化外置登录与材质平台"));
        expect(fatal(text_model.blocks.front().inline_items.front().source_span.source_range.start == 3u));
    }

    auto badges = build_model(
        "<p align=\"center\">\n"
        "  <a href='one'>\n    <img src='one.svg'>\n  </a>\n"
        "  <a href='two'>\n    <img src='two.svg'>\n  </a>\n"
        "  <img src='three.svg'>\n"
        "</p>");
    expect(fatal(badges.blocks.size() == 1u));
    if (badges.blocks.empty()) return;
    std::size_t links = 0;
    std::size_t images = 0;
    std::size_t separating_spaces = 0;
    for (const auto& item : badges.blocks.front().inline_items) {
        if (item.kind == InlineRenderItem::Kind::Link) {
            ++links;
            for (const auto& child : item.special().semantic().children) {
                if (child.kind == InlineRenderItem::Kind::Image) ++images;
                expect(!(child.kind == InlineRenderItem::Kind::Text
                    && child.text == U" ")) << "media-only anchors discard formatting indentation";
            }
        } else if (item.kind == InlineRenderItem::Kind::Image) {
            ++images;
        } else if (item.kind == InlineRenderItem::Kind::Text && item.text == U" ") {
            ++separating_spaces;
        }
    }
    expect(fatal(links == 2u));
    expect(fatal(images == 3u));
    expect(fatal(separating_spaces == 2u));
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
    expect(fatal(bool((m.blocks[0].special().table_cells.size()) == (4u))));
    expect(fatal(bool(!m.blocks[0].special().table_cells[0].empty())));
    expect(fatal(bool(!m.blocks[0].special().table_cells[1].empty())));
    if (!m.blocks[0].special().table_cells[0].empty() && !m.blocks[0].special().table_cells[1].empty()) {
        expect(fatal(bool((m.blocks[0].special().table_cells[0][0].source_span.source_range.start) == (0u))));
        expect(fatal(bool((m.blocks[0].special().table_cells[1][0].source_span.source_range.start) == (0u))));
        expect(fatal(bool(m.blocks[0].special().table_cells[0][0].source_span.container_id != m.blocks[0].special().table_cells[1][0].source_span.container_id)));
    }
};

"table_render_cells_preserve_inline_styles"_test = [] {
    auto m = build_model("| **bold** | plain |\n| --- | --- |\n| `code` | ~~gone~~ |\n");
    expect(fatal(bool(m.blocks[0].kind == RenderBlockKind::Table)));
    expect(fatal(bool((m.blocks[0].special().table_cells.size()) == (4u))));
    bool bold = false;
    bool code = false;
    bool strike = false;
    for (auto const& item : m.blocks[0].special().table_cells[0]) bold = bold || item.style.bold;
    for (auto const& item : m.blocks[0].special().table_cells[2]) code = code || item.style.code;
    for (auto const& item : m.blocks[0].special().table_cells[3]) strike = strike || item.style.strikethrough;
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
        expect(fatal(bool(m.blocks[0].child_blocks[1].kind == RenderBlockKind::Quote)));
        expect(fatal(bool((m.blocks[0].child_blocks[0].flow_local_indent_columns) == (2u))));
        expect(fatal(bool((m.blocks[0].child_blocks[1].flow_local_indent_columns) == (2u))));
        expect(fatal(bool((m.blocks[0].child_blocks[1].child_blocks.size()) == (1u))));
        if (!m.blocks[0].child_blocks[1].child_blocks.empty())
            expect(fatal(bool((m.blocks[0].child_blocks[1].child_blocks[0].flow_local_indent_columns) == (2u))));
    }
};

"display_math_keeps_an_explicit_block_container"_test = [] {
    auto model = build_model("$$\nx + y\n$$");
    expect(fatal(bool(model.blocks.size() == 1u)));
    expect(fatal(bool(model.blocks.front().kind == RenderBlockKind::Math)));
    expect(fatal(bool(model.blocks.front().block_style.background.has_value())));
};

}; // suite render_model_content_tests
