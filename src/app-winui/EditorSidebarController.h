#pragma once

#include "EditorSession.h"
#include "EditorSurfaceRenderer.h"
#include "EditorTextInputController.h"

namespace winrt::ElMd
{
    struct EditorSidebarController
    {
        using Render = std::function<void()>;

        void Attach(
            EditorSession& session,
            EditorSurfaceRenderer& renderer,
            EditorTextInputController& textInput,
            winrt::Microsoft::UI::Xaml::Controls::ListView const& outline,
            winrt::Microsoft::UI::Xaml::Controls::ListView const& diagnostics,
            Render render);
        void Detach();
        void Refresh();
        void SelectOutline(winrt::Windows::Foundation::IInspectable const& selectedItem);
        void SelectDiagnostic(winrt::Windows::Foundation::IInspectable const& selectedItem);

    private:
        void RefreshOutline();
        void RefreshDiagnostics();

        EditorSession* session_ = nullptr;
        EditorSurfaceRenderer* renderer_ = nullptr;
        EditorTextInputController* textInput_ = nullptr;
        winrt::Microsoft::UI::Xaml::Controls::ListView outline_{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ListView diagnostics_{ nullptr };
        Render render_;
        std::vector<elmd::TextPosition> outlinePositions_;
        std::vector<std::optional<elmd::TextPosition>> diagnosticPositions_;
        std::vector<std::wstring> outlineLabels_;
        std::vector<std::wstring> diagnosticLabels_;
    };
}
