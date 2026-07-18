#include "pch.h"
#include "editor/interaction/EditorKeyboardController.h"

import folia.platform.editor_input_command;
import folia.core.snippet_template;
import folia.platform.editor_shortcuts;
import folia.platform.editor_snippet_session;

namespace winrt::Folia
{
    void EditorKeyboardController::Attach(
        EditorSession& session,
        EditorSurfaceRenderer& renderer,
        EditorTextInputController& textInput,
        ExecuteCommand executeCommand,
        Action copy,
        Action cut,
        Action paste,
        ApplicationAction applicationAction,
        std::vector<folia::platform::editor::EditorShortcutBinding> shortcuts,
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
        applicationAction_ = std::move(applicationAction);
        shortcuts_ = std::move(shortcuts);
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
        applicationAction_ = {};
        shortcuts_.clear();
        render_ = {};
        CancelSnippetSession();
        pendingHighSurrogate_.reset();
        ResetCaretGoal();
    }

    void EditorKeyboardController::SetShortcuts(
        std::vector<folia::platform::editor::EditorShortcutBinding> shortcuts)
    {
        shortcuts_ = std::move(shortcuts);
        CancelSnippetSession();
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
        if (!executeCommand_(folia::Command::InsertText(text))) return false;
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
        auto gesture = folia::platform::editor::EditorKeyGesture{
            .key = static_cast<folia::platform::editor::EditorKey>(
                static_cast<std::uint32_t>(key)),
            .control = ctrl,
            .shift = shift,
            .alt = alt,
        };
        if (auto shortcut = folia::platform::editor::resolve_editor_shortcut(
            shortcuts_, gesture, session_->ShortcutScope()))
            return DispatchShortcut(shortcuts_[*shortcut]);
        return DispatchInputAction(
            folia::platform::editor::TranslateEditorKeyGesture(gesture));
    }

    bool EditorKeyboardController::InsertNewline()
    {
        if (!executeCommand_) return false;
        auto shift = KeyDown(winrt::Windows::System::VirtualKey::Shift)
            || KeyDown(winrt::Windows::System::VirtualKey::LeftShift)
            || KeyDown(winrt::Windows::System::VirtualKey::RightShift);
        return DispatchInputAction(
            folia::platform::editor::TranslateEditorKeyGesture({
                .key = folia::platform::editor::EditorKey::Enter,
                .shift = shift,
            }));
    }

    bool EditorKeyboardController::DispatchInputAction(
        folia::platform::editor::EditorInputAction const& action)
    {
        using folia::platform::editor::EditorInputActionKind;
        if (!executeCommand_) return false;
        switch (action.kind)
        {
            case EditorInputActionKind::None:
                return false;
            case EditorInputActionKind::ExecuteCommand:
                CancelSnippetSession();
                if (action.command.kind == folia::CommandKind::MoveLeft
                    || action.command.kind == folia::CommandKind::MoveRight)
                    ResetCaretGoal();
                executeCommand_(action.command);
                return true;
            case EditorInputActionKind::ExecuteCommandIfApplied:
                CancelSnippetSession();
                return executeCommand_(action.command);
            case EditorInputActionKind::Copy:
                if (copy_) copy_();
                return true;
            case EditorInputActionKind::Cut:
                CancelSnippetSession();
                if (cut_) cut_();
                return true;
            case EditorInputActionKind::Paste:
                CancelSnippetSession();
                if (paste_) paste_();
                return true;
            case EditorInputActionKind::VisualLineUp:
                CancelSnippetSession();
                return MoveCaretVerticalStep(false, action.command.extend_selection);
            case EditorInputActionKind::VisualLineDown:
                CancelSnippetSession();
                return MoveCaretVerticalStep(true, action.command.extend_selection);
            case EditorInputActionKind::VisualLineStart:
            case EditorInputActionKind::VisualLineEnd:
            {
                CancelSnippetSession();
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
                    ? folia::TextAffinity::Downstream
                    : folia::TextAffinity::Upstream;
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
                if (MoveSnippetTabStop(true)) return true;
                auto command = folia::Command{.kind = folia::CommandKind::OutdentListItem};
                if (executeCommand_(command)) return true;
                command.kind = folia::CommandKind::MoveTableCellPrevious;
                executeCommand_(command);
                return true;
            }
            case EditorInputActionKind::TabForward:
            {
                if (MoveSnippetTabStop(false)) return true;
                auto command = folia::Command{.kind = folia::CommandKind::IndentListItem};
                if (executeCommand_(command)) return true;
                command.kind = folia::CommandKind::MoveTableCellNext;
                if (!executeCommand_(command))
                    executeCommand_(folia::Command::InsertText(U"    "));
                return true;
            }
        }
        return false;
    }

    bool EditorKeyboardController::DispatchShortcut(
        folia::platform::editor::EditorShortcutBinding const& shortcut)
    {
        using folia::platform::editor::EditorShortcutActionKind;
        if (shortcut.action_kind == EditorShortcutActionKind::InsertSnippet)
            return InsertSnippet(shortcut.snippet);
        auto action = folia::platform::editor::editor_shortcut_input_action(shortcut.action_id);
        if (action.Handled()) return DispatchInputAction(action);
        CancelSnippetSession();
        if (!applicationAction_) return false;
        applicationAction_(shortcut.action_id);
        return true;
    }

    bool EditorKeyboardController::InsertSnippet(std::u32string_view source)
    {
        if (!session_ || !executeCommand_) return false;
        auto parsed = folia::parse_snippet_template(source);
        auto before = session_->Selection();
        auto container = before.active.container_id;
        auto base = before.active.source_offset;
        if (before.anchor.container_id == container)
            base = (std::min)(base, before.anchor.source_offset);
        CancelSnippetSession();
        if (!executeCommand_(folia::Command::InsertText(parsed.text))) return false;
        if (parsed.tab_stops.empty()) return true;
        auto after = session_->Selection();
        if (after.active.container_id != container) return true;

        auto position = snippetSession_.Start(container, base, parsed.tab_stops);
        if (!position) return true;
        session_->SetSelection(*position, *position);
        if (textInput_) textInput_->NotifySelectionChanged();
        if (render_) render_();
        return true;
    }

    bool EditorKeyboardController::MoveSnippetTabStop(bool backward)
    {
        if (!session_) return false;
        auto result = snippetSession_.Navigate(session_->Selection(), backward);
        if (!result.Handled()) return false;
        if (!result.position) return true;
        session_->SetSelection(*result.position, *result.position);
        if (textInput_) textInput_->NotifySelectionChanged();
        if (render_) render_();
        return true;
    }

    void EditorKeyboardController::ResetCaretGoal()
    {
        caretGoalX_ = -1.0f;
    }

    void EditorKeyboardController::CancelSnippetSession()
    {
        snippetSession_.Cancel();
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
