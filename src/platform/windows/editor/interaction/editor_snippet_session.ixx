// folia.platform.editor_snippet_session — deterministic snippet tab-stop navigation.
export module folia.platform.editor_snippet_session;
import std;
import folia.core.ids;
import folia.core.snippet_template;
import folia.core.text_edit;

export namespace folia::platform::editor {

enum class SnippetNavigationKind {
    NotHandled,
    Stay,
    Move,
    Complete,
};

struct SnippetNavigationResult {
    SnippetNavigationKind kind = SnippetNavigationKind::NotHandled;
    std::optional<folia::TextPosition> position;

    bool Handled() const { return kind != SnippetNavigationKind::NotHandled; }
};

class EditorSnippetSession {
public:
    std::optional<folia::TextPosition> Start(
        folia::NodeId container,
        std::size_t base_offset,
        std::span<folia::SnippetTabStop const> tab_stops) {
        Cancel();
        if (tab_stops.empty()) return std::nullopt;
        state_.emplace();
        state_->container = container;
        state_->offsets.reserve(tab_stops.size());
        for (auto const& stop : tab_stops) {
            state_->offsets.push_back(SaturatingAdd(base_offset, stop.range.start));
        }
        return PositionAtCurrent();
    }

    SnippetNavigationResult Navigate(
        folia::TextSelection const& selection,
        bool backward) {
        if (!state_) return {};
        auto& state = *state_;
        if (selection.active.container_id != state.container
            || selection.anchor.container_id != state.container) {
            Cancel();
            return {};
        }

        RebaseFollowingStops(selection.active.source_offset);
        if (backward) {
            if (state.current == 0) return {SnippetNavigationKind::Stay};
            --state.current;
        } else {
            if (state.current + 1 >= state.offsets.size()) {
                Cancel();
                return {SnippetNavigationKind::Complete};
            }
            ++state.current;
        }
        return {SnippetNavigationKind::Move, PositionAtCurrent()};
    }

    void Cancel() { state_.reset(); }
    bool Active() const { return state_.has_value(); }

private:
    struct State {
        folia::NodeId container;
        std::vector<std::size_t> offsets;
        std::size_t current = 0;
    };

    static std::size_t SaturatingAdd(std::size_t value, std::size_t delta) {
        auto maximum = (std::numeric_limits<std::size_t>::max)();
        return delta > maximum - value ? maximum : value + delta;
    }

    void RebaseFollowingStops(std::size_t caret) {
        auto& state = *state_;
        auto current_offset = state.offsets[state.current];
        if (caret == current_offset) return;
        for (std::size_t index = state.current + 1; index < state.offsets.size(); ++index) {
            if (caret > current_offset) {
                state.offsets[index] = SaturatingAdd(
                    state.offsets[index], caret - current_offset);
            } else {
                auto delta = current_offset - caret;
                state.offsets[index] = delta > state.offsets[index]
                    ? 0
                    : state.offsets[index] - delta;
            }
        }
        state.offsets[state.current] = caret;
    }

    std::optional<folia::TextPosition> PositionAtCurrent() const {
        if (!state_ || state_->current >= state_->offsets.size()) return std::nullopt;
        return folia::TextPosition{
            state_->container,
            state_->offsets[state_->current],
            folia::TextAffinity::Downstream,
        };
    }

    std::optional<State> state_;
};

} // namespace folia::platform::editor
