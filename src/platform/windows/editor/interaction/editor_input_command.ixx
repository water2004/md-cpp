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
        End = 35,
        Home = 36,
        Left = 37,
        Up = 38,
        Right = 39,
        Down = 40,
        DeleteKey = 46,
        Number1 = 49,
        Number2 = 50,
        Number7 = 55,
        Number8 = 56,
        Number9 = 57,
        A = 65,
        B = 66,
        C = 67,
        I = 73,
        Q = 81,
        T = 84,
        V = 86,
        X = 88,
        Y = 89,
        Z = 90,
    };

    struct EditorKeyGesture
    {
        EditorKey key{};
        bool control = false;
        bool shift = false;
        bool alt = false;
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

        if (gesture.control)
        {
            switch (gesture.key)
            {
                case EditorKey::Up:
                    command.kind = gesture.alt
                        ? folia::CommandKind::MoveTableRowUp
                        : folia::CommandKind::InsertTableRowAbove;
                    break;
                case EditorKey::Down:
                    command.kind = gesture.alt
                        ? folia::CommandKind::MoveTableRowDown
                        : folia::CommandKind::InsertTableRowBelow;
                    break;
                case EditorKey::Left:
                    command.kind = gesture.alt
                        ? folia::CommandKind::MoveTableColumnLeft
                        : folia::CommandKind::InsertTableColumnLeft;
                    break;
                case EditorKey::Right:
                    command.kind = gesture.alt
                        ? folia::CommandKind::MoveTableColumnRight
                        : folia::CommandKind::InsertTableColumnRight;
                    break;
                case EditorKey::Back:
                    command.kind = folia::CommandKind::DeleteTableRow;
                    break;
                case EditorKey::DeleteKey:
                    command.kind = folia::CommandKind::DeleteTableColumn;
                    break;
                case EditorKey::Home:
                    command.kind = folia::CommandKind::MoveDocumentStart;
                    command.extend_selection = gesture.shift;
                    break;
                case EditorKey::End:
                    command.kind = folia::CommandKind::MoveDocumentEnd;
                    command.extend_selection = gesture.shift;
                    break;
                case EditorKey::Number1:
                    command.kind = folia::CommandKind::SetHeading;
                    command.level = 1;
                    break;
                case EditorKey::Number2:
                    command.kind = folia::CommandKind::SetHeading;
                    command.level = 2;
                    break;
                case EditorKey::Number7:
                    command.kind = folia::CommandKind::ToggleOrderedList;
                    break;
                case EditorKey::Number8:
                    command.kind = folia::CommandKind::ToggleUnorderedList;
                    break;
                case EditorKey::Number9:
                    command.kind = folia::CommandKind::ToggleTaskList;
                    break;
                case EditorKey::B:
                    command.kind = folia::CommandKind::ToggleStrong;
                    break;
                case EditorKey::I:
                    command.kind = folia::CommandKind::ToggleEmphasis;
                    break;
                case EditorKey::Q:
                    command.kind = folia::CommandKind::ToggleBlockQuote;
                    break;
                case EditorKey::T:
                    command.kind = folia::CommandKind::InsertTable;
                    command.rows = 2;
                    command.cols = 3;
                    break;
                case EditorKey::Z:
                    command.kind = gesture.shift
                        ? folia::CommandKind::Redo
                        : folia::CommandKind::Undo;
                    break;
                case EditorKey::Y:
                    command.kind = folia::CommandKind::Redo;
                    break;
                case EditorKey::A:
                    command.kind = folia::CommandKind::SelectAll;
                    break;
                case EditorKey::C:
                    return {.kind = EditorInputActionKind::Copy};
                case EditorKey::X:
                    return {.kind = EditorInputActionKind::Cut};
                case EditorKey::V:
                    return {.kind = EditorInputActionKind::Paste};
                default:
                    return {};
            }
            return execute(std::move(command));
        }

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
