#include <string>
#include <vector>

#include "elmd_test.hpp"
import elmd.core.slug;
import elmd.core.outline;
import elmd.core.ast;
import elmd.core.ids;
import elmd.core.parser;

using namespace elmd;
using namespace boost::ut;


suite outline_tests = [] {

"test_basic_slug"_test = [] {
    expect(fatal(bool((generate_slug("Hello World", {})) == (std::string("hello-world")))));
};

"test_duplicate_slug"_test = [] {
    std::vector<std::string> titles = {"intro", "intro", "intro"};
    auto slugs = generate_unique_slugs(titles);
    expect(fatal(bool((slugs.size()) == (3u))));
    expect(fatal(bool((slugs[0]) == (std::string("intro")))));
    expect(fatal(bool((slugs[1]) == (std::string("intro-1")))));
    expect(fatal(bool((slugs[2]) == (std::string("intro-2")))));
};

"test_special_chars_slug"_test = [] {
    expect(fatal(bool((generate_slug("C# is #1!", {})) == (std::string("c-is-1")))));
};

"test_empty_title_slug"_test = [] {
    expect(fatal(bool((generate_slug("", {})) == (std::string("section")))));
};

"test_nested_outline"_test = [] {
    // headings H1 H2 H3 H2b
    auto out = parse_text(1, "# H1\n## H2\n### H3\n## H2b\n");
    auto& items = out.outline.items;
    expect(fatal(bool((items.size()) == (1u))));
    if (!items.empty()) {
        expect(fatal(bool((items[0].children.size()) == (2u))));
        if (items[0].children.size() == 2) {
            expect(fatal(bool((items[0].children[0].children.size()) == (1u))));
        }
    }
};

"test_find_by_slug"_test = [] {
    auto out = parse_text(1, "# Intro\n## Details\n");
    auto p = out.outline.find_item_by_slug("details");
    expect(fatal(bool(p != nullptr)));
    auto none = out.outline.find_item_by_slug("nonexistent");
    expect(fatal(bool(none == nullptr)));
};

}; // suite outline_tests
