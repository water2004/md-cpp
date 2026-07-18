// folia.platform.editor_input_command — deterministic Windows key gesture mapping.
export module folia.platform.editor_input_command;
import std;
export import folia.core.command;

export namespace folia::platform::editor
{
    // Values intentionally match Windows.System.VirtualKey without importing
    // WinRT into the deterministic input model.
    enum class EditorKey : std::uint32_t
    {
        Back = 8,
        Tab = 9,
        Enter = 13,
        Escape = 27,
        Space = 32,
        PageUp = 33,
        PageDown = 34,
        End = 35,
        Home = 36,
        Left = 37,
        Up = 38,
        Right = 39,
        Down = 40,
        DeleteKey = 46,
        Number0 = 48,
        Number1 = 49,
        Number2 = 50,
        Number3 = 51,
        Number4 = 52,
        Number5 = 53,
        Number6 = 54,
        Number7 = 55,
        Number8 = 56,
        Number9 = 57,
        A = 65,
        B = 66,
        C = 67,
        D = 68,
        E = 69,
        F = 70,
        G = 71,
        H = 72,
        I = 73,
        J = 74,
        K = 75,
        L = 76,
        M = 77,
        N = 78,
        O = 79,
        P = 80,
        Q = 81,
        R = 82,
        S = 83,
        T = 84,
        U = 85,
        V = 86,
        W = 87,
        X = 88,
        Y = 89,
        Z = 90,
        F1 = 112,
        F2 = 113,
        F3 = 114,
        F4 = 115,
        F5 = 116,
        F6 = 117,
        F7 = 118,
        F8 = 119,
        F9 = 120,
        F10 = 121,
        F11 = 122,
        F12 = 123,
    };

    struct EditorKeyGesture
    {
        EditorKey key{};
        bool control = false;
        bool shift = false;
        bool alt = false;

        bool operator==(EditorKeyGesture const&) const = default;
    };

    enum class EditorInputActionKind
    {
        None,
        ExecuteCommand,
        ExecuteCommandIfApplied,
        Copy,
        Cut,
        Paste,
        VisualLineStart,
        VisualLineEnd,
        VisualLineUp,
        VisualLineDown,
        TabForward,
        TabBackward,
    };

    struct EditorInputAction
    {
        EditorInputActionKind kind = EditorInputActionKind::None;
        folia::Command command{};

        bool Handled() const noexcept { return kind != EditorInputActionKind::None; }
    };

    // Hardware Enter is executed by the semantic block editor before
    // CoreText reports the corresponding text update.  Recognize that the
    // insertion is already present in the text service's UTF-16 projection
    // so it is not executed a second time.  The caller owns the ordered
    // ledger of outstanding hardware updates; therefore the caret may have
    // advanced through later edits by the time an earlier acknowledgement
    // arrives.
    inline bool IsAppliedSemanticNewlineUpdate(
        std::wstring_view text,
        std::size_t updateStart) noexcept
    {
        return updateStart < text.size() && text[updateStart] == L'\n';
    }

    inline EditorInputAction TranslateEditorKeyGesture(EditorKeyGesture gesture)
    {
        auto execute = [](folia::Command command)
        {
            return EditorInputAction{
                .kind = EditorInputActionKind::ExecuteCommand,
                .command = std::move(command),
            };
        };
        auto command = folia::Command{};

        // Configurable shortcuts are resolved by editor_shortcuts. Keeping a
        // second Ctrl/Alt mapping here would make an unset binding fire its
        // legacy action and violate the single command-mapping model.
        if (gesture.control || gesture.alt) return {};

        switch (gesture.key)
        {
            case EditorKey::Back:
                command.kind = folia::CommandKind::DeleteBackward;
                return execute(std::move(command));
            case EditorKey::DeleteKey:
                command.kind = folia::CommandKind::DeleteForward;
                return execute(std::move(command));
            case EditorKey::Enter:
                command.kind = gesture.shift
                    ? folia::CommandKind::InsertSoftBreak
                    : folia::CommandKind::InsertNewline;
                return {
                    .kind = EditorInputActionKind::ExecuteCommandIfApplied,
                    .command = std::move(command),
                };
            case EditorKey::Left:
                command.kind = folia::CommandKind::MoveLeft;
                command.extend_selection = gesture.shift;
                return execute(std::move(command));
            case EditorKey::Right:
                command.kind = folia::CommandKind::MoveRight;
                command.extend_selection = gesture.shift;
                return execute(std::move(command));
            case EditorKey::Up:
                return {
                    .kind = EditorInputActionKind::VisualLineUp,
                    .command = folia::Command::MoveUp(gesture.shift),
                };
            case EditorKey::Down:
                return {
                    .kind = EditorInputActionKind::VisualLineDown,
                    .command = folia::Command::MoveDown(gesture.shift),
                };
            case EditorKey::Home:
                command.kind = folia::CommandKind::MoveLineStart;
                command.extend_selection = gesture.shift;
                return {
                    .kind = EditorInputActionKind::VisualLineStart,
                    .command = std::move(command),
                };
            case EditorKey::End:
                command.kind = folia::CommandKind::MoveLineEnd;
                command.extend_selection = gesture.shift;
                return {
                    .kind = EditorInputActionKind::VisualLineEnd,
                    .command = std::move(command),
                };
            case EditorKey::Tab:
                return {
                    .kind = gesture.shift
                        ? EditorInputActionKind::TabBackward
                        : EditorInputActionKind::TabForward,
                };
            default:
                return {};
        }
    }
}
