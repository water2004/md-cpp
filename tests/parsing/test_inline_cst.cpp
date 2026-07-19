#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "support/folia_test.hpp"
import folia.core.dialect;
import folia.core.inline_cst;
import folia.core.inline_document;
import folia.core.inline_parser;
import folia.core.inline_source_edit;
import folia.core.selection;
import folia.core.text_edit;
import folia.core.utf;

using namespace folia;
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

const InlineCstNode* first_html_node(
    const InlineCstNodes& nodes,
    std::string_view tag) {
    for (const auto& node : nodes) {
        if (node.kind == InlineCstKind::HtmlElement
            && node.semantics().html_tag == tag) {
            return &node;
        }
        if (const auto* nested = first_html_node(node.children, tag)) return nested;
    }
    return nullptr;
}

bool contains_kind(const InlineCstNodes& nodes, InlineCstKind kind) {
    for (const auto& node : nodes) {
        if (node.kind == kind || contains_kind(node.children, kind)) return true;
    }
    return false;
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

"semantic_children_remain_editable_inside_unclosed_delimiters"_test = [] {
    const auto source = std::u32string{U"p**<br>abcq"};
    auto document = InlineDocument{source, parse_inline(source, test_context())};
    const auto* incomplete = first_node(document.tree, InlineCstKind::Incomplete);
    const auto* hard_break = first_html_node(document.tree.nodes, "br");
    expect(fatal(bool(incomplete != nullptr)));
    expect(fatal(bool(hard_break != nullptr)));
    if (!hard_break) return;
    expect(fatal(bool(source.substr(hard_break->range.start, hard_break->range.length()) == U"<br>")));
    const auto deletion = inline_backward_delete_range(document, hard_break->range.end);
    expect(fatal(bool(deletion.has_value())));
    if (!deletion) return;
    expect(fatal(bool(*deletion == hard_break->range)));
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
        expect(stars_source.substr(star_strong->delimiter_ranges().opening.start, star_strong->delimiter_ranges().opening.length()) == U"**");
        expect(underscores_source.substr(underscore_strong->delimiter_ranges().opening.start, underscore_strong->delimiter_ranges().opening.length()) == U"__");
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

"selection_affinity_does_not_create_a_source_range"_test = [] {
    const auto owner = NodeId{7};
    const TextSelection selection{
        {owner, 2, TextAffinity::Upstream},
        {owner, 2, TextAffinity::Downstream},
    };
    expect(selection.is_caret());
    expect(selection.anchor != selection.active);
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

"hard_breaks_are_single_visual_delete_units"_test = [] {
    InlineDocument html{U"a<br>b", {}};
    reparse_inline_document(html, test_context());
    expect(inline_backward_delete_range(html, 5) == SourceRange{1, 5});
    expect(inline_backward_delete_range(html, 3) == SourceRange{1, 5});
    expect(inline_forward_delete_range(html, 1) == SourceRange{1, 5});
    expect(inline_forward_delete_range(html, 3) == SourceRange{1, 5});

    InlineDocument markdown{U"a  \nb", {}};
    reparse_inline_document(markdown, test_context());
    expect(inline_backward_delete_range(markdown, 4) == SourceRange{1, 4});
    expect(inline_forward_delete_range(markdown, 1) == SourceRange{1, 4});
};

"physical line endings_are_lossless_single_break_nodes"_test = [] {
    const std::array<std::u32string, 3> sources{U"a\r\nb", U"a\rb", U"a\nb"};
    for (auto const& source : sources) {
        const auto tree = parse_inline(source, test_context());
        expect(fatal(bool(flatten_tokens(tree, source) == source)));
        expect(fatal(bool(serialize_lossless(tree, source) == source)));
        const auto* soft_break = first_node(tree, InlineCstKind::SoftBreak);
        expect(fatal(soft_break != nullptr));
        expect(fatal(bool(soft_break->range.start == 1u)));
        expect(fatal(bool(soft_break->range.end == source.size() - 1u)));
    }

    InlineDocument markdown{U"a  \r\nb", {}};
    reparse_inline_document(markdown, test_context());
    const auto* hard_break = first_node(markdown.tree, InlineCstKind::HardBreak);
    expect(fatal(hard_break != nullptr));
    expect(fatal(bool(hard_break->range == SourceRange{1, 5})));
    expect(fatal(bool(inline_backward_delete_range(markdown, 5) == SourceRange{1, 5})));
    expect(fatal(bool(inline_forward_delete_range(markdown, 1) == SourceRange{1, 5})));
};

"underscore delimiters respect CommonMark word boundaries"_test = [] {
    const std::vector<std::u32string> literal_sources{
        U"0x1_fffff_ffff",
        U"trig_out",
        U"a_b_c",
        U"foo_",
        U"foo__",
        U"0x1_fffff_ffff and trig_out across\nsoft lines",
    };
    for (auto const& source : literal_sources) {
        const auto tree = parse_inline(source, test_context());
        expect(first_node(tree, InlineCstKind::Emphasis) == nullptr)
            << "source=" << cps_to_utf8(source);
        expect(first_node(tree, InlineCstKind::Strong) == nullptr)
            << "source=" << cps_to_utf8(source);
        expect(is_lossless(source));
    }

    const std::vector<std::pair<std::u32string, InlineCstKind>> formatted_sources{
        {U"_word_", InlineCstKind::Emphasis},
        {U"__word__", InlineCstKind::Strong},
        {U"a _word_ b", InlineCstKind::Emphasis},
        {U"(_word_)", InlineCstKind::Emphasis},
    };
    for (auto const& [source, kind] : formatted_sources) {
        const auto tree = parse_inline(source, test_context());
        expect(first_node(tree, kind) != nullptr)
            << "source=" << cps_to_utf8(source);
        expect(is_lossless(source));
    }

    for (auto const& source : {std::u32string{U"_"}, std::u32string{U"__"}}) {
        const auto tree = parse_inline(source, test_context());
        expect(first_node(tree, InlineCstKind::Incomplete) != nullptr);
        expect(is_lossless(source));
    }
};

"html_text_mode_keeps_markdown_markers_literal_across_edits"_test = [] {
    InlineDocument document{
        U"*literal* <strong>bold</strong> $math$",
        {},
        InlineSyntaxMode::HtmlText};
    reparse_inline_document(document, test_context());
    expect(is_lossless(document.source));
    expect(!inline_contains_kind(document, InlineCstKind::Emphasis));
    expect(!inline_contains_kind(document, InlineCstKind::InlineMath));
    expect(!inline_contains_kind(document, InlineCstKind::Strong));
    expect(first_html_node(document.tree.nodes, "strong") != nullptr);

    const auto inserted = document.source.find(U"literal") + 7;
    apply_inline_source_edit(
        NodeId{7},
        document,
        TextEdit{NodeId{7}, {inserted, inserted}, U"*"},
        test_context());
    expect(is_lossless(document.source));
    expect(!inline_contains_kind(document, InlineCstKind::Emphasis));
    expect(!inline_contains_kind(document, InlineCstKind::Strong));
    expect(first_html_node(document.tree.nodes, "strong") != nullptr);
};

"inline_html_is_a_recursive_lossless_syntax_island"_test = [] {
    const auto source = std::u32string{
        U"**outside** <span STYLE=\"color:#f00\" onclick=\"ignored()\">"
        U"**literal** $literal$ <strong>bold <em>*still literal*</em></strong>"
        U"</span> *outside*"};
    const auto tree = parse_inline(source, test_context());
    expect(is_lossless(source));

    const auto* span = first_html_node(tree.nodes, "span");
    expect(fatal(bool(span != nullptr)));
    if (!span) return;
    expect(!contains_kind(span->children, InlineCstKind::Strong));
    expect(!contains_kind(span->children, InlineCstKind::Emphasis));
    expect(!contains_kind(span->children, InlineCstKind::InlineMath));
    expect(first_html_node(span->children, "strong") != nullptr);
    expect(first_html_node(span->children, "em") != nullptr);
    expect(span->semantics().html_attributes.contains("style"));
    expect(!span->semantics().html_attributes.contains("onclick"));
    expect(first_node(tree, InlineCstKind::Strong) != nullptr);
    expect(first_node(tree, InlineCstKind::Emphasis) != nullptr);
};

"html_void_elements_remain_html_cst_nodes"_test = [] {
    const auto source = std::u32string{U"a<br><img src=\"x.svg\" alt=\"logo\">b"};
    const auto tree = parse_inline(source, test_context());
    expect(is_lossless(source));
    const auto* br = first_html_node(tree.nodes, "br");
    const auto* image = first_html_node(tree.nodes, "img");
    expect(fatal(bool(br != nullptr)));
    expect(fatal(bool(image != nullptr)));
    expect(first_node(tree, InlineCstKind::HardBreak) == nullptr);
    expect(first_node(tree, InlineCstKind::Image) == nullptr);
    if (image) {
        expect(image->semantics().href == "x.svg");
        expect(image->semantics().alt == "logo");
    }
};

"percent_escapes_remain_exact_only_inside_link_targets"_test = [] {
    const auto source = std::u32string{
        U"100% [local](docs/My%20File.md#part%202) "
        U"<a href=\"#hello%2Dworld\">anchor</a>"};
    const auto tree = parse_inline(source, test_context());
    expect(fatal(is_lossless(source)));
    const auto* link = first_node(tree, InlineCstKind::Link);
    const auto* anchor = first_html_node(tree.nodes, "a");
    expect(fatal(link != nullptr));
    expect(fatal(anchor != nullptr));
    if (link) expect(fatal(link->semantics().href == "docs/My%20File.md#part%202"));
    if (anchor) expect(fatal(anchor->semantics().href == "#hello%2Dworld"));
    InlineDocument document{source, tree};
    expect(fatal(inline_visible_text(document) == U"100% local anchor"));
};

"html_comments_are_lossless_inert_inline_nodes"_test = [] {
    const auto source = std::u32string{U"before <!-- hidden\ncontent --> after"};
    const auto tree = parse_inline(source, test_context());
    expect(fatal(is_lossless(source)));
    const auto* comment = first_node(tree, InlineCstKind::HtmlComment);
    expect(fatal(comment != nullptr));
    if (comment) {
        expect(fatal(comment->status == ParseStatus::Complete));
        expect(fatal(source.substr(comment->range.start, comment->range.length())
            == U"<!-- hidden\ncontent -->"));
    }

    InlineDocument document{source, tree};
    expect(fatal(inline_visible_text(document) == U"before  after"));
};

"unfinished_html_comments_remain_visible_editing_text"_test = [] {
    const auto source = std::u32string{U"before <!-- unfinished"};
    const auto tree = parse_inline(source, test_context());
    expect(fatal(is_lossless(source)));
    expect(fatal(first_node(tree, InlineCstKind::HtmlComment) == nullptr));
    InlineDocument document{source, tree};
    expect(fatal(inline_visible_text(document) == source));
};

"html_text_islands_hide_nested_comments_without_enabling_markdown"_test = [] {
    const auto source = std::u32string{
        U"<span>*literal*<!-- hidden --><strong>visible</strong></span>"};
    const auto tree = parse_inline(source, test_context());
    expect(fatal(is_lossless(source)));
    const auto* span = first_html_node(tree.nodes, "span");
    expect(fatal(span != nullptr));
    if (span) {
        expect(fatal(contains_kind(span->children, InlineCstKind::HtmlComment)));
        expect(fatal(!contains_kind(span->children, InlineCstKind::Emphasis)));
    }
    InlineDocument document{source, tree};
    expect(fatal(inline_visible_text(document) == U"*literal*visible"));
};

}; // suite inline_cst_tests
