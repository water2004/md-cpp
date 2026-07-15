#pragma once

#include "editor/session/EditorSession.h"
#include "editor/rendering/EditorSurfaceRenderer.h"
#include "editor/interaction/EditorTextInputController.h"

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
            Render render);
        void Detach();
        void Refresh();
        void SelectOutline(winrt::Windows::Foundation::IInspectable const& selectedItem);

    private:
        void RefreshOutline();

        EditorSession* session_ = nullptr;
        EditorSurfaceRenderer* renderer_ = nullptr;
        EditorTextInputController* textInput_ = nullptr;
        winrt::Microsoft::UI::Xaml::Controls::ListView outline_{ nullptr };
        Render render_;
        std::vector<elmd::TextPosition> outlinePositions_;
        std::vector<std::wstring> outlineLabels_;
    };
}
