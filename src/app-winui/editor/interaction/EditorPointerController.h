#pragma once

#include "editor/session/EditorSession.h"
#include "editor/rendering/EditorSurfaceRenderer.h"
#include "editor/interaction/EditorScrollController.h"
#include "editor/interaction/EditorSelectionDrag.h"
#include "editor/interaction/EditorTextInputController.h"

namespace winrt::Folia
{
    using folia::platform::editor::EditorTableAction;
    using folia::platform::editor::EditorVisualFootnoteHit;

    struct EditorPointerController
    {
        using ExecuteCommand = std::function<bool(folia::Command const&)>;
        using Render = std::function<void()>;
        using OpenLink = std::function<void(std::string)>;
        using OpenFootnote = std::function<void(EditorVisualFootnoteHit, winrt::Windows::Foundation::Point)>;
        using ResetCaretGoal = std::function<void()>;

        void Attach(
            EditorSession& session,
            EditorSurfaceRenderer& renderer,
            EditorScrollController& scroll,
            EditorTextInputController& textInput,
            winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& surface,
            ExecuteCommand executeCommand,
            Render render,
            OpenLink openLink,
            OpenFootnote openFootnote,
            ResetCaretGoal resetCaretGoal);
        void Detach();
        void PointerPressed(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void PointerMoved(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void PointerReleased(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void PointerExited();
        void CancelPointerInteraction();
        void DoubleTapped(winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args);

    private:
        bool SelectWordAt(folia::TextPosition position);
        void SetLinkCursor(bool link);
        std::optional<std::string> LinkAtPosition(folia::TextPosition position) const;

        EditorSession* session_ = nullptr;
        EditorSurfaceRenderer* renderer_ = nullptr;
        EditorScrollController* scroll_ = nullptr;
        EditorTextInputController* textInput_ = nullptr;
        EditorSelectionDrag selectionDrag_;
        winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel surface_{ nullptr };
        winrt::Microsoft::UI::Input::InputCursor textCursor_{ nullptr };
        winrt::Microsoft::UI::Input::InputCursor linkCursor_{ nullptr };
        ExecuteCommand executeCommand_;
        Render render_;
        OpenLink openLink_;
        OpenFootnote openFootnote_;
        ResetCaretGoal resetCaretGoal_;
        bool linkCursorActive_ = false;
        std::optional<std::string> hoverTooltip_;
        std::optional<folia::TextPosition> hoverTaskCheckbox_;
        std::optional<EditorTableAction> hoverTableAction_;
        std::optional<EditorTableAction> tableDrag_;
        std::optional<std::size_t> tableDropIndex_;
    };
}
