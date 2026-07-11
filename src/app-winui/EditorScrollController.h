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
        void Start();
        void OnFrame(winrt::Windows::Foundation::IInspectable const&, winrt::Windows::Foundation::IInspectable const&);

        EditorSurfaceRenderer* renderer = nullptr;
        winrt::Microsoft::UI::Xaml::Controls::Primitives::ScrollBar scrollBar{ nullptr };
        winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition column{ nullptr };
        std::function<void()> render;
        winrt::event_token valueChangedToken{};
        winrt::event_token renderingToken{};
        std::chrono::steady_clock::time_point lastFrame{};
        bool attached = false;
        bool rendering = false;
        bool synchronizing = false;
    };
}
