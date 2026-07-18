#pragma once

#include "editor/session/EditorSession.h"
#include "editor/rendering/EditorSurfaceRenderer.h"
#include "editor/interaction/EditorScrollController.h"
#include "editor/interaction/EditorTextInputController.h"

namespace winrt::ElMd
{
    struct EditorPointerController
    {
        using ExecuteCommand = std::function<bool(elmd::Command const&)>;
        using Render = std::function<void()>;
        using OpenLink = std::function<void(std::string)>;
        using OpenFootnote = std::function<void(EditorSurfaceRenderer::FootnoteHit, winrt::Windows::Foundation::Point)>;
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
        bool SelectWordAt(elmd::TextPosition position);
        void UpdateDragSelection(float x, float y, bool updateAutoScroll);
        void StopSelectionAutoScroll();
        std::optional<std::string> LinkAtPosition(elmd::TextPosition position) const;
        std::optional<std::string> TooltipAtPosition(elmd::TextPosition position) const;

        EditorSession* session_ = nullptr;
        EditorSurfaceRenderer* renderer_ = nullptr;
        EditorScrollController* scroll_ = nullptr;
        EditorTextInputController* textInput_ = nullptr;
        winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel surface_{ nullptr };
        ExecuteCommand executeCommand_;
        Render render_;
        OpenLink openLink_;
        OpenFootnote openFootnote_;
        ResetCaretGoal resetCaretGoal_;
        bool selecting_ = false;
        bool selectionAutoScrolling_ = false;
        float pointerX_ = 0.0f;
        float pointerY_ = 0.0f;
        std::optional<std::string> hoverTooltip_;
        std::optional<elmd::TextPosition> hoverTaskCheckbox_;
        std::optional<EditorSurfaceRenderer::TableAction> hoverTableAction_;
        elmd::TextPosition anchor_;
        std::optional<EditorSurfaceRenderer::TableAction> tableDrag_;
        std::optional<std::size_t> tableDropIndex_;
    };
}
