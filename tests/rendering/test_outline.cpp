#include <string>
#include <vector>

#include "support/folia_test.hpp"
import folia.core.slug;
import folia.core.outline;
import folia.core.ast;
import folia.core.ids;
import folia.core.parser;

using namespace folia;
using namespace boost::ut;


suite outline_tests = [] {

"url_component_percent_decoding_is_scoped_and_loss_tolerant"_test = [] {
    expect(fatal(percent_decode_url_component("a%20b") == "a b"));
    expect(fatal(percent_decode_url_component("%E4%B8%AD%E6%96%87") == "中文"));
    expect(fatal(percent_decode_url_component("%2520") == "%20"));
    expect(fatal(percent_decode_url_component("a+b") == "a+b"));
    expect(fatal(percent_decode_url_component("bad%2%ZZtail") == "bad%2%ZZtail"));
    expect(fatal(percent_decode_url_component("%00tail") == "%00tail"));
};

"unicode_heading_slugs_are_addressable_by_encoded_page_urls"_test = [] {
    auto out = parse_text(1, "# 中文标题\n");
    const auto* item = out.outline.find_item_by_url_fragment(
        "#%E4%B8%AD%E6%96%87%E6%A0%87%E9%A2%98");
    expect(fatal(item != nullptr));
    if (item) expect(fatal(item->slug == "中文标题"));
};

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
    auto out = parse_text(1, "# Intro\n## Hello World\n");
    auto p = out.outline.find_item_by_slug("hello-world");
    expect(fatal(bool(p != nullptr)));
    expect(fatal(out.outline.find_item_by_url_fragment("#hello%2Dworld") == p));
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
