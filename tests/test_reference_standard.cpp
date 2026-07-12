#include <algorithm>
#include <string>

#include "elmd_test.hpp"
import elmd.core.parser;
import elmd.core.ast;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.utf;

using namespace elmd;
using namespace boost::ut;

static bool reference_inline_kind(InlineDocument const& document, InlineCstKind kind) {
    return inline_contains_kind(document, kind);
}

static bool reference_block_kind(BlockVec const& nodes, BlockKind kind) {
    for (auto const& node : nodes) {
        if (node.kind == kind) return true;
        if (reference_block_kind(node.children, kind)) return true;
    }
    return false;
}


suite reference_standard_tests = [] {

"reference_standard_covers_basic_block_forms"_test = [] {
    auto parsed = parse_text(1,
        "# ATX\n\n"
        "Setext\n=======\n\n"
        "first line\nsecond line  \nthird line\n\n"
        "> outer\n>\n> > nested\n\n"
        "    indented code\n\n"
        "1. ordered\n2. second\n   - nested\n\n"
        "***\n");
    expect(fatal(bool(reference_block_kind(parsed.document.root.children, BlockKind::Heading))));
    expect(fatal(bool(reference_block_kind(parsed.document.root.children, BlockKind::BlockQuote))));
    expect(fatal(bool(reference_block_kind(parsed.document.root.children, BlockKind::List))));
    expect(fatal(bool(reference_block_kind(parsed.document.root.children, BlockKind::CodeBlock))));
    expect(fatal(bool(reference_block_kind(parsed.document.root.children, BlockKind::ThematicBreak))));
    auto paragraph = std::find_if(parsed.document.root.children.begin(), parsed.document.root.children.end(), [](auto const& block) {
        return block.kind == BlockKind::Paragraph && reference_inline_kind(block.inline_content, InlineCstKind::SoftBreak);
    });
    expect(fatal(bool(paragraph != parsed.document.root.children.end())));
    if (paragraph != parsed.document.root.children.end()) expect(fatal(bool(reference_inline_kind(paragraph->inline_content, InlineCstKind::HardBreak))));
};

"reference_standard_covers_links_images_and_inline_forms"_test = [] {
    auto parsed = parse_text(1,
        "**strong** __strong__ *emphasis* _emphasis_ `code` ``use `code` inside``\n\n"
        "[inline](https://example.com \"Title\") [reference][ref] <https://example.com> <me@example.com>\n\n"
        "[**formatted**](https://example.com) [![linked](image.png \"Image\")](https://example.com)\n\n"
        "[ref]: <https://example.com> 'Reference title'\n");
    auto has = [&](InlineCstKind kind) {
        for (auto const& block : parsed.document.root.children) if (reference_inline_kind(block.inline_content, kind)) return true;
        return false;
    };
    expect(fatal(bool(has(InlineCstKind::Strong))));
    expect(fatal(bool(has(InlineCstKind::Emphasis))));
    expect(fatal(bool(has(InlineCstKind::CodeSpan))));
    expect(fatal(bool(has(InlineCstKind::Link))));
    expect(fatal(bool(has(InlineCstKind::Image))));
    expect(fatal(bool(reference_block_kind(parsed.document.root.children, BlockKind::LinkDefinition))));
    expect(fatal(bool((parsed.symbols.images.size()) == (1u))));
    expect(fatal(bool(parsed.symbols.links.size() >= 6u)));
};

"reference_standard_covers_safe_html_without_css_or_script"_test = [] {
    auto parsed = parse_text(1,
        "<span>**bold** <cite>citation</cite> <del>gone</del></span> "
        "<a href=\"https://example.com\" title=\"safe\">link</a> "
        "<img src=\"image.png\" alt=\"alt\" width=\"320\" height=\"180\"> <br> next\n\n"
        "<div>\nblock *markdown stays literal*\n</div>\n\n"
        "<pre><code>&lt;tag&gt;</code></pre>\n\n"
        "<table><tr><th>A</th><th>B</th></tr><tr><td>1</td><td>2</td></tr></table>\n\n"
        "<script>alert(1)</script>\n");
    auto paragraph = std::find_if(parsed.document.root.children.begin(), parsed.document.root.children.end(), [](auto const& block) {
        return block.kind == BlockKind::Paragraph && block.inline_content.source.starts_with(U"<span>");
    });
    expect(fatal(bool(paragraph != parsed.document.root.children.end())));
    if (paragraph != parsed.document.root.children.end()) {
        expect(fatal(bool(reference_inline_kind(paragraph->inline_content, InlineCstKind::Strong))));
        expect(fatal(bool(reference_inline_kind(paragraph->inline_content, InlineCstKind::Emphasis))));
        expect(fatal(bool(reference_inline_kind(paragraph->inline_content, InlineCstKind::Strikethrough))));
        expect(fatal(bool(reference_inline_kind(paragraph->inline_content, InlineCstKind::Link))));
        expect(fatal(bool(reference_inline_kind(paragraph->inline_content, InlineCstKind::Image))));
        expect(fatal(bool(reference_inline_kind(paragraph->inline_content, InlineCstKind::HardBreak))));
    }
    expect(fatal(bool(reference_block_kind(parsed.document.root.children, BlockKind::CodeBlock))));
    expect(fatal(bool(reference_block_kind(parsed.document.root.children, BlockKind::Table))));
    expect(fatal(bool(reference_block_kind(parsed.document.root.children, BlockKind::UnsupportedMarkup))));
    expect(fatal(bool(std::any_of(parsed.diagnostics.begin(), parsed.diagnostics.end(), [](auto const& diagnostic) {
        return diagnostic.code && *diagnostic.code == std::string(DIAG_RAW_HTML_DISABLED);
    }))));
};

"reference_standard_covers_nested_list_elements"_test = [] {
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
    expect(fatal(bool((parsed.document.root.children.size()) == (1u))));
    if (parsed.document.root.children.empty()) return;
    auto const& list = parsed.document.root.children.front();
    expect(fatal(bool(list.kind == BlockKind::List)));
    expect(fatal(bool((list.children.size()) == (3u))));
    if (list.children.size() < 2) return;
    auto const& children = list.children[1].children;
    expect(fatal(bool(reference_block_kind(children, BlockKind::BlockQuote))));
    expect(fatal(bool(reference_block_kind(children, BlockKind::CodeBlock))));
    expect(fatal(bool(reference_block_kind(children, BlockKind::ImageBlock))));
    expect(fatal(bool(reference_block_kind(children, BlockKind::List))));
};

}; // suite reference_standard_tests
