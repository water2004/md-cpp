#include <string>
#include <vector>

#include "support/folia_test.hpp"
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

"rendered_outline_keeps_headings_after_crlf_tables_and_fences"_test = [] {
    const std::string source =
        "# API\r\n\r\n"
        "## Routes\r\n\r\n"
        "| Group | Prefix |\r\n"
        "| --- | --- |\r\n"
        "| session | `/login` |\r\n\r\n"
        "## Example\r\n\r\n"
        "```text\r\n"
        "/v1\r\n"
        "```\r\n\r\n"
        "## Tail\r\n"
        "outside";
    const auto parsed = parse_text(1, source);
    const auto flat = parsed.outline.flat_items();
    expect(fatal(bool(flat.size() == 4u)));
    if (flat.size() != 4u) return;
    expect(fatal(bool(flat[0]->title_plain_text == "API")));
    expect(fatal(bool(flat[1]->title_plain_text == "Routes")));
    expect(fatal(bool(flat[2]->title_plain_text == "Example")));
    expect(fatal(bool(flat[3]->title_plain_text == "Tail")));
    expect(fatal(bool(flat[0]->children.size() == 3u)));
};

}; // suite outline_tests
