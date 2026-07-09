// elmd.core.input — platform-agnostic editor input events.
export module elmd.core.input;
import std;
import elmd.core.types;
import elmd.core.theme;
import elmd.core.selection;

export namespace elmd {

enum class KeyCode {
    Char, Left, Right, Up, Down, Home, End, PageUp, PageDown,
    Backspace, Delete, Enter, Tab, Escape, Space, Unknown,
};

struct Modifiers {
    bool ctrl = false, shift = false, alt = false, meta = false;
};

struct KeyEvent {
    KeyCode key_code = KeyCode::Unknown;
    Modifiers modifiers{};
    bool is_repeat = false;
    char32_t ch = 0; // when key_code == Char
};

enum class PointerButton { None, Left, Right, Middle };
struct PointerEvent {
    LogicalPoint position{};
    PointerButton button = PointerButton::None;
    Modifiers modifiers{};
    std::uint32_t click_count = 0;
};

struct WheelEvent {
    float delta = 0.0f;
    LogicalPoint position{};
    Modifiers modifiers{};
};

struct TextInputEvent {
    enum class Kind { InsertText, CompositionStart, CompositionUpdate, CompositionCommit, CompositionCancel };
    Kind kind = Kind::InsertText;
    std::u32string text;
    TextRange<CharOffset> range;       // composition range / commit replace range
};

struct EditorInputEvent {
    enum class Kind {
        KeyDown, KeyUp, TextInput, PointerDown, PointerMove, PointerUp,
        Wheel, ClipboardPaste, FocusGained, FocusLost, DpiChanged, ThemeChanged,
    };
    Kind kind = Kind::KeyDown;
    KeyEvent key{};
    TextInputEvent text_input{};
    PointerEvent pointer{};
    WheelEvent wheel{};
    std::u32string paste_payload;
    float dpi = 0.0f;
    Theme theme = Theme::Dark;
};

} // namespace elmd