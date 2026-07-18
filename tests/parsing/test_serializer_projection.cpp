#include "support/folia_test.hpp"
import elmd.core.parser;
import elmd.core.serializer;
import elmd.core.text_edit;

using namespace elmd;
using namespace boost::ut;

suite serializer_projection_tests = [] {
    "generated_block_separator_respects_text_affinity"_test = [] {
        const auto parsed = parse_text(1, "first\n\nsecond");
        expect(fatal(parsed.document.root.children.size() == 2u));
        if (parsed.document.root.children.size() != 2u) return;

        const auto& first = parsed.document.root.children.front();
        const auto& second = parsed.document.root.children.back();
        const auto projection = serialize_markdown_projection(parsed.document);
        expect(fatal(projection.text == U"first\n\nsecond"));

        const auto separator_offset = projection.text.find(U"\n\n") + 1u;
        const auto upstream = source_position_for_serialized_offset(
            projection,
            separator_offset,
            TextAffinity::Upstream);
        const auto downstream = source_position_for_serialized_offset(
            projection,
            separator_offset,
            TextAffinity::Downstream);

        expect(fatal(upstream.has_value()));
        expect(fatal(downstream.has_value()));
        if (upstream) {
            expect(fatal(upstream->container_id == first.id));
            expect(fatal(upstream->source_offset == first.inline_content.source.size()));
        }
        if (downstream) {
            expect(fatal(downstream->container_id == second.id));
            expect(fatal(downstream->source_offset == 0u));
        }
    };
};
