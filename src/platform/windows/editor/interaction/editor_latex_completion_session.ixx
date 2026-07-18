// folia.platform.editor_latex_completion_session — deterministic candidate navigation state.
export module folia.platform.editor_latex_completion_session;
import std;
import folia.core.ids;
import folia.core.latex_completion;
import folia.core.text_edit;

export namespace folia::platform::editor {

struct LatexCompletionAcceptance {
    folia::NodeId container;
    folia::SourceRange replacement;
    folia::LatexCommandDefinition command;
};

class EditorLatexCompletionSession {
public:
    bool Update(
        folia::NodeId container,
        folia::SourceRange replacement,
        std::vector<folia::LatexCompletionCandidate> candidates) {
        if (candidates.empty()) {
            Cancel();
            return false;
        }
        auto previous_id = Selected() ? std::optional{Selected()->command.id} : std::nullopt;
        state_ = State{
            .container = container,
            .replacement = replacement,
            .candidates = std::move(candidates),
        };
        if (previous_id) {
            auto found = std::ranges::find(
                state_->candidates, *previous_id,
                [](auto const& candidate) { return candidate.command.id; });
            if (found != state_->candidates.end())
                state_->selected = static_cast<std::size_t>(found - state_->candidates.begin());
        }
        return true;
    }

    bool Move(int delta) {
        if (!state_ || state_->candidates.empty() || delta == 0) return false;
        auto count = static_cast<std::int64_t>(state_->candidates.size());
        auto next = static_cast<std::int64_t>(state_->selected) + delta;
        next %= count;
        if (next < 0) next += count;
        state_->selected = static_cast<std::size_t>(next);
        return true;
    }

    bool Select(std::size_t index) {
        if (!state_ || index >= state_->candidates.size()) return false;
        state_->selected = index;
        return true;
    }

    std::optional<LatexCompletionAcceptance> Accept() {
        auto const* selected = Selected();
        if (!state_ || !selected) return std::nullopt;
        auto result = LatexCompletionAcceptance{
            state_->container,
            state_->replacement,
            selected->command,
        };
        Cancel();
        return result;
    }

    void Cancel() { state_.reset(); }
    bool Active() const { return state_.has_value(); }
    std::size_t SelectedIndex() const { return state_ ? state_->selected : 0; }

    std::span<folia::LatexCompletionCandidate const> Candidates() const {
        return state_ ? std::span<folia::LatexCompletionCandidate const>{state_->candidates}
                      : std::span<folia::LatexCompletionCandidate const>{};
    }

    folia::LatexCompletionCandidate const* Selected() const {
        if (!state_ || state_->selected >= state_->candidates.size()) return nullptr;
        return &state_->candidates[state_->selected];
    }

private:
    struct State {
        folia::NodeId container;
        folia::SourceRange replacement;
        std::vector<folia::LatexCompletionCandidate> candidates;
        std::size_t selected = 0;
    };
    std::optional<State> state_;
};

} // namespace folia::platform::editor
