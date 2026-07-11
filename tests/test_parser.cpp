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

ELMD_TEST(test_setext_headings_take_precedence_over_thematic_breaks) {
    auto out = parse_text(1, "Primary\n===\n\nSecondary\n------\n\n---\n");
    ELMD_CHECK_EQ(out.document.blocks.size(), 3u);
    if (out.document.blocks.size() >= 3) {
        ELMD_CHECK(out.document.blocks[0].kind == BlockKind::Heading);
        ELMD_CHECK_EQ(out.document.blocks[0].level, 1);
        ELMD_CHECK(out.document.blocks[1].kind == BlockKind::Heading);
        ELMD_CHECK_EQ(out.document.blocks[1].level, 2);
        ELMD_CHECK(out.document.blocks[2].kind == BlockKind::ThematicBreak);
        auto range = out.document.source_map.find_node_by_id(out.document.blocks[1].id);
        ELMD_CHECK(range != nullptr);
        if (range) {
            ELMD_CHECK_EQ(range->content_range.start.v, 13u);
            ELMD_CHECK_EQ(range->content_range.end.v, 22u);
            ELMD_CHECK_EQ(range->marker_ranges.size(), 1u);
        }
    }
}

ELMD_TEST(test_atx_heading_uses_the_complete_inline_parser) {
    auto out = parse_text(1, "### [#](https://example.com/path#fragment) **bold** `code` <em>html</em>\n");
    ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
    if (!out.document.blocks.empty()) {
        auto const& heading = out.document.blocks[0];
        ELMD_CHECK(heading.kind == BlockKind::Heading);
        ELMD_CHECK_EQ(heading.level, 3);
        ELMD_CHECK(inlines_have_kind(heading.children, InlineKind::Link));
        ELMD_CHECK(inlines_have_kind(heading.children, InlineKind::Strong));
        ELMD_CHECK(inlines_have_kind(heading.children, InlineKind::InlineCode));
        ELMD_CHECK(inlines_have_kind(heading.children, InlineKind::Emphasis));
    }
    ELMD_CHECK_EQ(out.symbols.links.size(), 1u);
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

ELMD_TEST(test_two_spaces_before_newline_are_a_hard_break) {
    auto out = parse_text(1, "Hello  \nWorld");
    ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
    ELMD_CHECK(out.document.blocks[0].kind == BlockKind::Paragraph);
    ELMD_CHECK(inlines_have_kind(out.document.blocks[0].children, InlineKind::HardBreak));
    auto hard = std::find_if(out.document.blocks[0].children.begin(), out.document.blocks[0].children.end(), [](auto const& node) { return node.kind == InlineKind::HardBreak; });
    ELMD_CHECK(hard != out.document.blocks[0].children.end());
    if (hard != out.document.blocks[0].children.end()) {
        auto range = out.document.source_map.find_node_by_id(hard->id);
        ELMD_CHECK(range != nullptr);
        if (range) {
            ELMD_CHECK_EQ(range->source_range.start.v, 5u);
            ELMD_CHECK_EQ(range->source_range.end.v, 8u);
            ELMD_CHECK_EQ(range->content_range.start.v, 7u);
            ELMD_CHECK_EQ(range->content_range.end.v, 8u);
            ELMD_CHECK_EQ(range->marker_ranges.size(), 1u);
        }
    }
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

ELMD_TEST(test_delimited_inline_nodes_preserve_exact_source_markers) {
    struct Case {
        std::string source;
        InlineKind kind;
        std::u32string marker;
        std::size_t content_start;
        std::size_t content_end;
    };
    const std::vector<Case> cases{
        {"*word*", InlineKind::Emphasis, U"*", 1, 5},
        {"_word_", InlineKind::Emphasis, U"_", 1, 5},
        {"**word**", InlineKind::Strong, U"**", 2, 6},
        {"__word__", InlineKind::Strong, U"__", 2, 6},
        {"~~word~~", InlineKind::Strike, U"~~", 2, 6},
    };
    for (auto const& test : cases) {
        auto out = parse_text(1, test.source);
        auto* paragraph = first_of(out.document.blocks, BlockKind::Paragraph);
        ELMD_CHECK(paragraph != nullptr);
        if (!paragraph) continue;
        auto node = std::find_if(paragraph->children.begin(), paragraph->children.end(), [&](auto const& child) {
            return child.kind == test.kind;
        });
        ELMD_CHECK(node != paragraph->children.end());
        if (node == paragraph->children.end()) continue;
        ELMD_CHECK_EQ(node->opening_marker, test.marker);
        ELMD_CHECK_EQ(node->closing_marker, test.marker);
        auto range = out.document.source_map.find_node_by_id(node->id);
        ELMD_CHECK(range != nullptr);
        if (!range) continue;
        ELMD_CHECK_EQ(range->source_range.start.v, 0u);
        ELMD_CHECK_EQ(range->source_range.end.v, test.source.size());
        ELMD_CHECK_EQ(range->content_range.start.v, test.content_start);
        ELMD_CHECK_EQ(range->content_range.end.v, test.content_end);
        ELMD_CHECK_EQ(range->marker_ranges.size(), 2u);
        if (range->marker_ranges.size() == 2) {
            ELMD_CHECK_EQ(range->marker_ranges[0].start.v, 0u);
            ELMD_CHECK_EQ(range->marker_ranges[0].end.v, test.marker.size());
            ELMD_CHECK_EQ(range->marker_ranges[1].start.v, test.content_end);
            ELMD_CHECK_EQ(range->marker_ranges[1].end.v, test.source.size());
        }
    }
}

ELMD_TEST(test_mixed_nested_delimiters_keep_independent_source_ranges) {
    auto out = parse_text(1, "_outer **inner** tail_");
    auto* paragraph = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(paragraph != nullptr);
    if (!paragraph || paragraph->children.empty()) return;
    auto const& emphasis = paragraph->children.front();
    ELMD_CHECK(emphasis.kind == InlineKind::Emphasis);
    ELMD_CHECK_EQ(emphasis.opening_marker, std::u32string(U"_"));
    auto strong = std::find_if(emphasis.children.begin(), emphasis.children.end(), [](auto const& child) {
        return child.kind == InlineKind::Strong;
    });
    ELMD_CHECK(strong != emphasis.children.end());
    auto outer_range = out.document.source_map.find_node_by_id(emphasis.id);
    ELMD_CHECK(outer_range != nullptr);
    if (outer_range) {
        ELMD_CHECK_EQ(outer_range->source_range.start.v, 0u);
        ELMD_CHECK_EQ(outer_range->source_range.end.v, 22u);
        ELMD_CHECK_EQ(outer_range->content_range.start.v, 1u);
        ELMD_CHECK_EQ(outer_range->content_range.end.v, 21u);
    }
    if (strong != emphasis.children.end()) {
        auto inner_range = out.document.source_map.find_node_by_id(strong->id);
        ELMD_CHECK(inner_range != nullptr);
        if (inner_range) {
            ELMD_CHECK_EQ(inner_range->source_range.start.v, 7u);
            ELMD_CHECK_EQ(inner_range->source_range.end.v, 16u);
            ELMD_CHECK_EQ(inner_range->content_range.start.v, 9u);
            ELMD_CHECK_EQ(inner_range->content_range.end.v, 14u);
        }
    }
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

ELMD_TEST(test_backslash_escapes_all_ascii_punctuation) {
    auto out = parse_text(1, R"(\!\"\#\$\%\&\'\*\+\,\-\.\/\:\;\<\=\>\?\@\[\\\]\^\_\`\{\|\}\~)");
    auto* paragraph = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(paragraph != nullptr);
    if (paragraph) {
        auto actual = cps_to_utf8(block_inline_text_content(paragraph->children));
        ELMD_CHECK_EQ(actual, std::string("!\"#$%&'*+,-./:;<=>?@[\\]^_`{|}~"));
    }
}

ELMD_TEST(test_structural_inlines_inside_emphasis) {
    auto out = parse_text(1, "**[link](https://example.com) and `code` and <span>html</span>**\n");
    auto* paragraph = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(paragraph != nullptr);
    if (paragraph) {
        auto strong = std::find_if(paragraph->children.begin(), paragraph->children.end(), [](auto const& node) { return node.kind == InlineKind::Strong; });
        ELMD_CHECK(strong != paragraph->children.end());
        if (strong != paragraph->children.end()) {
            ELMD_CHECK(inlines_have_kind(strong->children, InlineKind::Link));
            ELMD_CHECK(inlines_have_kind(strong->children, InlineKind::InlineCode));
            ELMD_CHECK(inlines_have_kind(strong->children, InlineKind::Span));
        }
    }
}

ELMD_TEST(test_parse_inline_math) {
    auto out = parse_text(1, "Hello $x+1$ world\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(p && inlines_have_kind(p->children, InlineKind::InlineMath));
}

ELMD_TEST(test_structural_inline_at_paragraph_end_updates_content_range) {
    std::vector<std::string> sources{
        "$x$", "\\(x\\)", "**x**", "*x*", "__x__", "_x_", "~~x~~", "`x`",
        "[x](u)", "a![x](p)", "[^x]", "[[x]]",
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

ELMD_TEST(test_thematic_break_is_consumed_once_with_exact_ranges) {
    auto out = parse_text(1, "---\n\nbody");
    ELMD_CHECK_EQ(out.document.blocks.size(), 2u);
    ELMD_CHECK(out.document.blocks[0].kind == BlockKind::ThematicBreak);
    auto range = out.document.source_map.find_node_by_id(out.document.blocks[0].id);
    ELMD_CHECK(range != nullptr);
    ELMD_CHECK_EQ(range->source_range.start.v, 0u);
    ELMD_CHECK_EQ(range->source_range.end.v, 3u);
    ELMD_CHECK_EQ(range->content_range.start.v, 0u);
    ELMD_CHECK_EQ(range->content_range.end.v, 3u);
    ELMD_CHECK(out.document.blocks[1].kind == BlockKind::Paragraph);
}

ELMD_TEST(test_thematic_break_rejects_spacing_between_markers) {
    for (auto const& source : {std::string("- - -"), std::string("*  *  *"), std::string("_\t_\t_")}) {
        auto out = parse_text(1, source);
        ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
        ELMD_CHECK(out.document.blocks[0].kind != BlockKind::ThematicBreak);
    }
}

ELMD_TEST(test_thematic_break_accepts_arbitrarily_long_matching_marker_runs) {
    for (auto const& source : {std::string("----"), std::string("******"), std::string("_________________")}) {
        auto out = parse_text(1, source);
        ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
        ELMD_CHECK(out.document.blocks[0].kind == BlockKind::ThematicBreak);
    }
}

ELMD_TEST(test_spaced_markers_remain_paragraph_text) {
    auto out = parse_text(1, "before\n_ _ _ _\nafter");
    ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
    ELMD_CHECK(out.document.blocks[0].kind == BlockKind::Paragraph);
}

ELMD_TEST(test_long_dash_rule_before_text_is_thematic_and_after_text_is_setext) {
    auto out = parse_text(1, "------\nbody\n------");
    ELMD_CHECK_EQ(out.document.blocks.size(), 2u);
    if (out.document.blocks.size() >= 2) {
        ELMD_CHECK(out.document.blocks[0].kind == BlockKind::ThematicBreak);
        ELMD_CHECK(out.document.blocks[1].kind == BlockKind::Heading);
        ELMD_CHECK_EQ(out.document.blocks[1].level, 2);
    }
    ELMD_CHECK(!blocks_has_kind(out.document.blocks, BlockKind::Frontmatter));
}

ELMD_TEST(test_frontmatter_is_only_recognized_at_document_start) {
    auto out = parse_text(1, "body\n---\ntitle: nope\n---");
    ELMD_CHECK(!blocks_has_kind(out.document.blocks, BlockKind::Frontmatter));
    ELMD_CHECK_EQ(std::count_if(out.document.blocks.begin(), out.document.blocks.end(), [](auto const& block) {
        return block.kind == BlockKind::ThematicBreak;
    }), 0);
    ELMD_CHECK_EQ(std::count_if(out.document.blocks.begin(), out.document.blocks.end(), [](auto const& block) {
        return block.kind == BlockKind::Heading && block.level == 2;
    }), 2);
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

ELMD_TEST(test_reference_links_and_autolinks_resolve) {
    auto out = parse_text(1, "[full][target] [collapsed][] <https://example.com> <me@example.com>\n\n[target]: https://example.com \"Title\"\n[collapsed]: /relative\n");
    auto* paragraph = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(paragraph != nullptr);
    if (paragraph) {
        std::vector<std::string> hrefs;
        for (auto const& node : paragraph->children) if (node.kind == InlineKind::Link) hrefs.push_back(node.href);
        ELMD_CHECK_EQ(hrefs.size(), 4u);
        if (hrefs.size() == 4) {
            ELMD_CHECK_EQ(hrefs[0], std::string("https://example.com"));
            ELMD_CHECK_EQ(hrefs[1], std::string("/relative"));
            ELMD_CHECK_EQ(hrefs[2], std::string("https://example.com"));
            ELMD_CHECK_EQ(hrefs[3], std::string("mailto:me@example.com"));
        }
    }
    ELMD_CHECK_EQ(std::count_if(out.document.blocks.begin(), out.document.blocks.end(), [](auto const& block) { return block.kind == BlockKind::LinkDefinition; }), 2);
}

ELMD_TEST(test_parse_image) {
    auto out = parse_text(1, "![alt](image.png)\n");
    auto* image = first_of(out.document.blocks, BlockKind::ImageBlock);
    ELMD_CHECK(image != nullptr);
    if (image) {
        ELMD_CHECK_EQ(image->src, std::string("image.png"));
        ELMD_CHECK_EQ(image->image_alt, std::string("alt"));
    }
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

ELMD_TEST(test_parse_list_items_with_full_inline_syntax) {
    auto out = parse_text(1, "- before $x^2$ and **bold** and `code`\n- next \\(y\\)\n");
    ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
    auto const& list = out.document.blocks[0];
    ELMD_CHECK(list.kind == BlockKind::List);
    ELMD_CHECK_EQ(list.list_items.size(), 2u);
    auto const& first = list.list_items[0].children[0].children;
    auto const& second = list.list_items[1].children[0].children;
    ELMD_CHECK(inlines_have_kind(first, InlineKind::InlineMath));
    ELMD_CHECK(inlines_have_kind(first, InlineKind::Strong));
    ELMD_CHECK(inlines_have_kind(first, InlineKind::InlineCode));
    ELMD_CHECK(inlines_have_kind(second, InlineKind::InlineMath));
    auto math = std::find_if(first.begin(), first.end(), [](auto const& node) { return node.kind == InlineKind::InlineMath; });
    ELMD_CHECK(math != first.end());
    auto range = out.document.source_map.find_node_by_id(math->id);
    ELMD_CHECK(range != nullptr);
    ELMD_CHECK_EQ(range->source_range.start.v, 9u);
    ELMD_CHECK_EQ(range->source_range.end.v, 14u);
    ELMD_CHECK_EQ(range->content_range.start.v, 10u);
    ELMD_CHECK_EQ(range->content_range.end.v, 13u);

    auto bounded = parse_text(2, "- `open\n- close`\n");
    ELMD_CHECK_EQ(bounded.document.blocks.size(), 1u);
    ELMD_CHECK_EQ(bounded.document.blocks[0].list_items.size(), 2u);
    auto const& bounded_first = bounded.document.blocks[0].list_items[0].children[0].children;
    ELMD_CHECK(!inlines_have_kind(bounded_first, InlineKind::InlineCode));
}

ELMD_TEST(test_parse_math_inside_emphasis_nodes) {
    auto out = parse_text(1, "**before $x$ after** *\\(y\\)* ~~$z$~~\n");
    ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
    auto const& inlines = out.document.blocks[0].children;
    auto strong = std::find_if(inlines.begin(), inlines.end(), [](auto const& node) { return node.kind == InlineKind::Strong; });
    auto emphasis = std::find_if(inlines.begin(), inlines.end(), [](auto const& node) { return node.kind == InlineKind::Emphasis; });
    auto strike = std::find_if(inlines.begin(), inlines.end(), [](auto const& node) { return node.kind == InlineKind::Strike; });
    ELMD_CHECK(strong != inlines.end());
    ELMD_CHECK(emphasis != inlines.end());
    ELMD_CHECK(strike != inlines.end());
    ELMD_CHECK(inlines_have_kind(strong->children, InlineKind::InlineMath));
    ELMD_CHECK(inlines_have_kind(emphasis->children, InlineKind::InlineMath));
    ELMD_CHECK(inlines_have_kind(strike->children, InlineKind::InlineMath));

    auto quantified = parse_text(2, R"($\exists\delta>0,s.t. |x'-x_0|<\delta,0<|x''-x_0|<\delta$)");
    ELMD_CHECK_EQ(quantified.document.blocks.size(), 1u);
    ELMD_CHECK(inlines_have_kind(quantified.document.blocks[0].children, InlineKind::InlineMath));
    auto formula = std::find_if(quantified.document.blocks[0].children.begin(), quantified.document.blocks[0].children.end(), [](auto const& node) { return node.kind == InlineKind::InlineMath; });
    ELMD_CHECK(formula != quantified.document.blocks[0].children.end());
    ELMD_CHECK(formula->text == UR"(\exists\delta>0,s.t. |x'-x_0|<\delta,0<|x''-x_0|<\delta)");
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

ELMD_TEST(test_safe_block_html_is_rendered_as_text_content) {
    auto out = parse_text(1, "<div>\nhello *not emphasis*\n</div>\n");
    auto* paragraph = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(paragraph != nullptr);
    if (paragraph) {
        ELMD_CHECK_EQ(cps_to_utf8(block_inline_text_content(paragraph->children)), std::string("hello *not emphasis*"));
        ELMD_CHECK(!inlines_have_kind(paragraph->children, InlineKind::Emphasis));
    }
}

ELMD_TEST(test_safe_inline_html_is_structural) {
    auto out = parse_text(1, "hello <span>world</span>\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(p && inlines_have_kind(p->children, InlineKind::Span));
}

ELMD_TEST(test_nested_safe_inline_html_is_structural) {
    auto out = parse_text(1, "<span>outer <span>inner **bold**</span> tail</span>\n");
    auto* paragraph = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(paragraph != nullptr);
    if (paragraph) {
        auto outer = std::find_if(paragraph->children.begin(), paragraph->children.end(), [](auto const& node) { return node.kind == InlineKind::Span; });
        ELMD_CHECK(outer != paragraph->children.end());
        if (outer != paragraph->children.end()) {
            ELMD_CHECK(inlines_have_kind(outer->children, InlineKind::Span));
            ELMD_CHECK(inlines_have_kind(outer->children, InlineKind::Strong));
        }
    }
}

ELMD_TEST(test_unsafe_inline_html_targets_are_removed) {
    auto out = parse_text(1, "<a href=\" JavaScript:alert(1)\">unsafe</a> <img src=\"data:text/html;base64,QQ==\">\n");
    auto* paragraph = first_of(out.document.blocks, BlockKind::Paragraph);
    ELMD_CHECK(paragraph != nullptr);
    if (paragraph) {
        auto link = std::find_if(paragraph->children.begin(), paragraph->children.end(), [](auto const& node) { return node.kind == InlineKind::Link; });
        auto image = std::find_if(paragraph->children.begin(), paragraph->children.end(), [](auto const& node) { return node.kind == InlineKind::Image; });
        ELMD_CHECK(link != paragraph->children.end());
        ELMD_CHECK(image != paragraph->children.end());
        if (link != paragraph->children.end()) ELMD_CHECK(link->href.empty());
        if (image != paragraph->children.end()) ELMD_CHECK(image->href.empty());
    }
}

ELMD_TEST(test_nested_lists_preserve_nested_block_structure) {
    auto out = parse_text(1, "- parent\n  - child\n    1. grandchild\n\n  > quote\n\n  ![alt](image.png)\n");
    auto* list = first_of(out.document.blocks, BlockKind::List);
    ELMD_CHECK(list != nullptr);
    if (list && !list->list_items.empty()) {
        auto const& children = list->list_items[0].children;
        ELMD_CHECK(std::any_of(children.begin(), children.end(), [](auto const& block) { return block.kind == BlockKind::List; }));
        ELMD_CHECK(std::any_of(children.begin(), children.end(), [](auto const& block) { return block.kind == BlockKind::BlockQuote; }));
        ELMD_CHECK(std::any_of(children.begin(), children.end(), [](auto const& block) { return block.kind == BlockKind::ImageBlock; }));
    }
}

ELMD_TEST(test_html_img_is_an_image_block) {
    auto out = parse_text(1, "<img src=\"a.png\" alt=\"A\" width=\"320\" height=\"180px\">\n");
    auto* image = first_of(out.document.blocks, BlockKind::ImageBlock);
    ELMD_CHECK(image != nullptr);
    if (image) {
        ELMD_CHECK_EQ(image->src, std::string("a.png"));
        ELMD_CHECK_EQ(image->image_alt, std::string("A"));
        ELMD_CHECK(image->image_width.has_value());
        ELMD_CHECK(image->image_height.has_value());
        if (image->image_width) ELMD_CHECK_EQ(*image->image_width, 320.0f);
        if (image->image_height) ELMD_CHECK_EQ(*image->image_height, 180.0f);
    }
}

ELMD_TEST(test_markdown_image_supported) {
    auto out = parse_text(1, "![alt](a.png)\n");
    auto* image = first_of(out.document.blocks, BlockKind::ImageBlock);
    ELMD_CHECK(image != nullptr);
}

ELMD_TEST(test_linked_markdown_image_is_an_image_block) {
    auto out = parse_text(1, "[![rock](shiprock.jpg \"Shiprock\")](https://example.com)\n");
    auto* image = first_of(out.document.blocks, BlockKind::ImageBlock);
    ELMD_CHECK(image != nullptr);
    if (image) {
        ELMD_CHECK_EQ(image->src, std::string("shiprock.jpg"));
        ELMD_CHECK_EQ(image->image_alt, std::string("rock"));
        ELMD_CHECK(image->image_title && *image->image_title == "Shiprock");
        ELMD_CHECK(image->image_link && *image->image_link == "https://example.com");
    }
}

ELMD_TEST(test_indented_code_block_strips_one_indent_level) {
    auto out = parse_text(1, "    > first\n    >\n    >> nested\n");
    ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
    ELMD_CHECK(out.document.blocks[0].kind == BlockKind::CodeBlock);
    ELMD_CHECK(out.document.blocks[0].code_indented);
    ELMD_CHECK_EQ(cps_to_utf8(out.document.blocks[0].code_text), std::string("> first\n>\n>> nested\n"));
    auto* range = out.document.source_map.find_node_by_id(out.document.blocks[0].id);
    ELMD_CHECK(range != nullptr);
    if (range) ELMD_CHECK_EQ(range->marker_ranges.size(), 3u);
}

ELMD_TEST(test_indented_code_block_preserves_internal_blank_and_stops) {
    auto out = parse_text(1, "    first\n\n    second\nafter\n");
    ELMD_CHECK_EQ(out.document.blocks.size(), 2u);
    ELMD_CHECK(out.document.blocks[0].kind == BlockKind::CodeBlock);
    ELMD_CHECK_EQ(cps_to_utf8(out.document.blocks[0].code_text), std::string("first\n\nsecond\n"));
    ELMD_CHECK(out.document.blocks[1].kind == BlockKind::Paragraph);
    ELMD_CHECK_EQ(cps_to_utf8(block_inline_text_content(out.document.blocks[1].children)), std::string("after"));
}

ELMD_TEST(test_tab_indented_code_block) {
    auto out = parse_text(1, "\tcode\n");
    ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
    ELMD_CHECK(out.document.blocks[0].kind == BlockKind::CodeBlock);
    ELMD_CHECK(out.document.blocks[0].code_indented);
    ELMD_CHECK_EQ(cps_to_utf8(out.document.blocks[0].code_text), std::string("code\n"));
}

ELMD_TEST(test_indented_code_block_inside_ordered_list_keeps_block_semantics) {
    auto out = parse_text(1,
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
    ELMD_CHECK_EQ(out.document.blocks.size(), 1u);
    if (out.document.blocks.empty()) return;
    auto const& list = out.document.blocks.front();
    ELMD_CHECK(list.kind == BlockKind::List);
    ELMD_CHECK_EQ(list.list_items.size(), 3u);
    if (list.list_items.size() < 2) return;
    auto const& second = list.list_items[1];
    auto code = std::find_if(second.children.begin(), second.children.end(), [](auto const& child) {
        return child.kind == BlockKind::CodeBlock;
    });
    ELMD_CHECK(code != second.children.end());
    if (code != second.children.end()) {
        ELMD_CHECK(code->code_indented);
        ELMD_CHECK_EQ(cps_to_utf8(code->code_text), std::string("<html>\n  <head>\n    <title>Test</title>\n  </head>\n"));
    }
}

ELMD_TEST(test_list_container_indent_without_four_extra_columns_is_not_code) {
    auto out = parse_text(1,
        "1. First\n"
        "2. Second\n"
        "\n"
        "   <html>\n"
        "\n"
        "3. Third\n");
    auto* list = first_of(out.document.blocks, BlockKind::List);
    ELMD_CHECK(list != nullptr);
    if (!list || list->list_items.size() < 2) return;
    ELMD_CHECK(std::none_of(list->list_items[1].children.begin(), list->list_items[1].children.end(), [](auto const& child) {
        return child.kind == BlockKind::CodeBlock;
    }));
}

ELMD_TEST(test_blockquote_preserves_paragraph_text) {
    auto out = parse_text(1, "> quoted text\n");
    auto* quote = first_of(out.document.blocks, BlockKind::BlockQuote);
    ELMD_CHECK(quote != nullptr);
    if (quote) {
        ELMD_CHECK_EQ(quote->quote_children.size(), 1u);
        ELMD_CHECK(quote->quote_children[0].kind == BlockKind::Paragraph);
        ELMD_CHECK_EQ(cps_to_utf8(block_inline_text_content(quote->quote_children[0].children)), std::string("quoted text"));
    }
}

ELMD_TEST(test_blockquote_preserves_multiline_and_stops_before_following_text) {
    auto out = parse_text(1, "> first line\n> second line\nafter\n");
    ELMD_CHECK_EQ(out.document.blocks.size(), 2u);
    auto* quote = first_of(out.document.blocks, BlockKind::BlockQuote);
    ELMD_CHECK(quote != nullptr);
    if (quote) {
        ELMD_CHECK_EQ(quote->quote_children.size(), 1u);
        ELMD_CHECK(quote->quote_children[0].kind == BlockKind::Paragraph);
        ELMD_CHECK_EQ(cps_to_utf8(block_inline_text_content(quote->quote_children[0].children)), std::string("first line\nsecond line"));
    }
    if (!out.document.blocks.empty()) {
        ELMD_CHECK(out.document.blocks.back().kind == BlockKind::Paragraph);
        ELMD_CHECK_EQ(cps_to_utf8(block_inline_text_content(out.document.blocks.back().children)), std::string("after"));
    }
}

ELMD_TEST(test_blockquote_keeps_paragraphs_and_nested_blocks) {
    auto out = parse_text(1, "> first\n>\n> second\n> > nested\n");
    auto* quote = first_of(out.document.blocks, BlockKind::BlockQuote);
    ELMD_CHECK(quote != nullptr);
    if (quote) {
        ELMD_CHECK_EQ(quote->quote_children.size(), 3u);
        if (quote->quote_children.size() >= 3) {
            ELMD_CHECK(quote->quote_children[0].kind == BlockKind::Paragraph);
            ELMD_CHECK(quote->quote_children[1].kind == BlockKind::Paragraph);
            ELMD_CHECK(quote->quote_children[2].kind == BlockKind::BlockQuote);
            ELMD_CHECK_EQ(cps_to_utf8(block_inline_text_content(quote->quote_children[0].children)), std::string("first"));
            ELMD_CHECK_EQ(cps_to_utf8(block_inline_text_content(quote->quote_children[1].children)), std::string("second"));
            if (!quote->quote_children[2].quote_children.empty()) {
                ELMD_CHECK_EQ(cps_to_utf8(block_inline_text_content(quote->quote_children[2].quote_children[0].children)), std::string("nested"));
            }
        }
    }
}

ELMD_TEST(test_blockquote_preserves_heading_and_list_children) {
    auto out = parse_text(1, "> # heading\n> - item\n");
    auto* quote = first_of(out.document.blocks, BlockKind::BlockQuote);
    ELMD_CHECK(quote != nullptr);
    if (quote) {
        ELMD_CHECK_EQ(quote->quote_children.size(), 2u);
        if (quote->quote_children.size() >= 2) {
            ELMD_CHECK(quote->quote_children[0].kind == BlockKind::Heading);
            ELMD_CHECK(quote->quote_children[1].kind == BlockKind::List);
        }
    }
}

ELMD_TEST(test_safe_html_table_parsed) {
    auto out = parse_text(1, "<table>\n<tr><td>A</td></tr>\n</table>\n");
    ELMD_CHECK(blocks_has_kind(out.document.blocks, BlockKind::Table));
    auto* table = first_of(out.document.blocks, BlockKind::Table);
    ELMD_CHECK(table != nullptr);
    if (table) ELMD_CHECK(!table->table_header_row);
}

ELMD_TEST(test_safe_html_table_preserves_header_cells) {
    auto out = parse_text(1, "<table><tr><th>A</th><th>B</th></tr><tr><td>1</td><td>2</td></tr></table>\n");
    auto* table = first_of(out.document.blocks, BlockKind::Table);
    ELMD_CHECK(table != nullptr);
    if (table) {
        ELMD_CHECK(table->table_header_row);
        ELMD_CHECK_EQ(table->table_header.size(), 2u);
        ELMD_CHECK_EQ(table->table_rows.size(), 1u);
    }
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
        ELMD_CHECK_EQ(cps_to_utf8(block_inline_text_content(table->table_header[0].children)), std::string("A|B"));
    }
}

ELMD_TEST(test_gfm_table_cells_parse_inline_semantics) {
    auto out = parse_text(1, "| **bold** | $x$ |\n| --- | --- |\n| `code` | ~~gone~~ |\n");
    auto* table = first_of(out.document.blocks, BlockKind::Table);
    ELMD_CHECK(table != nullptr);
    if (table) {
        ELMD_CHECK(inlines_have_kind(table->table_header[0].children, InlineKind::Strong));
        ELMD_CHECK(inlines_have_kind(table->table_header[1].children, InlineKind::InlineMath));
        ELMD_CHECK(inlines_have_kind(table->table_rows[0].cells[0].children, InlineKind::InlineCode));
        ELMD_CHECK(inlines_have_kind(table->table_rows[0].cells[1].children, InlineKind::Strike));
    }
}

ELMD_TEST(test_gfm_table_inline_delimiters_do_not_cross_cells) {
    auto out = parse_text(1, "| *left | right* |\n| --- | --- |\n");
    auto* table = first_of(out.document.blocks, BlockKind::Table);
    ELMD_CHECK(table != nullptr);
    if (table) {
        ELMD_CHECK(!inlines_have_kind(table->table_header[0].children, InlineKind::Emphasis));
        ELMD_CHECK(!inlines_have_kind(table->table_header[1].children, InlineKind::Emphasis));
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
