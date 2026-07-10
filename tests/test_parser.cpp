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

ELMD_TEST(test_single_newline_inside_plain_text_is_soft_break) {
    auto out = parse_text(1, "Hello\nWorld");
    ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
    ELMD_CHECK(out.document.blocks[0].kind == BlockKind::Paragraph);
    ELMD_CHECK(inlines_have_kind(out.document.blocks[0].children, InlineKind::SoftBreak));
    ELMD_CHECK(cps_to_utf8(block_inline_text_content(out.document.blocks[0].children)) == "Hello\nWorld");
}

ELMD_TEST(test_trailing_blank_lines_are_not_ast_blocks) {
    auto out = parse_text(1, "Hello\n\n");
    ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
    ELMD_CHECK(out.document.blocks[0].kind == BlockKind::Paragraph);
}

ELMD_TEST(test_parse_block_break_between_paragraphs_is_separator_no_empty_block) {
    auto out = parse_text(1, "Hello\n\nWorld");
    ELMD_CHECK_EQ(out.document.blocks.size(), 2u);
    ELMD_CHECK(!out.document.blocks[0].children.empty());
    ELMD_CHECK(!out.document.blocks[1].children.empty());
}

ELMD_TEST(test_consecutive_blank_lines_remain_source_trivia) {
    auto out = parse_text(1, "Hello\n\n\nWorld");
    ELMD_CHECK_EQ(out.document.blocks.size(), 2u);
    ELMD_CHECK(!out.document.blocks[0].children.empty());
    ELMD_CHECK(!out.document.blocks[1].children.empty());
}

ELMD_TEST(test_parse_trailing_consecutive_blank_lines_as_trivia) {
    auto out = parse_text(1, "Hello\n\n\n");
    ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
}

ELMD_TEST(test_incremental_enter_rescans_from_previous_block_and_keeps_suffix) {
    std::string old_text = "one\n\ntwo\n\nthree";
    auto old_out = parse_text(1, old_text);
    std::string new_text = "one\n\ntwo\n\n\nthree";
    IncrementalParseEdit edit{CharRange(CharOffset(8), CharOffset(8)), U"\n"};
    auto out = parse_incremental(ParseInput(2, new_text), old_out.document, old_out.symbols, old_text, edit);
    ELMD_CHECK_EQ(out.document.blocks.size(), 3u);
    ELMD_CHECK(!out.document.blocks[0].children.empty());
    ELMD_CHECK(!out.document.blocks[1].children.empty());
    ELMD_CHECK(!out.document.blocks[2].children.empty());
    ELMD_CHECK(out.document.blocks[2].id == old_out.document.blocks[2].id);
    auto* suffix_range = out.document.source_map.find_node_by_id(out.document.blocks[2].id);
    ELMD_CHECK(suffix_range != nullptr);
    ELMD_CHECK_EQ(suffix_range->source_range.start.v, 11u);
}

ELMD_TEST(test_incremental_trailing_enter_keeps_ast_semantic) {
    std::string old_text = "alpha";
    auto old_out = parse_text(1, old_text);
    std::string new_text = "alpha\n";
    IncrementalParseEdit edit{CharRange(CharOffset(5), CharOffset(5)), U"\n"};
    auto out = parse_incremental(ParseInput(2, new_text), old_out.document, old_out.symbols, old_text, edit);
    ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
    ELMD_CHECK(out.document.blocks[0].kind == BlockKind::Paragraph);
}

ELMD_TEST(test_incremental_repeated_trailing_enter_keeps_ast_semantic) {
    std::string old_text = "alpha\n\n";
    auto old_out = parse_text(1, old_text);
    std::string new_text = "alpha\n\n\n";
    IncrementalParseEdit edit{CharRange(CharOffset(7), CharOffset(7)), U"\n"};
    auto out = parse_incremental(ParseInput(2, new_text), old_out.document, old_out.symbols, old_text, edit);
    ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
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

ELMD_TEST(test_inline_code_requires_equal_complete_backtick_runs) {
    auto out = parse_text(1, "x ``cpp ``` y\n");
    ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
    ELMD_CHECK(out.document.blocks[0].kind == BlockKind::Paragraph);
    ELMD_CHECK(!inlines_have_kind(out.document.blocks[0].children, InlineKind::InlineCode));
    ELMD_CHECK_EQ(cps_to_utf8(block_inline_text_content(out.document.blocks[0].children)), std::string("x ``cpp ``` y"));
}

ELMD_TEST(test_inline_code_preserves_exact_multi_backtick_marker_ranges) {
    auto out = parse_text(1, "``code ` tick``\n");
    auto* paragraph = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(paragraph != nullptr);
    const InlineNode* code = nullptr;
    if (paragraph) for (const auto& child : paragraph->children) if (child.kind == InlineKind::InlineCode) code = &child;
    ELMD_CHECK(code != nullptr);
    if (code) {
        ELMD_CHECK_EQ(cps_to_utf8(code->text), std::string("code ` tick"));
        auto* range = out.document.source_map.find_node_by_id(code->id);
        ELMD_CHECK(range != nullptr);
        if (range) {
            ELMD_CHECK_EQ(range->marker_ranges.size(), 2u);
            ELMD_CHECK_EQ(range->marker_ranges[0].len(), 2u);
            ELMD_CHECK_EQ(range->marker_ranges[1].len(), 2u);
        }
    }
}

ELMD_TEST(test_inline_code_normalizes_single_newline_to_space) {
    auto out = parse_text(1, "``a\nb``\n");
    auto* paragraph = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(paragraph != nullptr);
    const InlineNode* code = nullptr;
    if (paragraph) for (const auto& child : paragraph->children) if (child.kind == InlineKind::InlineCode) code = &child;
    ELMD_CHECK(code != nullptr);
    if (code) ELMD_CHECK_EQ(cps_to_utf8(code->text), std::string("a b"));
}

ELMD_TEST(test_unmatched_inline_delimiters_are_text) {
    auto out = parse_text(1, "Hello **world\nnext ~~line\nplain *text\ncode `span\n");
    ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
    for (auto const& block : out.document.blocks) {
        ELMD_CHECK(block.kind == BlockKind::Paragraph);
        ELMD_CHECK(!inlines_have_kind(block.children, InlineKind::Strong));
        ELMD_CHECK(!inlines_have_kind(block.children, InlineKind::Emphasis));
        ELMD_CHECK(!inlines_have_kind(block.children, InlineKind::Strike));
        ELMD_CHECK(!inlines_have_kind(block.children, InlineKind::InlineCode));
    }
    ELMD_CHECK(cps_to_utf8(block_inline_text_content(out.document.blocks[0].children)) == "Hello **world\nnext ~~line\nplain *text\ncode `span");
}

ELMD_TEST(test_parse_inline_math) {
    auto out = parse_text(1, "Hello $x+1$ world\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(p && inlines_have_kind(p->children, InlineKind::InlineMath));
}

ELMD_TEST(test_structural_inline_at_paragraph_end_updates_content_range) {
    std::vector<std::string> sources{
        "$x$", "\\(x\\)", "**x**", "*x*", "__x__", "_x_", "~~x~~", "`x`",
        "[x](u)", "![x](p)", "[^x]", "[[x]]",
    };
    for (auto const& source : sources) {
        auto out = parse_text(1, source);
        ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
        auto range = out.document.source_map.find_node_by_id(out.document.blocks[0].id);
        ELMD_CHECK(range != nullptr);
        ELMD_CHECK_EQ(range->content_range.start.v, 0u);
        ELMD_CHECK_EQ(range->content_range.end.v, utf8_to_cps(source).size());
    }
}

ELMD_TEST(test_parse_block_math) {
    auto out = parse_text(1, "$$\nE=mc^2\n$$\n");
    ELMD_CHECK(blocks_has_kind(out.document.blocks, BlockKind::MathBlock));
}

ELMD_TEST(test_parse_inline_paren_math) {
    auto out = parse_text(1, "Speed \\(v=\\frac{d}{t}\\) now\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(p && inlines_have_kind(p->children, InlineKind::InlineMath));
    auto it = std::find_if(p->children.begin(), p->children.end(), [](auto const& n) {
        return n.kind == InlineKind::InlineMath;
    });
    ELMD_CHECK(it != p->children.end());
    ELMD_CHECK(it->math_delim == MathDelimiter::InlineParen);
    ELMD_CHECK(cps_to_utf8(it->text) == "v=\\frac{d}{t}");
}

ELMD_TEST(test_parse_bracket_math_block) {
    auto out = parse_text(1, "\\[\nE=mc^2\n\\]\n");
    auto* math = first_of(out.document.blocks, BlockKind::MathBlock);
    ELMD_CHECK(math != nullptr);
    ELMD_CHECK(math->math_delim == MathDelimiter::BlockBracket);
    ELMD_CHECK(cps_to_utf8(math->tex) == "E=mc^2");
}

ELMD_TEST(test_parse_code_block) {
    std::string source = "```rust\nfn main() {}\n```\n";
    auto out = parse_text(1, source);
    auto* cb = first_of(out.document.blocks, BlockKind::CodeBlock);
    ELMD_CHECK(cb != nullptr);
    ELMD_CHECK(cb->language && *cb->language == "rust");
    ELMD_CHECK_EQ(cps_to_utf8(cb->code_text), std::string("fn main() {}\n"));
    auto* range = out.document.source_map.find_node_by_id(cb->id);
    ELMD_CHECK(range != nullptr);
    ELMD_CHECK_EQ(range->source_range.start.v, 0u);
    ELMD_CHECK_EQ(range->source_range.end.v, utf8_to_cps(source).size());
    ELMD_CHECK_EQ(range->content_range.start.v, 8u);
    ELMD_CHECK_EQ(range->content_range.end.v, 21u);
    ELMD_CHECK_EQ(range->marker_ranges.size(), 2u);
}

ELMD_TEST(test_parse_code_block_inline_math_inert) {
    auto out = parse_text(1, "```\n$a+b$\n```\n");
    auto* cb = first_of(out.document.blocks, BlockKind::CodeBlock);
    ELMD_CHECK(cb != nullptr);
    ELMD_CHECK(cps_to_utf8(cb->code_text).find("$a+b$") != std::string::npos);
}

ELMD_TEST(test_parse_empty_code_block_closes_on_first_content_line) {
    auto out = parse_text(1, "```cpp\n```\n");
    auto* block = first_of(out.document.blocks, BlockKind::CodeBlock);
    ELMD_CHECK(block != nullptr);
    ELMD_CHECK(block->language && *block->language == "cpp");
    ELMD_CHECK(block->code_text.empty());
    auto* range = out.document.source_map.find_node_by_id(block->id);
    ELMD_CHECK(range != nullptr);
    ELMD_CHECK_EQ(range->content_range.start.v, range->content_range.end.v);
    ELMD_CHECK_EQ(range->marker_ranges.size(), 2u);
}

ELMD_TEST(test_fenced_code_interrupts_paragraph_without_blank_line) {
    auto out = parse_text(1, "before\n```cpp\n```\n");
    ELMD_CHECK_EQ(out.document.blocks.size(), 2u);
    ELMD_CHECK(out.document.blocks[0].kind == BlockKind::Paragraph);
    ELMD_CHECK(out.document.blocks[1].kind == BlockKind::CodeBlock);
    ELMD_CHECK(out.document.blocks[1].language && *out.document.blocks[1].language == "cpp");
    auto* range = out.document.source_map.find_node_by_id(out.document.blocks[1].id);
    ELMD_CHECK(range != nullptr);
    if (range) {
        ELMD_CHECK_EQ(range->source_range.start.v, 7u);
        ELMD_CHECK_EQ(range->marker_ranges.size(), 2u);
    }
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

ELMD_TEST(test_parse_consecutive_unordered_list_as_one_block) {
    auto out = parse_text(1, "- alpha\n- beta\n- gamma\n");
    ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
    ELMD_CHECK(out.document.blocks[0].kind == BlockKind::List);
    ELMD_CHECK_EQ(out.document.blocks[0].list_items.size(), 3u);
    ELMD_CHECK(!out.document.blocks[0].list_ordered);
}

ELMD_TEST(test_parse_ordered_list_preserves_numbers_and_delimiter) {
    auto out = parse_text(1, "9) alpha\n10) beta\n");
    ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
    ELMD_CHECK(out.document.blocks[0].kind == BlockKind::List);
    ELMD_CHECK(out.document.blocks[0].list_ordered);
    ELMD_CHECK_EQ(out.document.blocks[0].list_start, 9ull);
    ELMD_CHECK_EQ(out.document.blocks[0].list_delimiter, U')');
    ELMD_CHECK_EQ(out.document.blocks[0].list_items.size(), 2u);
    ELMD_CHECK_EQ(out.document.blocks[0].list_items[1].marker, std::u32string(U"10) "));
}

ELMD_TEST(test_parse_consecutive_task_list_states) {
    auto out = parse_text(1, "- [ ] alpha\n- [x] beta\n");
    ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
    ELMD_CHECK(out.document.blocks[0].kind == BlockKind::TaskList);
    ELMD_CHECK_EQ(out.document.blocks[0].task_items.size(), 2u);
    ELMD_CHECK(!out.document.blocks[0].task_items[0].checked);
    ELMD_CHECK(out.document.blocks[0].task_items[1].checked);
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

ELMD_TEST(test_gfm_table_cell_ranges_and_alignments) {
    std::string source = "| A | B |\n| :--- | ---: |\n| 1 | 2 |\n";
    auto out = parse_text(1, source);
    auto* table = first_of(out.document.blocks, BlockKind::Table);
    ELMD_CHECK(table != nullptr);
    if (table) {
        ELMD_CHECK_EQ(table->table_header.size(), 2u);
        ELMD_CHECK_EQ(table->table_rows.size(), 1u);
        ELMD_CHECK_EQ(table->table_aligns.size(), 2u);
        ELMD_CHECK(table->table_aligns[0] == TableAlignment::Left);
        ELMD_CHECK(table->table_aligns[1] == TableAlignment::Right);
        auto* first_cell = out.document.source_map.find_node_by_id(table->table_header[0].id);
        auto* second_cell = out.document.source_map.find_node_by_id(table->table_header[1].id);
        ELMD_CHECK(first_cell != nullptr);
        ELMD_CHECK(second_cell != nullptr);
        if (first_cell && second_cell) {
            ELMD_CHECK_EQ(first_cell->content_range.start.v, 2u);
            ELMD_CHECK_EQ(first_cell->content_range.end.v, 3u);
            ELMD_CHECK_EQ(second_cell->content_range.start.v, 6u);
            ELMD_CHECK_EQ(second_cell->content_range.end.v, 7u);
        }
    }
}

ELMD_TEST(test_gfm_table_escaped_pipe_stays_in_its_cell) {
    auto out = parse_text(1, "| A\\|B | C |\n| --- | --- |\n| 1 | 2 |\n");
    auto* table = first_of(out.document.blocks, BlockKind::Table);
    ELMD_CHECK(table != nullptr);
    if (table) {
        ELMD_CHECK_EQ(table->table_header.size(), 2u);
        ELMD_CHECK_EQ(cps_to_utf8(block_inline_text_content(table->table_header[0].children)), std::string("A\\|B"));
    }
}

ELMD_TEST(test_invalid_table_probe_does_not_leave_orphan_source_ranges) {
    auto out = parse_text(1, "| A | B |\nnot a separator\n");
    ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
    ELMD_CHECK(out.document.blocks[0].kind == BlockKind::Paragraph);
    ELMD_CHECK_EQ(out.document.source_map.node_ranges.size(), 2u);
    auto* paragraph = out.document.source_map.find_node_by_id(out.document.blocks[0].id);
    ELMD_CHECK(paragraph != nullptr);
    if (paragraph) {
        ELMD_CHECK_EQ(paragraph->source_range.start.v, 0u);
        ELMD_CHECK_EQ(paragraph->source_range.end.v, 26u);
    }
}

ELMD_TEST(test_table_body_cells_are_capped_at_header_columns) {
    auto out = parse_text(1, "| A | B |\n| --- | --- |\n| 1 | 2 | 3 |\n");
    auto* table = first_of(out.document.blocks, BlockKind::Table);
    ELMD_CHECK(table != nullptr);
    if (table) {
        ELMD_CHECK_EQ(table->table_header.size(), 2u);
        ELMD_CHECK_EQ(table->table_rows.size(), 1u);
        ELMD_CHECK_EQ(table->table_rows[0].cells.size(), 2u);
    }
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
