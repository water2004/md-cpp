#pragma once

#include "EditorSurfaceRenderer.h"

namespace winrt::ElMd
{
    struct EditorScrollController
    {
        ~EditorScrollController();
        void Attach(
            EditorSurfaceRenderer& renderer,
            winrt::Microsoft::UI::Xaml::Controls::Primitives::ScrollBar const& scrollBar,
            winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition const& column,
            std::function<void()> render);
        void Detach();
        void Sync();
        void QueueScrollBy(float delta);
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
        std::chrono::steady_clock::time_point lastFrame{};
        std::shared_ptr<FrameDispatchState> frameDispatch;
        std::jthread frameThread;
        HANDLE frameRequestEvent = nullptr;
        HANDLE schedulerStopEvent = nullptr;
        std::uint64_t animationGeneration = 0;
        float frameIntervalEstimate = 1.0f / 120.0f;
        bool attached = false;
        bool rendering = false;
        bool synchronizing = false;
    };
}
