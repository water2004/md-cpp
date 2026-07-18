#pragma once

#include <cstdint>
#include <memory>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace winrt::Folia
{
    enum class MathJaxErrorKind
    {
        None,
        Formula,
        Infrastructure,
    };

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
        MathJaxErrorKind errorKind = MathJaxErrorKind::None;
        float width = 0.0f;
        float height = 0.0f;
        float verticalAlign = 0.0f;
        bool display = false;

        explicit operator bool() const
        {
            return errorKind != MathJaxErrorKind::Infrastructure
                && !fragments.empty() && width > 0.0f && height > 0.0f;
        }
    };

    class MathJaxRenderer
    {
    public:
        MathJaxRenderer();
        ~MathJaxRenderer();
        MathJaxRenderer(MathJaxRenderer const&) = delete;
        MathJaxRenderer& operator=(MathJaxRenderer const&) = delete;

        void SetEnabled(bool enabled);
        bool Enabled() const;
        std::shared_ptr<MathJaxSvg const> GetOrQueue(
            std::string_view tex,
            bool display,
            float em,
            float containerWidth,
            bool allowQueue = true,
            bool highPriority = false);
        void SetCompletionCallback(std::function<void()> callback);
        void SetBackgroundPaused(bool paused);
        void Clear();

    private:
        struct State;

        std::unique_ptr<State> state;
    };
}
