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

};
