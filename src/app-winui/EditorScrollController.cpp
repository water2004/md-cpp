#include "pch.h"
#include "EditorScrollController.h"

namespace winrt::ElMd
{
    struct EditorScrollController::FrameDispatchState
    {
        std::atomic_bool attached = false;
        std::atomic_uint64_t generation = 0;
        std::atomic_uint64_t requestedGeneration = 0;
        std::atomic_uint64_t requestedFrameId = 0;
        std::atomic_uint64_t frameSequence = 0;
        std::atomic_uint64_t outstandingFrameId = 0;
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
        std::function<void()> renderCallback)
    {
        Detach();
        renderer = &value;
        scrollBar = bar;
        column = valueColumn;
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
            column.Width(winrt::Microsoft::UI::Xaml::GridLengthHelper::FromPixels(visible ? 16.0 : 0.0));
        }
    }

    void EditorScrollController::QueueScrollBy(float delta)
    {
        if (!renderer) return;
        renderer->QueueScrollBy(delta);
        Start();
    }

    void EditorScrollController::Start()
    {
        if (rendering)
        {
            RequestFrame();
            return;
        }
        // The latency handle may already be signalled when an animation starts.
        // Treat the first presentation as a real frame instead of advancing by an
        // almost-zero interval and visibly accelerating on the following frame.
        lastFrame = {};
        rendering = true;
        animationGeneration = frameDispatch
            ? frameDispatch->generation.fetch_add(1, std::memory_order_acq_rel) + 1
            : animationGeneration + 1;
        RequestFrame();
    }

    void EditorScrollController::Stop()
    {
        if (!rendering) return;
        rendering = false;
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
        frameDispatch = std::make_shared<FrameDispatchState>();
        frameDispatch->owner = this;
        frameDispatch->dispatcher = scrollBar.DispatcherQueue();
        frameDispatch->attached.store(true, std::memory_order_release);
        auto state = frameDispatch;
        auto requestEvent = frameRequestEvent;
        auto stopEvent = schedulerStopEvent;
        frameThread = std::jthread([state, requestEvent, stopEvent, frameLatencyWaitableObject](std::stop_token stopToken)
        {
            std::stop_callback stopCallback(stopToken, [stopEvent] { SetEvent(stopEvent); });
            HANDLE requestHandles[] = { stopEvent, requestEvent };
            HANDLE frameHandles[] = { stopEvent, frameLatencyWaitableObject };
            auto lastRequestId = std::uint64_t{0};
            while (!stopToken.stop_requested())
            {
                if (WaitForMultipleObjects(2, requestHandles, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) break;
                auto frameId = state->requestedFrameId.load(std::memory_order_acquire);
                auto generation = state->requestedGeneration.load(std::memory_order_acquire);
                if (frameId == 0 || frameId == lastRequestId) continue;
                lastRequestId = frameId;
                if (WaitForMultipleObjects(2, frameHandles, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) break;
                if (!state->attached.load(std::memory_order_acquire)) break;
                auto queued = state->dispatcher.TryEnqueue([state, generation, frameId]
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
        frameRequestEvent = nullptr;
        schedulerStopEvent = nullptr;
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
        auto now = std::chrono::steady_clock::now();
        auto elapsed = lastFrame.time_since_epoch().count() == 0
            ? frameIntervalEstimate
            : std::chrono::duration<float>(now - lastFrame).count();
        if (lastFrame.time_since_epoch().count() != 0 && elapsed >= 0.002f && elapsed <= 0.05f)
        {
            constexpr float estimateResponse = 0.125f;
            frameIntervalEstimate += (elapsed - frameIntervalEstimate) * estimateResponse;
        }
        lastFrame = now;
        auto active = renderer->AdvanceScrollAnimation((std::min)(elapsed, 0.05f));
        if (render) render();
        if (active) RequestFrame();
        else Stop();
    }
}
