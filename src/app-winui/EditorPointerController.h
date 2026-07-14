#pragma once

#include "EditorSession.h"
#include "EditorSurfaceRenderer.h"
#include "EditorTextInputController.h"

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
        void DoubleTapped(winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const& args);

    private:
        bool SelectWordAt(elmd::TextPosition position);
        std::optional<std::string> LinkAtPosition(elmd::TextPosition position) const;
        std::optional<std::string> TooltipAtPosition(elmd::TextPosition position) const;

        EditorSession* session_ = nullptr;
        EditorSurfaceRenderer* renderer_ = nullptr;
        EditorTextInputController* textInput_ = nullptr;
        winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel surface_{ nullptr };
        ExecuteCommand executeCommand_;
        Render render_;
        OpenLink openLink_;
        OpenFootnote openFootnote_;
        ResetCaretGoal resetCaretGoal_;
        bool selecting_ = false;
        std::optional<std::string> hoverTooltip_;
        elmd::TextPosition anchor_;
        std::optional<EditorSurfaceRenderer::TableAction> tableDrag_;
        std::optional<std::size_t> tableDropIndex_;
    };
}
