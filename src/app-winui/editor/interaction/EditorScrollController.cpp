#include "pch.h"
#include "editor/interaction/EditorScrollController.h"

namespace winrt::ElMd
{
    EditorScrollController::~EditorScrollController()
    {
        Detach();
    }

    void EditorScrollController::Attach(
        EditorSurfaceRenderer& value,
        winrt::Microsoft::UI::Xaml::Controls::Primitives::ScrollBar const& bar,
        winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition const& valueColumn,
        HWND windowHandle,
        std::function<void()> renderCallback)
    {
        Detach();
        renderer = &value;
        scrollBar = bar;
        column = valueColumn;
        render = std::move(renderCallback);
        frameScheduler.Attach(
            scrollBar.DispatcherQueue(),
            value.FrameLatencyWaitableObject(),
            windowHandle,
            [this](float elapsedSeconds) { return OnFrame(elapsedSeconds); });
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
        frameScheduler.Detach();
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
        frameScheduler.Start();
    }

    void EditorScrollController::Stop()
    {
        selectionAutoScrollVelocity = 0.0f;
        updateSelectionDuringAutoScroll = {};
        if (renderer) renderer->SetScrollOffset(renderer->ScrollOffset());
        frameScheduler.Stop();
    }

    bool EditorScrollController::OnFrame(float elapsedSeconds)
    {
        if (!renderer) return false;
        auto active = false;
        if (selectionAutoScrollVelocity != 0.0f)
        {
            auto before = renderer->ScrollOffset();
            // Do not turn a temporarily blocked UI thread into a large selection
            // jump when it resumes.
            auto stepSeconds = (std::clamp)(elapsedSeconds, 0.0f, 0.05f);
            renderer->ScrollBy(selectionAutoScrollVelocity * stepSeconds);
            active = std::fabs(renderer->ScrollOffset() - before) > 0.01f;
            if (active && updateSelectionDuringAutoScroll) updateSelectionDuringAutoScroll();
        }
        else
        {
            active = renderer->AdvanceScrollAnimation(
                (std::clamp)(elapsedSeconds, 0.0f, 0.5f));
        }
        if (render) render();
        if (!active)
        {
            selectionAutoScrollVelocity = 0.0f;
            updateSelectionDuringAutoScroll = {};
            renderer->SetScrollOffset(renderer->ScrollOffset());
        }
        return active;
    }
}
