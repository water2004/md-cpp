#include "pch.h"
#include "EditorKeyboardController.h"

import elmd.core.command;

namespace winrt::ElMd
{
    void EditorKeyboardController::Attach(
        EditorSession& session,
        EditorSurfaceRenderer& renderer,
        EditorTextInputController& textInput,
        ExecuteCommand executeCommand,
        Action copy,
        Action cut,
        Action paste,
        Render render)
    {
        Detach();
        session_ = &session;
        renderer_ = &renderer;
        textInput_ = &textInput;
        executeCommand_ = std::move(executeCommand);
        copy_ = std::move(copy);
        cut_ = std::move(cut);
        paste_ = std::move(paste);
        render_ = std::move(render);
    }

    void EditorKeyboardController::Detach()
    {
        session_ = nullptr;
        renderer_ = nullptr;
        textInput_ = nullptr;
        executeCommand_ = {};
        copy_ = {};
        cut_ = {};
        paste_ = {};
        render_ = {};
        ResetCaretGoal();
    }

    bool EditorKeyboardController::Character(char32_t character)
    {
        if (!session_ || !executeCommand_) return false;
        if (character == U'\r' || character == U'\n') return InsertNewline();
        if (character < 0x20 || character == 0x7f) return false;
        auto selection = session_->Selection();
        auto start = (std::min)(session_->AcpOffset(selection.anchor), session_->AcpOffset(selection.active));
        std::u32string text(1, character);
        if (!executeCommand_(elmd::Command::InsertText(text))) return false;
        if (textInput_) textInput_->RecordCharacterTextUpdate(start, std::move(text));
        return true;
    }

    bool EditorKeyboardController::Key(winrt::Windows::System::VirtualKey key)
    {
        if (!session_ || !renderer_ || !executeCommand_) return false;
        elmd::Command command;
        if (textInput_) textInput_->ClearPendingCharacterTextUpdate();
        auto ctrl = KeyDown(winrt::Windows::System::VirtualKey::Control)
            || KeyDown(winrt::Windows::System::VirtualKey::LeftControl)
            || KeyDown(winrt::Windows::System::VirtualKey::RightControl);
        auto shift = KeyDown(winrt::Windows::System::VirtualKey::Shift)
            || KeyDown(winrt::Windows::System::VirtualKey::LeftShift)
            || KeyDown(winrt::Windows::System::VirtualKey::RightShift);
        auto alt = KeyDown(winrt::Windows::System::VirtualKey::Menu)
            || KeyDown(winrt::Windows::System::VirtualKey::LeftMenu)
            || KeyDown(winrt::Windows::System::VirtualKey::RightMenu);

        if (ctrl)
        {
            switch (key)
            {
                case winrt::Windows::System::VirtualKey::Up:
                    command.kind = alt ? elmd::CommandKind::MoveTableRowUp : elmd::CommandKind::InsertTableRowAbove;
                    break;
                case winrt::Windows::System::VirtualKey::Down:
                    command.kind = alt ? elmd::CommandKind::MoveTableRowDown : elmd::CommandKind::InsertTableRowBelow;
                    break;
                case winrt::Windows::System::VirtualKey::Left:
                    command.kind = alt ? elmd::CommandKind::MoveTableColumnLeft : elmd::CommandKind::InsertTableColumnLeft;
                    break;
                case winrt::Windows::System::VirtualKey::Right:
                    command.kind = alt ? elmd::CommandKind::MoveTableColumnRight : elmd::CommandKind::InsertTableColumnRight;
                    break;
                case winrt::Windows::System::VirtualKey::Back:
                    command.kind = elmd::CommandKind::DeleteTableRow;
                    break;
                case winrt::Windows::System::VirtualKey::Delete:
                    command.kind = elmd::CommandKind::DeleteTableColumn;
                    break;
                case winrt::Windows::System::VirtualKey::Home:
                    command.kind = elmd::CommandKind::MoveDocumentStart;
                    command.extend_selection = shift;
                    break;
                case winrt::Windows::System::VirtualKey::End:
                    command.kind = elmd::CommandKind::MoveDocumentEnd;
                    command.extend_selection = shift;
                    break;
                case winrt::Windows::System::VirtualKey::Number1:
                    command.kind = elmd::CommandKind::SetHeading;
                    command.level = 1;
                    break;
                case winrt::Windows::System::VirtualKey::Number2:
                    command.kind = elmd::CommandKind::SetHeading;
                    command.level = 2;
                    break;
                case winrt::Windows::System::VirtualKey::Number7:
                    command.kind = elmd::CommandKind::ToggleOrderedList;
                    break;
                case winrt::Windows::System::VirtualKey::Number8:
                    command.kind = elmd::CommandKind::ToggleUnorderedList;
                    break;
                case winrt::Windows::System::VirtualKey::Number9:
                    command.kind = elmd::CommandKind::ToggleTaskList;
                    break;
                case winrt::Windows::System::VirtualKey::B:
                    command.kind = elmd::CommandKind::ToggleStrong;
                    break;
                case winrt::Windows::System::VirtualKey::I:
                    command.kind = elmd::CommandKind::ToggleEmphasis;
                    break;
                case winrt::Windows::System::VirtualKey::Q:
                    command.kind = elmd::CommandKind::ToggleBlockQuote;
                    break;
                case winrt::Windows::System::VirtualKey::T:
                    command.kind = elmd::CommandKind::InsertTable;
                    command.rows = 2;
                    command.cols = 3;
                    break;
                case winrt::Windows::System::VirtualKey::Z:
                    command.kind = shift ? elmd::CommandKind::Redo : elmd::CommandKind::Undo;
                    break;
                case winrt::Windows::System::VirtualKey::Y:
                    command.kind = elmd::CommandKind::Redo;
                    break;
                case winrt::Windows::System::VirtualKey::A:
                    command.kind = elmd::CommandKind::SelectAll;
                    break;
                case winrt::Windows::System::VirtualKey::C:
                    if (copy_) copy_();
                    return true;
                case winrt::Windows::System::VirtualKey::X:
                    if (cut_) cut_();
                    return true;
                case winrt::Windows::System::VirtualKey::V:
                    if (paste_) paste_();
                    return true;
                default:
                    return false;
            }
            executeCommand_(command);
            return true;
        }

        switch (key)
        {
            case winrt::Windows::System::VirtualKey::Back:
                command.kind = elmd::CommandKind::DeleteBackward;
                break;
            case winrt::Windows::System::VirtualKey::Delete:
                command.kind = elmd::CommandKind::DeleteForward;
                break;
            case winrt::Windows::System::VirtualKey::Enter:
                return InsertNewline();
            case winrt::Windows::System::VirtualKey::Left:
                command.kind = elmd::CommandKind::MoveLeft;
                command.extend_selection = shift;
                ResetCaretGoal();
                break;
            case winrt::Windows::System::VirtualKey::Right:
                command.kind = elmd::CommandKind::MoveRight;
                command.extend_selection = shift;
                ResetCaretGoal();
                break;
            case winrt::Windows::System::VirtualKey::Up:
                return MoveCaretVerticalStep(false, shift);
            case winrt::Windows::System::VirtualKey::Down:
                return MoveCaretVerticalStep(true, shift);
            case winrt::Windows::System::VirtualKey::Home:
            {
                ResetCaretGoal();
                auto selection = session_->Selection();
                auto upstream = selection.active.affinity == elmd::TextAffinity::Upstream;
                if (auto position = renderer_->VisualLineStart(selection.active, upstream))
                {
                    position->affinity = elmd::TextAffinity::Downstream;
                    session_->SetSelection(shift ? selection.anchor : *position, *position);
                    if (textInput_) textInput_->NotifySelectionChanged();
                    if (render_) render_();
                    if (renderer_->ScrollToPosition(*position) && render_) render_();
                    return true;
                }
                command.kind = elmd::CommandKind::MoveLineStart;
                command.extend_selection = shift;
                break;
            }
            case winrt::Windows::System::VirtualKey::End:
            {
                ResetCaretGoal();
                auto selection = session_->Selection();
                auto upstream = selection.active.affinity == elmd::TextAffinity::Upstream;
                if (auto position = renderer_->VisualLineEnd(selection.active, upstream))
                {
                    position->affinity = elmd::TextAffinity::Upstream;
                    session_->SetSelection(shift ? selection.anchor : *position, *position);
                    if (textInput_) textInput_->NotifySelectionChanged();
                    if (render_) render_();
                    if (renderer_->ScrollToPosition(*position) && render_) render_();
                    return true;
                }
                command.kind = elmd::CommandKind::MoveLineEnd;
                command.extend_selection = shift;
                break;
            }
            case winrt::Windows::System::VirtualKey::Tab:
                if (shift)
                {
                    command.kind = elmd::CommandKind::OutdentListItem;
                    if (executeCommand_(command)) return true;
                    command.kind = elmd::CommandKind::MoveTableCellPrevious;
                    executeCommand_(command);
                    return true;
                }
                command.kind = elmd::CommandKind::IndentListItem;
                if (executeCommand_(command)) return true;
                command.kind = elmd::CommandKind::MoveTableCellNext;
                if (!executeCommand_(command)) executeCommand_(elmd::Command::InsertText(U"    "));
                return true;
            default:
                return false;
        }
        executeCommand_(command);
        return true;
    }

    bool EditorKeyboardController::InsertNewline()
    {
        if (!executeCommand_) return false;
        if (textInput_) textInput_->ClearPendingCharacterTextUpdate();
        auto shift = KeyDown(winrt::Windows::System::VirtualKey::Shift)
            || KeyDown(winrt::Windows::System::VirtualKey::LeftShift)
            || KeyDown(winrt::Windows::System::VirtualKey::RightShift);
        elmd::Command command;
        command.kind = shift ? elmd::CommandKind::InsertSoftBreak : elmd::CommandKind::InsertNewline;
        return executeCommand_(command);
    }

    void EditorKeyboardController::ResetCaretGoal()
    {
        caretGoalX_ = -1.0f;
    }

    bool EditorKeyboardController::MoveCaretVerticalStep(bool down, bool extend)
    {
        if (!session_ || !renderer_) return false;
        auto selection = session_->Selection();
        auto upstream = selection.active.affinity == elmd::TextAffinity::Upstream;
        auto move = renderer_->MoveCaretVertically(selection.active, upstream, down, caretGoalX_);
        if (!move)
        {
            ResetCaretGoal();
            return false;
        }
        auto affinity = move->upstream ? elmd::TextAffinity::Upstream : elmd::TextAffinity::Downstream;
        move->position.affinity = affinity;
        session_->SetSelection(extend ? selection.anchor : move->position, move->position);
        if (textInput_) textInput_->NotifySelectionChanged();
        if (render_) render_();
        if (renderer_->ScrollToPosition(move->position) && render_) render_();
        return true;
    }

    bool EditorKeyboardController::KeyDown(winrt::Windows::System::VirtualKey key) const
    {
        auto state = winrt::Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(key);
        return (static_cast<std::uint32_t>(state) & 0x1u) != 0;
    }
}
