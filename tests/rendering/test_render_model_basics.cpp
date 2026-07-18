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

suite render_model_basic_tests = [] {

"list_nested_quote_keeps_structural_layout_metadata"_test = [] {
    auto model = build_model("- item\n  > quoted");
    expect(fatal(bool(model.blocks.size() == 1u)));
    expect(fatal(bool(model.blocks.front().kind == RenderBlockKind::Text)));
    auto quote = find_render_block(model.blocks.front(), RenderBlockKind::Quote);
    expect(fatal(bool(quote != nullptr)));
    if (quote) {
        expect(fatal(bool(flow_indent_for(model.blocks.front(), quote->id) == 2u)));
        expect(fatal(bool(!quote->child_blocks.empty())));
        auto marker = std::find_if(model.blocks.front().inline_items.begin(), model.blocks.front().inline_items.end(), [&](auto const& item) {
            return item.special().marker_role == MarkerRole::ListBullet && item.source_span.container_id.v != 0;
        });
        expect(fatal(bool(marker != model.blocks.front().inline_items.end())));
    }
    auto indent = std::find_if(model.blocks.front().inline_items.begin(), model.blocks.front().inline_items.end(), [](auto const& item) {
        return item.special().marker_role == MarkerRole::Structural && !item.special().display_text.empty()
            && std::all_of(item.special().display_text.begin(), item.special().display_text.end(), [](char32_t value) { return value == U' '; });
    });
    expect(fatal(bool(indent != model.blocks.front().inline_items.end())));
};

"quote_only_list_item_maps_its_marker_to_editable_content"_test = [] {
    auto model = build_model("- > quoted\n");
    expect(fatal(bool(model.blocks.size() == 1u)));
    if (model.blocks.empty()) return;
    auto const& list = model.blocks.front();
    auto quote = find_render_block(list, RenderBlockKind::Quote);
    expect(fatal(bool(quote != nullptr)));
    if (!quote) return;
    auto editable = first_render_leaf(*quote);
    expect(fatal(bool(editable != nullptr)));
    if (!editable) return;
    auto editableOwner = editable->source_span.container_id;
    auto marker = std::find_if(list.inline_items.begin(), list.inline_items.end(), [](auto const& item) {
        return item.special().marker_role == MarkerRole::ListBullet;
    });
    expect(fatal(bool(marker != list.inline_items.end())));
    if (marker != list.inline_items.end())
        expect(fatal(bool(marker->source_span.container_id == editableOwner)));
    auto indent = std::find_if(list.inline_items.begin(), list.inline_items.end(), [&](auto const& item) {
        return item.source_span.container_id == editableOwner
            && item.special().marker_role == MarkerRole::Structural
            && item.special().display_text == U"  ";
    });
    expect(fatal(bool(indent != list.inline_items.end())));
};

"test_empty_document_yields_editable_blank_model"_test = [] {
    EditorDocument doc = EditorDocument::empty(1);
    Outline o = Outline::empty(1);
    auto m = build_render_model(doc, o, DocumentSymbolIndex{}, default_theme_profile());
    expect(fatal(bool((m.blocks.size()) == (1u))));
    expect(fatal(bool(m.blocks[0].kind == RenderBlockKind::Blank)));
    expect(fatal(bool((m.revision) == (1ull))));
};

"builds_heading_with_marker_and_text"_test = [] {
    auto m = build_model("# Title");
    expect(fatal(bool(!m.blocks.empty())));
    bool has_opening = false;
    for (const auto& it : m.blocks[0].inline_items) {
        expect(fatal(bool(it.source_span.source_range.end >= it.source_span.source_range.start)));
        if (it.kind == InlineRenderItem::Kind::Marker && it.special().marker_role == MarkerRole::Heading) {
            has_opening = true;
            expect(fatal(bool(it.special().generated_boundary_affinity == TextAffinity::Downstream)));
        }
    }
    bool has_text = false;
    for (const auto& it : m.blocks[0].inline_items) if (it.kind == InlineRenderItem::Kind::Text) has_text = true;
    expect(fatal(bool(has_opening)));
    expect(fatal(bool(has_text)));
    expect(fatal(bool(m.blocks[0].text_heading_level == 1u)));
    expect(fatal(bool(m.blocks[0].estimated_characters >= 7u)));
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
            if (item.kind == InlineRenderItem::Kind::Marker && item.special().marker_role == MarkerRole::Heading && item.text == U"\n-------") {
                underline = true;
                expect(fatal(bool(item.special().generated_boundary_affinity == TextAffinity::Upstream)));
            }
        }
    }
    expect(fatal(bool(headingText)));
    expect(fatal(bool(underline)));
};

"atx_heading_link_reaches_the_render_model_as_a_link"_test = [] {
    auto m = build_model("### [#](https://example.com)可做转义的字符\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    if (!m.blocks.empty()) expect(fatal(bool(std::any_of(m.blocks[0].inline_items.begin(), m.blocks[0].inline_items.end(), [](auto const& item) {
            return item.kind == InlineRenderItem::Kind::Link
                && item.special().semantic().href == "https://example.com";
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
        expect(fatal(bool(markers[0]->special().marker_owner.has_value())));
        expect(fatal(bool(markers[1]->special().marker_owner == markers[0]->special().marker_owner)));
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
    expect(fatal(bool(m.blocks[0].special().language && *m.blocks[0].special().language == "rust")));
    expect(fatal(bool(m.blocks[0].special().line_count == 1u)));
};

"code_block_terminal_endings_do_not_create_phantom_visual_lines"_test = [] {
    struct Case {
        std::string source;
        std::size_t expected_lines;
        std::u32string expected_last_line;
    };
    const std::vector<Case> cases{
        {"```cpp\nvalue\n```", 1u, U"value"},
        {"```cpp\nvalue\n\n```", 2u, U""},
        {"```cpp\r\nvalue\r\n```", 1u, U"value"},
        {"```cpp\r\nvalue\r\n\r\n```", 2u, U""},
        {"```cpp\rvalue\r```", 1u, U"value"},
        {"```cpp\n```", 1u, U""},
        {"```cpp\nvalue", 1u, U"value"},
    };
    StubMeasurer measurer(6.0f);
    for (const auto& item : cases) {
        const auto model = build_model(item.source);
        expect(fatal(bool(model.blocks.size() == 1u)));
        if (model.blocks.empty()) continue;
        const auto& code = model.blocks.front();
        expect(fatal(bool(code.kind == RenderBlockKind::Code)));
        expect(fatal(bool(code.special().language == std::optional<std::string>{"cpp"})));
        expect(fatal(bool(code.special().line_count == item.expected_lines)));
        const auto layout = layout_blocks(
            model.blocks,
            800.0f,
            1.0f,
            measurer,
            std::nullopt,
            LogicalPoint(0, 0),
            Outline::empty(1));
        expect(fatal(bool(layout.blocks.size() == 1u)));
        if (layout.blocks.empty()) continue;
        expect(fatal(bool(layout.blocks.front().children.size() == item.expected_lines)));
        if (!layout.blocks.front().children.empty()) {
            const auto& line = layout.blocks.front().children.back().line;
            expect(fatal(bool(line.runs.size() == 1u)));
            if (!line.runs.empty()) {
                expect(fatal(bool(line.runs.front().text == item.expected_last_line)));
            }
        }
    }
};

"builds_indented_code_block_with_local_code_source"_test = [] {
    auto m = build_model("    **alpha**\n    _beta_ $gamma$\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    expect(fatal(bool(m.blocks[0].kind == RenderBlockKind::Code)));
    expect(fatal(bool(m.blocks[0].special().code_indented)));
    expect(fatal(bool((m.blocks[0].special().code_text) == (std::u32string(U"**alpha**\n_beta_ $gamma$\n")))));
    expect(fatal(bool(m.blocks[0].inline_items.empty())));
    expect(fatal(bool((m.blocks[0].source_span.source_range.start) == (0u))));
    expect(fatal(bool((m.blocks[0].source_span.source_range.end) == (m.blocks[0].special().raw_source.size()))));
};

"nested_indented_code_keeps_markdown_markers_literal"_test = [] {
    auto model = build_model(
        "1. item\n"
        "\n"
        "       **bold** _italic_ $math$\n");
    const auto* code = find_render_block(model.blocks.front(), RenderBlockKind::Code);
    expect(fatal(bool(code != nullptr))) << "nested code node";
    if (!code) return;
    expect(fatal(bool(code->special().code_indented))) << "indented code kind";
    auto literal = code->special().code_text;
    if (!literal.empty() && literal.back() == U'\n') literal.pop_back();
    expect(fatal(bool(literal == U"**bold** _italic_ $math$"))) << "literal code source";

    std::u32string displayed;
    for (const auto& item : model.blocks.front().inline_items) {
        if (item.source_span.container_id != code->id
            || item.kind != InlineRenderItem::Kind::Text) continue;
        displayed += item.text;
        expect(fatal(bool(item.style.code))) << "code style only";
        expect(fatal(bool(!item.style.bold && !item.style.italic
            && !item.style.link && !item.style.strikethrough))) << "no markdown inline style";
    }
    const auto presentation_lines = code_presentation_lines(code->special().code_text);
    const auto presentation_end = presentation_lines.empty()
        ? std::size_t{0}
        : presentation_lines.back().content_range.end;
    expect(fatal(bool(displayed == code->special().code_text.substr(0, presentation_end))))
        << "displayed literal source without a phantom terminal line";
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
    auto code = find_render_block(list, RenderBlockKind::Code);
    expect(fatal(bool(code != nullptr)));
    if (code) {
        const auto code_indent = flow_indent_for(list, code->id);
        expect(fatal(bool(code_indent.has_value())));
        expect(fatal(bool(code->special().code_indented)));
        expect(fatal(bool((code->special().code_text) == (std::u32string(U"<html>\n  <head>\n    <title>Test</title>\n  </head>\n")))));
        expect(fatal(bool((code->source_span.source_range.end) == (code->special().raw_source.size()))));
        std::size_t line_indents = 0;
        std::u32string flattened_code;
        for (auto const& item : list.inline_items) {
            if (item.source_span.container_id != code->id) continue;
            if (item.special().marker_role == MarkerRole::Structural && !item.special().display_text.empty()
                && std::all_of(item.special().display_text.begin(), item.special().display_text.end(), [](char32_t value) { return value == U' '; })) {
                ++line_indents;
                expect(fatal(bool(item.special().display_text.size() == code_indent.value_or(0) + 2u)));
            } else if (item.kind == InlineRenderItem::Kind::Text && item.style.code) {
                flattened_code += item.text;
            }
        }
        expect(fatal(bool(line_indents == code->special().line_count)));
        const auto presentation_lines = code_presentation_lines(code->special().code_text);
        const auto presentation_end = presentation_lines.empty()
            ? std::size_t{0}
            : presentation_lines.back().content_range.end;
        expect(fatal(bool(flattened_code == code->special().code_text.substr(0, presentation_end))));
        auto trailingBreak = std::find_if(list.inline_items.begin(), list.inline_items.end(), [&](auto const& item) {
            return item.special().marker_role == MarkerRole::Structural && item.text == U"\n"
                && item.source_span.container_id == code->id
                && item.source_span.source_range.start == code->content_span.source_range.end;
        });
        expect(fatal(bool(trailingBreak != list.inline_items.end())));
    }
    bool styled_code = false;
    for (auto const& item : list.inline_items) {
        if (item.kind == InlineRenderItem::Kind::Text && item.style.code) styled_code = true;
    }
    expect(fatal(bool(styled_code)));
};

"enter_at_nested_code_end_keeps_the_caret_on_list_content_indent"_test = [] {
    auto parsed = parse_text(1,
        "1. item\n"
        "\n"
        "        code\n"
        "\n"
        "2. next\n");
    BlockNode const* code = nullptr;
    walk_blocks(parsed.document.root, [&](BlockNode const& block) {
        if (!code && block.kind == BlockKind::CodeBlock) code = &block;
    });
    expect(fatal(bool(code != nullptr)));
    auto const content = code ? block_source_content(code->block_source) : std::u32string{};
    if (!code || content.empty() || content.back() != U'\n') return;
    auto const code_id = code->id;

    auto inserted = document_enter(parsed.document, TextSelection::caret({
        code_id,
        block_source_offset_for_content(code->block_source, content.size() - 1),
        TextAffinity::Downstream}));
    expect(fatal(bool(inserted.has_value())));
    if (!inserted) return;
    rebuild_document_block_index(parsed.document);
    expect(fatal(bool(inserted->selection_after.active.container_id == code_id)));
    auto model = build_render_model(
        parsed.document,
        Outline::empty(parsed.document.revision),
        build_document_symbol_index(parsed.document),
        default_theme_profile());
    expect(fatal(bool(model.blocks.size() == 1u)));
    if (model.blocks.empty()) return;
    auto caretIndent = std::find_if(model.blocks.front().inline_items.begin(), model.blocks.front().inline_items.end(), [&](auto const& item) {
        return item.special().marker_role == MarkerRole::Structural
            && item.source_span.container_id == inserted->selection_after.active.container_id
            && item.source_span.source_range.start == inserted->selection_after.active.source_offset
            && !item.special().display_text.empty()
            && std::all_of(item.special().display_text.begin(), item.special().display_text.end(), [](char32_t value) { return value == U' '; });
    });
    expect(fatal(bool(caretIndent != model.blocks.front().inline_items.end())));
};

"exiting_nested_code_maps_the_empty_paragraph_to_list_content_indent"_test = [] {
    auto parsed = parse_text(1,
        "1. item\n"
        "\n"
        "        code\n"
        "\n"
        "2. next\n");
    BlockNode const* code = nullptr;
    walk_blocks(parsed.document.root, [&](BlockNode const& block) {
        if (!code && block.kind == BlockKind::CodeBlock) code = &block;
    });
    expect(fatal(bool(code != nullptr)));
    if (!code) return;
    auto const code_id = code->id;

    auto exited = document_enter(parsed.document, TextSelection::caret({
        code_id,
        code->block_source.source().size(),
        TextAffinity::Downstream}));
    expect(fatal(bool(exited.has_value())));
    if (!exited) return;
    rebuild_document_block_index(parsed.document);
    expect(fatal(bool(exited->selection_after.active.container_id != code_id)));
    auto model = build_render_model(
        parsed.document,
        Outline::empty(parsed.document.revision),
        build_document_symbol_index(parsed.document),
        default_theme_profile());
    expect(fatal(bool(model.blocks.size() == 1u)));
    if (model.blocks.empty()) return;
    auto caretIndent = std::find_if(model.blocks.front().inline_items.begin(), model.blocks.front().inline_items.end(), [&](auto const& item) {
        return item.special().marker_role == MarkerRole::Structural
            && item.source_span.container_id == exited->selection_after.active.container_id
            && item.source_span.source_range.start == 0u
            && !item.special().display_text.empty()
            && std::all_of(item.special().display_text.begin(), item.special().display_text.end(), [](char32_t value) { return value == U' '; });
    });
    expect(fatal(bool(caretIndent != model.blocks.front().inline_items.end())));
};

"list_render_model_retains_nested_quote_identity_and_indent"_test = [] {
    auto m = build_model("- first\n- second\n  > quoted\n- third\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    if (m.blocks.empty()) return;
    auto const& list = m.blocks[0];
    auto quote = find_render_block(list, RenderBlockKind::Quote);
    expect(fatal(bool(quote != nullptr)));
    if (quote) {
        expect(fatal(bool(flow_indent_for(list, quote->id) == 2u)));
        expect(fatal(bool(!quote->child_blocks.empty())));
        if (!quote->child_blocks.empty()) {
            auto const quoteContent = quote->child_blocks.front().source_span;
            auto trailingBreak = std::find_if(list.inline_items.begin(), list.inline_items.end(), [&](auto const& item) {
                return item.special().marker_role == MarkerRole::Structural && item.text == U"\n"
                    && item.source_span.container_id == quoteContent.container_id
                    && item.source_span.source_range.start == quoteContent.source_range.end;
            });
            expect(fatal(bool(trailingBreak != list.inline_items.end())));
        }
    }
};

"nested_list_markers_use_semantic_depth"_test = [] {
    auto m = build_model("1. first\n2. second\n   - nested\n   - nested again\n3. third\n");
    expect(fatal(bool((m.blocks.size()) == (1u))));
    if (m.blocks.empty()) return;
    std::vector<std::u32string> bullets;
    for (auto const& item : m.blocks[0].inline_items) {
        if (item.special().marker_role == MarkerRole::ListBullet) {
            bullets.push_back(item.special().display_text);
        }
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

}; // suite render_model_basic_tests
