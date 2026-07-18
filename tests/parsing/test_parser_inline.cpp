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

suite parser_inline_tests = [] {

"paragraph_inline_cst_preserves_every_spelling"_test = [] {
    const std::vector<std::string> cases{
        "abc", "*abc*", "_abc_", "**abc**", "__abc__", "**", "**abc",
        "a***b***c", "~~abc~~", "~~abc", "`abc`", "`abc ",
        "[title](url)", "[title](<url>)", "[title](url \"name\")", "[title](",
        "$abc$", "$abc", R"(\*abc\*)", "&amp;", R"(a\**b*)",
    };
    for (const auto& source : cases) {
        const auto parsed = parse_text(1, source);
        expect(fatal(bool(parsed.document.root.children.size() == 1u)));
        const auto& paragraph = parsed.document.root.children.front();
        expect(fatal(bool(paragraph.kind == BlockKind::Paragraph)));
        expect(fatal(bool(paragraph.inline_content.source == utf8_to_cps(source))));
        expect_lossless(paragraph.inline_content);
        expect(fatal(bool(serialize_markdown_cps(parsed.document) == utf8_to_cps(source))));
    }
};

"complete_inline_nodes_keep_exact_marker_ranges"_test = [] {
    struct Case { std::string source; InlineCstKind kind; std::size_t marker; };
    const std::vector<Case> cases{
        {"*word*", InlineCstKind::Emphasis, 1},
        {"_word_", InlineCstKind::Emphasis, 1},
        {"**word**", InlineCstKind::Strong, 2},
        {"__word__", InlineCstKind::Strong, 2},
        {"~~word~~", InlineCstKind::Strikethrough, 2},
        {"``code ` tick``", InlineCstKind::CodeSpan, 2},
        {"$word$", InlineCstKind::InlineMath, 1},
    };
    for (const auto& test : cases) {
        const auto parsed = parse_text(1, test.source);
        const auto& document = parsed.document.root.children.front().inline_content;
        const auto* node = first_inline(document, test.kind);
        expect(fatal(bool(node != nullptr)));
        if (!node) continue;
        expect(fatal(bool(node->status == ParseStatus::Complete)));
        expect(fatal(bool(node->range == SourceRange{0, document.source.size()})));
        const auto& delim = node->delimiter_ranges();
        expect(fatal(bool(delim.opening == SourceRange{0, test.marker})));
        expect(fatal(bool(delim.closing.has_value())));
        if (delim.closing) {
            expect(fatal(bool(delim.closing->length() == test.marker)));
            expect(fatal(bool(delim.closing->end == document.source.size())));
        }
    }
};

"incomplete_inline_syntax_remains_structural"_test = [] {
    for (const auto& source : {"**", "**abc", "~~abc", "`abc ", "[title](", "$abc"}) {
        const auto parsed = parse_text(1, source);
        const auto& document = parsed.document.root.children.front().inline_content;
        expect_lossless(document);
        const bool incomplete = std::any_of(document.tree.nodes.begin(), document.tree.nodes.end(), [](const auto& node) {
            return node.status != ParseStatus::Complete || node.kind == InlineCstKind::Incomplete || node.kind == InlineCstKind::Error;
        });
        expect(fatal(bool(incomplete)));
    }
};

"nested_inline_nodes_use_block_local_source_ranges"_test = [] {
    const auto parsed = parse_text(1, "_outer **inner** tail_");
    const auto& document = parsed.document.root.children.front().inline_content;
    const auto* emphasis = first_inline(document, InlineCstKind::Emphasis);
    const auto* strong = first_inline(document, InlineCstKind::Strong);
    expect(fatal(bool(emphasis != nullptr)));
    expect(fatal(bool(strong != nullptr)));
    if (emphasis) {
        expect(fatal(bool(emphasis->range == SourceRange{0, 22})));
        expect(fatal(bool(emphasis->delimiter_ranges().content == SourceRange{1, 21})));
    }
    if (strong) {
        expect(fatal(bool(strong->range == SourceRange{7, 16})));
        expect(fatal(bool(strong->delimiter_ranges().content == SourceRange{9, 14})));
    }
};

"line_breaks_are_lossless_and_visible"_test = [] {
    const auto soft = parse_text(1, "Hello\nWorld");
    const auto hard = parse_text(2, "Hello  \nWorld");
    expect(fatal(bool(has_inline(soft.document.root.children.front().inline_content, InlineCstKind::SoftBreak))));
    expect(fatal(bool(has_inline(hard.document.root.children.front().inline_content, InlineCstKind::HardBreak))));
    expect(fatal(bool(inline_visible_text(soft.document.root.children.front().inline_content) == U"Hello\nWorld")));
    expect(fatal(bool(inline_visible_text(hard.document.root.children.front().inline_content) == U"Hello\nWorld")));
    expect_lossless(soft.document.root.children.front().inline_content);
    expect_lossless(hard.document.root.children.front().inline_content);
};

"links_images_math_code_and_entities_are_structural"_test = [] {
    const auto parsed = parse_text(1,
        "[link](https://example.com \"title\") ![alt](image.png) `code` $x$ &amp; [[Page|alias]]");
    const auto& document = parsed.document.root.children.front().inline_content;
    expect(fatal(bool(has_inline(document, InlineCstKind::Link))));
    expect(fatal(bool(has_inline(document, InlineCstKind::Image))));
    expect(fatal(bool(has_inline(document, InlineCstKind::CodeSpan))));
    expect(fatal(bool(has_inline(document, InlineCstKind::InlineMath))));
    expect(fatal(bool(has_inline(document, InlineCstKind::Entity))));
    expect(fatal(bool(has_inline(document, InlineCstKind::WikiLink))));
    expect_lossless(document);
};

"block_forms_build_expected_structure"_test = [] {
    const auto parsed = parse_text(1,
        "> quote\n\n"
        "1. one\n2. two\n\n"
        "- [ ] todo\n- [x] done\n\n"
        "```cpp\nint x;\n```\n\n"
        "$$\nx+y\n$$\n\n"
        "---\n");
    expect(fatal(bool(first_block(parsed.document.root.children, BlockKind::BlockQuote) != nullptr)));
    const auto* list = first_block(parsed.document.root.children, BlockKind::List);
    expect(fatal(bool(list != nullptr)));
    if (list) {
        expect(fatal(bool(list->children.size() == 2u)));
        expect(fatal(bool(list->list_special().ordered)));
        expect(fatal(bool(list->list_special().start == 1u)));
    }
    const auto* tasks = first_block(parsed.document.root.children, BlockKind::TaskList);
    expect(fatal(bool(tasks != nullptr)));
    if (tasks) {
        expect(fatal(bool(tasks->children.size() == 2u)));
        expect(fatal(bool(!tasks->children[0].item_special().checked)));
        expect(fatal(bool(tasks->children[1].item_special().checked)));
    }
    expect(fatal(bool(first_block(parsed.document.root.children, BlockKind::CodeBlock) != nullptr)));
    expect(fatal(bool(first_block(parsed.document.root.children, BlockKind::MathBlock) != nullptr)));
    expect(fatal(bool(first_block(parsed.document.root.children, BlockKind::ThematicBreak) != nullptr)));
};

"fenced_code_accepts_lossless_backtick_and_tilde_fences"_test = [] {
    struct Case {
        std::string source;
        std::string language;
    };
    const std::vector<Case> cases{
        {"```cpp\nint x;\n```", "cpp"},
        {"~~~cpp\nint x;\n~~~", "cpp"},
        // A shorter run and a run of the other marker are code content. A
        // longer run of the opening marker is a valid closer.
        {"~~~~cpp\nalpha\n~~~\n```\nomega\n~~~~", "cpp"},
        {"   ~~~text\nvalue\n   ~~~~~", "text"},
    };

    for (const auto& test_case : cases) {
        const auto parsed = parse_text(1, test_case.source);
        expect(fatal(bool(parsed.document.root.children.size() == 1u))) << test_case.source;
        if (parsed.document.root.children.size() != 1u) continue;
        const auto& block = parsed.document.root.children.front();
        expect(fatal(bool(block.kind == BlockKind::CodeBlock))) << test_case.source;
        expect(fatal(bool(block.block_source.source() == utf8_to_cps(test_case.source)))) << test_case.source;
        expect(fatal(bool(block.block_source.tree().language
            == std::optional<std::string>{test_case.language}))) << test_case.source;
        expect(fatal(bool(block_source_tokens_partition(block.block_source)))) << test_case.source;
        expect(fatal(bool(flatten_block_source_tokens(block.block_source)
            == block.block_source.source()))) << test_case.source;
        expect(fatal(bool(serialize_markdown(parsed.document) == test_case.source))) << test_case.source;
    }

    const auto nested = parse_text(
        2,
        "> ~~~cpp\n"
        "> quoted();\n"
        "> ~~~\n\n"
        "- ~~~js\n"
        "  listed();\n"
        "  ~~~");
    const auto* quoted = first_block(nested.document.root.children, BlockKind::BlockQuote);
    const auto* listed = first_block(nested.document.root.children, BlockKind::List);
    expect(fatal(bool(quoted != nullptr)));
    expect(fatal(bool(listed != nullptr)));
    if (quoted) {
        const auto* code = first_block(quoted->children, BlockKind::CodeBlock);
        expect(fatal(bool(code != nullptr)));
        if (code) expect(fatal(bool(block_source_content(code->block_source) == U"quoted();\n")));
    }
    if (listed) {
        const auto* code = first_block(listed->children, BlockKind::CodeBlock);
        expect(fatal(bool(code != nullptr)));
        if (code) expect(fatal(bool(block_source_content(code->block_source) == U"listed();\n")));
    }
};

"math_blocks_preserve_exact_local_source_and_fences"_test = [] {
    for (const auto& source : {
             std::string{"$$\n  x + y  \n$$"},
             std::string{"\\[ x + y \\]"},
             std::string{"```math\n  x + y  \n```"},
             std::string{"$$\n  unclosed  "},
         }) {
        const auto parsed = parse_text(1, source);
        expect(fatal(bool(parsed.document.root.children.size() == 1u))) << source;
        if (parsed.document.root.children.size() != 1) continue;
        const auto& block = parsed.document.root.children.front();
        expect(fatal(bool(block.kind == BlockKind::MathBlock))) << source;
        expect(fatal(bool(block.block_source.source() == utf8_to_cps(source)))) << source;
        expect(fatal(bool(block_source_tokens_partition(block.block_source)))) << source;
        expect(fatal(bool(flatten_block_source_tokens(block.block_source) == block.block_source.source()))) << source;
        expect(fatal(bool(serialize_markdown(parsed.document) == source))) << source;
    }
};

}; // suite parser_inline_tests
