#include "pch.h"
#include "editor/interaction/EditorSelectionDrag.h"

import folia.platform.editor_selection_drag_model;

namespace winrt::Folia
{
    void EditorSelectionDrag::Attach(
        EditorSession& valueSession,
        EditorSurfaceRenderer& valueRenderer,
        EditorScrollController& valueScroll,
        EditorTextInputController& valueTextInput,
        winrt::Microsoft::UI::Xaml::Controls::SwapChainPanel const& valueSurface)
    {
        Detach();
        session = &valueSession;
        renderer = &valueRenderer;
        scroll = &valueScroll;
        textInput = &valueTextInput;
        surface = valueSurface;
    }

    void EditorSelectionDrag::Detach()
    {
        Cancel();
        surface = nullptr;
        textInput = nullptr;
        scroll = nullptr;
        renderer = nullptr;
        session = nullptr;
    }

    bool EditorSelectionDrag::Active() const
    {
        return active;
    }

    void EditorSelectionDrag::Begin(folia::TextPosition valueAnchor)
    {
        if (!session) return;
        StopAutoScroll();
        active = true;
        anchor = valueAnchor;
        session->SetSelection(anchor, anchor);
        if (textInput) textInput->NotifySelectionChanged();
    }

    bool EditorSelectionDrag::Update(float x, float y)
    {
        pointerX = x;
        pointerY = y;
        return UpdateSelection(x, y, true);
    }

    bool EditorSelectionDrag::Finish(float x, float y)
    {
        if (!active) return false;
        pointerX = x;
        pointerY = y;
        auto changed = UpdateSelection(x, y, false);
        StopAutoScroll();
        active = false;
        return changed;
    }

    void EditorSelectionDrag::Cancel()
    {
        StopAutoScroll();
        active = false;
    }

    bool EditorSelectionDrag::UpdateSelection(float x, float y, bool updateAutoScroll)
    {
        if (!active || !session || !renderer) return false;
        auto viewportHeight = renderer->ViewportHeight();
        auto width = surface ? static_cast<float>(surface.ActualWidth()) : 0.0f;
        auto projection = folia::platform::editor::ProjectEditorSelectionPointer(
            x,
            y,
            width,
            viewportHeight);
        if (updateAutoScroll)
        {
            auto velocity = projection.autoScrollVelocity;
            if (velocity != 0.0f && scroll)
            {
                autoScrolling = true;
                scroll->BeginSelectionAutoScroll(velocity, [this]
                {
                    UpdateSelection(pointerX, pointerY, false);
                });
            }
            else
            {
                StopAutoScroll();
            }
        }

        auto hit = renderer->HitTest(projection.hitX, projection.hitY);
        if (!hit) return false;
        auto selection = session->Selection();
        if (selection.anchor == anchor && selection.active == *hit) return false;
        session->SetSelection(anchor, *hit);
        if (textInput) textInput->NotifySelectionChanged();
        return true;
    }

    void EditorSelectionDrag::StopAutoScroll()
    {
        if (!autoScrolling) return;
        autoScrolling = false;
        if (scroll) scroll->EndSelectionAutoScroll();
    }
}
