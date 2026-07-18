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

suite parser_block_tests = [] {

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
    expect(fatal(bool(parsed.document.root.children.size() == 2u)));
    if (parsed.document.root.children.size() != 2u) return;
    const auto& atx = parsed.document.root.children[0];
    const auto& setext = parsed.document.root.children[1];
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

    const auto separated = parse_text(2, "# first\n\nsecond");
    expect(fatal(bool(separated.document.root.children.size() == 2u)));
    if (separated.document.root.children.size() == 2u) {
        expect(fatal(bool(separated.document.root.children[0].kind == BlockKind::Heading)));
        expect(fatal(bool(separated.document.root.children[1].kind == BlockKind::Paragraph)));
        expect(fatal(bool(separated.document.root.children[1].inline_content.source == U"second")));
        expect(fatal(bool(serialize_markdown(separated.document) == "# first\n\nsecond")));

        const auto projection = serialize_markdown_projection(separated.document);
        expect(fatal(bool(projection.source_maps.size() == 2u)));
        const auto separator_offset = projection.text.find(U"\n\n") + 1u;
        const auto restored = source_position_for_serialized_offset(
            projection,
            separator_offset,
            TextAffinity::Downstream);
        expect(fatal(bool(restored.has_value())));
        if (restored) {
            expect(fatal(bool(restored->container_id == separated.document.root.children[1].id)));
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

"crlf_heading_boundaries_keep_separator_lossless_without_a_phantom_block"_test = [] {
    const std::string source = "# first\r\n\r\n## second\r\n";
    const auto parsed = parse_text(1, source);
    expect(bool(parsed.document.root.children.size() == 2u))
        << "CRLF root block count: " << parsed.document.root.children.size();
    if (parsed.document.root.children.size() != 2u) return;
    expect(fatal(bool(parsed.document.root.children[0].kind == BlockKind::Heading)));
    expect(fatal(bool(parsed.document.root.children[0].inline_content.source == U"first")));
    expect(fatal(bool(parsed.document.root.children[1].kind == BlockKind::Heading)));
    expect(fatal(bool(parsed.document.root.children[1].inline_content.source == U"second")));
    expect(fatal(bool(serialize_markdown(parsed.document) == source)));

    const auto projection = serialize_markdown_projection(parsed.document);
    expect(fatal(bool(projection.source_maps.size() == 2u)));
    const auto separator_offset = projection.text.find(U"\r\n\r\n") + 2u;
    const auto restored = source_position_for_serialized_offset(
        projection,
        separator_offset,
        TextAffinity::Downstream);
    expect(fatal(bool(restored.has_value())));
    if (restored)
        expect(fatal(bool(restored->container_id == parsed.document.root.children[1].id)));
};

"only_blank_lines_beyond_the_required_block_separator_have_block_identity"_test = [] {
    struct Case {
        std::string source;
        std::size_t block_count;
        std::size_t empty_count;
    };
    const std::vector<Case> cases{
        {"\n# first", 2u, 1u},
        {"\r\n# first", 2u, 1u},
        {"# first\n", 1u, 0u},
        {"# first\r\n", 1u, 0u},
        {"# first\n\n", 2u, 1u},
        {"# first\r\n\r\n", 2u, 1u},
        {"first\n\nsecond", 2u, 0u},
        {"first\r\n\r\nsecond", 2u, 0u},
        {"# first\n\nsecond", 2u, 0u},
        {"# first\r\n\r\nsecond", 2u, 0u},
        {"first\n\n\nsecond", 3u, 1u},
        {"first\r\n\r\n\r\nsecond", 3u, 1u},
        {"# first\n\n\nsecond", 3u, 1u},
        {"# first\r\n\r\n\r\nsecond", 3u, 1u},
        {"first\n\n\n\nsecond", 4u, 2u},
        {"first\r\n\r\n\r\n\r\nsecond", 4u, 2u},
        {"# first\n\n\n\nsecond", 4u, 2u},
        {"# first\r\n\r\n\r\n\r\nsecond", 4u, 2u},
        {"\n\n# first\n\n", 4u, 3u},
        {"\r\n\r\n# first\r\n\r\n", 4u, 3u},
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

"ordinary_block_separators_project_to_the_neighboring_content_blocks"_test = [] {
    struct Case {
        std::string source;
        BlockKind first_kind;
        BlockKind second_kind;
        std::u32string separator;
    };
    const std::vector<Case> cases{
        {"first\n\nsecond", BlockKind::Paragraph, BlockKind::Paragraph, U"\n\n"},
        {"# first\n\nsecond", BlockKind::Heading, BlockKind::Paragraph, U"\n\n"},
        {"first\n\n# second", BlockKind::Paragraph, BlockKind::Heading, U"\n\n"},
        {"first\r\n\r\nsecond", BlockKind::Paragraph, BlockKind::Paragraph, U"\r\n\r\n"},
        {"# first\r\n\r\nsecond", BlockKind::Heading, BlockKind::Paragraph, U"\r\n\r\n"},
        {"first\r\n\r\n# second", BlockKind::Paragraph, BlockKind::Heading, U"\r\n\r\n"},
    };

    for (const auto& entry : cases) {
        const auto parsed = parse_text(1, entry.source);
        expect(fatal(bool(parsed.document.root.children.size() == 2u))) << entry.source;
        if (parsed.document.root.children.size() != 2u) continue;
        const auto& first = parsed.document.root.children[0];
        const auto& second = parsed.document.root.children[1];
        expect(fatal(bool(first.kind == entry.first_kind))) << entry.source;
        expect(fatal(bool(second.kind == entry.second_kind))) << entry.source;
        expect(fatal(bool(first.inline_content.source == U"first"))) << entry.source;
        expect(fatal(bool(second.inline_content.source == U"second"))) << entry.source;

        const auto projection = serialize_markdown_projection(parsed.document);
        expect(fatal(bool(projection.text == utf8_to_cps(entry.source)))) << entry.source;
        expect(fatal(bool(projection.source_maps.size() == 2u))) << entry.source;
        const auto separator_start = projection.text.find(entry.separator);
        expect(fatal(bool(separator_start != std::u32string::npos))) << entry.source;
        if (separator_start == std::u32string::npos) continue;
        const auto inside_separator = separator_start + entry.separator.size() / 2u;
        const auto restored = source_position_for_serialized_offset(
            projection,
            inside_separator,
            TextAffinity::Downstream);
        expect(fatal(bool(restored.has_value()))) << entry.source;
        if (restored) {
            expect(fatal(bool(restored->container_id == second.id))) << entry.source;
            expect(fatal(bool(restored->source_offset == 0u))) << entry.source;
        }

        const auto saved = serialize_markdown(parsed.document);
        expect(fatal(bool(saved == entry.source))) << entry.source;
        const auto reloaded = parse_text(2, saved);
        expect(fatal(bool(reloaded.document.root.children.size() == 2u))) << entry.source;
        expect(fatal(bool(serialize_markdown(reloaded.document) == entry.source))) << entry.source;
    }
};

}; // suite parser_block_tests
