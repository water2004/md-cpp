#include "support/folia_test.hpp"

import elmd.core.html_cst;
import elmd.core.utf;

using namespace boost::ut;
using namespace elmd;

namespace {

const HtmlCstNode* find_element(
    const std::vector<HtmlCstNode>& nodes,
    std::string_view tag) {
    for (const auto& node : nodes) {
        if (node.kind == HtmlCstKind::Element && node.tag_name == tag) return &node;
        if (const auto* nested = find_element(node.children, tag)) return nested;
    }
    return nullptr;
}

} // namespace

suite html_cst_tests = [] {
    "html_cst_is_character_exact_for_recursive_unicode_table"_test = [] {
        const auto source = utf8_to_cps(
            "<table data-name='寄存器'><tbody><tr><td>%A0</td><td>32</td>"
            "</tr></tbody></table>");
        const auto tree = parse_html_cst(source);
        expect(html_tokens_partition_source(tree, source.size()));
        expect(flatten_html_tokens(tree, source) == source);
        const auto* table = find_element(tree.nodes, "table");
        expect(fatal(table != nullptr));
        expect(table->status == HtmlParseStatus::Complete);
        expect(table->attributes.size() == 1_u);
        expect(table->attributes.front().name == "data-name");
        expect(find_element(table->children, "tbody") != nullptr);
        expect(find_element(table->children, "tr") != nullptr);
        expect(find_element(table->children, "td") != nullptr);
    };

    "html_cst_preserves_mixed_quotes_case_and_whitespace"_test = [] {
        const auto source = utf8_to_cps("<DIV id = \"A\" data-x='1' disabled >x</DIV>");
        const auto tree = parse_html_cst(source);
        expect(flatten_html_tokens(tree, source) == source);
        const auto* div = find_element(tree.nodes, "div");
        expect(fatal(div != nullptr));
        expect(div->attributes.size() == 3_u);
        expect(div->opening.length() > 0_u);
        expect(div->closing.has_value());
    };

    "html_cst_keeps_incomplete_markup_lossless"_test = [] {
        const auto source = utf8_to_cps("<table><tr><td>value</tr>");
        const auto tree = parse_html_cst(source);
        expect(html_tokens_partition_source(tree, source.size()));
        expect(flatten_html_tokens(tree, source) == source);
        expect(tree.has_error);
    };

    "html_cst_classifies_unsafe_elements_without_executing_them"_test = [] {
        const auto source = utf8_to_cps("<script>alert(1)</script><iframe src='x'></iframe>");
        const auto tree = parse_html_cst(source);
        expect(flatten_html_tokens(tree, source) == source);
        const auto* script = find_element(tree.nodes, "script");
        const auto* iframe = find_element(tree.nodes, "iframe");
        expect(fatal(script != nullptr));
        expect(fatal(iframe != nullptr));
        expect(html_is_unsafe_element(script->tag_name));
        expect(html_is_unsafe_element(iframe->tag_name));
    };
};
