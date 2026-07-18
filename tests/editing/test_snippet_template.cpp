#include "support/folia_test.hpp"

import folia.core.snippet_template;

using namespace boost::ut;
using namespace folia;

suite snippet_template_tests = [] {

"snippet placeholders produce ordered zero-width tab stops"_test = [] {
    auto parsed = parse_snippet_template(U"\\frac{$2}{$1}$0");
    expect(parsed.text == U"\\frac{}{}");
    expect(parsed.tab_stops.size() == 3_u);
    expect(parsed.tab_stops[0] == SnippetTabStop{1, {8, 8}});
    expect(parsed.tab_stops[1] == SnippetTabStop{2, {6, 6}});
    expect(parsed.tab_stops[2] == SnippetTabStop{0, {9, 9}});
};

"snippet dollar escaping and incomplete markers are lossless"_test = [] {
    auto parsed = parse_snippet_template(U"$$x$-$$1$10");
    expect(parsed.text == U"$x$-$1");
    expect(parsed.tab_stops.size() == 1_u);
    expect(parsed.tab_stops[0] == SnippetTabStop{10, {6, 6}});
};

"duplicate placeholders retain deterministic occurrence order"_test = [] {
    auto parsed = parse_snippet_template(U"a$2b$1c$2");
    expect(parsed.text == U"abc");
    expect(parsed.tab_stops.size() == 3_u);
    expect(parsed.tab_stops[0].range.start == 2_u);
    expect(parsed.tab_stops[1].range.start == 1_u);
    expect(parsed.tab_stops[2].range.start == 3_u);
};

"multiline templates preserve every line and order their stops"_test = [] {
    auto parsed = parse_snippet_template(UR"(\begin{matrix}
${1:a & b} \\
$2 & $3
\end{matrix}$0)");
    expect(parsed.text ==
        U"\\begin{matrix}\n"
        U"a & b \\\\\n"
        U" & \n"
        U"\\end{matrix}");
    expect(parsed.tab_stops.size() == 4_u);
    expect(parsed.tab_stops[0] == SnippetTabStop{1, {15, 20}});
    expect(parsed.tab_stops[1].index == 2_u);
    expect(parsed.tab_stops[2].index == 3_u);
    expect(parsed.tab_stops[3].index == 0_u);
};

"selected text variables can populate and select a placeholder"_test = [] {
    std::u32string selected = U"x_1 + x_2";
    auto parsed = parse_snippet_template(
        U"\\mathbf{${1:${TM_SELECTED_TEXT}}}$0",
        {.selected_text = selected});
    expect(parsed.text == U"\\mathbf{x_1 + x_2}");
    expect(parsed.tab_stops.size() == 2_u);
    expect(parsed.tab_stops[0] == SnippetTabStop{1, {8, 17}});
    expect(parsed.tab_stops[1] == SnippetTabStop{0, {18, 18}});
};

"selected text is inserted verbatim instead of being reparsed as a template"_test = [] {
    std::u32string selected = U"$1 ${2:not a stop}\nnext";
    auto parsed = parse_snippet_template(
        U"before ${TM_SELECTED_TEXT} after",
        {.selected_text = selected});
    expect(parsed.text == U"before $1 ${2:not a stop}\nnext after");
    expect(parsed.tab_stops.empty());
};

"braced placeholders support defaults and selected text fallback"_test = [] {
    auto parsed = parse_snippet_template(
        U"${1:default} ${2:\\frac{$3}{x}} ${TM_SELECTED_TEXT:fallback}$0");
    expect(parsed.text == U"default \\frac{}{x} fallback");
    expect(parsed.tab_stops.size() == 4_u);
    expect(parsed.tab_stops[0] == SnippetTabStop{1, {0, 7}});
    expect(parsed.tab_stops[1].index == 2_u);
    expect(parsed.tab_stops[2].index == 3_u);
    expect(parsed.tab_stops[3].index == 0_u);
};

"unknown and incomplete variables remain literal"_test = [] {
    auto parsed = parse_snippet_template(U"$UNKNOWN ${UNKNOWN:value} ${1:open");
    expect(parsed.text == U"$UNKNOWN ${UNKNOWN:value} ${1:open");
    expect(parsed.tab_stops.empty());
};

};
