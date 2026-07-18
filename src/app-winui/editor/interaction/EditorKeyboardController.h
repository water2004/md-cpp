#pragma once

#include "editor/session/EditorSession.h"
#include "editor/rendering/EditorSurfaceRenderer.h"
#include "editor/interaction/EditorTextInputController.h"

import elmd.platform.editor_input_command;

namespace winrt::ElMd
{
    struct EditorKeyboardController
    {
        using ExecuteCommand = std::function<bool(elmd::Command const&)>;
        using Action = std::function<void()>;
        using Render = std::function<void()>;

        void Attach(
            EditorSession& session,
            EditorSurfaceRenderer& renderer,
            EditorTextInputController& textInput,
            ExecuteCommand executeCommand,
            Action copy,
            Action cut,
            Action paste,
            Render render);
        void Detach();
        bool Character(char16_t character);
        bool Key(winrt::Windows::System::VirtualKey key);
        bool InsertNewline();
        void ResetCaretGoal();

    private:
        bool DispatchInputAction(elmd::platform::editor::EditorInputAction const& action);
        bool MoveCaretVerticalStep(bool down, bool extend);
        bool KeyDown(winrt::Windows::System::VirtualKey key) const;

        EditorSession* session_ = nullptr;
        EditorSurfaceRenderer* renderer_ = nullptr;
        EditorTextInputController* textInput_ = nullptr;
        ExecuteCommand executeCommand_;
        Action copy_;
        Action cut_;
        Action paste_;
        Render render_;
        float caretGoalX_ = -1.0f;
        std::optional<char16_t> pendingHighSurrogate_;
    };
}
