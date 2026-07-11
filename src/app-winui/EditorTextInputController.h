#pragma once

#include "EditorSession.h"
#include "EditorSurfaceRenderer.h"

namespace winrt::ElMd
{
    struct EditorTextInputController
    {
        using ExecuteCommand = std::function<bool(elmd::Command const&)>;
        using Render = std::function<void()>;
        using WindowHandle = std::function<HWND()>;

        EditorTextInputController() = default;
        ~EditorTextInputController();
        EditorTextInputController(EditorTextInputController const&) = delete;
        EditorTextInputController& operator=(EditorTextInputController const&) = delete;

        void Attach(
            EditorSession& session,
            EditorSurfaceRenderer& renderer,
            winrt::Microsoft::UI::Xaml::FrameworkElement const& surface,
            ExecuteCommand executeCommand,
            Render render,
            WindowHandle windowHandle);
        void Detach();
        void FocusEnter();
        void FocusLeave();
        void NotifyTextChanged(std::size_t oldLength);
        void NotifySelectionChanged();
        void RecordCharacterTextUpdate(std::size_t start, std::u32string text);
        void ClearPendingCharacterTextUpdate();

    private:
        winrt::Windows::UI::Text::Core::CoreTextRange CurrentSelection() const;
        void RegisterHandlers();
        void RevokeHandlers();

        EditorSession* session_ = nullptr;
        EditorSurfaceRenderer* renderer_ = nullptr;
        winrt::Microsoft::UI::Xaml::FrameworkElement surface_{ nullptr };
        ExecuteCommand executeCommand_;
        Render render_;
        WindowHandle windowHandle_;
        winrt::Windows::UI::Text::Core::CoreTextEditContext context_{ nullptr };
        winrt::event_token textRequestedToken_{};
        winrt::event_token selectionRequestedToken_{};
        winrt::event_token layoutRequestedToken_{};
        winrt::event_token selectionUpdatingToken_{};
        winrt::event_token textUpdatingToken_{};
        std::size_t knownLength_ = 0;
        bool focused_ = false;
        bool updating_ = false;
        bool pendingCharacterUpdate_ = false;
        std::size_t pendingCharacterStart_ = 0;
        std::u32string pendingCharacterText_;
    };
}
