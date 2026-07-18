#include "pch.h"
#include "editor/interaction/EditorKeyboardController.h"

import elmd.platform.editor_input_command;

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
        pendingHighSurrogate_.reset();
        ResetCaretGoal();
    }

    bool EditorKeyboardController::Character(char16_t character)
    {
        if (!session_ || !executeCommand_) return false;

        char32_t codepoint = character;
        if (character >= 0xd800 && character <= 0xdbff)
        {
            pendingHighSurrogate_ = character;
            return true;
        }
        if (character >= 0xdc00 && character <= 0xdfff)
        {
            if (pendingHighSurrogate_)
            {
                codepoint = 0x10000
                    + ((static_cast<char32_t>(*pendingHighSurrogate_) - 0xd800) << 10)
                    + (static_cast<char32_t>(character) - 0xdc00);
            }
            else
            {
                codepoint = 0xfffd;
            }
        }
        pendingHighSurrogate_.reset();

        if (codepoint == U'\r' || codepoint == U'\n') return false;
        if (codepoint < 0x20 || codepoint == 0x7f) return false;

        std::u32string text(1, codepoint);
        if (textInput_ && textInput_->ConsumeCommittedCoreTextCharacter(text)) return true;

        auto selection = session_->Selection();
        auto start = session_->TextInputAcpOffset(selection.active);
        if (selection.anchor.container_id == selection.active.container_id)
        {
            start = (std::min)(start, session_->TextInputAcpOffset(selection.anchor));
        }
        if (!executeCommand_(elmd::Command::InsertText(text))) return false;
        if (textInput_) textInput_->RecordCharacterTextUpdate(start, std::move(text));
        return true;
    }

    bool EditorKeyboardController::Key(winrt::Windows::System::VirtualKey key)
    {
        if (!session_ || !renderer_ || !executeCommand_) return false;
        if (textInput_) textInput_->BeginHardwareKey();
        pendingHighSurrogate_.reset();
        auto ctrl = KeyDown(winrt::Windows::System::VirtualKey::Control)
            || KeyDown(winrt::Windows::System::VirtualKey::LeftControl)
            || KeyDown(winrt::Windows::System::VirtualKey::RightControl);
        auto shift = KeyDown(winrt::Windows::System::VirtualKey::Shift)
            || KeyDown(winrt::Windows::System::VirtualKey::LeftShift)
            || KeyDown(winrt::Windows::System::VirtualKey::RightShift);
        auto alt = KeyDown(winrt::Windows::System::VirtualKey::Menu)
            || KeyDown(winrt::Windows::System::VirtualKey::LeftMenu)
            || KeyDown(winrt::Windows::System::VirtualKey::RightMenu);
        auto action = elmd::platform::editor::TranslateEditorKeyGesture({
            .key = static_cast<elmd::platform::editor::EditorKey>(
                static_cast<std::uint32_t>(key)),
            .control = ctrl,
            .shift = shift,
            .alt = alt,
        });
        return DispatchInputAction(action);
    }

    bool EditorKeyboardController::InsertNewline()
    {
        if (!executeCommand_) return false;
        auto shift = KeyDown(winrt::Windows::System::VirtualKey::Shift)
            || KeyDown(winrt::Windows::System::VirtualKey::LeftShift)
            || KeyDown(winrt::Windows::System::VirtualKey::RightShift);
        return DispatchInputAction(
            elmd::platform::editor::TranslateEditorKeyGesture({
                .key = elmd::platform::editor::EditorKey::Enter,
                .shift = shift,
            }));
    }

    bool EditorKeyboardController::DispatchInputAction(
        elmd::platform::editor::EditorInputAction const& action)
    {
        using elmd::platform::editor::EditorInputActionKind;
        if (!executeCommand_) return false;
        switch (action.kind)
        {
            case EditorInputActionKind::None:
                return false;
            case EditorInputActionKind::ExecuteCommand:
                if (action.command.kind == elmd::CommandKind::MoveLeft
                    || action.command.kind == elmd::CommandKind::MoveRight)
                    ResetCaretGoal();
                executeCommand_(action.command);
                return true;
            case EditorInputActionKind::ExecuteCommandIfApplied:
                return executeCommand_(action.command);
            case EditorInputActionKind::Copy:
                if (copy_) copy_();
                return true;
            case EditorInputActionKind::Cut:
                if (cut_) cut_();
                return true;
            case EditorInputActionKind::Paste:
                if (paste_) paste_();
                return true;
            case EditorInputActionKind::VisualLineUp:
                return MoveCaretVerticalStep(false, action.command.extend_selection);
            case EditorInputActionKind::VisualLineDown:
                return MoveCaretVerticalStep(true, action.command.extend_selection);
            case EditorInputActionKind::VisualLineStart:
            case EditorInputActionKind::VisualLineEnd:
            {
                ResetCaretGoal();
                auto selection = session_->Selection();
                auto position = action.kind == EditorInputActionKind::VisualLineStart
                    ? renderer_->VisualLineStart(selection.active)
                    : renderer_->VisualLineEnd(selection.active);
                if (!position)
                {
                    executeCommand_(action.command);
                    return true;
                }
                position->affinity = action.kind == EditorInputActionKind::VisualLineStart
                    ? elmd::TextAffinity::Downstream
                    : elmd::TextAffinity::Upstream;
                session_->SetSelection(
                    action.command.extend_selection ? selection.anchor : *position,
                    *position);
                if (textInput_) textInput_->NotifySelectionChanged();
                if (render_) render_();
                if (renderer_->ScrollToPosition(*position) && render_) render_();
                return true;
            }
            case EditorInputActionKind::TabBackward:
            {
                auto command = elmd::Command{.kind = elmd::CommandKind::OutdentListItem};
                if (executeCommand_(command)) return true;
                command.kind = elmd::CommandKind::MoveTableCellPrevious;
                executeCommand_(command);
                return true;
            }
            case EditorInputActionKind::TabForward:
            {
                auto command = elmd::Command{.kind = elmd::CommandKind::IndentListItem};
                if (executeCommand_(command)) return true;
                command.kind = elmd::CommandKind::MoveTableCellNext;
                if (!executeCommand_(command))
                    executeCommand_(elmd::Command::InsertText(U"    "));
                return true;
            }
        }
        return false;
    }

    void EditorKeyboardController::ResetCaretGoal()
    {
        caretGoalX_ = -1.0f;
    }

    bool EditorKeyboardController::MoveCaretVerticalStep(bool down, bool extend)
    {
        if (!session_ || !renderer_) return false;
        auto selection = session_->Selection();
        auto move = renderer_->MoveCaretVertically(selection.active, down, caretGoalX_);
        if (!move)
        {
            ResetCaretGoal();
            return false;
        }
        session_->SetSelection(extend ? selection.anchor : *move, *move);
        if (textInput_) textInput_->NotifySelectionChanged();
        if (render_) render_();
        if (renderer_->ScrollToPosition(*move) && render_) render_();
        return true;
    }

    bool EditorKeyboardController::KeyDown(winrt::Windows::System::VirtualKey key) const
    {
        auto state = winrt::Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(key);
        return (static_cast<std::uint32_t>(state) & 0x1u) != 0;
    }
}
