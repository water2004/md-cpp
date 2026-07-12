#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "elmd_test.hpp"
import elmd.core.dialect;
import elmd.core.inline_cst;
import elmd.core.inline_document;
import elmd.core.inline_parser;
import elmd.core.inline_source_edit;
import elmd.core.selection;
import elmd.core.text_edit;
import elmd.core.utf;

using namespace elmd;
using namespace boost::ut;

namespace {

InlineParseContext test_context() {
    InlineParseContext context;
    context.dialect = default_dialect();
    context.next_id = NodeId{1000};
    return context;
}

bool is_lossless(std::u32string_view source) {
    const auto tree = parse_inline(source, test_context());
    return tokens_partition_source(tree, source.size())
        && roots_partition_source(tree, source.size())
        && flatten_tokens(tree, source) == source
        && serialize_lossless(tree, source) == source;
}

const InlineCstNode* first_node(const InlineCstTree& tree, InlineCstKind kind) {
    std::function<const InlineCstNode*(const InlineCstNodes&)> find =
        [&](const InlineCstNodes& nodes) -> const InlineCstNode* {
            for (const auto& node : nodes) {
                if (node.kind == kind) return &node;
                if (const auto* nested = find(node.children)) return nested;
            }
            return nullptr;
        };
    return find(tree.nodes);
}

} // namespace

suite inline_cst_tests = [] {

"lossless representative editing states"_test = [] {
    const std::vector<std::u32string> sources{
        U"", U"abc", U"*abc*", U"_abc_", U"**abc**", U"__abc__",
        U"**", U"**abc", U"a***b***c", U"~~abc~~", U"~~abc",
        U"`abc`", U"`abc", U"[title](url)", U"[title](<url>)",
        U"[title](url \"name\")", U"[title](", U"![alt](url)",
        U"$abc$", U"$abc", U"\\*abc\\*", U"&amp;", U"a\\**b*",
        U"two  \nlines", U"two\\\nlines", U"'文本'", U"\"文本\"",
    };

    for (const auto& source : sources) {
        expect(is_lossless(source)) << "source=" << cps_to_utf8(source);
    }
};

"unclosed syntax remains structural"_test = [] {
    const std::vector<std::u32string> sources{U"**", U"**abc", U"*", U"`abc", U"[title](", U"$abc", U"~~abc"};
    for (const auto& source : sources) {
        const auto tree = parse_inline(source, test_context());
        const auto* incomplete = first_node(tree, InlineCstKind::Incomplete);
        expect(incomplete != nullptr) << "source=" << cps_to_utf8(source);
        if (incomplete) {
            expect(incomplete->status == ParseStatus::MissingCloser);
            expect(incomplete->range.valid_for(source.size()));
        }
    }
};

"original marker and link spellings survive"_test = [] {
    const auto stars_source = std::u32string{U"**abc**"};
    const auto underscores_source = std::u32string{U"__abc__"};
    const auto stars = parse_inline(stars_source, test_context());
    const auto underscores = parse_inline(underscores_source, test_context());
    const auto* star_strong = first_node(stars, InlineCstKind::Strong);
    const auto* underscore_strong = first_node(underscores, InlineCstKind::Strong);
    expect(star_strong != nullptr);
    expect(underscore_strong != nullptr);
    if (star_strong && underscore_strong) {
        expect(stars_source.substr(star_strong->delim.opening.start, star_strong->delim.opening.length()) == U"**");
        expect(underscores_source.substr(underscore_strong->delim.opening.start, underscore_strong->delim.opening.length()) == U"__");
    }

    const std::vector<std::u32string> links{U"[title](url)", U"[title](<url>)", U"[title](url 'name')"};
    for (const auto& link : links) {
        const auto tree = parse_inline(link, test_context());
        expect(first_node(tree, InlineCstKind::Link) != nullptr);
        expect(serialize_lossless(tree, link) == link);
    }
};

"text edit applies source replacement and affinity"_test = [] {
    std::u32string source = U"**abc**";
    const TextEdit edit{NodeId{7}, {2, 5}, U"xyz"};
    expect(apply_text_edit(source, edit) == 0_i);
    expect(source == U"**xyz**");

    const TextEdit insertion{NodeId{7}, {2, 2}, U"Q"};
    expect(translate_position(TextPosition{NodeId{7}, 2, TextAffinity::Upstream}, insertion).source_offset == 2_u);
    expect(translate_position(TextPosition{NodeId{7}, 2, TextAffinity::Downstream}, insertion).source_offset == 3_u);
    expect(translate_position(TextPosition{NodeId{8}, 2, TextAffinity::Downstream}, insertion).source_offset == 2_u);

    bool rejected = false;
    try {
        apply_text_edit(source, TextEdit{NodeId{7}, {0, 99}, U""});
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    expect(rejected);
};

"every character boundary remains lossless after insert and delete"_test = [] {
    const std::vector<std::u32string> samples{
        U"abc", U"**abc**", U"__abc__", U"**", U"**abc", U"a***b***c",
        U"`abc`", U"`abc", U"[title](url)", U"[title](", U"$abc$", U"$abc",
        U"~~abc~~", U"~~abc", U"\\*abc\\*",
    };
    for (const auto& original : samples) {
        for (std::size_t offset = 0; offset <= original.size(); ++offset) {
            auto inserted = original;
            apply_text_edit(inserted, TextEdit{NodeId{1}, {offset, offset}, U"x"});
            expect(is_lossless(inserted));
        }
        for (std::size_t offset = 0; offset < original.size(); ++offset) {
            auto deleted = original;
            apply_text_edit(deleted, TextEdit{NodeId{1}, {offset, offset + 1}, U""});
            expect(is_lossless(deleted));
        }
    }
};

"random source edits preserve token partition"_test = [] {
    std::mt19937_64 random{0xC57C57u};
    const std::u32string alphabet = U"abc *_~`[]()!$\\&;<>\n'\"😀";
    for (std::size_t iteration = 0; iteration < 500; ++iteration) {
        std::u32string source;
        const auto length = static_cast<std::size_t>(random() % 80);
        for (std::size_t i = 0; i < length; ++i) source.push_back(alphabet[random() % alphabet.size()]);
        expect(is_lossless(source));

        const auto offset = static_cast<std::size_t>(random() % (source.size() + 1));
        if ((random() & 1u) != 0 || source.empty()) {
            apply_text_edit(source, TextEdit{NodeId{1}, {offset, offset}, U"x"});
        } else {
            const auto end = (std::min)(source.size(), offset + 1);
            apply_text_edit(source, TextEdit{NodeId{1}, {offset, end}, U""});
        }
        expect(is_lossless(source));
    }
};

"inline document serializes authoritative source"_test = [] {
    InlineDocument document{U"[title](<url>)", {}};
    reparse_inline_document(document, test_context());
    expect(document.serialize() == U"[title](<url>)");
    expect(flatten_tokens(document.tree, document.source) == document.source);
};

"source edit reparses one document and reconciles untouched ids"_test = [] {
    InlineDocument document{U"left **middle** right", {}};
    auto initial = test_context();
    initial.next_id = NodeId{100};
    reparse_inline_document(document, initial);
    expect(document.tree.nodes.size() == 3_u);
    if (document.tree.nodes.size() != 3) return;

    const auto left_id = document.tree.nodes.front().id;
    const auto edited_id = document.tree.nodes[1].id;
    const auto right_id = document.tree.nodes.back().id;
    std::uint64_t allocated = 10000;
    auto reparsing = test_context();
    reparsing.allocate_id = [&] { return NodeId{allocated++}; };

    const TextEdit edit{NodeId{7}, {8, 8}, U"X"};
    const auto applied = apply_inline_source_edit(NodeId{7}, document, edit, reparsing);
    expect(document.source == U"left **mXiddle** right");
    expect(document.tree.nodes.size() == 3_u);
    if (document.tree.nodes.size() == 3) {
        expect(document.tree.nodes.front().id == left_id);
        expect(document.tree.nodes[1].id != edited_id);
        expect(document.tree.nodes.back().id == right_id);
        expect(document.tree.nodes.back().range == SourceRange{16, 22});
    }
    expect(applied.inverse.container_id == NodeId{7});
    expect(applied.inverse.range == SourceRange{8, 9});
    expect(applied.inverse.replacement.empty());
    expect(is_lossless(document.source));
};

"source edit rejects the wrong owner"_test = [] {
    InlineDocument document{U"abc", {}};
    reparse_inline_document(document, test_context());
    bool rejected = false;
    try {
        apply_inline_source_edit(NodeId{1}, document, TextEdit{NodeId{2}, {0, 0}, U"x"}, test_context());
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    expect(rejected);
    expect(document.source == U"abc");
};

}; // suite inline_cst_tests
