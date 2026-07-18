// folia.platform.editor_snippet_session — deterministic snippet selection navigation.
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
    std::optional<folia::TextSelection> selection;

    bool Handled() const { return kind != SnippetNavigationKind::NotHandled; }
};

struct EditorSnippetPlaceholder {
    folia::NodeId container;
    folia::SourceRange range;
    std::size_t tab_index = 0;
    bool current = false;

    bool operator==(EditorSnippetPlaceholder const&) const = default;
};

class EditorSnippetSession {
public:
    std::optional<folia::TextSelection> Start(
        folia::NodeId container,
        std::size_t base_offset,
        std::span<folia::SnippetTabStop const> tab_stops) {
        Cancel();
        if (tab_stops.empty()) return std::nullopt;
        state_.emplace();
        state_->container = container;
        state_->stops.reserve(tab_stops.size());
        for (auto const& stop : tab_stops) {
            state_->stops.push_back({
                stop.index,
                {
                    SaturatingAdd(base_offset, stop.range.start),
                    SaturatingAdd(base_offset, stop.range.end),
                },
            });
        }
        return SelectionAtCurrent();
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

        RebaseCurrent(selection);
        if (backward) {
            if (state.current == 0) return {SnippetNavigationKind::Stay};
            --state.current;
        } else {
            if (state.current + 1 >= state.stops.size()) {
                Cancel();
                return {SnippetNavigationKind::Complete};
            }
            ++state.current;
        }
        return {SnippetNavigationKind::Move, SelectionAtCurrent()};
    }

    std::vector<EditorSnippetPlaceholder> Placeholders(
        folia::TextSelection const& selection) {
        if (!state_) return {};
        if (selection.active.container_id != state_->container
            || selection.anchor.container_id != state_->container) {
            Cancel();
            return {};
        }

        RebaseCurrent(selection);
        std::vector<EditorSnippetPlaceholder> result;
        result.reserve(state_->stops.size());
        for (std::size_t index = 0; index < state_->stops.size(); ++index) {
            auto const& stop = state_->stops[index];
            // $0 is the final caret destination, not an editable placeholder.
            if (stop.tab_index == 0) continue;
            result.push_back({
                state_->container,
                stop.range,
                stop.tab_index,
                index == state_->current,
            });
        }
        return result;
    }

    void Cancel() { state_.reset(); }
    bool Active() const { return state_.has_value(); }

private:
    struct Stop {
        std::size_t tab_index = 0;
        folia::SourceRange range;
    };

    struct State {
        folia::NodeId container;
        std::vector<Stop> stops;
        std::size_t current = 0;
    };

    static std::size_t SaturatingAdd(std::size_t value, std::size_t delta) {
        auto maximum = (std::numeric_limits<std::size_t>::max)();
        return delta > maximum - value ? maximum : value + delta;
    }

    static std::size_t Shift(std::size_t value, std::int64_t delta) {
        if (delta >= 0) return SaturatingAdd(value, static_cast<std::size_t>(delta));
        auto magnitude = static_cast<std::size_t>(-(delta + 1)) + 1;
        return magnitude > value ? 0 : value - magnitude;
    }

    void RebaseCurrent(folia::TextSelection const& selection) {
        auto& state = *state_;
        auto& current = state.stops[state.current].range;
        auto actual = folia::SourceRange{
            (std::min)(selection.anchor.source_offset, selection.active.source_offset),
            (std::max)(selection.anchor.source_offset, selection.active.source_offset),
        };
        if (actual == current) return;

        auto old_end = current.end;
        std::int64_t delta = 0;
        if (actual.end >= old_end) {
            auto difference = actual.end - old_end;
            delta = difference > static_cast<std::size_t>((std::numeric_limits<std::int64_t>::max)())
                ? (std::numeric_limits<std::int64_t>::max)()
                : static_cast<std::int64_t>(difference);
        } else {
            auto difference = old_end - actual.end;
            delta = difference > static_cast<std::size_t>((std::numeric_limits<std::int64_t>::max)())
                ? (std::numeric_limits<std::int64_t>::min)() + 1
                : -static_cast<std::int64_t>(difference);
        }
        for (std::size_t index = 0; index < state.stops.size(); ++index) {
            auto& range = state.stops[index].range;
            if (index == state.current || range.start < old_end) continue;
            range.start = Shift(range.start, delta);
            range.end = Shift(range.end, delta);
        }
        current = actual;
    }

    std::optional<folia::TextSelection> SelectionAtCurrent() const {
        if (!state_ || state_->current >= state_->stops.size()) return std::nullopt;
        auto range = state_->stops[state_->current].range;
        if (range.empty()) {
            return folia::TextSelection::caret({
                state_->container,
                range.start,
                folia::TextAffinity::Downstream,
            });
        }
        return folia::TextSelection{
            {state_->container, range.start, folia::TextAffinity::Downstream},
            {state_->container, range.end, folia::TextAffinity::Upstream},
        };
    }

    std::optional<State> state_;
};

} // namespace folia::platform::editor
