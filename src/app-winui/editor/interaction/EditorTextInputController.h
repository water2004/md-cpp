#pragma once

#include "editor/session/EditorSession.h"
#include "editor/rendering/EditorSurfaceRenderer.h"

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
        void NotifyTextChanged();
        void NotifySelectionChanged();

    private:
        winrt::Windows::UI::Text::Core::CoreTextRange CurrentSelection() const;
        void QueueSynchronization();
        void SynchronizeTextStore();
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
        std::wstring knownText_;
        std::uint64_t knownRevision_ = 0;
        std::shared_ptr<int> lifetime_;
        bool focused_ = false;
        bool updating_ = false;
        bool notifying_ = false;
        bool synchronizationQueued_ = false;
    };
}
