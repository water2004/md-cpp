import std;
#include "test_framework.h"
import elmd.core.parser;
import elmd.core.ast;
import elmd.core.utf;

using namespace elmd;

static bool reference_inline_kind(InlineVec const& nodes, InlineKind kind) {
    for (auto const& node : nodes) {
        if (node.kind == kind || reference_inline_kind(node.children, kind)) return true;
    }
    return false;
}

static bool reference_block_kind(BlockVec const& nodes, BlockKind kind) {
    for (auto const& node : nodes) {
        if (node.kind == kind) return true;
        if (reference_block_kind(node.quote_children, kind)) return true;
        for (auto const& item : node.list_items) if (reference_block_kind(item.children, kind)) return true;
        for (auto const& item : node.task_items) if (reference_block_kind(item.children, kind)) return true;
    }
    return false;
}

ELMD_TEST(reference_standard_covers_basic_block_forms) {
    auto parsed = parse_text(1,
        "# ATX\n\n"
        "Setext\n=======\n\n"
        "first line\nsecond line  \nthird line\n\n"
        "> outer\n>\n> > nested\n\n"
        "    indented code\n\n"
        "1. ordered\n2. second\n   - nested\n\n"
        "***\n");
    ELMD_CHECK(reference_block_kind(parsed.document.blocks, BlockKind::Heading));
    ELMD_CHECK(reference_block_kind(parsed.document.blocks, BlockKind::BlockQuote));
    ELMD_CHECK(reference_block_kind(parsed.document.blocks, BlockKind::List));
    ELMD_CHECK(reference_block_kind(parsed.document.blocks, BlockKind::CodeBlock));
    ELMD_CHECK(reference_block_kind(parsed.document.blocks, BlockKind::ThematicBreak));
    auto paragraph = std::find_if(parsed.document.blocks.begin(), parsed.document.blocks.end(), [](auto const& block) {
        return block.kind == BlockKind::Paragraph && reference_inline_kind(block.children, InlineKind::SoftBreak);
    });
    ELMD_CHECK(paragraph != parsed.document.blocks.end());
    if (paragraph != parsed.document.blocks.end()) ELMD_CHECK(reference_inline_kind(paragraph->children, InlineKind::HardBreak));
}

ELMD_TEST(reference_standard_covers_links_images_and_inline_forms) {
    auto parsed = parse_text(1,
        "**strong** __strong__ *emphasis* _emphasis_ `code` ``use `code` inside``\n\n"
        "[inline](https://example.com \"Title\") [reference][ref] <https://example.com> <me@example.com>\n\n"
        "[**formatted**](https://example.com) [![linked](image.png \"Image\")](https://example.com)\n\n"
        "[ref]: <https://example.com> 'Reference title'\n");
    auto has = [&](InlineKind kind) {
        for (auto const& block : parsed.document.blocks) if (reference_inline_kind(block.children, kind)) return true;
        return false;
    };
    ELMD_CHECK(has(InlineKind::Strong));
    ELMD_CHECK(has(InlineKind::Emphasis));
    ELMD_CHECK(has(InlineKind::InlineCode));
    ELMD_CHECK(has(InlineKind::Link));
    ELMD_CHECK(has(InlineKind::Image));
    ELMD_CHECK(reference_block_kind(parsed.document.blocks, BlockKind::LinkDefinition));
    ELMD_CHECK_EQ(parsed.symbols.images.size(), 1u);
    ELMD_CHECK(parsed.symbols.links.size() >= 6u);
}

ELMD_TEST(reference_standard_covers_safe_html_without_css_or_script) {
    auto parsed = parse_text(1,
        "<span>**bold** <cite>citation</cite> <del>gone</del></span> "
        "<a href=\"https://example.com\" title=\"safe\">link</a> "
        "<img src=\"image.png\" alt=\"alt\" width=\"320\" height=\"180\"> <br> next\n\n"
        "<div>\nblock *markdown stays literal*\n</div>\n\n"
        "<pre><code>&lt;tag&gt;</code></pre>\n\n"
        "<table><tr><th>A</th><th>B</th></tr><tr><td>1</td><td>2</td></tr></table>\n\n"
        "<script>alert(1)</script>\n");
    auto paragraph = std::find_if(parsed.document.blocks.begin(), parsed.document.blocks.end(), [](auto const& block) {
        return block.kind == BlockKind::Paragraph && reference_inline_kind(block.children, InlineKind::Span);
    });
    ELMD_CHECK(paragraph != parsed.document.blocks.end());
    if (paragraph != parsed.document.blocks.end()) {
        ELMD_CHECK(reference_inline_kind(paragraph->children, InlineKind::Strong));
        ELMD_CHECK(reference_inline_kind(paragraph->children, InlineKind::Emphasis));
        ELMD_CHECK(reference_inline_kind(paragraph->children, InlineKind::Strike));
        ELMD_CHECK(reference_inline_kind(paragraph->children, InlineKind::Link));
        ELMD_CHECK(reference_inline_kind(paragraph->children, InlineKind::Image));
        ELMD_CHECK(reference_inline_kind(paragraph->children, InlineKind::HardBreak));
    }
    ELMD_CHECK(reference_block_kind(parsed.document.blocks, BlockKind::CodeBlock));
    ELMD_CHECK(reference_block_kind(parsed.document.blocks, BlockKind::Table));
    ELMD_CHECK(reference_block_kind(parsed.document.blocks, BlockKind::UnsupportedMarkup));
    ELMD_CHECK(std::any_of(parsed.diagnostics.begin(), parsed.diagnostics.end(), [](auto const& diagnostic) {
        return diagnostic.code && *diagnostic.code == std::string(DIAG_RAW_HTML_DISABLED);
    }));
}

ELMD_TEST(reference_standard_covers_nested_list_elements) {
    auto parsed = parse_text(1,
        "1.  Open the file.\n"
        "\n"
        "2.  Find the following elements:\n"
        "\n"
        "    > quoted\n"
        "\n"
        "        <html>\n"
        "          <head>\n"
        "          </head>\n"
        "\n"
        "    ![image](image.png)\n"
        "\n"
        "    - nested item\n"
        "\n"
        "3.  Finish.\n");
    ELMD_CHECK_EQ(parsed.document.blocks.size(), 1u);
    if (parsed.document.blocks.empty()) return;
    auto const& list = parsed.document.blocks.front();
    ELMD_CHECK(list.kind == BlockKind::List);
    ELMD_CHECK_EQ(list.list_items.size(), 3u);
    if (list.list_items.size() < 2) return;
    auto const& children = list.list_items[1].children;
    ELMD_CHECK(reference_block_kind(children, BlockKind::BlockQuote));
    ELMD_CHECK(reference_block_kind(children, BlockKind::CodeBlock));
    ELMD_CHECK(reference_block_kind(children, BlockKind::ImageBlock));
    ELMD_CHECK(reference_block_kind(children, BlockKind::List));
}
