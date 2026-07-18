#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "support/folia_test.hpp"
import folia.core.parser;
import folia.core.ast;
import folia.core.block_source;
import folia.core.block_tree;
import folia.core.document_text;
import folia.core.image_dimension;
import folia.core.inline_cst;
import folia.core.inline_document;
import folia.core.serializer;
import folia.core.utf;

using namespace folia;
using namespace boost::ut;

#include "support/parser_test_support.hpp"

suite parser_format_tests = [] {

"tables_give_every_cell_its_own_inline_document"_test = [] {
    const auto parsed = parse_text(1,
        "| **A** | $B$ |\n"
        "| :--- | ---: |\n"
        "| `one` | ~~two~~ |\n");
    const auto* table = first_block(parsed.document.root.children, BlockKind::Table);
    expect(fatal(bool(table != nullptr)));
    if (!table) return;
    expect(fatal(bool(table->children.front().children.size() == 2u)));
    expect(fatal(bool(table->children.size() == 2u)));
    expect(fatal(bool(table->table_special().table_aligns
        == std::vector{TableAlignment::Left, TableAlignment::Right})));
    expect(fatal(bool(has_inline(table->children[0].children[0].inline_content, InlineCstKind::Strong))));
    expect(fatal(bool(has_inline(table->children[0].children[1].inline_content, InlineCstKind::InlineMath))));
    expect(fatal(bool(has_inline(table->children[1].children[0].inline_content, InlineCstKind::CodeSpan))));
    expect(fatal(bool(has_inline(table->children[1].children[1].inline_content, InlineCstKind::Strikethrough))));
    for (const auto& row : table->children) for (const auto& cell : row.children) expect_lossless(cell.inline_content);
};

"block_line_endings_keep_pipe_tables_and_fenced_code_structurally_equivalent"_test = [] {
    const auto source_with = [](std::string_view eol) {
        std::string source;
        const auto line = [&](std::string_view text) {
            source.append(text);
            source.append(eol);
        };
        line("# API");
        line("");
        line("## Routes");
        line("");
        line("| Group | Prefix | Action |");
        line("| --- | --- | --- |");
        line("| session | `/login` | migrate |");
        line("| profile | `/me` | preserve |");
        line("");
        line("## Example");
        line("");
        line("```text");
        line("/v1");
        line("```");
        line("");
        line("## Tail");
        source.append("outside");
        return source;
    };

    for (const auto eol : {std::string_view{"\n"}, std::string_view{"\r\n"}, std::string_view{"\r"}}) {
        const auto source = source_with(eol);
        const auto parsed = parse_text(1, source);
        const std::vector expected_kinds{
            BlockKind::Heading,
            BlockKind::Heading,
            BlockKind::Table,
            BlockKind::Heading,
            BlockKind::CodeBlock,
            BlockKind::Heading,
            BlockKind::Paragraph,
        };
        std::vector<BlockKind> actual_kinds;
        for (const auto& block : parsed.document.root.children) actual_kinds.push_back(block.kind);
        expect(bool(actual_kinds.size() == expected_kinds.size()))
            << "top-level block count for line ending size: " << eol.size();
        if (actual_kinds.size() != expected_kinds.size()) continue;
        for (std::size_t index = 0; index < expected_kinds.size(); ++index) {
            expect(bool(actual_kinds[index] == expected_kinds[index]))
                << "top-level block kind at index " << index
                << " for line ending size: " << eol.size();
        }
        expect(bool(serialize_markdown(parsed.document) == source))
            << "exact save for line ending size: " << eol.size();

        const auto* table = first_block(parsed.document.root.children, BlockKind::Table);
        expect(fatal(bool(table != nullptr))) << "line ending size: " << eol.size();
        if (table) {
            expect(fatal(bool(table->children.size() == 3u))) << "line ending size: " << eol.size();
            expect(fatal(bool(std::ranges::all_of(table->children, [](const auto& row) {
                return row.children.size() == 3u;
            }))));
        }
        const auto* code = first_block(parsed.document.root.children, BlockKind::CodeBlock);
        expect(fatal(bool(code != nullptr))) << "line ending size: " << eol.size();
        if (code) {
            expect(fatal(bool(block_source_tokens_partition(code->block_source))));
            expect(fatal(bool(flatten_block_source_tokens(code->block_source) == code->block_source.source())));
        }
    }

    const std::string mixed_line_endings =
        "| A | B |\r\n"
        "| --- | --- |\n"
        "| one | two |\r"
        "| three | four |";
    const auto mixed = parse_text(1, mixed_line_endings);
    expect(fatal(bool(mixed.document.root.children.size() == 1u)));
    expect(fatal(bool(mixed.document.root.children.front().kind == BlockKind::Table)));
    expect(fatal(bool(serialize_markdown(mixed.document) == mixed_line_endings)));
};

"fenced_block_source_classifies_physical_line_endings_without_polluting_content"_test = [] {
    for (const auto& source : {
        std::string{"```cpp\nvalue\n```"},
        std::string{"```cpp\r\nvalue\r\n```"},
        std::string{"```cpp\rvalue\r```"},
    }) {
        const auto parsed = parse_text(1, source);
        expect(fatal(bool(parsed.document.root.children.size() == 1u)));
        if (parsed.document.root.children.empty()) continue;
        const auto& block = parsed.document.root.children.front();
        expect(fatal(bool(block.kind == BlockKind::CodeBlock)));
        expect(fatal(bool(block.block_source.tree().language == std::optional<std::string>{"cpp"})));
        expect(fatal(bool(block_source_tokens_partition(block.block_source))));
        expect(fatal(bool(flatten_block_source_tokens(block.block_source) == block.block_source.source())));
        expect(fatal(bool(serialize_markdown(parsed.document) == source)));
    }
};

"empty_nested_containers_get_editable_paragraphs"_test = [] {
    const auto list = parse_text(1, "- ");
    expect(fatal(bool(list.document.root.children.front().kind == BlockKind::List)));
    expect(fatal(bool(list.document.root.children.front().children.front().children.size() == 1u)));
    expect(fatal(bool(list.document.root.children.front().children.front().children.front().kind == BlockKind::Paragraph)));
    const auto quote = parse_text(2, "> ");
    expect(fatal(bool(quote.document.root.children.front().kind == BlockKind::BlockQuote)));
    expect(fatal(bool(quote.document.root.children.front().children.size() == 1u)));
    expect(fatal(bool(quote.document.root.children.front().children.front().kind == BlockKind::Paragraph)));
};

"frontmatter_callouts_footnotes_and_toc_are_recognized"_test = [] {
    const auto parsed = parse_text(1,
        "---\ntitle: Hello\n---\n\n"
        "[TOC]\n\n"
        "> [!NOTE]\n> body\n\n"
        "[^1]: footnote\n");
    expect(fatal(bool(first_block(parsed.document.root.children, BlockKind::Frontmatter) != nullptr)));
    expect(fatal(bool(first_block(parsed.document.root.children, BlockKind::Toc) != nullptr)));
    expect(fatal(bool(first_block(parsed.document.root.children, BlockKind::Callout) != nullptr)));
    expect(fatal(bool(first_block(parsed.document.root.children, BlockKind::FootnoteDefinition) != nullptr)));
    expect(fatal(bool(parsed.document.metadata.title == std::optional<std::string>{"Hello"})));
};

"image_only_paragraphs_become_image_blocks"_test = [] {
    const auto markdown = parse_text(1, "![alt](image.png \"Title\")\n");
    const auto* image = first_block(markdown.document.root.children, BlockKind::ImageBlock);
    expect(fatal(bool(image != nullptr)));
    if (image) {
        expect(fatal(bool(image->image_special().src == "image.png")));
        expect(fatal(bool(image->image_special().image_alt == "alt")));
        expect(fatal(bool(image->image_special().image_title == std::optional<std::string>{"Title"})));
    }

    const std::string html_source =
        "<IMG data-x='1' src=\"image.png\" alt='alt' width='320' height=180>";
    const auto html = parse_text(2, html_source);
    const auto* html_image = first_block(html.document.root.children, BlockKind::ImageBlock);
    expect(fatal(html_image != nullptr));
    if (html_image) {
        expect(fatal(html_image->has_html_source()));
        expect(fatal(html_image->image_special().src == "image.png"));
        expect(fatal(html_image->image_special().image_width
            == std::optional<ImageDimension>{ImageDimension::pixels(320.0f)}));
    }
    expect(fatal(serialize_markdown(html.document) == html_source));
};

"html_image_dimensions_are_unit_aware_and_strict"_test = [] {
    const std::string source =
        "<p align=\"center\">\n"
        "  <img src=\"./img/readme-header.svg\" width=\"100%\" alt=\"Header\">\n"
        "</p>";
    const auto parsed = parse_text(1, source);
    expect(fatal(parsed.document.root.children.size() == 1u));
    if (parsed.document.root.children.empty()) return;
    const auto& paragraph = parsed.document.root.children.front();
    expect(fatal(paragraph.kind == BlockKind::Paragraph));
    const auto image = std::ranges::find_if(
        paragraph.inline_content.tree.nodes,
        [](auto const& node) {
            return node.kind == InlineCstKind::HtmlElement
                && node.semantics().html_tag == "img";
        });
    expect(fatal(image != paragraph.inline_content.tree.nodes.end()));
    if (image != paragraph.inline_content.tree.nodes.end()) {
        expect(fatal(image->semantics().image_width
            == std::optional<ImageDimension>{ImageDimension::percent(100.0f)}));
    }
    expect(fatal(serialize_markdown(parsed.document) == source));

    const auto direct = parse_text(2, "<img src='x.svg' width='50%'>");
    const auto* direct_image = first_block(direct.document.root.children, BlockKind::ImageBlock);
    expect(fatal(direct_image != nullptr));
    if (direct_image) {
        expect(fatal(direct_image->image_special().image_width
            == std::optional<ImageDimension>{ImageDimension::percent(50.0f)}));
    }

    const auto malformed = parse_text(3, "<img src='x.svg' width='100oops'>");
    const auto* malformed_image = first_block(
        malformed.document.root.children,
        BlockKind::ImageBlock);
    expect(fatal(malformed_image != nullptr));
    if (malformed_image)
        expect(fatal(!malformed_image->image_special().image_width.has_value()));
};

"html_block_text_alignment_is_semantic_and_lossless"_test = [] {
    const std::string source =
        "<p align=\"center\">center</p>\n"
        "<p align='left' style=\"color: red; text-align: right\">right</p>\n"
        "<p style='text-align: justify !important'>justify</p>\n"
        "<p align='sideways'>default</p>";
    const auto parsed = parse_text(1, source);
    expect(fatal(parsed.document.root.children.size() == 4u));
    if (parsed.document.root.children.size() != 4u) return;

    expect(fatal(parsed.document.root.children[0].html_special().text_alignment
        == std::optional<TextAlignment>{TextAlignment::Center}));
    expect(fatal(parsed.document.root.children[1].html_special().text_alignment
        == std::optional<TextAlignment>{TextAlignment::End}));
    expect(fatal(parsed.document.root.children[2].html_special().text_alignment
        == std::optional<TextAlignment>{TextAlignment::Justify}));
    expect(fatal(!parsed.document.root.children[3].html_special().text_alignment));
    expect(fatal(serialize_markdown(parsed.document) == source));
};

}; // suite parser_format_tests
