#include "pch.h"
#include "editor/interaction/EditorSelectionDrag.h"

namespace winrt::ElMd
{
    namespace
    {
        float AutoScrollVelocity(float y, float viewportHeight)
        {
            if (viewportHeight <= 0.0f || (y >= 0.0f && y <= viewportHeight)) return 0.0f;
            auto distance = y < 0.0f ? -y : y - viewportHeight;
            auto speed = (std::min)(2400.0f, 80.0f + 8.0f * distance + 0.02f * distance * distance);
            return y < 0.0f ? -speed : speed;
        }
    }

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

    void EditorSelectionDrag::Begin(elmd::TextPosition valueAnchor)
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
        if (updateAutoScroll)
        {
            auto velocity = AutoScrollVelocity(y, viewportHeight);
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

        auto width = surface ? static_cast<float>(surface.ActualWidth()) : 0.0f;
        auto hitX = width > 0.0f ? (std::clamp)(x, 0.0f, width) : x;
        auto hitY = viewportHeight > 0.0f
            ? (std::clamp)(y, 0.0f, (std::max)(0.0f, viewportHeight - 0.5f))
            : y;
        auto hit = renderer->HitTest(hitX, hitY);
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
