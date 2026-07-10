#pragma once

#include <cstdint>
#include <memory>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace winrt::ElMd
{
    struct MathJaxSvgFragment
    {
        std::uint64_t renderId = 0;
        std::shared_ptr<std::string const> svg;
        float width = 0.0f;
        float height = 0.0f;
        float verticalAlign = 0.0f;
        float breakSpace = 0.0f;
        bool breakBefore = false;
    };

    struct MathJaxSvg
    {
        std::vector<MathJaxSvgFragment> fragments;
        std::string error;
        float width = 0.0f;
        float height = 0.0f;
        float verticalAlign = 0.0f;
        bool display = false;

        explicit operator bool() const { return error.empty() && !fragments.empty() && width > 0.0f && height > 0.0f; }
    };

    class MathJaxRenderer
    {
    public:
        MathJaxRenderer();
        ~MathJaxRenderer();
        MathJaxRenderer(MathJaxRenderer const&) = delete;
        MathJaxRenderer& operator=(MathJaxRenderer const&) = delete;

        std::optional<MathJaxSvg> GetOrQueue(std::string_view tex, bool display, float em, float containerWidth, bool allowQueue = true);
        void SetCompletionCallback(std::function<void()> callback);
        void Clear();

    private:
        struct State;

        std::unique_ptr<State> state;
    };
}
