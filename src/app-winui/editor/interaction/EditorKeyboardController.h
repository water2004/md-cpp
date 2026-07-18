#pragma once

#include "editor/session/EditorSession.h"
#include "editor/rendering/EditorSurfaceRenderer.h"
#include "editor/interaction/EditorTextInputController.h"

import folia.platform.editor_input_command;
import folia.platform.editor_shortcuts;
import folia.platform.editor_snippet_session;

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
        bool InsertSnippetReplacing(
            folia::NodeId container,
            folia::SourceRange replacement,
            std::u32string_view source);
        void ResetCaretGoal();
        void CancelSnippetSession();
        std::vector<folia::platform::editor::EditorSnippetPlaceholder> SnippetPlaceholders();

    private:
        bool DispatchInputAction(folia::platform::editor::EditorInputAction const& action);
        bool DispatchShortcut(folia::platform::editor::EditorShortcutBinding const& shortcut);
        bool InsertSnippet(
            std::u32string_view source,
            std::optional<std::u32string> selectedText = std::nullopt);
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
        folia::platform::editor::EditorSnippetSession snippetSession_;
        float caretGoalX_ = -1.0f;
        std::optional<char16_t> pendingHighSurrogate_;
    };
}
