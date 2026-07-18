#include "pch.h"
#include "editor/interaction/EditorScrollController.h"

namespace winrt::ElMd
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

    struct EditorScrollController::FrameDispatchState
    {
        std::atomic_bool attached = false;
        std::atomic_uint64_t generation = 0;
        std::atomic_uint64_t requestedGeneration = 0;
        std::atomic_uint64_t requestedFrameId = 0;
        std::atomic_uint64_t frameSequence = 0;
        std::atomic_uint64_t outstandingFrameId = 0;
        std::atomic<float> targetFrameIntervalSeconds = 1.0f / 60.0f;
        EditorScrollController* owner = nullptr;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher{ nullptr };
    };

    EditorScrollController::~EditorScrollController()
    {
        Detach();
    }

    void EditorScrollController::Attach(
        EditorSurfaceRenderer& value,
        winrt::Microsoft::UI::Xaml::Controls::Primitives::ScrollBar const& bar,
        winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition const& valueColumn,
        HWND valueWindowHandle,
        std::function<void()> renderCallback)
    {
        Detach();
        renderer = &value;
        scrollBar = bar;
        column = valueColumn;
        windowHandle = valueWindowHandle;
        render = std::move(renderCallback);
        StartFrameScheduler(value.FrameLatencyWaitableObject());
        valueChangedToken = scrollBar.ValueChanged([this](auto const&, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args)
        {
            if (synchronizing || !renderer) return;
            renderer->SetScrollOffset(static_cast<float>(args.NewValue()));
            if (render) render();
        });
        attached = true;
        Sync();
    }

    void EditorScrollController::Detach()
    {
        Stop();
        StopFrameScheduler();
        if (attached && scrollBar) scrollBar.ValueChanged(valueChangedToken);
        attached = false;
        renderer = nullptr;
        scrollBar = nullptr;
        column = nullptr;
        windowHandle = nullptr;
        render = {};
    }

    void EditorScrollController::Sync()
    {
        if (!renderer || !scrollBar || !column) return;
        synchronizing = true;
        struct ResetFlag { bool& value; ~ResetFlag() { value = false; } } resetFlag{ synchronizing };
        auto maximum = static_cast<double>(renderer->MaximumScrollOffset());
        auto viewport = static_cast<double>(renderer->ViewportHeight());
        if (std::fabs(scrollBar.Maximum() - maximum) > 0.5) scrollBar.Maximum(maximum);
        if (std::fabs(scrollBar.ViewportSize() - viewport) > 0.5) scrollBar.ViewportSize(viewport);
        auto largeChange = (std::max)(48.0, viewport * 0.9);
        if (std::fabs(scrollBar.LargeChange() - largeChange) > 0.5) scrollBar.LargeChange(largeChange);
        auto value = static_cast<double>((std::min)(renderer->ScrollOffset(), static_cast<float>(maximum)));
        if (std::fabs(scrollBar.Value() - value) > 0.1) scrollBar.Value(value);
        auto visible = maximum > 0.5;
        auto visibility = visible ? winrt::Microsoft::UI::Xaml::Visibility::Visible : winrt::Microsoft::UI::Xaml::Visibility::Collapsed;
        if (scrollBar.Visibility() != visibility)
        {
            scrollBar.Visibility(visibility);
            column.Width(winrt::Microsoft::UI::Xaml::GridLengthHelper::FromPixels(visible ? width : 0.0f));
        }
    }

    void EditorScrollController::SetWidth(float value)
    {
        width = (std::max)(0.0f, value);
        if (scrollBar) scrollBar.Width(width);
        if (column)
        {
            const auto visible = scrollBar && scrollBar.Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible;
            column.Width(winrt::Microsoft::UI::Xaml::GridLengthHelper::FromPixels(visible ? width : 0.0f));
        }
    }

    void EditorScrollController::QueueScrollBy(float delta)
    {
        if (!renderer) return;
        selectionAutoScrollVelocity = 0.0f;
        updateSelectionDuringAutoScroll = {};
        renderer->QueueScrollBy(delta);
        Start();
    }

    void EditorScrollController::ScrollPreciselyBy(float delta)
    {
        if (!renderer || !std::isfinite(delta) || delta == 0.0f) return;
        selectionAutoScrollVelocity = 0.0f;
        updateSelectionDuringAutoScroll = {};
        // Precision touchpads already emit a continuous, inertial stream. Move
        // the model directly and use the frame scheduler only to coalesce paint.
        renderer->ScrollBy(delta);
        Start();
    }

    void EditorScrollController::BeginSelectionAutoScroll(
        float velocityPixelsPerSecond,
        std::function<void()> updateSelection)
    {
        if (!renderer || !std::isfinite(velocityPixelsPerSecond) || velocityPixelsPerSecond == 0.0f)
        {
            EndSelectionAutoScroll();
            return;
        }
        if (selectionAutoScrollVelocity == 0.0f)
        {
            // A drag selection owns scrolling while it is outside the viewport.
            // Discard a pending wheel target so two motion models cannot compete.
            renderer->SetScrollOffset(renderer->ScrollOffset());
        }
        selectionAutoScrollVelocity = velocityPixelsPerSecond;
        updateSelectionDuringAutoScroll = std::move(updateSelection);
        Start();
    }

    void EditorScrollController::EndSelectionAutoScroll()
    {
        if (selectionAutoScrollVelocity == 0.0f && !updateSelectionDuringAutoScroll) return;
        selectionAutoScrollVelocity = 0.0f;
        updateSelectionDuringAutoScroll = {};
        Stop();
    }

    void EditorScrollController::Start()
    {
        if (frameDispatch)
            frameDispatch->targetFrameIntervalSeconds.store(
                DisplayRefreshInterval(windowHandle),
                std::memory_order_release);
        if (rendering)
        {
            RequestFrame();
            return;
        }
        rendering = true;
        lastAnimationFrame = std::chrono::steady_clock::now();
        animationGeneration = frameDispatch
            ? frameDispatch->generation.fetch_add(1, std::memory_order_acq_rel) + 1
            : animationGeneration + 1;
        RequestFrame();
    }

    void EditorScrollController::Stop()
    {
        selectionAutoScrollVelocity = 0.0f;
        updateSelectionDuringAutoScroll = {};
        if (renderer) renderer->SetScrollOffset(renderer->ScrollOffset());
        if (!rendering) return;
        rendering = false;
        lastAnimationFrame = {};
        if (frameDispatch)
        {
            frameDispatch->outstandingFrameId.store(0, std::memory_order_release);
            frameDispatch->generation.fetch_add(1, std::memory_order_acq_rel);
        }
    }

    void EditorScrollController::StartFrameScheduler(HANDLE frameLatencyWaitableObject)
    {
        winrt::check_bool(frameLatencyWaitableObject != nullptr);
        frameRequestEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!frameRequestEvent)
        {
            auto error = HRESULT_FROM_WIN32(GetLastError());
            winrt::throw_hresult(error);
        }
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
        frameDispatch = std::make_shared<FrameDispatchState>();
        frameDispatch->owner = this;
        frameDispatch->dispatcher = scrollBar.DispatcherQueue();
        frameDispatch->attached.store(true, std::memory_order_release);
        auto state = frameDispatch;
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
                auto generation = state->requestedGeneration.load(std::memory_order_acquire);
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
                        previousSubmission = {};
                }
                previousSubmission = now;
                if (!state->attached.load(std::memory_order_acquire)) break;
                auto queued = state->dispatcher.TryEnqueue(
                    winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::High,
                    [state, generation, frameId]
                    {
                        auto expected = frameId;
                        state->outstandingFrameId.compare_exchange_strong(
                            expected,
                            0,
                            std::memory_order_acq_rel);
                        if (!state->attached.load(std::memory_order_acquire)
                            || state->generation.load(std::memory_order_acquire) != generation
                            || !state->owner) return;
                        state->owner->OnFrame(generation);
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

    void EditorScrollController::StopFrameScheduler()
    {
        if (frameDispatch)
        {
            frameDispatch->attached.store(false, std::memory_order_release);
            frameDispatch->generation.fetch_add(1, std::memory_order_acq_rel);
            frameDispatch->outstandingFrameId.store(0, std::memory_order_release);
            frameDispatch->owner = nullptr;
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
        frameDispatch.reset();
    }

    void EditorScrollController::RequestFrame()
    {
        if (!rendering || !frameDispatch || !frameRequestEvent) return;
        auto expected = std::uint64_t{0};
        auto frameId = frameDispatch->frameSequence.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (!frameDispatch->outstandingFrameId.compare_exchange_strong(
                expected,
                frameId,
                std::memory_order_acq_rel)) return;
        frameDispatch->requestedGeneration.store(animationGeneration, std::memory_order_release);
        frameDispatch->requestedFrameId.store(frameId, std::memory_order_release);
        if (!SetEvent(frameRequestEvent))
        {
            expected = frameId;
            frameDispatch->outstandingFrameId.compare_exchange_strong(
                expected,
                0,
                std::memory_order_acq_rel);
        }
    }

    void EditorScrollController::OnFrame(std::uint64_t generation)
    {
        if (!renderer || !rendering || generation != animationGeneration)
        {
            return;
        }
        auto nominalFrameInterval = frameDispatch
            ? frameDispatch->targetFrameIntervalSeconds.load(std::memory_order_acquire)
            : 1.0f / 60.0f;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = lastAnimationFrame.time_since_epoch().count() == 0
            ? nominalFrameInterval
            : std::chrono::duration<float>(now - lastAnimationFrame).count();
        lastAnimationFrame = now;
        auto active = false;
        if (selectionAutoScrollVelocity != 0.0f)
        {
            auto before = renderer->ScrollOffset();
            // Do not turn a temporarily blocked UI thread into a large selection
            // jump when it resumes.
            auto stepSeconds = (std::clamp)(elapsed, 0.0f, 0.05f);
            renderer->ScrollBy(selectionAutoScrollVelocity * stepSeconds);
            active = std::fabs(renderer->ScrollOffset() - before) > 0.01f;
            if (active && updateSelectionDuringAutoScroll) updateSelectionDuringAutoScroll();
        }
        else
        {
            active = renderer->AdvanceScrollAnimation(
                (std::clamp)(elapsed, 0.0f, 0.5f));
        }
        if (render) render();
        if (active) RequestFrame();
        else Stop();
    }
}
