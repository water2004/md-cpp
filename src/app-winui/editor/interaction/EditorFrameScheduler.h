#pragma once

namespace winrt::ElMd
{
    // Presents UI-thread animation work at the active display cadence while
    // pacing submissions with the renderer's frame-latency waitable object.
    struct EditorFrameScheduler
    {
        using FrameCallback = std::function<bool(float elapsedSeconds)>;

        ~EditorFrameScheduler();
        void Attach(
            winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher,
            HANDLE frameLatencyWaitableObject,
            HWND windowHandle,
            FrameCallback frameCallback);
        void Detach();
        void Start();
        void Stop();

    private:
        struct FrameDispatchState;

        void StartThread(HANDLE frameLatencyWaitableObject);
        void StopThread();
        void RequestFrame();
        void OnFrame(std::uint64_t generation);

        std::shared_ptr<FrameDispatchState> dispatchState;
        std::jthread frameThread;
        HANDLE frameRequestEvent = nullptr;
        HANDLE schedulerStopEvent = nullptr;
        HANDLE framePacingTimer = nullptr;
        HWND windowHandle = nullptr;
        FrameCallback frameCallback;
        std::uint64_t generation = 0;
        std::chrono::steady_clock::time_point lastFrame{};
        bool running = false;
    };
}
