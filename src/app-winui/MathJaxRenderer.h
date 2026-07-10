#pragma once

#include <memory>
#include <string>
#include <string_view>

extern "C"
{
    typedef struct JSRuntime JSRuntime;
}

namespace winrt::ElMd
{
    struct MathJaxSvg
    {
        std::string svg;
        std::string error;
        float width = 0.0f;
        float height = 0.0f;
        float verticalAlign = 0.0f;
        bool display = false;

        explicit operator bool() const { return error.empty() && !svg.empty() && width > 0.0f && height > 0.0f; }
    };

    class MathJaxRenderer
    {
    public:
        MathJaxRenderer();
        ~MathJaxRenderer();
        MathJaxRenderer(MathJaxRenderer const&) = delete;
        MathJaxRenderer& operator=(MathJaxRenderer const&) = delete;

        MathJaxSvg Render(std::string_view tex, bool display, float em, float containerWidth);
        void Clear();

    private:
        struct State;

        bool Initialize();
        std::string ExceptionText();
        static int Interrupt(JSRuntime*, void* opaque);

        std::unique_ptr<State> state;
    };
}
