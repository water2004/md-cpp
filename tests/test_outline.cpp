import std;
#include "test_framework.h"
import elmd.core.slug;
import elmd.core.outline;
import elmd.core.ast;
import elmd.core.ids;
import elmd.core.parser;

using namespace elmd;

ELMD_TEST(test_basic_slug) {
    ELMD_CHECK_EQ(generate_slug("Hello World", {}), std::string("hello-world"));
}

ELMD_TEST(test_duplicate_slug) {
    std::vector<std::string> titles = {"intro", "intro", "intro"};
    auto slugs = generate_unique_slugs(titles);
    ELMD_CHECK_EQ(slugs.size(), 3u);
    ELMD_CHECK_EQ(slugs[0], std::string("intro"));
    ELMD_CHECK_EQ(slugs[1], std::string("intro-1"));
    ELMD_CHECK_EQ(slugs[2], std::string("intro-2"));
}

ELMD_TEST(test_special_chars_slug) {
    ELMD_CHECK_EQ(generate_slug("C# is #1!", {}), std::string("c-is-1"));
}

ELMD_TEST(test_empty_title_slug) {
    ELMD_CHECK_EQ(generate_slug("", {}), std::string("section"));
}

ELMD_TEST(test_nested_outline) {
    // headings H1 H2 H3 H2b
    auto out = parse_text(1, "# H1\n## H2\n### H3\n## H2b\n");
    auto& items = out.outline.items;
    ELMD_CHECK_EQ(items.size(), 1u);
    if (!items.empty()) {
        ELMD_CHECK_EQ(items[0].children.size(), 2u);
        if (items[0].children.size() == 2) {
            ELMD_CHECK_EQ(items[0].children[0].children.size(), 1u);
        }
    }
}

ELMD_TEST(test_find_by_slug) {
    auto out = parse_text(1, "# Intro\n## Details\n");
    auto p = out.outline.find_item_by_slug("details");
    ELMD_CHECK(p != nullptr);
    auto none = out.outline.find_item_by_slug("nonexistent");
    ELMD_CHECK(none == nullptr);
}