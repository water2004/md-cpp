#include "support/folia_test.hpp"

import folia.core.snippet_template;
import folia.core.ids;
import folia.core.text_edit;
import folia.platform.editor_snippet_session;

using namespace boost::ut;
using namespace folia;
using namespace folia::platform::editor;

namespace
{
    TextSelection Caret(NodeId container, std::size_t offset)
    {
        return TextSelection::caret({container, offset, TextAffinity::Downstream});
    }

    TextSelection Selected(NodeId container, std::size_t start, std::size_t end)
    {
        return {
            {container, start, TextAffinity::Downstream},
            {container, end, TextAffinity::Upstream},
        };
    }
}

suite editor_snippet_session_tests = [] {

"snippet session starts at the first ordered stop and walks forward"_test = [] {
    auto parsed = parse_snippet_template(U"\\frac{$1}{$2}$0");
    EditorSnippetSession session;
    auto first = session.Start(NodeId{41}, 10, parsed.tab_stops);
    expect(fatal(first.has_value()));
    expect(*first == Caret(NodeId{41}, 16));
    auto second = session.Navigate(Caret(NodeId{41}, 16), false);
    expect(second.kind == SnippetNavigationKind::Move);
    expect(second.selection == Caret(NodeId{41}, 18));
    auto final = session.Navigate(Caret(NodeId{41}, 18), false);
    expect(final.kind == SnippetNavigationKind::Move);
    expect(final.selection == Caret(NodeId{41}, 19));
    auto completed = session.Navigate(Caret(NodeId{41}, 19), false);
    expect(completed.kind == SnippetNavigationKind::Complete);
    expect(!session.Active());
};

"typing at a stop rebases every following stop before navigation"_test = [] {
    auto parsed = parse_snippet_template(U"a$1b$2c$0");
    EditorSnippetSession session;
    auto first = session.Start(NodeId{42}, 20, parsed.tab_stops);
    expect(first == Caret(NodeId{42}, 21));
    auto second = session.Navigate(Caret(NodeId{42}, 25), false);
    expect(second.selection == Caret(NodeId{42}, 26));
    auto back = session.Navigate(Caret(NodeId{42}, 26), true);
    expect(back.selection == Caret(NodeId{42}, 25));
};

"a caret moved backward rebases later stops without unsigned underflow"_test = [] {
    auto parsed = parse_snippet_template(U"$1abc$2$0");
    EditorSnippetSession session;
    session.Start(NodeId{47}, 10, parsed.tab_stops);
    auto next = session.Navigate(Caret(NodeId{47}, 8), false);
    expect(next.selection == Caret(NodeId{47}, 11));
    auto final = session.Navigate(Caret(NodeId{47}, 11), false);
    expect(final.selection == Caret(NodeId{47}, 11));
};

"default placeholder text is selected and later stops rebase after replacement"_test = [] {
    auto parsed = parse_snippet_template(U"A${1:selected}B$2C$0");
    EditorSnippetSession session;
    auto first = session.Start(NodeId{50}, 10, parsed.tab_stops);
    expect(first == Selected(NodeId{50}, 11, 19));

    auto second = session.Navigate(Caret(NodeId{50}, 14), false);
    expect(second.selection == Caret(NodeId{50}, 15));
    auto final = session.Navigate(Caret(NodeId{50}, 15), false);
    expect(final.selection == Caret(NodeId{50}, 16));
};

"backward navigation at the first stop is handled without moving"_test = [] {
    auto parsed = parse_snippet_template(U"$1x$0");
    EditorSnippetSession session;
    session.Start(NodeId{43}, 0, parsed.tab_stops);
    auto result = session.Navigate(Caret(NodeId{43}, 0), true);
    expect(result.kind == SnippetNavigationKind::Stay);
    expect(!result.selection);
    expect(session.Active());
};

"selection leaving the insertion container cancels the session"_test = [] {
    auto parsed = parse_snippet_template(U"$1x$0");
    EditorSnippetSession session;
    session.Start(NodeId{44}, 0, parsed.tab_stops);
    auto result = session.Navigate(Caret(NodeId{45}, 0), false);
    expect(result.kind == SnippetNavigationKind::NotHandled);
    expect(!session.Active());
    expect(!session.Navigate(Caret(NodeId{44}, 0), false).Handled());
};

"a cross-container selection anchor also cancels the session"_test = [] {
    auto parsed = parse_snippet_template(U"$1x$0");
    EditorSnippetSession session;
    session.Start(NodeId{48}, 0, parsed.tab_stops);
    auto selection = TextSelection{
        {NodeId{49}, 0, TextAffinity::Downstream},
        {NodeId{48}, 0, TextAffinity::Downstream},
    };
    expect(!session.Navigate(selection, false).Handled());
    expect(!session.Active());
};

"empty templates do not create a navigation session"_test = [] {
    EditorSnippetSession session;
    expect(!session.Start(NodeId{46}, 0, {}).has_value());
    expect(!session.Active());
};

};
