#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace winrt::ElMd
{
    struct NormalizedSvg
    {
        std::uint64_t id = 0;
        std::shared_ptr<std::string const> svg;
        std::string error;

        explicit operator bool() const { return id != 0 && error.empty() && svg && !svg->empty(); }
    };

    class SvgNormalizer
    {
    public:
        SvgNormalizer();
        ~SvgNormalizer();
        SvgNormalizer(SvgNormalizer const&) = delete;
        SvgNormalizer& operator=(SvgNormalizer const&) = delete;

        std::optional<NormalizedSvg> GetOrQueue(std::string_view source, float fontSize, bool allowQueue = true);
        void SetCompletionCallback(std::function<void()> callback);

    private:
        struct State;
        std::unique_ptr<State> state;
    };
}
