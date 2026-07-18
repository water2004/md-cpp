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

suite render_footnote_html_tests = [] {

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
    auto definition_at = std::ranges::find_if(model.blocks, [](auto const& block) {
        return block.kind == RenderBlockKind::Footnote;
    });
    expect(fatal(bool(definition_at != model.blocks.end())));
    if (definition_at == model.blocks.end()) return;
    auto const& definition = *definition_at;
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
    auto definition_at = std::ranges::find_if(model.blocks, [](auto const& block) {
        return block.kind == RenderBlockKind::Footnote;
    });
    expect(fatal(bool(definition_at != model.blocks.end())));
    if (definition_at == model.blocks.end()) return;
    auto const& definition = *definition_at;
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
    std::vector<std::reference_wrapper<const RenderBlock>> definitions;
    for (auto const& block : model.blocks) {
        if (block.kind == RenderBlockKind::Footnote) definitions.emplace_back(block);
    }
    expect(fatal(bool(definitions.size() == 2u)));
    if (definitions.size() != 2u) return;
    expect(fatal(bool(definition_label(definitions[0]) == U"2. ")));
    expect(fatal(bool(definition_label(definitions[1]) == U"1. ")));
};

"inline_html_presentation_reaches_the_render_model"_test = [] {
    auto model = build_model(
        "<span style=\"color:#123456; background-color:rgba(10,20,30,.5); "
        "font-family:'Segoe UI'; font-size:20px\">"
        "<strong style=\"font-weight:normal\">plain</strong>"
        "<em><u>styled</u></em></span>\n");
    const auto* plain = find_text_item(model, U"plain");
    const auto* styled = find_text_item(model, U"styled");
    expect(fatal(bool(plain != nullptr)));
    expect(fatal(bool(styled != nullptr)));
    if (plain) {
        expect(!plain->style.bold);
        expect(fatal(bool(plain->style.presentation != nullptr)));
        if (plain->style.presentation) {
            expect(plain->style.presentation->foreground
                == std::optional<Color>{Color(0x12, 0x34, 0x56)});
            expect(plain->style.presentation->background
                == std::optional<Color>{Color(10, 20, 30, 128)});
            expect(plain->style.presentation->font_family
                == std::optional<std::string>{"Segoe UI"});
            expect(plain->style.presentation->absolute_font_size
                == std::optional<float>{20.0f});
        }
    }
    if (styled) {
        expect(styled->style.italic);
        expect(styled->style.underline);
        expect(fatal(bool(styled->style.presentation != nullptr)));
    }
};

"inline_html_styles_work_in_every_markdown_container"_test = [] {
    const std::vector<std::string> sources{
        "<span style='color:red'>paragraph</span>\n",
        "## <span style='color:red'>heading</span>\n",
        "> <span style='color:red'>quote</span>\n",
        "- <span style='color:red'>list</span>\n",
        "| <span style='color:red'>cell</span> |\n| --- |\n",
        "> [!NOTE]\n> <span style='color:red'>callout</span>\n",
        "body[^a]\n\n[^a]: <span style='color:red'>footnote</span>\n",
    };
    const std::vector<std::u32string> labels{
        U"paragraph", U"heading", U"quote", U"list", U"cell", U"callout", U"footnote"};
    for (std::size_t index = 0; index < sources.size(); ++index) {
        auto model = build_model(sources[index]);
        const auto* item = find_text_item(model, labels[index]);
        expect(fatal(bool(item != nullptr))) << "container=" << index;
        if (!item) continue;
        expect(fatal(bool(item->style.presentation != nullptr))) << "container=" << index;
        if (item->style.presentation) {
            expect(item->style.presentation->foreground
                == std::optional<Color>{Color(255, 0, 0)}) << "container=" << index;
        }
    }
};

}; // suite render_footnote_html_tests
