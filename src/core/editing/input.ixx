// folia.core.input — platform-agnostic editor input events.
export module folia.core.input;
import std;
import folia.core.types;
import folia.core.selection;
import folia.core.text_edit;
import folia.core.ids;

export namespace folia {

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
    NodeId container_id{};
    SourceRange range;                 // composition range / commit replace range
};

struct EditorInputEvent {
    enum class Kind {
        KeyDown, KeyUp, TextInput, PointerDown, PointerMove, PointerUp,
        Wheel, ClipboardPaste, FocusGained, FocusLost, DpiChanged,
    };
    Kind kind = Kind::KeyDown;
    KeyEvent key{};
    TextInputEvent text_input{};
    PointerEvent pointer{};
    WheelEvent wheel{};
    std::u32string paste_payload;
    float dpi = 0.0f;
};

} // namespace folia
