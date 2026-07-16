#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "elmd_test.hpp"
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

static RenderModel build_model(const std::string& src) {
    auto out = parse_text(1, src);
    return build_render_model(
        out.document,
        out.outline,
        out.symbols,
        default_theme_profile());
}

static const RenderBlock* find_render_block(const RenderBlock& root, RenderBlockKind kind) {
    if (root.kind == kind) return &root;
    for (auto const& child : root.child_blocks) {
        if (auto found = find_render_block(child, kind)) return found;
    }
    return nullptr;
}

static const RenderBlock* first_render_leaf(const RenderBlock& root) {
    if (root.child_blocks.empty()) return &root;
    for (auto const& child : root.child_blocks) {
        if (auto found = first_render_leaf(child)) return found;
    }
    return nullptr;
}

static const RenderBlock* find_render_block(const RenderBlock& root, NodeId id) {
    if (root.id == id) return &root;
    for (auto const& child : root.child_blocks) {
        if (auto found = find_render_block(child, id)) return found;
    }
    return nullptr;
}

static std::optional<std::size_t> flow_indent_for(
    const RenderBlock& root,
    NodeId id,
    std::size_t parent_indent = 0) {
    const auto indent = parent_indent + root.flow_local_indent_columns;
    if (root.id == id) return indent;
    for (auto const& child : root.child_blocks) {
        if (auto found = flow_indent_for(child, id, indent)) return found;
    }
    return std::nullopt;
}

static BlockNode make_render_block(BlockKind kind, std::uint64_t& next_id) {
    BlockNode block;
    block.id = NodeId{next_id++};
    block.kind = kind;
    return block;
}

static BlockNode make_render_text_block(BlockKind kind, std::u32string source, std::uint64_t& next_id) {
    auto block = make_render_block(kind, next_id);
    block.inline_content.source = std::move(source);
    InlineParseContext context;
    context.allocate_id = [&] { return NodeId{next_id++}; };
    reparse_inline_document(block.inline_content, context);
    return block;
}

static RenderModel build_model(BlockNode block, std::uint64_t next_id) {
    EditorDocument document;
    document.root.id = NodeId{next_id++};
    document.root.kind = BlockKind::Document;
    document.root.children.push_back(std::move(block));
    document.revision = 1;
    rebuild_document_block_index(document);
    const auto symbols = build_document_symbol_index(document);
    return build_render_model(
        document,
        Outline::empty(1),
        symbols,
        default_theme_profile());
}


suite render_layout_tests = [] {

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
    expect(fatal(bool(m.blocks[0].special().line_count >= 1)));
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
    expect(fatal(bool(displayed == code->special().code_text))) << "displayed literal source";
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
        expect(fatal(bool(flattened_code == code->special().code_text)));
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

"unified_flow_composes_list_quote_list_and_code"_test = [] {
    std::uint64_t next_id = 1;
    auto outer_text = make_render_text_block(BlockKind::Paragraph, U"outer", next_id);
    auto quoted_text = make_render_text_block(BlockKind::Paragraph, U"quoted", next_id);
    auto inner_text = make_render_text_block(BlockKind::Paragraph, U"inner", next_id);
    auto code = make_render_block(BlockKind::CodeBlock, next_id);
    code.block_source = make_block_source(U"    int x;\n", BlockSourceKind::IndentedCode);
    code.ensure_special().code_indented = true;
    auto code_id = code.id;

    auto inner_item = make_render_block(BlockKind::ListItem, next_id);
    inner_item.ensure_special().marker = U"1. ";
    inner_item.children.push_back(std::move(inner_text));
    inner_item.children.push_back(std::move(code));
    auto inner_list = make_render_block(BlockKind::List, next_id);
    inner_list.ensure_special().list_ordered = true;
    inner_list.children.push_back(std::move(inner_item));
    auto quote = make_render_block(BlockKind::BlockQuote, next_id);
    quote.children.push_back(std::move(quoted_text));
    quote.children.push_back(std::move(inner_list));
    auto quote_id = quote.id;
    auto outer_item = make_render_block(BlockKind::ListItem, next_id);
    outer_item.ensure_special().marker = U"- ";
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
    expect(fatal(bool(flattened_code == U"int x;\n")));
    expect(fatal(bool((code_indents) == (2u))));
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
    footnote.ensure_special().footnote_label = "n";
    footnote.children.push_back(std::move(quote));
    auto footnote_id = footnote.id;
    auto callout = make_render_block(BlockKind::Callout, next_id);
    callout.ensure_special().callout_kind = "NOTE";
    callout.children.push_back(std::move(footnote));
    auto callout_id = callout.id;
    auto task_item = make_render_block(BlockKind::TaskListItem, next_id);
    task_item.ensure_special().marker = U"- [ ] ";
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
        expect(fatal(bool(lines.size() == 3u)));
        if (lines.size() == 3u) {
            expect(fatal(bool(lines[0].line.source_span.source_range == SourceRange{7, 10})));
            expect(fatal(bool(lines[1].line.source_span.source_range == SourceRange{11, 14})));
            expect(fatal(bool(lines[2].line.source_span.source_range == SourceRange{15, 15})));
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

"render_model_preserves_link_children_and_footnote_label"_test = [] {
    auto model = build_model("See [site](https://example.com) and [^note].\n\n[^note]: source\n");
    expect(fatal(bool(!model.blocks.empty())));
    bool link = false;
    bool footnote = false;
    for (auto const& item : model.blocks.front().inline_items) {
        if (item.kind == InlineRenderItem::Kind::Link) {
            link = !item.special().semantic().children.empty()
                && item.special().semantic().href == "https://example.com";
        }
        if (item.kind == InlineRenderItem::Kind::FootnoteReference
            && item.special().semantic().footnote_label == "note"
            && item.special().semantic().source_text == U"[^note]") footnote = true;
    }
    expect(fatal(bool(link)));
    expect(fatal(bool(footnote)));
};

"footnote_definitions_are_numbered_item_blocks_without_generated_backlinks"_test = [] {
    auto model = build_model("text[^note]\n\n[^note]: source\n");
    expect(fatal(bool(model.blocks.size() == 2u)));
    if (model.blocks.size() != 2u) return;
    auto const& definition = model.blocks[1];
    expect(fatal(bool(definition.kind == RenderBlockKind::Footnote)));
    bool label = false;
    for (auto const& item : definition.inline_items) {
        if (item.kind != InlineRenderItem::Kind::Marker) continue;
        if (item.special().marker_role == MarkerRole::FootnoteLabel) {
            label = item.special().semantic().footnote_label == "note"
                && item.source_span.source_range.empty()
                && item.special().display_text == U"1. ";
        }
    }
    expect(fatal(label));
    expect(fatal(bool(definition.block_style.background.has_value())));
    expect(fatal(bool(definition.child_blocks.size() == 1u)));
};

"multiblock_footnote_uses_one_hanging_content_column"_test = [] {
    auto model = build_model(
        "text[^note]\n\n"
        "[^note]: first\n\n"
        "    second\n");
    expect(fatal(bool(model.blocks.size() == 2u)));
    if (model.blocks.size() != 2u) return;
    auto const& definition = model.blocks[1];
    expect(fatal(bool(definition.kind == RenderBlockKind::Footnote)));
    expect(fatal(bool(definition.child_blocks.size() == 2u)));

    std::u32string visible;
    for (auto const& item : definition.inline_items) {
        visible += item.special().display_text.empty() ? item.text : item.special().display_text;
    }
    auto const first = visible.find(U"first");
    auto const second = visible.find(U"second");
    expect(fatal(bool(first != std::u32string::npos)));
    expect(fatal(bool(second != std::u32string::npos)));
    if (first == std::u32string::npos || second == std::u32string::npos) return;
    auto const first_line = visible.rfind(U'\n', first);
    auto const second_line = visible.rfind(U'\n', second);
    auto const first_column = first_line == std::u32string::npos ? first : first - first_line - 1;
    auto const second_column = second_line == std::u32string::npos ? second : second - second_line - 1;
    expect(fatal(bool(first_column == 3u)));
    expect(fatal(bool(second_column == first_column)));
};

"footnote_numbers_follow_first_reference_order_and_reuse_repeated_labels"_test = [] {
    auto model = build_model(
        "first[^z] second[^a] repeat[^z]\n\n"
        "[^a]: A\n\n"
        "[^z]: Z\n");
    expect(fatal(bool(model.blocks.size() == 3u)));
    if (model.blocks.size() != 3u) return;

    std::vector<std::u32string> references;
    for (auto const& item : model.blocks[0].inline_items) {
        if (item.kind == InlineRenderItem::Kind::FootnoteReference) {
            references.push_back(item.special().display_text);
        }
    }
    expect(fatal(bool(references == std::vector<std::u32string>{U"1", U"2", U"1"})));

    auto definition_label = [](RenderBlock const& block) {
        for (auto const& item : block.inline_items) {
            if (item.special().marker_role == MarkerRole::FootnoteLabel) {
                return item.special().display_text;
            }
        }
        return std::u32string{};
    };
    expect(fatal(bool(definition_label(model.blocks[1]) == U"2. ")));
    expect(fatal(bool(definition_label(model.blocks[2]) == U"1. ")));
};

}; // suite render_layout_tests
