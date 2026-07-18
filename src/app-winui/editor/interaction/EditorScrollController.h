#pragma once

#include "editor/rendering/EditorSurfaceRenderer.h"

namespace winrt::ElMd
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
        struct FrameDispatchState;

        void Start();
        void StartFrameScheduler(HANDLE frameLatencyWaitableObject);
        void StopFrameScheduler();
        void RequestFrame();
        void OnFrame(std::uint64_t generation);

        EditorSurfaceRenderer* renderer = nullptr;
        winrt::Microsoft::UI::Xaml::Controls::Primitives::ScrollBar scrollBar{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition column{ nullptr };
        std::function<void()> render;
        winrt::event_token valueChangedToken{};
        std::shared_ptr<FrameDispatchState> frameDispatch;
        std::jthread frameThread;
        HANDLE frameRequestEvent = nullptr;
        HANDLE schedulerStopEvent = nullptr;
        HANDLE framePacingTimer = nullptr;
        HWND windowHandle = nullptr;
        std::uint64_t animationGeneration = 0;
        std::chrono::steady_clock::time_point lastAnimationFrame{};
        bool attached = false;
        bool rendering = false;
        bool synchronizing = false;
        float width = 16.0f;
        float selectionAutoScrollVelocity = 0.0f;
        std::function<void()> updateSelectionDuringAutoScroll;
    };
}
