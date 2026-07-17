#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "elmd_test.hpp"
import elmd.core.parser;
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.block_tree;
import elmd.core.document_text;
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

"top_level_parse_progress_is_monotonic_and_reaches_source_end"_test = [] {
    const std::string source =
        "# heading\n\n"
        "> quote\n"
        "> - nested\n\n"
        "paragraph\n";
    std::vector<std::pair<std::size_t, std::size_t>> reports;
    const auto parsed = parse_text(
        1,
        source,
        default_dialect(),
        [&](std::size_t consumed, std::size_t total) {
            reports.emplace_back(consumed, total);
        });

    expect(fatal(!reports.empty()));
    const auto expectedTotal = utf8_to_cps(source).size();
    std::size_t previous = 0;
    for (auto [consumed, total] : reports) {
        expect(total == expectedTotal);
        expect(consumed >= previous);
        expect(consumed <= total);
        previous = consumed;
    }
    expect(previous == expectedTotal);
    expect(serialize_markdown(parsed.document) == source);
};

"block_kind_payload_copies_with_value_semantics"_test = [] {
    static_assert(sizeof(BlockNodeSpecial) <= 7 * sizeof(void*));
    static_assert(sizeof(InlineToken) == sizeof(std::uint64_t));

    BlockNode paragraph;
    expect(fatal(bool(paragraph.payload == nullptr)));

    BlockNode heading;
    heading.kind = BlockKind::Heading;
    heading.ensure_text_special().level = 2;
    heading.ensure_text_special().slug = "original";
    expect(fatal(bool(heading.special().text != nullptr)));
    expect(fatal(bool(heading.special().list == nullptr)));
    expect(fatal(bool(heading.special().atomic == nullptr)));

    auto copy = heading;
    copy.ensure_text_special().level = 3;
    copy.ensure_text_special().slug = "copy";

    expect(fatal(bool(heading.text_special().level == 2u)));
    expect(fatal(bool(heading.text_special().slug == "original")));
    expect(fatal(bool(copy.text_special().level == 3u)));
    expect(fatal(bool(copy.text_special().slug == "copy")));

    BlockNode kinds;
    kinds.ensure_list_special().start = 7;
    kinds.ensure_item_special().marker = U"- [x] ";
    kinds.ensure_atomic_special().raw = "raw";
    kinds.ensure_table_special().table_aligns = {TableAlignment::Center};
    kinds.ensure_image_special().src = "original.png";
    kinds.ensure_container_special().footnote_label = "note";

    auto kinds_copy = kinds;
    kinds_copy.ensure_list_special().start = 9;
    kinds_copy.ensure_item_special().marker = U"1. ";
    kinds_copy.ensure_atomic_special().raw = "copy";
    kinds_copy.ensure_table_special().table_aligns.front() = TableAlignment::Right;
    kinds_copy.ensure_image_special().src = "copy.png";
    kinds_copy.ensure_container_special().footnote_label = "copy";

    expect(fatal(bool(kinds.list_special().start == 7u)));
    expect(fatal(bool(kinds.item_special().marker == U"- [x] ")));
    expect(fatal(bool(kinds.atomic_special().raw == "raw")));
    expect(fatal(bool(kinds.table_special().table_aligns.front() == TableAlignment::Center)));
    expect(fatal(bool(kinds.image_special().src == "original.png")));
    expect(fatal(bool(kinds.container_special().footnote_label == "note")));
};

"empty_document_has_no_blocks"_test = [] {
    const auto parsed = parse_text(1, "");
    expect(fatal(bool(parsed.document.root.children.empty())));
    expect(fatal(bool(parsed.revision == 1u)));
};

"headings_preserve_source_and_outline_data"_test = [] {
    const auto parsed = parse_text(1, "# *Hello*\n\nSetext\n------\n");
    expect(fatal(bool(parsed.document.root.children.size() == 3u)));
    const auto& atx = parsed.document.root.children[0];
    const auto& setext = parsed.document.root.children[2];
    expect(fatal(bool(atx.kind == BlockKind::Heading)));
    expect(fatal(bool(atx.text_special().level == 1u)));
    expect(fatal(bool(atx.inline_content.source == U"*Hello*")));
    expect(fatal(bool(has_inline(atx.inline_content, InlineCstKind::Emphasis))));
    expect_lossless(atx.inline_content);
    expect(fatal(bool(setext.kind == BlockKind::Heading)));
    expect(fatal(bool(setext.text_special().level == 2u)));
    expect(fatal(bool(setext.inline_content.source == U"Setext")));
    expect(fatal(bool(parsed.symbols.headings.size() == 2u)));
};

"heading_boundaries_keep_soft_breaks_block_local"_test = [] {
    const auto parsed = parse_text(1, "# first\nsecond");
    expect(fatal(bool(parsed.document.root.children.size() == 2u)));
    if (parsed.document.root.children.size() != 2u) return;
    const auto& heading = parsed.document.root.children.front();
    expect(fatal(bool(heading.kind == BlockKind::Heading)));
    expect(fatal(bool(heading.inline_content.source == U"first")));
    expect(fatal(!has_inline(heading.inline_content, InlineCstKind::SoftBreak)));
    expect_lossless(heading.inline_content);
    expect(fatal(bool(parsed.document.root.children.back().kind == BlockKind::Paragraph)));
    expect(fatal(bool(parsed.document.root.children.back().inline_content.source == U"second")));
    expect(fatal(bool(serialize_markdown(parsed.document) == "# first\nsecond")));

    const auto blank = parse_text(2, "# first\n\nsecond");
    expect(fatal(bool(blank.document.root.children.size() == 3u)));
    if (blank.document.root.children.size() == 3u) {
        expect(fatal(bool(blank.document.root.children[0].kind == BlockKind::Heading)));
        expect(fatal(bool(blank.document.root.children[1].kind == BlockKind::Paragraph)));
        expect(fatal(bool(blank.document.root.children[1].inline_content.source.empty())));
        expect(fatal(bool(blank.document.root.children[2].kind == BlockKind::Paragraph)));
        expect(fatal(bool(serialize_markdown(blank.document) == "# first\n\nsecond")));

        const auto projection = serialize_markdown_projection(blank.document);
        const auto blank_line_offset = projection.text.find(U"\n\n") + 1u;
        const auto restored = source_position_for_serialized_offset(
            projection,
            blank_line_offset,
            TextAffinity::Downstream);
        expect(fatal(bool(restored.has_value())));
        if (restored) {
            expect(fatal(bool(restored->container_id == blank.document.root.children[1].id)));
            expect(fatal(bool(restored->source_offset == 0u)));
        }
    }

    const auto setext = parse_text(3, "first\nsecond\n---");
    expect(fatal(bool(setext.document.root.children.size() == 1u)));
    if (setext.document.root.children.size() == 1u) {
        const auto& multiline = setext.document.root.children.front();
        expect(fatal(bool(multiline.kind == BlockKind::Heading)));
        expect(fatal(bool(multiline.inline_content.source == U"first\nsecond")));
        expect(fatal(bool(has_inline(multiline.inline_content, InlineCstKind::SoftBreak))));
        expect_lossless(multiline.inline_content);
        expect(fatal(bool(serialize_markdown(setext.document) == "first\nsecond\n---")));
    }
};

"blank_lines_have_editable_block_identity_at_document_boundaries"_test = [] {
    struct Case {
        std::string source;
        std::size_t block_count;
        std::size_t empty_count;
    };
    const std::vector<Case> cases{
        {"\n# first", 2u, 1u},
        {"# first\n", 1u, 0u},
        {"# first\n\n", 2u, 1u},
        {"# first\n\n\nsecond", 4u, 2u},
        {"\n\n# first\n\n", 4u, 3u},
    };
    for (auto const& entry : cases) {
        const auto parsed = parse_text(1, entry.source);
        expect(fatal(bool(parsed.document.root.children.size() == entry.block_count)))
            << entry.source;
        const auto empty_count = std::ranges::count_if(
            parsed.document.root.children,
            [](auto const& block) {
                return block.kind == BlockKind::Paragraph
                    && block.inline_content.source.empty();
            });
        expect(fatal(bool(empty_count == entry.empty_count))) << entry.source;
        expect(fatal(bool(serialize_markdown(parsed.document) == entry.source))) << entry.source;

        const auto reparsed = parse_text(2, serialize_markdown(parsed.document));
        expect(fatal(bool(reparsed.document.root.children.size() == entry.block_count)))
            << entry.source;
        expect(fatal(bool(serialize_markdown(reparsed.document) == entry.source))) << entry.source;
    }
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
    expect(fatal(bool(parsed.document.root.children.size() == 5u)));
    if (parsed.document.root.children.size() == 5u) {
        expect(fatal(bool(parsed.document.root.children[0].kind == BlockKind::BlockQuote)));
        expect(fatal(bool(parsed.document.root.children[1].kind == BlockKind::Paragraph
            && parsed.document.root.children[1].inline_content.source.empty())));
        expect(fatal(bool(parsed.document.root.children[2].kind == BlockKind::FootnoteDefinition)));
        expect(fatal(bool(parsed.document.root.children[3].kind == BlockKind::Paragraph
            && parsed.document.root.children[3].inline_content.source.empty())));
        expect(fatal(bool(parsed.document.root.children[4].kind == BlockKind::Callout)));
    }
    if (callout) {
        const auto* title = callout_title_block(*callout);
        expect(fatal(bool(title != nullptr)));
        if (title) expect_lossless(title->inline_content);
    }
};

"footnote_definitions_stop_at_root_siblings_and_parse_recursive_body_blocks"_test = [] {
    const std::string source =
        "text[^n]\n\n"
        "[^n]: first\n"
        "    continuation\n\n"
        "    - item\n"
        "      - nested\n\n"
        "after\n";
    const auto parsed = parse_text(1, source);
    const auto footnote_at = std::ranges::find_if(parsed.document.root.children, [](auto const& block) {
        return block.kind == BlockKind::FootnoteDefinition;
    });
    expect(fatal(bool(footnote_at != parsed.document.root.children.end())));
    if (footnote_at == parsed.document.root.children.end()) return;
    const auto after_at = std::ranges::find_if(parsed.document.root.children, [](auto const& block) {
        return block.kind == BlockKind::Paragraph && block.inline_content.source == U"after";
    });
    expect(fatal(bool(after_at != parsed.document.root.children.end())));
    expect(fatal(bool(after_at > footnote_at)));

    const auto& footnote = *footnote_at;
    expect(fatal(bool(footnote.kind == BlockKind::FootnoteDefinition)));
    expect(fatal(bool(footnote.container_special().footnote_label == "n")));
    expect(fatal(bool(footnote.children.size() == 3u)));
    if (footnote.children.size() == 3u) {
        expect(fatal(bool(footnote.children[0].kind == BlockKind::Paragraph)));
        expect(fatal(bool(footnote.children[0].inline_content.source == U"first\ncontinuation")));
        expect_lossless(footnote.children[0].inline_content);
        expect(fatal(bool(footnote.children[1].kind == BlockKind::Paragraph)));
        expect(fatal(bool(footnote.children[1].inline_content.source.empty())));
        expect(fatal(bool(footnote.children[2].kind == BlockKind::List)));
        expect(fatal(bool(first_block(footnote.children[2].children, BlockKind::List) != nullptr)));
    }
    expect(fatal(bool(serialize_markdown(parsed.document) == source)));
};

"footnote_body_recursively_parses_quotes_and_nested_lists"_test = [] {
    const std::string source =
        "[^q]: > quote\n"
        "    >\n"
        "    > - item\n";
    const auto parsed = parse_text(1, source);
    expect(fatal(bool(parsed.document.root.children.size() == 1u)));
    if (parsed.document.root.children.size() != 1u) return;
    const auto& footnote = parsed.document.root.children.front();
    expect(fatal(bool(footnote.kind == BlockKind::FootnoteDefinition)));
    expect(fatal(bool(footnote.children.size() == 1u)));
    expect(fatal(bool(first_block(footnote.children, BlockKind::BlockQuote) != nullptr)));
    expect(fatal(bool(first_block(footnote.children, BlockKind::List) != nullptr)));
    expect(fatal(bool(serialize_markdown(parsed.document) == source)));
};

"footnote_multiblock_forms_preserve_marktext_compatible_source_exactly"_test = [] {
    const std::vector<std::string> simple_forms{
        "[^n]:\n    body on the next line\n",
        "[^n]:\n\n    body after a blank line\n",
        "[^n]: inline body\n\n    continuation paragraph\n",
    };
    for (auto const& source : simple_forms) {
        const auto parsed = parse_text(1, source);
        expect(fatal(bool(parsed.document.root.children.size() == 1u))) << source;
        if (parsed.document.root.children.size() != 1u) continue;
        auto const& footnote = parsed.document.root.children.front();
        expect(bool(footnote.kind == BlockKind::FootnoteDefinition)) << "footnote kind: " << source;
        expect(bool(first_block(footnote.children, BlockKind::Paragraph) != nullptr)) << "paragraph body: " << source;
        expect(fatal(bool(serialize_markdown(parsed.document) == source))) << source;
    }

    const std::string multiblock =
        "text[^m]\n\n"
        "[^m]:\n\n"
        "    First paragraph.\n\n"
        "    - item\n"
        "      - nested\n\n"
        "    ```cpp\n"
        "    int value = 1;\n"
        "    ```\n\n"
        "    Last paragraph.\n\n"
        "outside\n";
    const auto parsed = parse_text(1, multiblock);
    const auto footnote_at = std::ranges::find_if(parsed.document.root.children, [](auto const& block) {
        return block.kind == BlockKind::FootnoteDefinition;
    });
    expect(fatal(bool(footnote_at != parsed.document.root.children.end())));
    if (footnote_at == parsed.document.root.children.end()) return;
    const auto outside_at = std::ranges::find_if(parsed.document.root.children, [](auto const& block) {
        return block.kind == BlockKind::Paragraph && block.inline_content.source == U"outside";
    });
    expect(fatal(bool(outside_at != parsed.document.root.children.end())));
    expect(fatal(bool(outside_at > footnote_at)));
    auto const& footnote = *footnote_at;
    expect(bool(footnote.kind == BlockKind::FootnoteDefinition)) << "multiblock footnote kind";
    expect(bool(first_block(footnote.children, BlockKind::Paragraph) != nullptr)) << "multiblock paragraph";
    expect(bool(first_block(footnote.children, BlockKind::List) != nullptr)) << "multiblock list";
    expect(bool(first_block(footnote.children, BlockKind::CodeBlock) != nullptr)) << "multiblock fenced code";
    auto const saved = serialize_markdown(parsed.document);
    expect(bool(saved == multiblock)) << "multiblock exact save:\n" << saved;

    const std::string terminated =
        "text[^t]\n\n"
        "[^t]: inline\n\n"
        "    continuation\n\n"
        "  two-space text is outside\n";
    const auto termination = parse_text(1, terminated);
    const auto definition_at = std::ranges::find_if(termination.document.root.children, [](auto const& block) {
        return block.kind == BlockKind::FootnoteDefinition;
    });
    expect(bool(definition_at != termination.document.root.children.end())) << "termination footnote";
    if (definition_at != termination.document.root.children.end()) {
        auto const& definition = *definition_at;
        expect(bool(serialize_markdown_fragment(definition.children).find(U"two-space") == std::u32string::npos));
        const auto outside_at = std::ranges::find_if(termination.document.root.children, [](auto const& block) {
            return block.kind == BlockKind::Paragraph
                && block.inline_content.source.find(U"two-space") != std::u32string::npos;
        });
        expect(bool(outside_at != termination.document.root.children.end()));
        expect(bool(outside_at > definition_at));
        expect(bool(serialize_markdown(termination.document) == terminated));
    }
};

"callout_headers_preserve_exact_markers_and_title_whitespace"_test = [] {
    struct Case {
        std::string source;
        std::string kind;
        std::optional<std::u32string> title;
    };
    for (const auto& test : std::vector<Case>{
             {"> [!note]  title  \n> body", "NOTE", U" title  "},
             {">[!TIP]title\n> body", "TIP", U"title"},
             {"> [!WARNING] \n> body", "WARNING", std::nullopt},
             {"> [!caution]\n> body", "CAUTION", std::nullopt},
             {"> [!IMPORTANT]\n> body", "IMPORTANT", std::nullopt},
         }) {
        const auto parsed = parse_text(1, test.source);
        expect(fatal(bool(parsed.document.root.children.size() == 1u))) << test.source;
        if (parsed.document.root.children.size() != 1) continue;
        const auto& callout = parsed.document.root.children.front();
        expect(fatal(bool(callout.kind == BlockKind::Callout))) << test.source;
        expect(fatal(bool(callout.container_special().callout_kind == test.kind))) << test.source;
        expect(fatal(bool(!callout.text_special().opening_marker.empty()))) << test.source;
        const auto* title = callout_title_block(callout);
        expect(fatal(bool((title != nullptr) == test.title.has_value()))) << test.source;
        if (test.title && title) {
            expect(fatal(bool(title->inline_content.source == *test.title))) << test.source;
            expect_lossless(title->inline_content);
        }
        expect(fatal(bool(serialize_markdown(parsed.document) == test.source))) << test.source;
    }

    const auto unknown = parse_text(1, "> [!CUSTOM] title\n> body");
    expect(fatal(bool(unknown.document.root.children.size() == 1u)));
    if (!unknown.document.root.children.empty()) {
        expect(fatal(bool(unknown.document.root.children.front().kind == BlockKind::BlockQuote)));
        expect(fatal(bool(serialize_markdown(unknown.document) == "> [!CUSTOM] title\n> body")));
    }
};

"callout_title_is_a_first_class_unified_tree_child"_test = [] {
    const auto parsed = parse_text(7, "> [!NOTE] _title_\n> body");
    expect(fatal(bool(parsed.document.root.children.size() == 1u)));
    if (parsed.document.root.children.size() != 1u) return;
    const auto& callout = parsed.document.root.children.front();
    expect(fatal(bool(callout.kind == BlockKind::Callout)));
    expect(fatal(bool(editable_inline_document(callout) == nullptr)));
    expect(fatal(bool(callout.children.size() == 2u)));
    if (callout.children.size() != 2u) return;

    const auto& title = callout.children.front();
    const auto& body = callout.children.back();
    expect(fatal(bool(title.kind == BlockKind::CalloutTitle)));
    expect(fatal(bool(title.id != callout.id)));
    expect(fatal(bool(title.inline_content.source == U"_title_")));
    expect_lossless(title.inline_content);
    expect(fatal(bool(body.kind == BlockKind::Paragraph)));

    const auto fragments = document_text_fragments(parsed.document);
    expect(fatal(bool(fragments.size() == 2u)));
    expect(fatal(bool(document_text_character_count(parsed.document) == 12u)));
    if (fragments.size() == 2u) {
        expect(fatal(bool(fragments[0].container_id == title.id)));
        expect(fatal(bool(fragments[0].text == U"_title_")));
        expect(fatal(bool(fragments[1].container_id == body.id)));
        expect(fatal(bool(fragments[1].text == U"body")));
    }

    const auto projection = serialize_markdown_projection(parsed.document);
    const auto title_map = std::ranges::find(
        projection.source_maps, title.id, &SerializedSourceMap::container_id);
    expect(fatal(bool(title_map != projection.source_maps.end())));
    expect(fatal(bool(projection.text == U"> [!NOTE] _title_\n> body")));
};

"atx_heading_keeps_non_closing_trailing_whitespace_in_local_source"_test = [] {
    for (const auto& source : {
             std::string{"# title  "},
             std::string{"## _title_\t"},
         }) {
        const auto parsed = parse_text(1, source);
        expect(fatal(bool(parsed.document.root.children.size() == 1u))) << source;
        if (parsed.document.root.children.size() != 1) continue;
        const auto& heading = parsed.document.root.children.front();
        expect(fatal(bool(heading.kind == BlockKind::Heading))) << source;
        expect(fatal(bool(heading.inline_content.source.ends_with(
            source.back() == '\t' ? U"\t" : U"  ")))) << source;
        expect_lossless(heading.inline_content);
        expect(fatal(bool(serialize_markdown(parsed.document) == source))) << source;
    }

    const auto closed = parse_text(1, "# title  ###  ");
    const auto& heading = closed.document.root.children.front();
    expect(fatal(bool(heading.inline_content.source == U"title")));
    expect(fatal(bool(heading.text_special().closing_marker == U"  ###  ")));
    expect(fatal(bool(serialize_markdown(closed.document) == "# title  ###  ")));
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
    expect(fatal(bool(table->table_special().table_aligns
        == std::vector{TableAlignment::Left, TableAlignment::Right})));
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
        expect(fatal(html_image->image_special().image_width == std::optional<float>{320.0f}));
    }
    expect(fatal(serialize_markdown(html.document) == html_source));
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
    expect(inline_contains_kind(paragraph->inline_content, InlineCstKind::Strong));
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

}; // suite parser_tests
