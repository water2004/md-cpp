#include "support/folia_test.hpp"

import folia.core.latex_completion;
import folia.core.ids;
import folia.platform.editor_latex_completion_session;

using namespace boost::ut;
using namespace folia;
using namespace folia::platform::editor;

namespace {

LatexCompletionCandidate Candidate(std::string id) {
    return {
        .command = {
            .id = std::move(id),
            .trigger = U"command",
            .snippet = U"\\command$0",
        },
    };
}

} // namespace

suite editor_latex_completion_session_tests = [] {

"completion navigation wraps and acceptance preserves the replacement range"_test = [] {
    EditorLatexCompletionSession session;
    expect(session.Update(NodeId{7}, {4, 8}, {Candidate("a"), Candidate("b")}));
    expect(session.Selected()->command.id == "a");
    expect(session.Move(-1));
    expect(session.Selected()->command.id == "b");
    expect(session.Move(1));
    auto accepted = session.Accept();
    expect(fatal(accepted.has_value()));
    expect(accepted->container == NodeId{7});
    expect(accepted->replacement == SourceRange{4, 8});
    expect(accepted->command.id == "a");
    expect(!session.Active());
};

"refresh keeps the selected command when ranking changes"_test = [] {
    EditorLatexCompletionSession session;
    session.Update(NodeId{8}, {1, 3}, {Candidate("a"), Candidate("b")});
    session.Select(1);
    session.Update(NodeId{8}, {1, 4}, {Candidate("b"), Candidate("a")});
    expect(session.SelectedIndex() == 0_u);
    expect(session.Selected()->command.id == "b");
};

"empty refresh and invalid selection close or preserve state predictably"_test = [] {
    EditorLatexCompletionSession session;
    session.Update(NodeId{9}, {0, 1}, {Candidate("a")});
    expect(!session.Select(2));
    expect(session.Active());
    expect(!session.Update(NodeId{9}, {0, 1}, {}));
    expect(!session.Active());
    expect(!session.Accept().has_value());
};

};
