// elmd.platform.editor_input_command — deterministic Windows key gesture mapping.
export module elmd.platform.editor_input_command;
import std;
export import elmd.core.command;

export namespace elmd::platform::editor
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
        elmd::Command command{};

        bool Handled() const noexcept { return kind != EditorInputActionKind::None; }
    };

    inline EditorInputAction TranslateEditorKeyGesture(EditorKeyGesture gesture)
    {
        auto execute = [](elmd::Command command)
        {
            return EditorInputAction{
                .kind = EditorInputActionKind::ExecuteCommand,
                .command = std::move(command),
            };
        };
        auto command = elmd::Command{};

        if (gesture.control)
        {
            switch (gesture.key)
            {
                case EditorKey::Up:
                    command.kind = gesture.alt
                        ? elmd::CommandKind::MoveTableRowUp
                        : elmd::CommandKind::InsertTableRowAbove;
                    break;
                case EditorKey::Down:
                    command.kind = gesture.alt
                        ? elmd::CommandKind::MoveTableRowDown
                        : elmd::CommandKind::InsertTableRowBelow;
                    break;
                case EditorKey::Left:
                    command.kind = gesture.alt
                        ? elmd::CommandKind::MoveTableColumnLeft
                        : elmd::CommandKind::InsertTableColumnLeft;
                    break;
                case EditorKey::Right:
                    command.kind = gesture.alt
                        ? elmd::CommandKind::MoveTableColumnRight
                        : elmd::CommandKind::InsertTableColumnRight;
                    break;
                case EditorKey::Back:
                    command.kind = elmd::CommandKind::DeleteTableRow;
                    break;
                case EditorKey::DeleteKey:
                    command.kind = elmd::CommandKind::DeleteTableColumn;
                    break;
                case EditorKey::Home:
                    command.kind = elmd::CommandKind::MoveDocumentStart;
                    command.extend_selection = gesture.shift;
                    break;
                case EditorKey::End:
                    command.kind = elmd::CommandKind::MoveDocumentEnd;
                    command.extend_selection = gesture.shift;
                    break;
                case EditorKey::Number1:
                    command.kind = elmd::CommandKind::SetHeading;
                    command.level = 1;
                    break;
                case EditorKey::Number2:
                    command.kind = elmd::CommandKind::SetHeading;
                    command.level = 2;
                    break;
                case EditorKey::Number7:
                    command.kind = elmd::CommandKind::ToggleOrderedList;
                    break;
                case EditorKey::Number8:
                    command.kind = elmd::CommandKind::ToggleUnorderedList;
                    break;
                case EditorKey::Number9:
                    command.kind = elmd::CommandKind::ToggleTaskList;
                    break;
                case EditorKey::B:
                    command.kind = elmd::CommandKind::ToggleStrong;
                    break;
                case EditorKey::I:
                    command.kind = elmd::CommandKind::ToggleEmphasis;
                    break;
                case EditorKey::Q:
                    command.kind = elmd::CommandKind::ToggleBlockQuote;
                    break;
                case EditorKey::T:
                    command.kind = elmd::CommandKind::InsertTable;
                    command.rows = 2;
                    command.cols = 3;
                    break;
                case EditorKey::Z:
                    command.kind = gesture.shift
                        ? elmd::CommandKind::Redo
                        : elmd::CommandKind::Undo;
                    break;
                case EditorKey::Y:
                    command.kind = elmd::CommandKind::Redo;
                    break;
                case EditorKey::A:
                    command.kind = elmd::CommandKind::SelectAll;
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
                command.kind = elmd::CommandKind::DeleteBackward;
                return execute(std::move(command));
            case EditorKey::DeleteKey:
                command.kind = elmd::CommandKind::DeleteForward;
                return execute(std::move(command));
            case EditorKey::Enter:
                command.kind = gesture.shift
                    ? elmd::CommandKind::InsertSoftBreak
                    : elmd::CommandKind::InsertNewline;
                return {
                    .kind = EditorInputActionKind::ExecuteCommandIfApplied,
                    .command = std::move(command),
                };
            case EditorKey::Left:
                command.kind = elmd::CommandKind::MoveLeft;
                command.extend_selection = gesture.shift;
                return execute(std::move(command));
            case EditorKey::Right:
                command.kind = elmd::CommandKind::MoveRight;
                command.extend_selection = gesture.shift;
                return execute(std::move(command));
            case EditorKey::Up:
                return {
                    .kind = EditorInputActionKind::VisualLineUp,
                    .command = elmd::Command::MoveUp(gesture.shift),
                };
            case EditorKey::Down:
                return {
                    .kind = EditorInputActionKind::VisualLineDown,
                    .command = elmd::Command::MoveDown(gesture.shift),
                };
            case EditorKey::Home:
                command.kind = elmd::CommandKind::MoveLineStart;
                command.extend_selection = gesture.shift;
                return {
                    .kind = EditorInputActionKind::VisualLineStart,
                    .command = std::move(command),
                };
            case EditorKey::End:
                command.kind = elmd::CommandKind::MoveLineEnd;
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
