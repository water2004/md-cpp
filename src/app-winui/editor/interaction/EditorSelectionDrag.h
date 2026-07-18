#pragma once

#include "editor/session/EditorSession.h"
#include "editor/rendering/EditorSurfaceRenderer.h"
#include "editor/interaction/EditorScrollController.h"
#include "editor/interaction/EditorTextInputController.h"

namespace winrt::Folia
{
    // Owns the complete lifetime of one pointer-driven text selection.
    struct EditorSelectionDrag
    {
        void Attach(
            EditorSession& session,
            EditorSurfaceRenderer& renderer,
            EditorScrollController& scroll,
            EditorTextInputController& textInput,
            winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& surface);
        void Detach();
        bool Active() const;
        void Begin(folia::TextPosition anchor);
        bool Update(float x, float y);
        bool Finish(float x, float y);
        void Cancel();

    private:
        bool UpdateSelection(float x, float y, bool updateAutoScroll);
        void StopAutoScroll();

        EditorSession* session = nullptr;
        EditorSurfaceRenderer* renderer = nullptr;
        EditorScrollController* scroll = nullptr;
        EditorTextInputController* textInput = nullptr;
        winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel surface{ nullptr };
        folia::TextPosition anchor{};
        float pointerX = 0.0f;
        float pointerY = 0.0f;
        bool active = false;
        bool autoScrolling = false;
    };
}
