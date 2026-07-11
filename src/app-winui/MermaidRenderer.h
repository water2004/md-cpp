#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace winrt::ElMd
{
    struct MermaidSvg
    {
        std::uint64_t renderId = 0;
        std::string svg;
        std::string error;
        float width = 0.0f;
        float height = 0.0f;

        explicit operator bool() const { return error.empty() && !svg.empty() && width > 0.0f && height > 0.0f; }
    };

    class MermaidRenderer
    {
    public:
        MermaidRenderer();
        ~MermaidRenderer();
        MermaidRenderer(MermaidRenderer const&) = delete;
        MermaidRenderer& operator=(MermaidRenderer const&) = delete;

        void Initialize(winrt::Microsoft::UI::Xaml::Controls::WebView2 const& webView, std::function<void()> completion);
        void SetCompletionCallback(std::function<void()> completion);
        std::optional<MermaidSvg> GetOrQueue(std::string_view source, bool dark, bool allowQueue = true);
        void Clear();

    private:
        struct State;
        std::shared_ptr<State> state;
    };
}
