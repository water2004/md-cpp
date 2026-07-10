#include "pch.h"
#include "MathJaxRenderer.h"

#pragma warning(push)
#pragma warning(disable : 4100 4244)
extern "C"
{
#include "third_party/quickjs/quickjs.h"
}
#pragma warning(pop)

namespace
{
    std::string ReadUtf8File(std::wstring const& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
        {
            return {};
        }
        return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    }

    std::wstring MathJaxBundlePath()
    {
        std::wstring path(32768, L'\0');
        auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        path.resize(length);
        auto separator = path.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
        {
            return L"Assets\\mathjax\\mathjax-quickjs.js";
        }
        return path.substr(0, separator + 1) + L"Assets\\mathjax\\mathjax-quickjs.js";
    }

    float ParseLength(std::string_view value, float em)
    {
        std::size_t consumed = 0;
        float number = 0.0f;
        try
        {
            number = std::stof(std::string(value), &consumed);
        }
        catch (...)
        {
            return 0.0f;
        }
        auto unit = value.substr(consumed);
        if (unit.starts_with("ex")) return number * em * 0.5f;
        if (unit.starts_with("em")) return number * em;
        if (unit.starts_with("pt")) return number * (96.0f / 72.0f);
        return number;
    }

    float AttributeLength(std::string_view svg, std::string_view attribute, float em)
    {
        auto marker = std::string(attribute) + "=\"";
        auto start = svg.find(marker);
        if (start == std::string_view::npos) return 0.0f;
        start += marker.size();
        auto end = svg.find('"', start);
        if (end == std::string_view::npos) return 0.0f;
        return ParseLength(svg.substr(start, end - start), em);
    }

    float VerticalAlignment(std::string_view svg, float em)
    {
        auto marker = std::string_view("vertical-align:");
        auto start = svg.find(marker);
        if (start == std::string_view::npos) return 0.0f;
        start += marker.size();
        while (start < svg.size() && svg[start] == ' ') ++start;
        auto end = svg.find(';', start);
        if (end == std::string_view::npos) end = svg.find('"', start);
        if (end == std::string_view::npos) end = svg.size();
        return ParseLength(svg.substr(start, end - start), em);
    }
}

namespace winrt::ElMd
{
    struct MathJaxRenderer::State
    {
        JSRuntime* runtime = nullptr;
        JSContext* context = nullptr;
        std::chrono::steady_clock::time_point deadline{};
        std::unordered_map<std::string, MathJaxSvg> cache;
    };

    MathJaxRenderer::MathJaxRenderer() = default;

    MathJaxRenderer::~MathJaxRenderer()
    {
        Clear();
    }

    void MathJaxRenderer::Clear()
    {
        if (!state)
        {
            return;
        }
        state->cache.clear();
        if (state->context)
        {
            JS_FreeContext(state->context);
            state->context = nullptr;
        }
        if (state->runtime)
        {
            JS_FreeRuntime(state->runtime);
            state->runtime = nullptr;
        }
        state.reset();
    }

    int MathJaxRenderer::Interrupt(JSRuntime*, void* opaque)
    {
        auto renderer = static_cast<MathJaxRenderer*>(opaque);
        return !renderer->state || std::chrono::steady_clock::now() >= renderer->state->deadline;
    }

    std::string MathJaxRenderer::ExceptionText()
    {
        auto exception = JS_GetException(state->context);
        auto message = JS_ToCString(state->context, exception);
        std::string text = message ? message : "MathJax evaluation failed";
        if (message) JS_FreeCString(state->context, message);
        JS_FreeValue(state->context, exception);
        return text;
    }

    bool MathJaxRenderer::Initialize()
    {
        if (state && state->context)
        {
            return true;
        }
        Clear();
        auto bundle = ReadUtf8File(MathJaxBundlePath());
        if (bundle.empty())
        {
            return false;
        }
        state = std::make_unique<State>();
        state->runtime = JS_NewRuntime();
        if (!state->runtime)
        {
            state.reset();
            return false;
        }
        JS_SetMemoryLimit(state->runtime, 128 * 1024 * 1024);
        JS_SetMaxStackSize(state->runtime, 8 * 1024 * 1024);
        JS_SetInterruptHandler(state->runtime, Interrupt, this);
        state->context = JS_NewContext(state->runtime);
        if (!state->context)
        {
            Clear();
            return false;
        }
        state->deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        auto result = JS_Eval(state->context, bundle.data(), bundle.size(), "mathjax-quickjs.js", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result))
        {
            JS_FreeValue(state->context, result);
            Clear();
            return false;
        }
        JS_FreeValue(state->context, result);
        return true;
    }

    MathJaxSvg MathJaxRenderer::Render(std::string_view tex, bool display, float em, float containerWidth)
    {
        MathJaxSvg rendered;
        rendered.display = display;
        if (!Initialize())
        {
            rendered.error = "MathJax runtime could not be initialized";
            return rendered;
        }
        auto key = std::string(tex) + '\x1f' + (display ? "1" : "0") + '\x1f' + std::to_string(em) + '\x1f' + std::to_string(containerWidth);
        if (auto found = state->cache.find(key); found != state->cache.end())
        {
            return found->second;
        }
        auto global = JS_GetGlobalObject(state->context);
        auto api = JS_GetPropertyStr(state->context, global, "ElMdMathJax");
        auto function = JS_GetPropertyStr(state->context, api, "render");
        JSValue arguments[] = {
            JS_NewStringLen(state->context, tex.data(), tex.size()),
            JS_NewBool(state->context, display),
            JS_NewFloat64(state->context, em),
            JS_NewFloat64(state->context, containerWidth),
        };
        state->deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        auto result = JS_Call(state->context, function, api, static_cast<int>(std::size(arguments)), arguments);
        for (auto& argument : arguments) JS_FreeValue(state->context, argument);
        JS_FreeValue(state->context, function);
        JS_FreeValue(state->context, api);
        JS_FreeValue(state->context, global);
        if (JS_IsException(result))
        {
            rendered.error = ExceptionText();
            JS_FreeValue(state->context, result);
            return rendered;
        }
        auto svg = JS_ToCString(state->context, result);
        if (svg)
        {
            rendered.svg = svg;
            JS_FreeCString(state->context, svg);
        }
        JS_FreeValue(state->context, result);
        auto svgStart = rendered.svg.find("<svg");
        auto svgEnd = rendered.svg.rfind("</svg>");
        if (svgStart != std::string::npos && svgEnd != std::string::npos && svgEnd >= svgStart)
        {
            rendered.svg = rendered.svg.substr(svgStart, svgEnd + std::string_view("</svg>").size() - svgStart);
        }
        rendered.width = AttributeLength(rendered.svg, "width", em);
        rendered.height = AttributeLength(rendered.svg, "height", em);
        rendered.verticalAlign = VerticalAlignment(rendered.svg, em);
        if (!rendered)
        {
            rendered.error = "MathJax returned an invalid SVG";
            return rendered;
        }
        if (state->cache.size() >= 256) state->cache.erase(state->cache.begin());
        state->cache.emplace(std::move(key), rendered);
        return rendered;
    }
}
