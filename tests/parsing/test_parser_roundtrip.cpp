#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "support/folia_test.hpp"
import elmd.core.parser;
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.block_tree;
import elmd.core.document_text;
import elmd.core.image_dimension;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.serializer;
import elmd.core.utf;

using namespace elmd;
using namespace boost::ut;

#include "support/parser_test_support.hpp"

suite parser_roundtrip_tests = [] {

"save_reload_is_character_exact_for_unchanged_content"_test = [] {
    const std::string source =
        "# __heading__\n\n"
        "> *quote* &amp;\n\n"
        "| [title](<url>) | `code` |\n"
        "| --- | --- |\n"
        "| ~~value~~ | $x$ |";
    const auto first = parse_text(1, source);
    const auto saved = serialize_markdown_cps(first.document);
    expect(fatal(bool(saved == utf8_to_cps(source))));
    const auto second = parse_text(2, cps_to_utf8(saved));
    expect(fatal(bool(serialize_markdown_cps(second.document) == saved)));
};

"safe_html_table_is_recursive_semantic_and_character_exact"_test = [] {
    const std::string source =
        "<table data-name='寄存器'><tbody>"
        "<tr><td>寄存器</td><td>位宽/bit</td><td>助记符</td><td>汇编用法</td><td>类型</td></tr>"
        "<tr><td>0</td><td>32</td><td>%A0</td><td>临时寄存器 0</td><td>通用寄存器</td></tr>"
        "</tbody></table>";
    const auto parsed = parse_text(1, source);
    expect(fatal(parsed.document.root.children.size() == 1u));
    if (parsed.document.root.children.empty()) return;
    const auto& table = parsed.document.root.children.front();
    expect(fatal(table.kind == BlockKind::Table));
    expect(fatal(table.has_html_source()));
    expect(fatal(table.html_special().root_tag == "table"));
    expect(fatal(table.children.size() == 2u));
    expect(fatal(table.children.front().children.size() == 5u));
    expect(fatal(table.children.back().children.size() == 5u));
    expect(fatal(table.children.back().children[2].inline_content.source == U"%A0"));
    expect(fatal(serialize_markdown(parsed.document) == source));

    const auto reloaded = parse_text(2, serialize_markdown(parsed.document));
    expect(fatal(serialize_markdown(reloaded.document) == source));
};

"safe_html_containers_map_recursively_without_losing_wrappers"_test = [] {
    const std::string source =
        "<DIV class='box'><h2>Title</h2><blockquote><p>x <strong>y</strong></p>"
        "<ol start='3'><li>a</li><li><code>b</code></li></ol></blockquote></DIV>";
    const auto parsed = parse_text(1, source);
    expect(fatal(parsed.document.root.children.size() == 1u));
    if (parsed.document.root.children.empty()) return;
    const auto& container = parsed.document.root.children.front();
    expect(fatal(container.kind == BlockKind::HtmlContainer));
    expect(fatal(container.has_html_source()));
    expect(fatal(first_block(container.children, BlockKind::Heading) != nullptr));
    expect(fatal(first_block(container.children, BlockKind::BlockQuote) != nullptr));
    const auto* list = first_block(container.children, BlockKind::List);
    expect(fatal(list != nullptr));
    if (list) {
        expect(fatal(list->list_special().ordered));
        expect(fatal(list->list_special().start == 3u));
    }
    expect(fatal(serialize_markdown(parsed.document) == source));
};

"block_html_text_uses_html_not_markdown_inline_rules"_test = [] {
    const std::string source =
        "<div><p>*literal* $also literal$ <strong>bold</strong> "
        "<mark>marked</mark></p></div>";
    const auto parsed = parse_text(1, source);
    expect(fatal(parsed.document.root.children.size() == 1u));
    if (parsed.document.root.children.empty()) return;
    const auto* paragraph = first_block(
        parsed.document.root.children.front().children,
        BlockKind::Paragraph);
    expect(fatal(paragraph != nullptr));
    if (!paragraph) return;
    expect(paragraph->inline_content.syntax_mode == InlineSyntaxMode::HtmlText);
    expect(!inline_contains_kind(paragraph->inline_content, InlineCstKind::Emphasis));
    expect(!inline_contains_kind(paragraph->inline_content, InlineCstKind::InlineMath));
    expect(!inline_contains_kind(paragraph->inline_content, InlineCstKind::Strong));
    expect(inline_contains_kind(paragraph->inline_content, InlineCstKind::HtmlElement));
    expect(fatal(serialize_markdown(parsed.document) == source));
};

"unsafe_script_html_remains_inert_and_lossless"_test = [] {
    const std::string source = "<script>alert(1)</script>";
    const auto parsed = parse_text(1, source);
    expect(fatal(parsed.document.root.children.size() == 1u));
    if (parsed.document.root.children.empty()) return;
    expect(fatal(parsed.document.root.children.front().kind == BlockKind::UnsupportedMarkup));
    expect(fatal(serialize_markdown(parsed.document) == source));
};

"nested_unsafe_html_remains_inert_and_lossless"_test = [] {
    const std::string source = "<div><iframe src='x'></iframe></div>";
    const auto parsed = parse_text(1, source);
    expect(fatal(parsed.document.root.children.size() == 1u));
    if (parsed.document.root.children.empty()) return;
    expect(fatal(parsed.document.root.children.front().kind == BlockKind::UnsupportedMarkup));
    expect(fatal(serialize_markdown(parsed.document) == source));
};

"malformed_html_remains_inert_and_lossless"_test = [] {
    const std::string source = "<table><tr><td>value</tr>";
    const auto parsed = parse_text(1, source);
    expect(fatal(parsed.document.root.children.size() == 1u));
    if (parsed.document.root.children.empty()) return;
    expect(fatal(parsed.document.root.children.front().kind == BlockKind::UnsupportedMarkup));
    expect(fatal(serialize_markdown(parsed.document) == source));
};

"direct_block_separators_round_trip_exactly"_test = [] {
    const std::vector<std::string> sources{
        "\nvalue\n```",
        "value\n```cpp\nx\n```",
        "value\n\n```cpp\nx\n```",
        "$$\n$$\nnext",
        "$$\n$$\n\nnext",
        "# heading\n> quote",
        "# heading\n\n> quote",
        "one\n\n",
        "\n\n",
        "one\n\n\nthree",
    };
    for (const auto& source : sources) {
        const auto parsed = parse_text(1, source);
        const auto saved = serialize_markdown(parsed.document);
        expect(fatal(bool(saved == source))) << source;
        const auto reloaded = parse_text(2, saved);
        expect(fatal(bool(serialize_markdown(reloaded.document) == source))) << source;
    }
};

"peer block markers terminate list items without a blank separator"_test = [] {
    struct Case {
        std::string source;
        BlockKind following_kind;
    };
    const std::vector<Case> cases{
        {"1. item\n## next\n", BlockKind::Heading},
        {"- item\n> quote\n", BlockKind::BlockQuote},
        {"1. item\n```cpp\nvalue\n```\n", BlockKind::CodeBlock},
        {"- item\n$$\nx + y\n$$\n", BlockKind::MathBlock},
    };
    for (auto const& value : cases) {
        const auto parsed = parse_text(1, value.source);
        expect(fatal(parsed.document.root.children.size() == 2u)) << value.source;
        if (parsed.document.root.children.size() != 2) continue;
        expect(parsed.document.root.children[0].kind == BlockKind::List) << value.source;
        expect(parsed.document.root.children[1].kind == value.following_kind) << value.source;
        expect(fatal(serialize_markdown(parsed.document) == value.source)) << value.source;
    }
};

"list boundary detection preserves lazy text and genuinely nested blocks"_test = [] {
    const std::string lazy_source = "1. item\ncontinuation\n";
    const auto lazy = parse_text(1, lazy_source);
    expect(lazy.document.root.children.size() == 1u)
        << "lazy top-level blocks=" << lazy.document.root.children.size();
    if (!lazy.document.root.children.empty()) {
        expect(lazy.document.root.children.front().kind == BlockKind::List);
        expect(first_block(lazy.document.root.children.front().children, BlockKind::Heading) == nullptr);
    }

    const std::string nested_source = "1. item\n   ## nested\n   body\n";
    const auto nested = parse_text(1, nested_source);
    expect(nested.document.root.children.size() == 1u)
        << "nested top-level blocks=" << nested.document.root.children.size();
    if (!nested.document.root.children.empty()) {
        expect(nested.document.root.children.front().kind == BlockKind::List);
        expect(first_block(nested.document.root.children.front().children, BlockKind::Heading) != nullptr);
    }
};

"serialized_projection_maps_repeated_nested_sources_without_text_search"_test = [] {
    const std::vector<std::string> sources{
        "> same\n>\n> same",
        "[^q]: > same\n    >\n    > - same\n",
        "> [!note]  same  \n> same",
        "| same | same |\n| --- | --- |",
        "```text\nsame\n```",
        "same\n\nsame\n\nsame",
    };
    for (const auto& source : sources) {
        const auto parsed = parse_text(1, source);
        const auto projection = serialize_markdown_projection(parsed.document);
        expect(fatal(projection.text == utf8_to_cps(source))) << source;

        std::vector<std::size_t> nonemptyStarts;
        walk_blocks(parsed.document.root, [&](const BlockNode& block) {
            const auto* inlineSource = editable_inline_document(block);
            const auto* rawSource = editable_raw_block_source(block);
            const auto* localSource = inlineSource ? &inlineSource->source : rawSource;
            if (!localSource) return;
            const auto map = std::ranges::find(
                projection.source_maps, block.id, &SerializedSourceMap::container_id);
            expect(fatal(map != projection.source_maps.end()));
            if (map == projection.source_maps.end()) return;
            expect(fatal(map->source_to_serialized.size() == localSource->size() + 1));
            if (!localSource->empty()) nonemptyStarts.push_back(map->source_to_serialized.front());
            for (std::size_t offset = 0; offset < localSource->size(); ++offset) {
                expect(fatal(map->source_to_serialized[offset] < projection.text.size()));
                expect(fatal(projection.text[map->source_to_serialized[offset]] == (*localSource)[offset]));
                expect(fatal(map->source_to_serialized[offset] < map->source_to_serialized[offset + 1]));
                const auto serialized = serialized_offset_for_source_position(
                    projection, {block.id, offset, TextAffinity::Downstream});
                expect(fatal(serialized.has_value()));
                if (!serialized) continue;
                const auto restored = source_position_for_serialized_offset(
                    projection, *serialized, TextAffinity::Downstream);
                expect(fatal(restored.has_value()));
                if (!restored) continue;
                expect(fatal(restored->container_id == block.id));
                expect(fatal(restored->source_offset == offset));
            }
        });
        std::ranges::sort(nonemptyStarts);
        expect(fatal(std::ranges::adjacent_find(nonemptyStarts) == nonemptyStarts.end()));
    }
};

}; // suite parser_roundtrip_tests
