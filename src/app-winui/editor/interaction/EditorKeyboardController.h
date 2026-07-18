#pragma once

#include "editor/session/EditorSession.h"
#include "editor/rendering/EditorSurfaceRenderer.h"
#include "editor/interaction/EditorTextInputController.h"

import folia.platform.editor_input_command;
import folia.platform.editor_shortcuts;

namespace winrt::Folia
{
    struct EditorKeyboardController
    {
        using ExecuteCommand = std::function<bool(folia::Command const&)>;
        using Action = std::function<void()>;
        using ApplicationAction = std::function<void(std::string_view)>;
        using Render = std::function<void()>;

        void Attach(
            EditorSession& session,
            EditorSurfaceRenderer& renderer,
            EditorTextInputController& textInput,
            ExecuteCommand executeCommand,
            Action copy,
            Action cut,
            Action paste,
            ApplicationAction applicationAction,
            std::vector<folia::platform::editor::EditorShortcutBinding> shortcuts,
            Render render);
        void Detach();
        void SetShortcuts(std::vector<folia::platform::editor::EditorShortcutBinding> shortcuts);
        bool Character(char16_t character);
        bool Key(winrt::Windows::System::VirtualKey key);
        bool InsertNewline();
        void ResetCaretGoal();
        void CancelSnippetSession();

    private:
        bool DispatchInputAction(folia::platform::editor::EditorInputAction const& action);
        bool DispatchShortcut(folia::platform::editor::EditorShortcutBinding const& shortcut);
        bool InsertSnippet(std::u32string_view source);
        bool MoveSnippetTabStop(bool backward);
        bool MoveCaretVerticalStep(bool down, bool extend);
        bool KeyDown(winrt::Windows::System::VirtualKey key) const;

        EditorSession* session_ = nullptr;
        EditorSurfaceRenderer* renderer_ = nullptr;
        EditorTextInputController* textInput_ = nullptr;
        ExecuteCommand executeCommand_;
        Action copy_;
        Action cut_;
        Action paste_;
        ApplicationAction applicationAction_;
        std::vector<folia::platform::editor::EditorShortcutBinding> shortcuts_;
        Render render_;
        struct SnippetSession
        {
            folia::NodeId container;
            std::vector<std::size_t> offsets;
            std::size_t current = 0;
        };
        std::optional<SnippetSession> snippetSession_;
        float caretGoalX_ = -1.0f;
        std::optional<char16_t> pendingHighSurrogate_;
    };
}
