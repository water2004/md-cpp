#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "support/folia_test.hpp"
import elmd.core.parser;
import elmd.core.ast;
import elmd.core.block_source;
import elmd.core.block_tree;
import elmd.core.document_text;
import elmd.core.image_dimension;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.serializer;
import elmd.core.utf;

using namespace elmd;
using namespace boost::ut;

#include "support/parser_test_support.hpp"

suite parser_container_tests = [] {

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
    expect(fatal(bool(parsed.document.root.children.size() == 3u)));
    if (parsed.document.root.children.size() == 3u) {
        expect(fatal(bool(parsed.document.root.children[0].kind == BlockKind::BlockQuote)));
        expect(fatal(bool(parsed.document.root.children[1].kind == BlockKind::FootnoteDefinition)));
        expect(fatal(bool(parsed.document.root.children[2].kind == BlockKind::Callout)));
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
    expect(fatal(bool(footnote.children.size() == 2u)));
    if (footnote.children.size() == 2u) {
        expect(fatal(bool(footnote.children[0].kind == BlockKind::Paragraph)));
        expect(fatal(bool(footnote.children[0].inline_content.source == U"first\ncontinuation")));
        expect_lossless(footnote.children[0].inline_content);
        expect(fatal(bool(footnote.children[1].kind == BlockKind::List)));
        expect(fatal(bool(first_block(footnote.children[1].children, BlockKind::List) != nullptr)));
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

}; // suite parser_container_tests
