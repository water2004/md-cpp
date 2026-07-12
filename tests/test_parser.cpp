import std;
import boost.ut;
import elmd.core.parser;
import elmd.core.dialect;
import elmd.core.ast;
import elmd.core.utf;

using namespace elmd;
using namespace boost::ut;

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


suite parser_tests = [] {

"test_parse_empty"_test = [] {
    auto out = parse_text(1, "");
    expect(fatal(bool((out.document.blocks.size()) == (0u))));
    expect(fatal(bool((out.revision) == (1ull))));
};

"test_parse_heading"_test = [] {
    auto out = parse_text(1, "# Hello\n");
    expect(fatal(bool((out.document.blocks.size()) == (1u))));
    auto* h = first_of(out.document.blocks, BlockKind::Heading);
    expect(fatal(bool(h != nullptr)));
    expect(fatal(bool((h->level) == (1))));
    expect(fatal(bool((h->slug) == (std::string("hello")))));
    expect(fatal(bool((out.symbols.headings.size()) == (1u))));
};

"test_setext_headings_take_precedence_over_thematic_breaks"_test = [] {
    auto out = parse_text(1, "Primary\n===\n\nSecondary\n------\n\n---\n");
    expect(fatal(bool((out.document.blocks.size()) == (3u))));
    if (out.document.blocks.size() >= 3) {
        expect(fatal(bool(out.document.blocks[0].kind == BlockKind::Heading)));
        expect(fatal(bool((out.document.blocks[0].level) == (1))));
        expect(fatal(bool(out.document.blocks[1].kind == BlockKind::Heading)));
        expect(fatal(bool((out.document.blocks[1].level) == (2))));
        expect(fatal(bool(out.document.blocks[2].kind == BlockKind::ThematicBreak)));
        auto range = out.document.source_map.find_node_by_id(out.document.blocks[1].id);
        expect(fatal(bool(range != nullptr)));
        if (range) {
            expect(fatal(bool((range->content_range.start.v) == (13u))));
            expect(fatal(bool((range->content_range.end.v) == (22u))));
            expect(fatal(bool((range->marker_ranges.size()) == (1u))));
        }
    }
};

"test_atx_heading_uses_the_complete_inline_parser"_test = [] {
    auto out = parse_text(1, "### [#](https://example.com/path#fragment) **bold** `code` <em>html</em>\n");
    expect(fatal(bool((out.document.blocks.size()) == (1u))));
    if (!out.document.blocks.empty()) {
        auto const& heading = out.document.blocks[0];
        expect(fatal(bool(heading.kind == BlockKind::Heading)));
        expect(fatal(bool((heading.level) == (3))));
        expect(fatal(bool(inlines_have_kind(heading.children, InlineKind::Link))));
        expect(fatal(bool(inlines_have_kind(heading.children, InlineKind::Strong))));
        expect(fatal(bool(inlines_have_kind(heading.children, InlineKind::InlineCode))));
        expect(fatal(bool(inlines_have_kind(heading.children, InlineKind::Emphasis))));
    }
    expect(fatal(bool((out.symbols.links.size()) == (1u))));
};

"test_parse_paragraph"_test = [] {
    auto out = parse_text(1, "Hello world\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    expect(fatal(bool(p != nullptr)));
    expect(fatal(bool(!p->children.empty())));
};

"test_single_newline_inside_plain_text_is_soft_break"_test = [] {
    auto out = parse_text(1, "Hello\nWorld");
    expect(fatal(bool((out.document.blocks.size()) == (1u))));
    expect(fatal(bool(out.document.blocks[0].kind == BlockKind::Paragraph)));
    expect(fatal(bool(inlines_have_kind(out.document.blocks[0].children, InlineKind::SoftBreak))));
    expect(fatal(bool(cps_to_utf8(block_inline_text_content(out.document.blocks[0].children)) == "Hello\nWorld")));
};

"test_two_spaces_before_newline_are_a_hard_break"_test = [] {
    auto out = parse_text(1, "Hello  \nWorld");
    expect(fatal(bool((out.document.blocks.size()) == (1u))));
    expect(fatal(bool(out.document.blocks[0].kind == BlockKind::Paragraph)));
    expect(fatal(bool(inlines_have_kind(out.document.blocks[0].children, InlineKind::HardBreak))));
    auto hard = std::find_if(out.document.blocks[0].children.begin(), out.document.blocks[0].children.end(), [](auto const& node) { return node.kind == InlineKind::HardBreak; });
    expect(fatal(bool(hard != out.document.blocks[0].children.end())));
    if (hard != out.document.blocks[0].children.end()) {
        auto range = out.document.source_map.find_node_by_id(hard->id);
        expect(fatal(bool(range != nullptr)));
        if (range) {
            expect(fatal(bool((range->source_range.start.v) == (5u))));
            expect(fatal(bool((range->source_range.end.v) == (8u))));
            expect(fatal(bool((range->content_range.start.v) == (7u))));
            expect(fatal(bool((range->content_range.end.v) == (8u))));
            expect(fatal(bool((range->marker_ranges.size()) == (1u))));
        }
    }
};

"test_trailing_blank_line_preserves_eof_empty_block"_test = [] {
    auto out = parse_text(1, "Hello\n\n");
    expect(fatal(bool((out.document.blocks.size()) == (2u))));
    expect(fatal(bool(out.document.blocks[0].kind == BlockKind::Paragraph)));
    expect(fatal(bool(out.document.blocks[1].kind == BlockKind::Paragraph)));
    expect(fatal(bool(out.document.blocks[1].children.empty())));
};

"test_parse_block_break_between_paragraphs_is_separator_no_empty_block"_test = [] {
    auto out = parse_text(1, "Hello\n\nWorld");
    expect(fatal(bool((out.document.blocks.size()) == (2u))));
    expect(fatal(bool(!out.document.blocks[0].children.empty())));
    expect(fatal(bool(!out.document.blocks[1].children.empty())));
};

"test_extra_blank_line_becomes_an_empty_paragraph"_test = [] {
    auto out = parse_text(1, "Hello\n\n\nWorld");
    expect(fatal(bool((out.document.blocks.size()) == (3u))));
    expect(fatal(bool(!out.document.blocks[0].children.empty())));
    expect(fatal(bool(out.document.blocks[1].children.empty())));
    expect(fatal(bool(!out.document.blocks[2].children.empty())));
};

"test_parse_trailing_consecutive_blank_lines_as_explicit_and_eof_blocks"_test = [] {
    auto out = parse_text(1, "Hello\n\n\n");
    expect(fatal(bool((out.document.blocks.size()) == (3u))));
    expect(fatal(bool(out.document.blocks[1].children.empty())));
    expect(fatal(bool(out.document.blocks[2].children.empty())));
};

"test_incremental_enter_rescans_from_previous_block_and_keeps_suffix"_test = [] {
    std::string old_text = "one\n\ntwo\n\nthree";
    auto old_out = parse_text(1, old_text);
    std::string new_text = "one\n\ntwo\n\n\nthree";
    IncrementalParseEdit edit{CharRange(CharOffset(8), CharOffset(8)), U"\n"};
    auto out = parse_incremental(ParseInput(2, new_text), old_out.document, old_out.symbols, old_text, edit);
    expect(fatal(bool((out.document.blocks.size()) == (4u))));
    expect(fatal(bool(!out.document.blocks[0].children.empty())));
    expect(fatal(bool(!out.document.blocks[1].children.empty())));
    expect(fatal(bool(out.document.blocks[2].children.empty())));
    expect(fatal(bool(!out.document.blocks[3].children.empty())));
    expect(fatal(bool(out.document.blocks[3].id == old_out.document.blocks[2].id)));
    auto* suffix_range = out.document.source_map.find_node_by_id(out.document.blocks[3].id);
    expect(fatal(bool(suffix_range != nullptr)));
    expect(fatal(bool((suffix_range->source_range.start.v) == (11u))));
};

"test_incremental_trailing_enter_keeps_ast_semantic"_test = [] {
    std::string old_text = "alpha";
    auto old_out = parse_text(1, old_text);
    std::string new_text = "alpha\n";
    IncrementalParseEdit edit{CharRange(CharOffset(5), CharOffset(5)), U"\n"};
    auto out = parse_incremental(ParseInput(2, new_text), old_out.document, old_out.symbols, old_text, edit);
    expect(fatal(bool((out.document.blocks.size()) == (1u))));
    expect(fatal(bool(out.document.blocks[0].kind == BlockKind::Paragraph)));
};

"test_incremental_repeated_trailing_enter_keeps_ast_semantic"_test = [] {
    std::string old_text = "alpha\n\n";
    auto old_out = parse_text(1, old_text);
    std::string new_text = "alpha\n\n\n";
    IncrementalParseEdit edit{CharRange(CharOffset(7), CharOffset(7)), U"\n"};
    auto out = parse_incremental(ParseInput(2, new_text), old_out.document, old_out.symbols, old_text, edit);
    expect(fatal(bool((out.document.blocks.size()) == (3u))));
    expect(fatal(bool(out.document.blocks[1].kind == BlockKind::Paragraph)));
    expect(fatal(bool(out.document.blocks[1].children.empty())));
    expect(fatal(bool(out.document.blocks[2].kind == BlockKind::Paragraph)));
    expect(fatal(bool(out.document.blocks[2].children.empty())));
};

"test_parse_strong"_test = [] {
    auto out = parse_text(1, "Hello **world**\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    expect(fatal(bool(p && inlines_have_kind(p->children, InlineKind::Strong))));
};

"test_parse_strikethrough"_test = [] {
    auto out = parse_text(1, "Hello ~~world~~\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    expect(fatal(bool(p && inlines_have_kind(p->children, InlineKind::Strike))));
};

"test_delimited_inline_nodes_preserve_exact_source_markers"_test = [] {
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
        expect(fatal(bool(paragraph != nullptr)));
        if (!paragraph) continue;
        auto node = std::find_if(paragraph->children.begin(), paragraph->children.end(), [&](auto const& child) {
            return child.kind == test.kind;
        });
        expect(fatal(bool(node != paragraph->children.end())));
        if (node == paragraph->children.end()) continue;
        expect(fatal(bool((node->opening_marker) == (test.marker))));
        expect(fatal(bool((node->closing_marker) == (test.marker))));
        auto range = out.document.source_map.find_node_by_id(node->id);
        expect(fatal(bool(range != nullptr)));
        if (!range) continue;
        expect(fatal(bool((range->source_range.start.v) == (0u))));
        expect(fatal(bool((range->source_range.end.v) == (test.source.size()))));
        expect(fatal(bool((range->content_range.start.v) == (test.content_start))));
        expect(fatal(bool((range->content_range.end.v) == (test.content_end))));
        expect(fatal(bool((range->marker_ranges.size()) == (2u))));
        if (range->marker_ranges.size() == 2) {
            expect(fatal(bool((range->marker_ranges[0].start.v) == (0u))));
            expect(fatal(bool((range->marker_ranges[0].end.v) == (test.marker.size()))));
            expect(fatal(bool((range->marker_ranges[1].start.v) == (test.content_end))));
            expect(fatal(bool((range->marker_ranges[1].end.v) == (test.source.size()))));
        }
    }
};

"test_mixed_nested_delimiters_keep_independent_source_ranges"_test = [] {
    auto out = parse_text(1, "_outer **inner** tail_");
    auto* paragraph = first_of(out.document.blocks, BlockKind::Paragraph);
    expect(fatal(bool(paragraph != nullptr)));
    if (!paragraph || paragraph->children.empty()) return;
    auto const& emphasis = paragraph->children.front();
    expect(fatal(bool(emphasis.kind == InlineKind::Emphasis)));
    expect(fatal(bool((emphasis.opening_marker) == (std::u32string(U"_")))));
    auto strong = std::find_if(emphasis.children.begin(), emphasis.children.end(), [](auto const& child) {
        return child.kind == InlineKind::Strong;
    });
    expect(fatal(bool(strong != emphasis.children.end())));
    auto outer_range = out.document.source_map.find_node_by_id(emphasis.id);
    expect(fatal(bool(outer_range != nullptr)));
    if (outer_range) {
        expect(fatal(bool((outer_range->source_range.start.v) == (0u))));
        expect(fatal(bool((outer_range->source_range.end.v) == (22u))));
        expect(fatal(bool((outer_range->content_range.start.v) == (1u))));
        expect(fatal(bool((outer_range->content_range.end.v) == (21u))));
    }
    if (strong != emphasis.children.end()) {
        auto inner_range = out.document.source_map.find_node_by_id(strong->id);
        expect(fatal(bool(inner_range != nullptr)));
        if (inner_range) {
            expect(fatal(bool((inner_range->source_range.start.v) == (7u))));
            expect(fatal(bool((inner_range->source_range.end.v) == (16u))));
            expect(fatal(bool((inner_range->content_range.start.v) == (9u))));
            expect(fatal(bool((inner_range->content_range.end.v) == (14u))));
        }
    }
};

"test_parse_inline_code"_test = [] {
    auto out = parse_text(1, "Hello `code` world\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    expect(fatal(bool(p && inlines_have_kind(p->children, InlineKind::InlineCode))));
};

"test_inline_code_requires_equal_complete_backtick_runs"_test = [] {
    auto out = parse_text(1, "x ``cpp ``` y\n");
    expect(fatal(bool((out.document.blocks.size()) == (1u))));
    expect(fatal(bool(out.document.blocks[0].kind == BlockKind::Paragraph)));
    expect(fatal(bool(!inlines_have_kind(out.document.blocks[0].children, InlineKind::InlineCode))));
    expect(fatal(bool((cps_to_utf8(block_inline_text_content(out.document.blocks[0].children))) == (std::string("x ``cpp ``` y")))));
};

"test_inline_code_preserves_exact_multi_backtick_marker_ranges"_test = [] {
    auto out = parse_text(1, "``code ` tick``\n");
    auto* paragraph = first_of(out.document.blocks, BlockKind::Paragraph);
    expect(fatal(bool(paragraph != nullptr)));
    const InlineNode* code = nullptr;
    if (paragraph) for (const auto& child : paragraph->children) if (child.kind == InlineKind::InlineCode) code = &child;
    expect(fatal(bool(code != nullptr)));
    if (code) {
        expect(fatal(bool((cps_to_utf8(code->text)) == (std::string("code ` tick")))));
        auto* range = out.document.source_map.find_node_by_id(code->id);
        expect(fatal(bool(range != nullptr)));
        if (range) {
            expect(fatal(bool((range->marker_ranges.size()) == (2u))));
            expect(fatal(bool((range->marker_ranges[0].len()) == (2u))));
            expect(fatal(bool((range->marker_ranges[1].len()) == (2u))));
        }
    }
};

"test_inline_code_normalizes_single_newline_to_space"_test = [] {
    auto out = parse_text(1, "``a\nb``\n");
    auto* paragraph = first_of(out.document.blocks, BlockKind::Paragraph);
    expect(fatal(bool(paragraph != nullptr)));
    const InlineNode* code = nullptr;
    if (paragraph) for (const auto& child : paragraph->children) if (child.kind == InlineKind::InlineCode) code = &child;
    expect(fatal(bool(code != nullptr)));
    if (code) expect(fatal(bool((cps_to_utf8(code->text)) == (std::string("a b")))));
};

"test_unmatched_inline_delimiters_are_text"_test = [] {
    auto out = parse_text(1, "Hello **world\nnext ~~line\nplain *text\ncode `span\n");
    expect(fatal(bool((out.document.blocks.size()) == (1u))));
    for (auto const& block : out.document.blocks) {
        expect(fatal(bool(block.kind == BlockKind::Paragraph)));
        expect(fatal(bool(!inlines_have_kind(block.children, InlineKind::Strong))));
        expect(fatal(bool(!inlines_have_kind(block.children, InlineKind::Emphasis))));
        expect(fatal(bool(!inlines_have_kind(block.children, InlineKind::Strike))));
        expect(fatal(bool(!inlines_have_kind(block.children, InlineKind::InlineCode))));
    }
    expect(fatal(bool(cps_to_utf8(block_inline_text_content(out.document.blocks[0].children)) == "Hello **world\nnext ~~line\nplain *text\ncode `span")));
};

"test_backslash_escapes_all_ascii_punctuation"_test = [] {
    auto out = parse_text(1, R"(\!\"\#\$\%\&\'\*\+\,\-\.\/\:\;\<\=\>\?\@\[\\\]\^\_\`\{\|\}\~)");
    auto* paragraph = first_of(out.document.blocks, BlockKind::Paragraph);
    expect(fatal(bool(paragraph != nullptr)));
    if (paragraph) {
        auto actual = cps_to_utf8(block_inline_text_content(paragraph->children));
        expect(fatal(bool((actual) == (std::string("!\"#$%&'*+,-./:;<=>?@[\\]^_`{|}~")))));
    }
};

"test_structural_inlines_inside_emphasis"_test = [] {
    auto out = parse_text(1, "**[link](https://example.com) and `code` and <span>html</span>**\n");
    auto* paragraph = first_of(out.document.blocks, BlockKind::Paragraph);
    expect(fatal(bool(paragraph != nullptr)));
    if (paragraph) {
        auto strong = std::find_if(paragraph->children.begin(), paragraph->children.end(), [](auto const& node) { return node.kind == InlineKind::Strong; });
        expect(fatal(bool(strong != paragraph->children.end())));
        if (strong != paragraph->children.end()) {
            expect(fatal(bool(inlines_have_kind(strong->children, InlineKind::Link))));
            expect(fatal(bool(inlines_have_kind(strong->children, InlineKind::InlineCode))));
            expect(fatal(bool(inlines_have_kind(strong->children, InlineKind::Span))));
        }
    }
};

"test_parse_inline_math"_test = [] {
    auto out = parse_text(1, "Hello $x+1$ world\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    expect(fatal(bool(p && inlines_have_kind(p->children, InlineKind::InlineMath))));
};

"test_structural_inline_at_paragraph_end_updates_content_range"_test = [] {
    std::vector<std::string> sources{
        "$x$", "\\(x\\)", "**x**", "*x*", "__x__", "_x_", "~~x~~", "`x`",
        "[x](u)", "a![x](p)", "[^x]", "[[x]]",
    };
    for (auto const& source : sources) {
        auto out = parse_text(1, source);
        expect(fatal(bool((out.document.blocks.size()) == (1u))));
        auto range = out.document.source_map.find_node_by_id(out.document.blocks[0].id);
        expect(fatal(bool(range != nullptr)));
        expect(fatal(bool((range->content_range.start.v) == (0u))));
        expect(fatal(bool((range->content_range.end.v) == (utf8_to_cps(source).size()))));
    }
};

"test_parse_block_math"_test = [] {
    auto out = parse_text(1, "$$\nE=mc^2\n$$\n");
    expect(fatal(bool(blocks_has_kind(out.document.blocks, BlockKind::MathBlock))));
};

"test_thematic_break_is_consumed_once_with_exact_ranges"_test = [] {
    auto out = parse_text(1, "---\n\nbody");
    expect(fatal(bool((out.document.blocks.size()) == (2u))));
    expect(fatal(bool(out.document.blocks[0].kind == BlockKind::ThematicBreak)));
    auto range = out.document.source_map.find_node_by_id(out.document.blocks[0].id);
    expect(fatal(bool(range != nullptr)));
    expect(fatal(bool((range->source_range.start.v) == (0u))));
    expect(fatal(bool((range->source_range.end.v) == (3u))));
    expect(fatal(bool((range->content_range.start.v) == (0u))));
    expect(fatal(bool((range->content_range.end.v) == (3u))));
    expect(fatal(bool(out.document.blocks[1].kind == BlockKind::Paragraph)));
};

"test_thematic_break_rejects_spacing_between_markers"_test = [] {
    for (auto const& source : {std::string("- - -"), std::string("*  *  *"), std::string("_\t_\t_")}) {
        auto out = parse_text(1, source);
        expect(fatal(bool((out.document.blocks.size()) == (1u))));
        expect(fatal(bool(out.document.blocks[0].kind != BlockKind::ThematicBreak)));
    }
};

"test_thematic_break_accepts_arbitrarily_long_matching_marker_runs"_test = [] {
    for (auto const& source : {std::string("----"), std::string("******"), std::string("_________________")}) {
        auto out = parse_text(1, source);
        expect(fatal(bool((out.document.blocks.size()) == (1u))));
        expect(fatal(bool(out.document.blocks[0].kind == BlockKind::ThematicBreak)));
    }
};

"test_spaced_markers_remain_paragraph_text"_test = [] {
    auto out = parse_text(1, "before\n_ _ _ _\nafter");
    expect(fatal(bool((out.document.blocks.size()) == (1u))));
    expect(fatal(bool(out.document.blocks[0].kind == BlockKind::Paragraph)));
};

"test_long_dash_rule_before_text_is_thematic_and_after_text_is_setext"_test = [] {
    auto out = parse_text(1, "------\nbody\n------");
    expect(fatal(bool((out.document.blocks.size()) == (2u))));
    if (out.document.blocks.size() >= 2) {
        expect(fatal(bool(out.document.blocks[0].kind == BlockKind::ThematicBreak)));
        expect(fatal(bool(out.document.blocks[1].kind == BlockKind::Heading)));
        expect(fatal(bool((out.document.blocks[1].level) == (2))));
    }
    expect(fatal(bool(!blocks_has_kind(out.document.blocks, BlockKind::Frontmatter))));
};

"test_frontmatter_is_only_recognized_at_document_start"_test = [] {
    auto out = parse_text(1, "body\n---\ntitle: nope\n---");
    expect(fatal(bool(!blocks_has_kind(out.document.blocks, BlockKind::Frontmatter))));
    expect(fatal(bool((std::count_if(out.document.blocks.begin(), out.document.blocks.end(), [](auto const& block) {
        return block.kind == BlockKind::ThematicBreak;
    })) == (0))));
    expect(fatal(bool((std::count_if(out.document.blocks.begin(), out.document.blocks.end(), [](auto const& block) {
        return block.kind == BlockKind::Heading && block.level == 2;
    })) == (2))));
};

"test_parse_inline_paren_math"_test = [] {
    auto out = parse_text(1, "Speed \\(v=\\frac{d}{t}\\) now\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    expect(fatal(bool(p && inlines_have_kind(p->children, InlineKind::InlineMath))));
    auto it = std::find_if(p->children.begin(), p->children.end(), [](auto const& n) {
        return n.kind == InlineKind::InlineMath;
    });
    expect(fatal(bool(it != p->children.end())));
    expect(fatal(bool(it->math_delim == MathDelimiter::InlineParen)));
    expect(fatal(bool(cps_to_utf8(it->text) == "v=\\frac{d}{t}")));
};

"test_parse_bracket_math_block"_test = [] {
    auto out = parse_text(1, "\\[\nE=mc^2\n\\]\n");
    auto* math = first_of(out.document.blocks, BlockKind::MathBlock);
    expect(fatal(bool(math != nullptr)));
    expect(fatal(bool(math->math_delim == MathDelimiter::BlockBracket)));
    expect(fatal(bool(cps_to_utf8(math->tex) == "E=mc^2")));
};

"test_parse_code_block"_test = [] {
    std::string source = "```rust\nfn main() {}\n```\n";
    auto out = parse_text(1, source);
    auto* cb = first_of(out.document.blocks, BlockKind::CodeBlock);
    expect(fatal(bool(cb != nullptr)));
    expect(fatal(bool(cb->language && *cb->language == "rust")));
    expect(fatal(bool((cps_to_utf8(cb->code_text)) == (std::string("fn main() {}\n")))));
    auto* range = out.document.source_map.find_node_by_id(cb->id);
    expect(fatal(bool(range != nullptr)));
    expect(fatal(bool((range->source_range.start.v) == (0u))));
    expect(fatal(bool((range->source_range.end.v) == (utf8_to_cps(source).size()))));
    expect(fatal(bool((range->content_range.start.v) == (8u))));
    expect(fatal(bool((range->content_range.end.v) == (21u))));
    expect(fatal(bool((range->marker_ranges.size()) == (2u))));
};

"test_parse_code_block_inline_math_inert"_test = [] {
    auto out = parse_text(1, "```\n$a+b$\n```\n");
    auto* cb = first_of(out.document.blocks, BlockKind::CodeBlock);
    expect(fatal(bool(cb != nullptr)));
    expect(fatal(bool(cps_to_utf8(cb->code_text).find("$a+b$") != std::string::npos)));
};

"test_parse_empty_code_block_closes_on_first_content_line"_test = [] {
    auto out = parse_text(1, "```cpp\n```\n");
    auto* block = first_of(out.document.blocks, BlockKind::CodeBlock);
    expect(fatal(bool(block != nullptr)));
    expect(fatal(bool(block->language && *block->language == "cpp")));
    expect(fatal(bool(block->code_text.empty())));
    auto* range = out.document.source_map.find_node_by_id(block->id);
    expect(fatal(bool(range != nullptr)));
    expect(fatal(bool((range->content_range.start.v) == (range->content_range.end.v))));
    expect(fatal(bool((range->marker_ranges.size()) == (2u))));
};

"test_fenced_code_interrupts_paragraph_without_blank_line"_test = [] {
    auto out = parse_text(1, "before\n```cpp\n```\n");
    expect(fatal(bool((out.document.blocks.size()) == (2u))));
    expect(fatal(bool(out.document.blocks[0].kind == BlockKind::Paragraph)));
    expect(fatal(bool(out.document.blocks[1].kind == BlockKind::CodeBlock)));
    expect(fatal(bool(out.document.blocks[1].language && *out.document.blocks[1].language == "cpp")));
    auto* range = out.document.source_map.find_node_by_id(out.document.blocks[1].id);
    expect(fatal(bool(range != nullptr)));
    if (range) {
        expect(fatal(bool((range->source_range.start.v) == (7u))));
        expect(fatal(bool((range->marker_ranges.size()) == (2u))));
    }
};

"test_parse_fenced_math"_test = [] {
    auto out = parse_text(1, "```math\nE=mc^2\n```\n");
    expect(fatal(bool(blocks_has_kind(out.document.blocks, BlockKind::MathBlock))));
};

"test_parse_toc"_test = [] {
    auto out = parse_text(1, "[TOC]\n");
    expect(fatal(bool(blocks_has_kind(out.document.blocks, BlockKind::Toc))));
};

"test_parse_wiki_toc"_test = [] {
    auto out = parse_text(1, "[[toc]]\n");
    expect(fatal(bool(blocks_has_kind(out.document.blocks, BlockKind::Toc))));
};

"test_parse_link"_test = [] {
    auto out = parse_text(1, "[link](https://example.com)\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    expect(fatal(bool(p && inlines_have_kind(p->children, InlineKind::Link))));
};

"test_reference_links_and_autolinks_resolve"_test = [] {
    auto out = parse_text(1, "[full][target] [collapsed][] <https://example.com> <me@example.com>\n\n[target]: https://example.com \"Title\"\n[collapsed]: /relative\n");
    auto* paragraph = first_of(out.document.blocks, BlockKind::Paragraph);
    expect(fatal(bool(paragraph != nullptr)));
    if (paragraph) {
        std::vector<std::string> hrefs;
        for (auto const& node : paragraph->children) if (node.kind == InlineKind::Link) hrefs.push_back(node.href);
        expect(fatal(bool((hrefs.size()) == (4u))));
        if (hrefs.size() == 4) {
            expect(fatal(bool((hrefs[0]) == (std::string("https://example.com")))));
            expect(fatal(bool((hrefs[1]) == (std::string("/relative")))));
            expect(fatal(bool((hrefs[2]) == (std::string("https://example.com")))));
            expect(fatal(bool((hrefs[3]) == (std::string("mailto:me@example.com")))));
        }
    }
    expect(fatal(bool((std::count_if(out.document.blocks.begin(), out.document.blocks.end(), [](auto const& block) { return block.kind == BlockKind::LinkDefinition; })) == (2))));
};

"test_parse_image"_test = [] {
    auto out = parse_text(1, "![alt](image.png)\n");
    auto* image = first_of(out.document.blocks, BlockKind::ImageBlock);
    expect(fatal(bool(image != nullptr)));
    if (image) {
        expect(fatal(bool((image->src) == (std::string("image.png")))));
        expect(fatal(bool((image->image_alt) == (std::string("alt")))));
    }
};

"test_parse_task_list"_test = [] {
    auto out = parse_text(1, "- [ ] todo\n");
    expect(fatal(bool(blocks_has_kind(out.document.blocks, BlockKind::TaskList))));
};

"test_parse_consecutive_unordered_list_as_one_block"_test = [] {
    auto out = parse_text(1, "- alpha\n- beta\n- gamma\n");
    expect(fatal(bool((out.document.blocks.size()) == (1u))));
    expect(fatal(bool(out.document.blocks[0].kind == BlockKind::List)));
    expect(fatal(bool((out.document.blocks[0].list_items.size()) == (3u))));
    expect(fatal(bool(!out.document.blocks[0].list_ordered)));
};

"test_parse_list_items_with_full_inline_syntax"_test = [] {
    auto out = parse_text(1, "- before $x^2$ and **bold** and `code`\n- next \\(y\\)\n");
    expect(fatal(bool((out.document.blocks.size()) == (1u))));
    auto const& list = out.document.blocks[0];
    expect(fatal(bool(list.kind == BlockKind::List)));
    expect(fatal(bool((list.list_items.size()) == (2u))));
    auto const& first = list.list_items[0].children[0].children;
    auto const& second = list.list_items[1].children[0].children;
    expect(fatal(bool(inlines_have_kind(first, InlineKind::InlineMath))));
    expect(fatal(bool(inlines_have_kind(first, InlineKind::Strong))));
    expect(fatal(bool(inlines_have_kind(first, InlineKind::InlineCode))));
    expect(fatal(bool(inlines_have_kind(second, InlineKind::InlineMath))));
    auto math = std::find_if(first.begin(), first.end(), [](auto const& node) { return node.kind == InlineKind::InlineMath; });
    expect(fatal(bool(math != first.end())));
    auto range = out.document.source_map.find_node_by_id(math->id);
    expect(fatal(bool(range != nullptr)));
    expect(fatal(bool((range->source_range.start.v) == (9u))));
    expect(fatal(bool((range->source_range.end.v) == (14u))));
    expect(fatal(bool((range->content_range.start.v) == (10u))));
    expect(fatal(bool((range->content_range.end.v) == (13u))));

    auto bounded = parse_text(2, "- `open\n- close`\n");
    expect(fatal(bool((bounded.document.blocks.size()) == (1u))));
    expect(fatal(bool((bounded.document.blocks[0].list_items.size()) == (2u))));
    auto const& bounded_first = bounded.document.blocks[0].list_items[0].children[0].children;
    expect(fatal(bool(!inlines_have_kind(bounded_first, InlineKind::InlineCode))));
};

"test_parse_math_inside_emphasis_nodes"_test = [] {
    auto out = parse_text(1, "**before $x$ after** *\\(y\\)* ~~$z$~~\n");
    expect(fatal(bool((out.document.blocks.size()) == (1u))));
    auto const& inlines = out.document.blocks[0].children;
    auto strong = std::find_if(inlines.begin(), inlines.end(), [](auto const& node) { return node.kind == InlineKind::Strong; });
    auto emphasis = std::find_if(inlines.begin(), inlines.end(), [](auto const& node) { return node.kind == InlineKind::Emphasis; });
    auto strike = std::find_if(inlines.begin(), inlines.end(), [](auto const& node) { return node.kind == InlineKind::Strike; });
    expect(fatal(bool(strong != inlines.end())));
    expect(fatal(bool(emphasis != inlines.end())));
    expect(fatal(bool(strike != inlines.end())));
    expect(fatal(bool(inlines_have_kind(strong->children, InlineKind::InlineMath))));
    expect(fatal(bool(inlines_have_kind(emphasis->children, InlineKind::InlineMath))));
    expect(fatal(bool(inlines_have_kind(strike->children, InlineKind::InlineMath))));

    auto quantified = parse_text(2, R"($\exists\delta>0,s.t. |x'-x_0|<\delta,0<|x''-x_0|<\delta$)");
    expect(fatal(bool((quantified.document.blocks.size()) == (1u))));
    expect(fatal(bool(inlines_have_kind(quantified.document.blocks[0].children, InlineKind::InlineMath))));
    auto formula = std::find_if(quantified.document.blocks[0].children.begin(), quantified.document.blocks[0].children.end(), [](auto const& node) { return node.kind == InlineKind::InlineMath; });
    expect(fatal(bool(formula != quantified.document.blocks[0].children.end())));
    expect(fatal(bool(formula->text == UR"(\exists\delta>0,s.t. |x'-x_0|<\delta,0<|x''-x_0|<\delta)")));
};

"test_parse_ordered_list_preserves_numbers_and_delimiter"_test = [] {
    auto out = parse_text(1, "9) alpha\n10) beta\n");
    expect(fatal(bool((out.document.blocks.size()) == (1u))));
    expect(fatal(bool(out.document.blocks[0].kind == BlockKind::List)));
    expect(fatal(bool(out.document.blocks[0].list_ordered)));
    expect(fatal(bool((out.document.blocks[0].list_start) == (9ull))));
    expect(fatal(bool((out.document.blocks[0].list_delimiter) == (U')'))));
    expect(fatal(bool((out.document.blocks[0].list_items.size()) == (2u))));
    expect(fatal(bool((out.document.blocks[0].list_items[1].marker) == (std::u32string(U"10) ")))));
};

"test_parse_consecutive_task_list_states"_test = [] {
    auto out = parse_text(1, "- [ ] alpha\n- [x] beta\n");
    expect(fatal(bool((out.document.blocks.size()) == (1u))));
    expect(fatal(bool(out.document.blocks[0].kind == BlockKind::TaskList)));
    expect(fatal(bool((out.document.blocks[0].task_items.size()) == (2u))));
    expect(fatal(bool(!out.document.blocks[0].task_items[0].checked)));
    expect(fatal(bool(out.document.blocks[0].task_items[1].checked)));
};

"test_parse_frontmatter_yaml"_test = [] {
    auto out = parse_text(1, "---\ntitle: Hello\ntags: [rust]\n---\n\nContent\n");
    expect(fatal(bool(blocks_has_kind(out.document.blocks, BlockKind::Frontmatter))));
    expect(fatal(bool(out.document.metadata.title && *out.document.metadata.title == "Hello")));
};

"test_safe_block_html_is_rendered_as_text_content"_test = [] {
    auto out = parse_text(1, "<div>\nhello *not emphasis*\n</div>\n");
    auto* paragraph = first_of(out.document.blocks, BlockKind::Paragraph);
    expect(fatal(bool(paragraph != nullptr)));
    if (paragraph) {
        expect(fatal(bool((cps_to_utf8(block_inline_text_content(paragraph->children))) == (std::string("hello *not emphasis*")))));
        expect(fatal(bool(!inlines_have_kind(paragraph->children, InlineKind::Emphasis))));
    }
};

"test_safe_inline_html_is_structural"_test = [] {
    auto out = parse_text(1, "hello <span>world</span>\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    expect(fatal(bool(p && inlines_have_kind(p->children, InlineKind::Span))));
};

"test_nested_safe_inline_html_is_structural"_test = [] {
    auto out = parse_text(1, "<span>outer <span>inner **bold**</span> tail</span>\n");
    auto* paragraph = first_of(out.document.blocks, BlockKind::Paragraph);
    expect(fatal(bool(paragraph != nullptr)));
    if (paragraph) {
        auto outer = std::find_if(paragraph->children.begin(), paragraph->children.end(), [](auto const& node) { return node.kind == InlineKind::Span; });
        expect(fatal(bool(outer != paragraph->children.end())));
        if (outer != paragraph->children.end()) {
            expect(fatal(bool(inlines_have_kind(outer->children, InlineKind::Span))));
            expect(fatal(bool(inlines_have_kind(outer->children, InlineKind::Strong))));
        }
    }
};

"test_unsafe_inline_html_targets_are_removed"_test = [] {
    auto out = parse_text(1, "<a href=\" JavaScript:alert(1)\">unsafe</a> <img src=\"data:text/html;base64,QQ==\">\n");
    auto* paragraph = first_of(out.document.blocks, BlockKind::Paragraph);
    expect(fatal(bool(paragraph != nullptr)));
    if (paragraph) {
        auto link = std::find_if(paragraph->children.begin(), paragraph->children.end(), [](auto const& node) { return node.kind == InlineKind::Link; });
        auto image = std::find_if(paragraph->children.begin(), paragraph->children.end(), [](auto const& node) { return node.kind == InlineKind::Image; });
        expect(fatal(bool(link != paragraph->children.end())));
        expect(fatal(bool(image != paragraph->children.end())));
        if (link != paragraph->children.end()) expect(fatal(bool(link->href.empty())));
        if (image != paragraph->children.end()) expect(fatal(bool(image->href.empty())));
    }
};

"test_nested_lists_preserve_nested_block_structure"_test = [] {
    auto out = parse_text(1, "- parent\n  - child\n    1. grandchild\n\n  > quote\n\n  ![alt](image.png)\n");
    auto* list = first_of(out.document.blocks, BlockKind::List);
    expect(fatal(bool(list != nullptr)));
    if (list && !list->list_items.empty()) {
        auto const& children = list->list_items[0].children;
        expect(fatal(bool(std::any_of(children.begin(), children.end(), [](auto const& block) { return block.kind == BlockKind::List; }))));
        expect(fatal(bool(std::any_of(children.begin(), children.end(), [](auto const& block) { return block.kind == BlockKind::BlockQuote; }))));
        expect(fatal(bool(std::any_of(children.begin(), children.end(), [](auto const& block) { return block.kind == BlockKind::ImageBlock; }))));
    }
};

"test_html_img_is_an_image_block"_test = [] {
    auto out = parse_text(1, "<img src=\"a.png\" alt=\"A\" width=\"320\" height=\"180px\">\n");
    auto* image = first_of(out.document.blocks, BlockKind::ImageBlock);
    expect(fatal(bool(image != nullptr)));
    if (image) {
        expect(fatal(bool((image->src) == (std::string("a.png")))));
        expect(fatal(bool((image->image_alt) == (std::string("A")))));
        expect(fatal(bool(image->image_width.has_value())));
        expect(fatal(bool(image->image_height.has_value())));
        if (image->image_width) expect(fatal(bool((*image->image_width) == (320.0f))));
        if (image->image_height) expect(fatal(bool((*image->image_height) == (180.0f))));
    }
};

"test_markdown_image_supported"_test = [] {
    auto out = parse_text(1, "![alt](a.png)\n");
    auto* image = first_of(out.document.blocks, BlockKind::ImageBlock);
    expect(fatal(bool(image != nullptr)));
};

"test_linked_markdown_image_is_an_image_block"_test = [] {
    auto out = parse_text(1, "[![rock](shiprock.jpg \"Shiprock\")](https://example.com)\n");
    auto* image = first_of(out.document.blocks, BlockKind::ImageBlock);
    expect(fatal(bool(image != nullptr)));
    if (image) {
        expect(fatal(bool((image->src) == (std::string("shiprock.jpg")))));
        expect(fatal(bool((image->image_alt) == (std::string("rock")))));
        expect(fatal(bool(image->image_title && *image->image_title == "Shiprock")));
        expect(fatal(bool(image->image_link && *image->image_link == "https://example.com")));
    }
};

"test_indented_code_block_strips_one_indent_level"_test = [] {
    auto out = parse_text(1, "    > first\n    >\n    >> nested\n");
    expect(fatal(bool((out.document.blocks.size()) == (1u))));
    expect(fatal(bool(out.document.blocks[0].kind == BlockKind::CodeBlock)));
    expect(fatal(bool(out.document.blocks[0].code_indented)));
    expect(fatal(bool((cps_to_utf8(out.document.blocks[0].code_text)) == (std::string("> first\n>\n>> nested\n")))));
    auto* range = out.document.source_map.find_node_by_id(out.document.blocks[0].id);
    expect(fatal(bool(range != nullptr)));
    if (range) expect(fatal(bool((range->marker_ranges.size()) == (3u))));
};

"test_indented_code_block_preserves_internal_blank_and_stops"_test = [] {
    auto out = parse_text(1, "    first\n\n    second\nafter\n");
    expect(fatal(bool((out.document.blocks.size()) == (2u))));
    expect(fatal(bool(out.document.blocks[0].kind == BlockKind::CodeBlock)));
    expect(fatal(bool((cps_to_utf8(out.document.blocks[0].code_text)) == (std::string("first\n\nsecond\n")))));
    expect(fatal(bool(out.document.blocks[1].kind == BlockKind::Paragraph)));
    expect(fatal(bool((cps_to_utf8(block_inline_text_content(out.document.blocks[1].children))) == (std::string("after")))));
};

"test_tab_indented_code_block"_test = [] {
    auto out = parse_text(1, "\tcode\n");
    expect(fatal(bool((out.document.blocks.size()) == (1u))));
    expect(fatal(bool(out.document.blocks[0].kind == BlockKind::CodeBlock)));
    expect(fatal(bool(out.document.blocks[0].code_indented)));
    expect(fatal(bool((cps_to_utf8(out.document.blocks[0].code_text)) == (std::string("code\n")))));
};

"test_indented_code_block_inside_ordered_list_keeps_block_semantics"_test = [] {
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
    expect(fatal(bool((out.document.blocks.size()) == (1u))));
    if (out.document.blocks.empty()) return;
    auto const& list = out.document.blocks.front();
    expect(fatal(bool(list.kind == BlockKind::List)));
    expect(fatal(bool((list.list_items.size()) == (3u))));
    if (list.list_items.size() < 2) return;
    auto const& second = list.list_items[1];
    auto code = std::find_if(second.children.begin(), second.children.end(), [](auto const& child) {
        return child.kind == BlockKind::CodeBlock;
    });
    expect(fatal(bool(code != second.children.end())));
    if (code != second.children.end()) {
        expect(fatal(bool(code->code_indented)));
        expect(fatal(bool((cps_to_utf8(code->code_text)) == (std::string("<html>\n  <head>\n    <title>Test</title>\n  </head>\n")))));
    }
};

"test_list_container_indent_without_four_extra_columns_is_not_code"_test = [] {
    auto out = parse_text(1,
        "1. First\n"
        "2. Second\n"
        "\n"
        "   <html>\n"
        "\n"
        "3. Third\n");
    auto* list = first_of(out.document.blocks, BlockKind::List);
    expect(fatal(bool(list != nullptr)));
    if (!list || list->list_items.size() < 2) return;
    expect(fatal(bool(std::none_of(list->list_items[1].children.begin(), list->list_items[1].children.end(), [](auto const& child) {
        return child.kind == BlockKind::CodeBlock;
    }))));
};

"test_blockquote_preserves_paragraph_text"_test = [] {
    auto out = parse_text(1, "> quoted text\n");
    auto* quote = first_of(out.document.blocks, BlockKind::BlockQuote);
    expect(fatal(bool(quote != nullptr)));
    if (quote) {
        expect(fatal(bool((quote->quote_children.size()) == (1u))));
        expect(fatal(bool(quote->quote_children[0].kind == BlockKind::Paragraph)));
        expect(fatal(bool((cps_to_utf8(block_inline_text_content(quote->quote_children[0].children))) == (std::string("quoted text")))));
    }
};

"test_blockquote_preserves_multiline_and_stops_before_following_text"_test = [] {
    auto out = parse_text(1, "> first line\n> second line\nafter\n");
    expect(fatal(bool((out.document.blocks.size()) == (2u))));
    auto* quote = first_of(out.document.blocks, BlockKind::BlockQuote);
    expect(fatal(bool(quote != nullptr)));
    if (quote) {
        expect(fatal(bool((quote->quote_children.size()) == (1u))));
        expect(fatal(bool(quote->quote_children[0].kind == BlockKind::Paragraph)));
        expect(fatal(bool((cps_to_utf8(block_inline_text_content(quote->quote_children[0].children))) == (std::string("first line\nsecond line")))));
    }
    if (!out.document.blocks.empty()) {
        expect(fatal(bool(out.document.blocks.back().kind == BlockKind::Paragraph)));
        expect(fatal(bool((cps_to_utf8(block_inline_text_content(out.document.blocks.back().children))) == (std::string("after")))));
    }
};

"test_blockquote_keeps_paragraphs_and_nested_blocks"_test = [] {
    auto out = parse_text(1, "> first\n>\n> second\n> > nested\n");
    auto* quote = first_of(out.document.blocks, BlockKind::BlockQuote);
    expect(fatal(bool(quote != nullptr)));
    if (quote) {
        expect(fatal(bool((quote->quote_children.size()) == (3u))));
        if (quote->quote_children.size() >= 3) {
            expect(fatal(bool(quote->quote_children[0].kind == BlockKind::Paragraph)));
            expect(fatal(bool(quote->quote_children[1].kind == BlockKind::Paragraph)));
            expect(fatal(bool(quote->quote_children[2].kind == BlockKind::BlockQuote)));
            expect(fatal(bool((cps_to_utf8(block_inline_text_content(quote->quote_children[0].children))) == (std::string("first")))));
            expect(fatal(bool((cps_to_utf8(block_inline_text_content(quote->quote_children[1].children))) == (std::string("second")))));
            if (!quote->quote_children[2].quote_children.empty()) {
                expect(fatal(bool((cps_to_utf8(block_inline_text_content(quote->quote_children[2].quote_children[0].children))) == (std::string("nested")))));
            }
        }
    }
};

"test_blockquote_preserves_heading_and_list_children"_test = [] {
    auto out = parse_text(1, "> # heading\n> - item\n");
    auto* quote = first_of(out.document.blocks, BlockKind::BlockQuote);
    expect(fatal(bool(quote != nullptr)));
    if (quote) {
        expect(fatal(bool((quote->quote_children.size()) == (2u))));
        if (quote->quote_children.size() >= 2) {
            expect(fatal(bool(quote->quote_children[0].kind == BlockKind::Heading)));
            expect(fatal(bool(quote->quote_children[1].kind == BlockKind::List)));
        }
    }
};

"test_safe_html_table_parsed"_test = [] {
    auto out = parse_text(1, "<table>\n<tr><td>A</td></tr>\n</table>\n");
    expect(fatal(bool(blocks_has_kind(out.document.blocks, BlockKind::Table))));
    auto* table = first_of(out.document.blocks, BlockKind::Table);
    expect(fatal(bool(table != nullptr)));
    if (table) expect(fatal(bool(!table->table_header_row)));
};

"test_safe_html_table_preserves_header_cells"_test = [] {
    auto out = parse_text(1, "<table><tr><th>A</th><th>B</th></tr><tr><td>1</td><td>2</td></tr></table>\n");
    auto* table = first_of(out.document.blocks, BlockKind::Table);
    expect(fatal(bool(table != nullptr)));
    if (table) {
        expect(fatal(bool(table->table_header_row)));
        expect(fatal(bool((table->table_header.size()) == (2u))));
        expect(fatal(bool((table->table_rows.size()) == (1u))));
    }
};

"test_gfm_table_parsed"_test = [] {
    auto out = parse_text(1, "| A | B |\n|---|---|\n| 1 | 2 |\n");
    expect(fatal(bool(blocks_has_kind(out.document.blocks, BlockKind::Table))));
};

"test_gfm_table_cell_ranges_and_alignments"_test = [] {
    std::string source = "| A | B |\n| :--- | ---: |\n| 1 | 2 |\n";
    auto out = parse_text(1, source);
    auto* table = first_of(out.document.blocks, BlockKind::Table);
    expect(fatal(bool(table != nullptr)));
    if (table) {
        expect(fatal(bool((table->table_header.size()) == (2u))));
        expect(fatal(bool((table->table_rows.size()) == (1u))));
        expect(fatal(bool((table->table_aligns.size()) == (2u))));
        expect(fatal(bool(table->table_aligns[0] == TableAlignment::Left)));
        expect(fatal(bool(table->table_aligns[1] == TableAlignment::Right)));
        auto* first_cell = out.document.source_map.find_node_by_id(table->table_header[0].id);
        auto* second_cell = out.document.source_map.find_node_by_id(table->table_header[1].id);
        expect(fatal(bool(first_cell != nullptr)));
        expect(fatal(bool(second_cell != nullptr)));
        if (first_cell && second_cell) {
            expect(fatal(bool((first_cell->content_range.start.v) == (2u))));
            expect(fatal(bool((first_cell->content_range.end.v) == (3u))));
            expect(fatal(bool((second_cell->content_range.start.v) == (6u))));
            expect(fatal(bool((second_cell->content_range.end.v) == (7u))));
        }
    }
};

"test_gfm_table_escaped_pipe_stays_in_its_cell"_test = [] {
    auto out = parse_text(1, "| A\\|B | C |\n| --- | --- |\n| 1 | 2 |\n");
    auto* table = first_of(out.document.blocks, BlockKind::Table);
    expect(fatal(bool(table != nullptr)));
    if (table) {
        expect(fatal(bool((table->table_header.size()) == (2u))));
        expect(fatal(bool((cps_to_utf8(block_inline_text_content(table->table_header[0].children))) == (std::string("A|B")))));
    }
};

"test_gfm_table_cells_parse_inline_semantics"_test = [] {
    auto out = parse_text(1, "| **bold** | $x$ |\n| --- | --- |\n| `code` | ~~gone~~ |\n");
    auto* table = first_of(out.document.blocks, BlockKind::Table);
    expect(fatal(bool(table != nullptr)));
    if (table) {
        expect(fatal(bool(inlines_have_kind(table->table_header[0].children, InlineKind::Strong))));
        expect(fatal(bool(inlines_have_kind(table->table_header[1].children, InlineKind::InlineMath))));
        expect(fatal(bool(inlines_have_kind(table->table_rows[0].cells[0].children, InlineKind::InlineCode))));
        expect(fatal(bool(inlines_have_kind(table->table_rows[0].cells[1].children, InlineKind::Strike))));
    }
};

"test_gfm_table_inline_delimiters_do_not_cross_cells"_test = [] {
    auto out = parse_text(1, "| *left | right* |\n| --- | --- |\n");
    auto* table = first_of(out.document.blocks, BlockKind::Table);
    expect(fatal(bool(table != nullptr)));
    if (table) {
        expect(fatal(bool(!inlines_have_kind(table->table_header[0].children, InlineKind::Emphasis))));
        expect(fatal(bool(!inlines_have_kind(table->table_header[1].children, InlineKind::Emphasis))));
    }
};

"test_invalid_table_probe_does_not_leave_orphan_source_ranges"_test = [] {
    auto out = parse_text(1, "| A | B |\nnot a separator\n");
    expect(fatal(bool((out.document.blocks.size()) == (1u))));
    expect(fatal(bool(out.document.blocks[0].kind == BlockKind::Paragraph)));
    expect(fatal(bool((out.document.source_map.node_ranges.size()) == (2u))));
    auto* paragraph = out.document.source_map.find_node_by_id(out.document.blocks[0].id);
    expect(fatal(bool(paragraph != nullptr)));
    if (paragraph) {
        expect(fatal(bool((paragraph->source_range.start.v) == (0u))));
        expect(fatal(bool((paragraph->source_range.end.v) == (26u))));
    }
};

"test_table_body_cells_are_capped_at_header_columns"_test = [] {
    auto out = parse_text(1, "| A | B |\n| --- | --- |\n| 1 | 2 | 3 |\n");
    auto* table = first_of(out.document.blocks, BlockKind::Table);
    expect(fatal(bool(table != nullptr)));
    if (table) {
        expect(fatal(bool((table->table_header.size()) == (2u))));
        expect(fatal(bool((table->table_rows.size()) == (1u))));
        expect(fatal(bool((table->table_rows[0].cells.size()) == (2u))));
    }
};

"test_wiki_link"_test = [] {
    auto out = parse_text(1, "[[Page Name]]\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    expect(fatal(bool(p && inlines_have_kind(p->children, InlineKind::WikiLink))));
};

"test_unclosed_math_diagnostic"_test = [] {
    auto out = parse_text(1, "$x + 1\n");
    bool has_e001 = false;
    for (const auto& d : out.diagnostics) if (d.code && *d.code == std::string(DIAG_UNCLOSED_MATH_DOLLAR)) has_e001 = true;
    expect(fatal(bool(has_e001)));
};

"test_heading_multiple_levels_outline"_test = [] {
    auto out = parse_text(1, "# H1\n## H2\n### H3\n");
    expect(fatal(bool((out.outline.items.size()) == (1u))));
    expect(fatal(bool((out.outline.items[0].children.size()) == (1u))));
    if (!out.outline.items.empty() && !out.outline.items[0].children.empty())
        expect(fatal(bool((out.outline.items[0].children[0].children.size()) == (1u))));
};

"test_callout_parsed"_test = [] {
    auto out = parse_text(1, "> [!NOTE]\n> This is a note\n");
    expect(fatal(bool(blocks_has_kind(out.document.blocks, BlockKind::Callout))));
};

"test_footnote_definition"_test = [] {
    auto out = parse_text(1, "[^1]: This is a footnote\n");
    expect(fatal(bool(blocks_has_kind(out.document.blocks, BlockKind::FootnoteDefinition))));
};

"parse_emphasis_not_strong"_test = [] {
    // Regression for HANDOFF bug #2: `*native*` must be Emphasis not Strong.
    auto out = parse_text(1, "Hello *native* world\n");
    auto* p = first_of(out.document.blocks, BlockKind::Paragraph);
    expect(fatal(bool(p && inlines_have_kind(p->children, InlineKind::Emphasis))));
    expect(fatal(bool(!inlines_have_kind(p->children, InlineKind::Strong))));
};

"parse_heading_no_space_is_not_heading"_test = [] {
    auto out = parse_text(1, "###no-space\n");
    expect(fatal(bool(!blocks_has_kind(out.document.blocks, BlockKind::Heading))));
};

"parse_empty_list_item_has_editable_paragraph_anchor"_test = [] {
    auto out = parse_text(1, "- ");
    expect(fatal(bool((out.document.blocks.size()) == (1u))));
    expect(fatal(bool(out.document.blocks[0].kind == BlockKind::List)));
    expect(fatal(bool((out.document.blocks[0].list_items.size()) == (1u))));
    expect(fatal(bool((out.document.blocks[0].list_items[0].children.size()) == (1u))));
    const auto& paragraph = out.document.blocks[0].list_items[0].children[0];
    expect(fatal(bool(paragraph.kind == BlockKind::Paragraph)));
    expect(fatal(bool(out.document.source_map.find_node_by_id(paragraph.id) != nullptr)));
};

"parse_empty_quote_has_editable_paragraph_anchor"_test = [] {
    auto out = parse_text(1, "> ");
    expect(fatal(bool((out.document.blocks.size()) == (1u))));
    expect(fatal(bool(out.document.blocks[0].kind == BlockKind::BlockQuote)));
    expect(fatal(bool((out.document.blocks[0].quote_children.size()) == (1u))));
    const auto& paragraph = out.document.blocks[0].quote_children[0];
    expect(fatal(bool(paragraph.kind == BlockKind::Paragraph)));
    expect(fatal(bool(out.document.source_map.find_node_by_id(paragraph.id) != nullptr)));
};

}; // suite parser_tests
