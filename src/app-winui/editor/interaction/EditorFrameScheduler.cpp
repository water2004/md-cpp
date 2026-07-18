#include "pch.h"
#include "editor/interaction/EditorFrameScheduler.h"

namespace winrt::Folia
{
    namespace
    {
        float DisplayRefreshInterval(HWND window)
        {
            if (!window) return 1.0f / 60.0f;
            auto monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (monitor && GetMonitorInfoW(monitor, &info))
            {
                auto queryFlags = static_cast<UINT32>(
                    QDC_ONLY_ACTIVE_PATHS
                    | QDC_VIRTUAL_MODE_AWARE
                    | QDC_VIRTUAL_REFRESH_RATE_AWARE);
                for (auto attempt = 0; attempt < 3; ++attempt)
                {
                    UINT32 pathCount = 0;
                    UINT32 modeCount = 0;
                    if (GetDisplayConfigBufferSizes(queryFlags, &pathCount, &modeCount) != ERROR_SUCCESS) break;
                    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
                    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
                    auto result = QueryDisplayConfig(
                        queryFlags,
                        &pathCount,
                        paths.data(),
                        &modeCount,
                        modes.data(),
                        nullptr);
                    if (result == ERROR_INSUFFICIENT_BUFFER) continue;
                    if (result != ERROR_SUCCESS) break;
                    paths.resize(pathCount);
                    modes.resize(modeCount);
                    for (auto const& path : paths)
                    {
                        DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName{};
                        sourceName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
                        sourceName.header.size = sizeof(sourceName);
                        sourceName.header.adapterId = path.sourceInfo.adapterId;
                        sourceName.header.id = path.sourceInfo.id;
                        if (DisplayConfigGetDeviceInfo(&sourceName.header) != ERROR_SUCCESS
                            || _wcsicmp(sourceName.viewGdiDeviceName, info.szDevice) != 0) continue;
                        for (auto const& modeInfo : modes)
                        {
                            if (modeInfo.infoType != DISPLAYCONFIG_MODE_INFO_TYPE_TARGET
                                || modeInfo.id != path.targetInfo.id
                                || modeInfo.adapterId.HighPart != path.targetInfo.adapterId.HighPart
                                || modeInfo.adapterId.LowPart != path.targetInfo.adapterId.LowPart) continue;
                            auto const& physicalRate = modeInfo.targetMode.targetVideoSignalInfo.vSyncFreq;
                            if (physicalRate.Denominator == 0) break;
                            auto physicalHertz = static_cast<float>(physicalRate.Numerator)
                                / static_cast<float>(physicalRate.Denominator);
                            if (physicalHertz >= 30.0f && physicalHertz <= 500.0f)
                                return 1.0f / physicalHertz;
                            break;
                        }
                        auto const& rate = path.targetInfo.refreshRate;
                        if (rate.Denominator == 0) break;
                        auto hertz = static_cast<float>(rate.Numerator)
                            / static_cast<float>(rate.Denominator);
                        if (hertz >= 30.0f && hertz <= 500.0f) return 1.0f / hertz;
                        break;
                    }
                    break;
                }
            }
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            if (!monitor
                || info.szDevice[0] == L'\0'
                || !EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode)
                || mode.dmDisplayFrequency < 30
                || mode.dmDisplayFrequency > 500) return 1.0f / 60.0f;
            return 1.0f / static_cast<float>(mode.dmDisplayFrequency);
        }
    }

    struct EditorFrameScheduler::FrameDispatchState
    {
        std::atomic_bool attached = false;
        std::atomic_uint64_t generation = 0;
        std::atomic_uint64_t requestedGeneration = 0;
        std::atomic_uint64_t requestedFrameId = 0;
        std::atomic_uint64_t frameSequence = 0;
        std::atomic_uint64_t outstandingFrameId = 0;
        std::atomic<float> targetFrameIntervalSeconds = 1.0f / 60.0f;
        EditorFrameScheduler* owner = nullptr;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher{ nullptr };
    };

    EditorFrameScheduler::~EditorFrameScheduler()
    {
        Detach();
    }

    void EditorFrameScheduler::Attach(
        winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher,
        HANDLE frameLatencyWaitableObject,
        HWND valueWindowHandle,
        FrameCallback callback)
    {
        Detach();
        winrt::check_bool(dispatcher != nullptr);
        winrt::check_bool(frameLatencyWaitableObject != nullptr);
        windowHandle = valueWindowHandle;
        frameCallback = std::move(callback);
        dispatchState = std::make_shared<FrameDispatchState>();
        dispatchState->owner = this;
        dispatchState->dispatcher = dispatcher;
        dispatchState->attached.store(true, std::memory_order_release);
        StartThread(frameLatencyWaitableObject);
    }

    void EditorFrameScheduler::Detach()
    {
        Stop();
        StopThread();
        frameCallback = {};
        windowHandle = nullptr;
    }

    void EditorFrameScheduler::Start()
    {
        if (!dispatchState) return;
        dispatchState->targetFrameIntervalSeconds.store(
            DisplayRefreshInterval(windowHandle),
            std::memory_order_release);
        if (running)
        {
            RequestFrame();
            return;
        }
        running = true;
        lastFrame = std::chrono::steady_clock::now();
        generation = dispatchState->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
        RequestFrame();
    }

    void EditorFrameScheduler::Stop()
    {
        if (!running) return;
        running = false;
        lastFrame = {};
        if (dispatchState)
        {
            dispatchState->outstandingFrameId.store(0, std::memory_order_release);
            dispatchState->generation.fetch_add(1, std::memory_order_acq_rel);
        }
    }

    void EditorFrameScheduler::StartThread(HANDLE frameLatencyWaitableObject)
    {
        frameRequestEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!frameRequestEvent) winrt::throw_hresult(HRESULT_FROM_WIN32(GetLastError()));
        schedulerStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!schedulerStopEvent)
        {
            auto error = HRESULT_FROM_WIN32(GetLastError());
            CloseHandle(frameRequestEvent);
            frameRequestEvent = nullptr;
            winrt::throw_hresult(error);
        }
        framePacingTimer = CreateWaitableTimerExW(
            nullptr,
            nullptr,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_ALL_ACCESS);
        if (!framePacingTimer) framePacingTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        if (!framePacingTimer)
        {
            auto error = HRESULT_FROM_WIN32(GetLastError());
            CloseHandle(frameRequestEvent);
            CloseHandle(schedulerStopEvent);
            frameRequestEvent = nullptr;
            schedulerStopEvent = nullptr;
            winrt::throw_hresult(error);
        }

        auto state = dispatchState;
        auto requestEvent = frameRequestEvent;
        auto stopEvent = schedulerStopEvent;
        auto pacingTimer = framePacingTimer;
        frameThread = std::jthread([state, requestEvent, stopEvent, pacingTimer, frameLatencyWaitableObject](std::stop_token stopToken)
        {
            std::stop_callback stopCallback(stopToken, [stopEvent] { SetEvent(stopEvent); });
            HANDLE requestHandles[] = { stopEvent, requestEvent };
            HANDLE frameHandles[] = { stopEvent, frameLatencyWaitableObject };
            HANDLE pacingHandles[] = { stopEvent, pacingTimer };
            auto lastRequestId = std::uint64_t{0};
            auto previousSubmission = std::chrono::steady_clock::time_point{};
            while (!stopToken.stop_requested())
            {
                if (WaitForMultipleObjects(2, requestHandles, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) break;
                auto frameId = state->requestedFrameId.load(std::memory_order_acquire);
                auto requestedGeneration = state->requestedGeneration.load(std::memory_order_acquire);
                if (frameId == 0 || frameId == lastRequestId) continue;
                lastRequestId = frameId;
                if (WaitForMultipleObjects(2, frameHandles, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) break;
                auto now = std::chrono::steady_clock::now();
                auto targetInterval = std::chrono::duration<float>(
                    state->targetFrameIntervalSeconds.load(std::memory_order_acquire));
                if (previousSubmission.time_since_epoch().count() != 0)
                {
                    auto deadline = previousSubmission + targetInterval;
                    if (now < deadline)
                    {
                        auto waitDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now).count();
                        LARGE_INTEGER dueTime{};
                        dueTime.QuadPart = -(std::max)(std::int64_t{1}, waitDuration / 100);
                        if (!SetWaitableTimer(pacingTimer, &dueTime, 0, nullptr, nullptr, FALSE)) break;
                        if (WaitForMultipleObjects(2, pacingHandles, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) break;
                        now = std::chrono::steady_clock::now();
                    }
                    else if (now - previousSubmission > targetInterval * 2.0f)
                    {
                        previousSubmission = {};
                    }
                }
                previousSubmission = now;
                if (!state->attached.load(std::memory_order_acquire)) break;
                auto queued = state->dispatcher.TryEnqueue(
                    winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::High,
                    [state, requestedGeneration, frameId]
                    {
                        auto expected = frameId;
                        state->outstandingFrameId.compare_exchange_strong(
                            expected,
                            0,
                            std::memory_order_acq_rel);
                        if (!state->attached.load(std::memory_order_acquire)
                            || state->generation.load(std::memory_order_acquire) != requestedGeneration
                            || !state->owner) return;
                        state->owner->OnFrame(requestedGeneration);
                    });
                if (!queued)
                {
                    auto expected = frameId;
                    state->outstandingFrameId.compare_exchange_strong(
                        expected,
                        0,
                        std::memory_order_acq_rel);
                }
            }
        });
    }

    void EditorFrameScheduler::StopThread()
    {
        if (dispatchState)
        {
            dispatchState->attached.store(false, std::memory_order_release);
            dispatchState->generation.fetch_add(1, std::memory_order_acq_rel);
            dispatchState->outstandingFrameId.store(0, std::memory_order_release);
            dispatchState->owner = nullptr;
        }
        if (frameThread.joinable())
        {
            frameThread.request_stop();
            if (schedulerStopEvent) SetEvent(schedulerStopEvent);
            frameThread.join();
        }
        if (frameRequestEvent) CloseHandle(frameRequestEvent);
        if (schedulerStopEvent) CloseHandle(schedulerStopEvent);
        if (framePacingTimer) CloseHandle(framePacingTimer);
        frameRequestEvent = nullptr;
        schedulerStopEvent = nullptr;
        framePacingTimer = nullptr;
        dispatchState.reset();
    }

    void EditorFrameScheduler::RequestFrame()
    {
        if (!running || !dispatchState || !frameRequestEvent) return;
        auto expected = std::uint64_t{0};
        auto frameId = dispatchState->frameSequence.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (!dispatchState->outstandingFrameId.compare_exchange_strong(
                expected,
                frameId,
                std::memory_order_acq_rel)) return;
        dispatchState->requestedGeneration.store(generation, std::memory_order_release);
        dispatchState->requestedFrameId.store(frameId, std::memory_order_release);
        if (!SetEvent(frameRequestEvent))
        {
            expected = frameId;
            dispatchState->outstandingFrameId.compare_exchange_strong(
                expected,
                0,
                std::memory_order_acq_rel);
        }
    }

    void EditorFrameScheduler::OnFrame(std::uint64_t requestedGeneration)
    {
        if (!running || requestedGeneration != generation) return;
        auto nominalFrameInterval = dispatchState
            ? dispatchState->targetFrameIntervalSeconds.load(std::memory_order_acquire)
            : 1.0f / 60.0f;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = lastFrame.time_since_epoch().count() == 0
            ? nominalFrameInterval
            : std::chrono::duration<float>(now - lastFrame).count();
        lastFrame = now;
        auto continueAnimation = frameCallback && frameCallback(elapsed);
        if (continueAnimation)
        {
            RequestFrame();
            return;
        }

        // The callback may complete the wheel animation and, while rendering
        // that final position, enqueue a new frame for an asynchronous math,
        // image, or layout completion. That request owns the next frame. Do
        // not cancel it merely because the motion itself has ended.
        if (dispatchState
            && dispatchState->outstandingFrameId.load(
                std::memory_order_acquire) != 0) return;
        Stop();
    }
}
