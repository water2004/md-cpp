import std;
#include "test_framework.h"
import elmd.core.parser;
import elmd.core.dialect;
import elmd.core.ast;
import elmd.core.utf;

using namespace elmd;

static bool blocks_has_kind(const std::vector<BlockNode>& bs, BlockKind k) {
    for (const auto& b : bs) if (b.kind == k) return true;
    for (const auto& b : bs) {
        if (b.kind == BlockKind::BlockQuote || b.kind == BlockKind::Callout || b.kind == BlockKind::FootnoteDefinition)
            if (blocks_has_kind(b.quote_children, k)) return true;
    }
    return false;
}
static const BlockNode* first_of(const std::vector<BlockNode>& bs, BlockKind k) {
    for (const auto& b : bs) if (b.kind == k) return &b;
    for (const auto& b : bs) {
        if (b.kind == BlockKind::BlockQuote || b.kind == BlockKind::Callout || b.kind == BlockKind::FootnoteDefinition)
            if (auto p = first_of(b.quote_children, k)) return p;
    }
    return nullptr;
}
static bool inlines_have_kind(const std::vector<InlineNode>& v, InlineKind k) {
    for (const auto& n : v) { if (n.kind == k) return true; if (!n.children.empty() && inlines_have_kind(n.children, k)) return true; }
    return false;
}

ELMD_TEST(test_parse_empty) {
    auto out = parse_text(1, "");
    ELMD_CHECK_EQ(out.document.blocks.size(), 0u);
    ELMD_CHECK_EQ(out.revision, 1ull);
}

ELMD_TEST(test_parse_heading) {
    auto out = parse_text(1, "# Hello\n");
    ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
    auto* h = first_of(out.document.blocks, BlockKind::Heading);
    ELMD_CHECK(h != nullptr);
    ELMD_CHECK_EQ(h->level, 1);
    ELMD_CHECK_EQ(h->slug, std::string("hello"));
    ELMD_CHECK_EQ(out.symbols.headings.size(), 1u);
}

ELMD_TEST(test_parse_paragraph) {
    auto out = parse_text(1, "Hello world\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(p != nullptr);
    ELMD_CHECK(!p->children.empty());
}

ELMD_TEST(test_parse_strong) {
    auto out = parse_text(1, "Hello **world**\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(p && inlines_have_kind(p->children, InlineKind::Strong));
}

ELMD_TEST(test_parse_strikethrough) {
    auto out = parse_text(1, "Hello ~~world~~\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(p && inlines_have_kind(p->children, InlineKind::Strike));
}

ELMD_TEST(test_parse_inline_code) {
    auto out = parse_text(1, "Hello `code` world\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(p && inlines_have_kind(p->children, InlineKind::InlineCode));
}

ELMD_TEST(test_unmatched_inline_delimiters_are_text) {
    auto out = parse_text(1, "Hello **world\nnext ~~line\nplain *text\ncode `span\n");
    ELMD_CHECK_EQ(out.document.blocks.size(), 4u);
    for (auto const& block : out.document.blocks) {
        ELMD_CHECK(block.kind == BlockKind::Paragraph);
        ELMD_CHECK(!inlines_have_kind(block.children, InlineKind::Strong));
        ELMD_CHECK(!inlines_have_kind(block.children, InlineKind::Emphasis));
        ELMD_CHECK(!inlines_have_kind(block.children, InlineKind::Strike));
        ELMD_CHECK(!inlines_have_kind(block.children, InlineKind::InlineCode));
    }
    ELMD_CHECK(cps_to_utf8(block_inline_text_content(out.document.blocks[0].children)) == "Hello **world");
    ELMD_CHECK(cps_to_utf8(block_inline_text_content(out.document.blocks[1].children)) == "next ~~line");
    ELMD_CHECK(cps_to_utf8(block_inline_text_content(out.document.blocks[2].children)) == "plain *text");
    ELMD_CHECK(cps_to_utf8(block_inline_text_content(out.document.blocks[3].children)) == "code `span");
}

ELMD_TEST(test_parse_inline_math) {
    auto out = parse_text(1, "Hello $x+1$ world\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(p && inlines_have_kind(p->children, InlineKind::InlineMath));
}

ELMD_TEST(test_parse_block_math) {
    auto out = parse_text(1, "$$\nE=mc^2\n$$\n");
    ELMD_CHECK(blocks_has_kind(out.document.blocks, BlockKind::MathBlock));
}

ELMD_TEST(test_parse_code_block) {
    auto out = parse_text(1, "```rust\nfn main() {}\n```\n");
    auto* cb = first_of(out.document.blocks, BlockKind::CodeBlock);
    ELMD_CHECK(cb != nullptr);
    ELMD_CHECK(cb->language && *cb->language == "rust");
    ELMD_CHECK(cps_to_utf8(cb->code_text).find("fn main") != std::string::npos);
}

ELMD_TEST(test_parse_code_block_inline_math_inert) {
    auto out = parse_text(1, "```\n$a+b$\n```\n");
    auto* cb = first_of(out.document.blocks, BlockKind::CodeBlock);
    ELMD_CHECK(cb != nullptr);
    ELMD_CHECK(cps_to_utf8(cb->code_text).find("$a+b$") != std::string::npos);
}

ELMD_TEST(test_parse_fenced_math) {
    auto out = parse_text(1, "```math\nE=mc^2\n```\n");
    ELMD_CHECK(blocks_has_kind(out.document.blocks, BlockKind::MathBlock));
}

ELMD_TEST(test_parse_toc) {
    auto out = parse_text(1, "[TOC]\n");
    ELMD_CHECK(blocks_has_kind(out.document.blocks, BlockKind::Toc));
}

ELMD_TEST(test_parse_wiki_toc) {
    auto out = parse_text(1, "[[toc]]\n");
    ELMD_CHECK(blocks_has_kind(out.document.blocks, BlockKind::Toc));
}

ELMD_TEST(test_parse_link) {
    auto out = parse_text(1, "[link](https://example.com)\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(p && inlines_have_kind(p->children, InlineKind::Link));
}

ELMD_TEST(test_parse_image) {
    auto out = parse_text(1, "![alt](image.png)\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(p && inlines_have_kind(p->children, InlineKind::Image));
}

ELMD_TEST(test_parse_task_list) {
    auto out = parse_text(1, "- [ ] todo\n");
    ELMD_CHECK(blocks_has_kind(out.document.blocks, BlockKind::TaskList));
}

ELMD_TEST(test_parse_frontmatter_yaml) {
    auto out = parse_text(1, "---\ntitle: Hello\ntags: [rust]\n---\n\nContent\n");
    ELMD_CHECK(blocks_has_kind(out.document.blocks, BlockKind::Frontmatter));
    ELMD_CHECK(out.document.metadata.title && *out.document.metadata.title == "Hello");
}

ELMD_TEST(test_raw_html_is_unsupported) {
    auto out = parse_text(1, "<div>\nhello\n</div>\n");
    ELMD_CHECK(blocks_has_kind(out.document.blocks, BlockKind::UnsupportedMarkup));
    // No HTML node types exist — fallthrough would fail to have other kinds.
}

ELMD_TEST(test_inline_html_is_unsupported) {
    auto out = parse_text(1, "hello <span>world</span>\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(p && inlines_have_kind(p->children, InlineKind::UnsupportedMarkup));
}

ELMD_TEST(test_html_img_not_image) {
    auto out = parse_text(1, "<img src=\"a.png\">\n");
    auto* p = first_of(out.document.blocks, BlockKind::UnsupportedMarkup);
    (void)p;
    ELMD_CHECK(!blocks_has_kind(out.document.blocks, BlockKind::ImageBlock));
}

ELMD_TEST(test_markdown_image_supported) {
    auto out = parse_text(1, "![alt](a.png)\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(p && inlines_have_kind(p->children, InlineKind::Image));
}

ELMD_TEST(test_html_table_not_parsed) {
    auto out = parse_text(1, "<table>\n<tr><td>A</td></tr>\n</table>\n");
    ELMD_CHECK(!blocks_has_kind(out.document.blocks, BlockKind::Table));
}

ELMD_TEST(test_gfm_table_parsed) {
    auto out = parse_text(1, "| A | B |\n|---|---|\n| 1 | 2 |\n");
    ELMD_CHECK(blocks_has_kind(out.document.blocks, BlockKind::Table));
}

ELMD_TEST(test_wiki_link) {
    auto out = parse_text(1, "[[Page Name]]\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(p && inlines_have_kind(p->children, InlineKind::WikiLink));
}

ELMD_TEST(test_unclosed_math_diagnostic) {
    auto out = parse_text(1, "$x + 1\n");
    bool has_e001 = false;
    for (const auto& d : out.diagnostics) if (d.code && *d.code == std::string(DIAG_UNCLOSED_MATH_DOLLAR)) has_e001 = true;
    ELMD_CHECK(has_e001);
}

ELMD_TEST(test_heading_multiple_levels_outline) {
    auto out = parse_text(1, "# H1\n## H2\n### H3\n");
    ELMD_CHECK_EQ(out.outline.items.size(), 1u);
    ELMD_CHECK_EQ(out.outline.items[0].children.size(), 1u);
    if (!out.outline.items.empty() && !out.outline.items[0].children.empty())
        ELMD_CHECK_EQ(out.outline.items[0].children[0].children.size(), 1u);
}

ELMD_TEST(test_callout_parsed) {
    auto out = parse_text(1, "> [!NOTE]\n> This is a note\n");
    ELMD_CHECK(blocks_has_kind(out.document.blocks, BlockKind::Callout));
}

ELMD_TEST(test_footnote_definition) {
    auto out = parse_text(1, "[^1]: This is a footnote\n");
    ELMD_CHECK(blocks_has_kind(out.document.blocks, BlockKind::FootnoteDefinition));
}

ELMD_TEST(parse_emphasis_not_strong) {
    // Regression for HANDOFF bug #2: `*native*` must be Emphasis not Strong.
    auto out = parse_text(1, "Hello *native* world\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(p && inlines_have_kind(p->children, InlineKind::Emphasis));
    ELMD_CHECK(!inlines_have_kind(p->children, InlineKind::Strong));
}

ELMD_TEST(parse_heading_no_space_is_not_heading) {
    auto out = parse_text(1, "###no-space\n");
    ELMD_CHECK(!blocks_has_kind(out.document.blocks, BlockKind::Heading));
}
