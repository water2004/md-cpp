#include "pch.h"
#include "EditorScrollController.h"

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
        std::function<void()> renderCallback)
    {
        Detach();
        renderer = &value;
        scrollBar = bar;
        column = valueColumn;
        render = std::move(renderCallback);
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
        lastFrame = std::chrono::steady_clock::now();
        if (rendering) return;
        renderingToken = winrt::Microsoft::UI::Xaml::Media::CompositionTarget::Rendering({ this, &EditorScrollController::OnFrame });
        rendering = true;
    }

    void EditorScrollController::Stop()
    {
        if (!rendering) return;
        winrt::Microsoft::UI::Xaml::Media::CompositionTarget::Rendering(renderingToken);
        rendering = false;
    }

    void EditorScrollController::OnFrame(winrt::Windows::Foundation::IInspectable const&, winrt::Windows::Foundation::IInspectable const&)
    {
        if (!renderer)
        {
            Stop();
            return;
        }
        auto now = std::chrono::steady_clock::now();
        auto elapsed = lastFrame.time_since_epoch().count() == 0
            ? 1.0f / 60.0f
            : std::chrono::duration<float>(now - lastFrame).count();
        lastFrame = now;
        auto active = renderer->AdvanceScrollAnimation((std::min)(elapsed, 0.05f));
        if (render) render();
        if (!active) Stop();
    }
}
