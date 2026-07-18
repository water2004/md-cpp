#include <string>

#include "support/folia_test.hpp"
import elmd.core.editor;
import elmd.core.search;
import elmd.core.utf;

using namespace elmd;
using namespace boost::ut;

namespace {
SearchOptions exact_options() {
    SearchOptions options;
    options.case_sensitive = true;
    return options;
}
}

suite rendered_search_tests = [] {

"rendered search excludes markdown markers link destinations and html tags"_test = [] {
    Editor editor(
        "**Alpha** and [beta](https://example.test/path) "
        "<strong data-name=\"hidden\">Gamma</strong>");
    for (auto hidden : {U"**", U"https", U"example", U"strong", U"hidden"}) {
        auto result = search_rendered_document(editor.document(), hidden, exact_options());
        expect(fatal(result.valid())) << cps_to_utf8(hidden);
        expect(bool(result.matches.empty())) << cps_to_utf8(hidden);
    }
    for (auto visible : {U"Alpha", U"beta", U"Gamma"}) {
        auto result = search_rendered_document(editor.document(), visible, exact_options());
        expect(fatal(result.valid())) << cps_to_utf8(visible);
        expect(bool(result.matches.size() == 1u)) << cps_to_utf8(visible);
    }
};

"rendered search maps a cross-format match to disjoint local source ranges"_test = [] {
    Editor editor("**foo**<em>bar</em>");
    auto result = search_rendered_document(editor.document(), U"foobar", exact_options());
    expect(fatal(result.valid()));
    expect(fatal(bool(result.matches.size() == 1u)));
    if (result.matches.empty()) return;
    auto const& match = result.matches.front();
    expect(fatal(bool(match.source_ranges.size() == 2u)));
    auto const editable = editor.editable_source(match.container_id);
    expect(fatal(editable.has_value()));
    if (!editable || match.source_ranges.size() != 2u) return;
    expect(bool(editable->substr(match.source_ranges[0].start, match.source_ranges[0].length()) == U"foo"));
    expect(bool(editable->substr(match.source_ranges[1].start, match.source_ranges[1].length()) == U"bar"));
};

"rendered replacement preserves markdown and html markers and is one undo step"_test = [] {
    Editor editor("**one** <em>one</em> [one](https://one.test)");
    auto found = search_rendered_document_for_replacement(
        editor.document(), U"one", U"two", exact_options());
    expect(fatal(found.valid()));
    expect(fatal(bool(found.matches.size() == 3u)));
    expect(fatal(bool(editor.execute_document_replace_matches(found.matches).has_value())));
    expect(bool(editor.markdown_utf8()
        == "**two** <em>two</em> [two](https://one.test)"));
    expect(fatal(editor.undo()));
    expect(bool(editor.markdown_utf8()
        == "**one** <em>one</em> [one](https://one.test)"));
    expect(fatal(editor.redo()));
    expect(bool(editor.markdown_utf8()
        == "**two** <em>two</em> [two](https://one.test)"));
};

};
