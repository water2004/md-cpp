// elmd.core.selection — Selection + TextAffinity + CaretState.
export module elmd.core.selection;
import std;
import elmd.core.types;
import elmd.core.utf;

export namespace elmd {

enum class TextAffinity { Upstream, Downstream };

struct Selection {
    CharOffset anchor{};
    CharOffset active{};
    TextAffinity affinity = TextAffinity::Downstream;

    static Selection caret(CharOffset p) {
        Selection s; s.anchor = s.active = p; s.affinity = TextAffinity::Downstream; return s;
    }
    bool is_caret() const { return anchor == active; }
    TextRange<CharOffset> normalized_range() const {
        if (anchor.v <= active.v) return {anchor, active};
        return {active, anchor};
    }
    CharOffset head() const { return active; } // the "cursor end"
    CharOffset tail() const { return anchor; } // the "fixed end"
};

struct CaretState {
    CharOffset position{};
    TextAffinity affinity = TextAffinity::Downstream;
    bool visible = true;
    bool blink_phase = true;
    CaretState() = default;
    explicit CaretState(CharOffset p) : position(p) {}
};

} // namespace elmd