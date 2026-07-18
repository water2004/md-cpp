#include <string>

#include "support/folia_test.hpp"
import folia.core.editor;
import folia.core.search;

using namespace folia;
using namespace boost::ut;

namespace {
SearchOptions exact_options() {
    SearchOptions options;
    options.case_sensitive = true;
    return options;
}
}

suite rendered_search_edge_tests = [] {

"rendered search decodes entities and retains exact source mappings"_test = [] {
    Editor editor("one &amp; \\* two");
    auto result = search_rendered_document(editor.document(), U"one & * two", exact_options());
    expect(fatal(result.valid()));
    expect(fatal(bool(result.matches.size() == 1u)));
    if (!result.matches.empty()) expect(bool(!result.matches.front().source_ranges.empty()));
};

"rendered search walks nested owners but never crosses block boundaries"_test = [] {
    Editor editor(
        "> before\n> - nested **needle**\n\n"
        "| key | value |\n| --- | --- |\n| cell | needle |\n\n"
        "left\n\nright");
    auto nested = search_rendered_document(editor.document(), U"needle", exact_options());
    expect(fatal(nested.valid()));
    expect(bool(nested.matches.size() == 2u));
    auto across = search_rendered_document(editor.document(), U"leftright", exact_options());
    expect(fatal(across.valid()));
    expect(bool(across.matches.empty()));
};

"rendered code search sees content but not fences or info strings"_test = [] {
    Editor editor("```cpp\nneedle();\n```");
    auto content = search_rendered_document(editor.document(), U"needle", exact_options());
    expect(fatal(content.valid()));
    expect(fatal(bool(content.matches.size() == 1u)));
    for (auto hidden : {U"```", U"cpp"}) {
        auto result = search_rendered_document(editor.document(), hidden, exact_options());
        expect(fatal(result.valid()));
        expect(bool(result.matches.empty()));
    }
};

};
