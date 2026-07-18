#pragma once

#include "editor/rendering/EditorSurfaceRenderer.h"
#include "editor/interaction/EditorFrameScheduler.h"

namespace winrt::Folia
{
    struct EditorScrollController
    {
        ~EditorScrollController();
        void Attach(
            EditorSurfaceRenderer& renderer,
            winrt::Microsoft::UI::Xaml::Controls::Primitives::ScrollBar const& scrollBar,
            winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition const& column,
            HWND windowHandle,
            std::function<void()> render);
        void Detach();
        void Sync();
        void SetWidth(float value);
        void QueueScrollBy(float delta);
        void ScrollPreciselyBy(float delta);
        void BeginSelectionAutoScroll(float velocityPixelsPerSecond, std::function<void()> updateSelection);
        void EndSelectionAutoScroll();
        void Stop();

    private:
        void Start();
        bool OnFrame(float elapsedSeconds);

        EditorSurfaceRenderer* renderer = nullptr;
        winrt::Microsoft::UI::Xaml::Controls::Primitives::ScrollBar scrollBar{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition column{ nullptr };
        std::function<void()> render;
        winrt::event_token valueChangedToken{};
        EditorFrameScheduler frameScheduler;
        bool attached = false;
        bool synchronizing = false;
        float width = 16.0f;
        float selectionAutoScrollVelocity = 0.0f;
        std::function<void()> updateSelectionDuringAutoScroll;
    };
}
