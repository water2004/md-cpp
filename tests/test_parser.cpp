#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "elmd_test.hpp"
import elmd.core.parser;
import elmd.core.ast;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.serializer;
import elmd.core.utf;

using namespace elmd;
using namespace boost::ut;

namespace {

const BlockNode* first_block(const BlockVec& blocks, BlockKind kind) {
    for (const auto& block : blocks) {
        if (block.kind == kind) return &block;
        if (auto* nested = first_block(block.children, kind)) return nested;
    }
    return nullptr;
}

bool has_inline(const InlineDocument& document, InlineCstKind kind) {
    return inline_contains_kind(document, kind);
}

const InlineCstNode* first_inline(const InlineCstNodes& nodes, InlineCstKind kind) {
    for (const auto& node : nodes) {
        if (node.kind == kind) return &node;
        if (auto* nested = first_inline(node.children, kind)) return nested;
    }
    return nullptr;
}

const InlineCstNode* first_inline(const InlineDocument& document, InlineCstKind kind) {
    return first_inline(document.tree.nodes, kind);
}

void expect_lossless(const InlineDocument& document) {
    expect(fatal(bool(tokens_partition_source(document.tree, document.source.size()))));
    expect(fatal(bool(roots_partition_source(document.tree, document.source.size()))));
    expect(fatal(bool(flatten_tokens(document.tree, document.source) == document.source)));
    expect(fatal(bool(serialize_lossless(document.tree, document.source) == document.source)));
}

} // namespace

suite parser_tests = [] {

"empty_document_has_no_blocks"_test = [] {
    const auto parsed = parse_text(1, "");
    expect(fatal(bool(parsed.document.root.children.empty())));
    expect(fatal(bool(parsed.revision == 1u)));
};

"headings_preserve_source_and_outline_data"_test = [] {
    const auto parsed = parse_text(1, "# *Hello*\n\nSetext\n------\n");
    expect(fatal(bool(parsed.document.root.children.size() == 2u)));
    const auto& atx = parsed.document.root.children[0];
    const auto& setext = parsed.document.root.children[1];
    expect(fatal(bool(atx.kind == BlockKind::Heading)));
    expect(fatal(bool(atx.level == 1u)));
    expect(fatal(bool(atx.inline_content.source == U"*Hello*")));
    expect(fatal(bool(has_inline(atx.inline_content, InlineCstKind::Emphasis))));
    expect_lossless(atx.inline_content);
    expect(fatal(bool(setext.kind == BlockKind::Heading)));
    expect(fatal(bool(setext.level == 2u)));
    expect(fatal(bool(setext.inline_content.source == U"Setext")));
    expect(fatal(bool(parsed.symbols.headings.size() == 2u)));
};

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
        expect(fatal(bool(node->delim.opening == SourceRange{0, test.marker})));
        expect(fatal(bool(node->delim.closing.has_value())));
        if (node->delim.closing) {
            expect(fatal(bool(node->delim.closing->length() == test.marker)));
            expect(fatal(bool(node->delim.closing->end == document.source.size())));
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
        expect(fatal(bool(emphasis->delim.content == SourceRange{1, 21})));
    }
    if (strong) {
        expect(fatal(bool(strong->range == SourceRange{7, 16})));
        expect(fatal(bool(strong->delim.content == SourceRange{9, 14})));
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
        expect(fatal(bool(list->list_ordered)));
        expect(fatal(bool(list->list_start == 1u)));
    }
    const auto* tasks = first_block(parsed.document.root.children, BlockKind::TaskList);
    expect(fatal(bool(tasks != nullptr)));
    if (tasks) {
        expect(fatal(bool(tasks->children.size() == 2u)));
        expect(fatal(bool(!tasks->children[0].checked)));
        expect(fatal(bool(tasks->children[1].checked)));
    }
    expect(fatal(bool(first_block(parsed.document.root.children, BlockKind::CodeBlock) != nullptr)));
    expect(fatal(bool(first_block(parsed.document.root.children, BlockKind::MathBlock) != nullptr)));
    expect(fatal(bool(first_block(parsed.document.root.children, BlockKind::ThematicBreak) != nullptr)));
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
        expect(fatal(bool(!block.opening_marker.empty()))) << source;
        expect(fatal(bool(serialize_markdown(parsed.document) == source))) << source;
    }
};

"nested_containers_retain_editable_inline_documents"_test = [] {
    const auto parsed = parse_text(1,
        "> # *heading*\n"
        "> - **item**\n\n"
        "[^n]: ~~footnote~~\n\n"
        "> [!NOTE] _title_\n> `body`\n");
    const auto* quote = first_block(parsed.document.root.children, BlockKind::BlockQuote);
    expect(fatal(bool(quote != nullptr)));
    const auto* heading = first_block(parsed.document.root.children, BlockKind::Heading);
    expect(fatal(bool(heading != nullptr)));
    if (heading) {
        expect(fatal(bool(heading->inline_content.source == U"*heading*")));
        expect_lossless(heading->inline_content);
    }
    const auto* footnote = first_block(parsed.document.root.children, BlockKind::FootnoteDefinition);
    expect(fatal(bool(footnote != nullptr)));
    const auto* callout = first_block(parsed.document.root.children, BlockKind::Callout);
    expect(fatal(bool(callout != nullptr)));
    if (callout && callout->callout_title) expect_lossless(*callout->callout_title);
};

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
    expect(fatal(bool(table->table_aligns == std::vector{TableAlignment::Left, TableAlignment::Right})));
    expect(fatal(bool(has_inline(table->children[0].children[0].inline_content, InlineCstKind::Strong))));
    expect(fatal(bool(has_inline(table->children[0].children[1].inline_content, InlineCstKind::InlineMath))));
    expect(fatal(bool(has_inline(table->children[1].children[0].inline_content, InlineCstKind::CodeSpan))));
    expect(fatal(bool(has_inline(table->children[1].children[1].inline_content, InlineCstKind::Strikethrough))));
    for (const auto& row : table->children) for (const auto& cell : row.children) expect_lossless(cell.inline_content);
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
        expect(fatal(bool(image->src == "image.png")));
        expect(fatal(bool(image->image_alt == "alt")));
        expect(fatal(bool(image->image_title == std::optional<std::string>{"Title"})));
    }
};

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

}; // suite parser_tests
