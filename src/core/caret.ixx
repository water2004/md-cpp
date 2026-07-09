// elmd.core.caret — caret visual state (position + blink), separated from
// selection core for the renderer. Pure core.
export module elmd.core.caret;
import std;
import elmd.core.types;
import elmd.core.selection;

export namespace elmd {

struct CaretState {
    CharOffset position{};
    TextAffinity affinity = TextAffinity::Downstream;
    bool visible = true;
    bool blink_phase = true;

    CaretState() = default;
    explicit CaretState(CharOffset p) : position(p) {}
    static CaretState at(CharOffset p) { return CaretState(p); }
};

} // namespace elmd