#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>
#include <d2d1_3.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

extern "C"
{
#include "../../src/app-winui/third_party/quickjs/quickjs.h"
}

std::chrono::steady_clock::time_point deadline;

int Interrupt(JSRuntime*, void*)
{
    return std::chrono::steady_clock::now() >= deadline;
}

struct SvgValidator
{
    Microsoft::WRL::ComPtr<ID2D1DeviceContext5> context;

    SvgValidator()
    {
        Microsoft::WRL::ComPtr<ID3D11Device> d3d;
        D3D_FEATURE_LEVEL level{};
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, d3d.GetAddressOf(), &level, nullptr))) return;
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
        if (FAILED(d3d.As(&dxgi))) return;
        Microsoft::WRL::ComPtr<ID2D1Factory1> factory;
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(factory.GetAddressOf())))) return;
        Microsoft::WRL::ComPtr<ID2D1Device> device;
        if (FAILED(factory->CreateDevice(dxgi.Get(), device.GetAddressOf()))) return;
        Microsoft::WRL::ComPtr<ID2D1DeviceContext> base;
        if (FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, base.GetAddressOf()))) return;
        base.As(&context);
    }

    HRESULT Validate(std::string_view source) const
    {
        if (!context || source.empty()) return E_FAIL;
        auto allocation = GlobalAlloc(GMEM_MOVEABLE, source.size());
        if (!allocation) return E_OUTOFMEMORY;
        auto bytes = GlobalLock(allocation);
        if (!bytes)
        {
            GlobalFree(allocation);
            return E_OUTOFMEMORY;
        }
        std::memcpy(bytes, source.data(), source.size());
        GlobalUnlock(allocation);
        Microsoft::WRL::ComPtr<IStream> stream;
        if (FAILED(CreateStreamOnHGlobal(allocation, TRUE, stream.GetAddressOf())) || !stream)
        {
            GlobalFree(allocation);
            return E_FAIL;
        }
        Microsoft::WRL::ComPtr<ID2D1SvgDocument> document;
        auto result = context->CreateSvgDocument(stream.Get(), D2D1::SizeF(100.0f, 100.0f), document.GetAddressOf());
        return document ? result : FAILED(result) ? result : E_FAIL;
    }
};

struct SvgNormalizerRuntime
{
    using Create = void* (*)();
    using Destroy = void (*)(void*);
    using Normalize = std::int32_t (*)(void*, std::uint8_t const*, std::size_t, float, std::uint8_t**, std::size_t*);
    using FreeBuffer = void (*)(std::uint8_t*, std::size_t);

    HMODULE module = nullptr;
    void* context = nullptr;
    Destroy destroy = nullptr;
    Normalize normalize = nullptr;
    FreeBuffer freeBuffer = nullptr;

    SvgNormalizerRuntime()
    {
        module = LoadLibraryW(L"../../src/app-winui/third_party/usvg-normalizer/bin/x64/folia_svg_normalizer.dll");
        if (!module) return;
        auto create = reinterpret_cast<Create>(GetProcAddress(module, "folia_svg_normalizer_create"));
        destroy = reinterpret_cast<Destroy>(GetProcAddress(module, "folia_svg_normalizer_destroy"));
        normalize = reinterpret_cast<Normalize>(GetProcAddress(module, "folia_svg_normalize"));
        freeBuffer = reinterpret_cast<FreeBuffer>(GetProcAddress(module, "folia_svg_buffer_destroy"));
        if (create && destroy && normalize && freeBuffer) context = create();
    }

    ~SvgNormalizerRuntime()
    {
        if (context && destroy) destroy(context);
        if (module) FreeLibrary(module);
    }

    bool Process(std::string_view source, std::string& output) const
    {
        if (!context) return false;
        std::uint8_t* data = nullptr;
        std::size_t length = 0;
        auto status = normalize(context, reinterpret_cast<std::uint8_t const*>(source.data()), source.size(), 18.0f, &data, &length);
        if (data && length > 0) output.assign(reinterpret_cast<char const*>(data), length);
        if (data) freeBuffer(data, length);
        return status == 0 && !output.empty();
    }
};

std::size_t SvgElementEnd(std::string_view markup, std::size_t start)
{
    std::size_t depth = 0;
    auto cursor = start;
    while (cursor < markup.size())
    {
        auto open = markup.find("<svg", cursor);
        auto close = markup.find("</svg>", cursor);
        if (close == std::string_view::npos) return std::string_view::npos;
        if (open != std::string_view::npos && open < close)
        {
            auto tagEnd = markup.find('>', open + 4);
            if (tagEnd == std::string_view::npos) return std::string_view::npos;
            if (tagEnd == open || markup[tagEnd - 1] != '/') ++depth;
            cursor = tagEnd + 1;
            continue;
        }
        if (depth == 0) return std::string_view::npos;
        --depth;
        cursor = close + std::string_view("</svg>").size();
        if (depth == 0) return cursor;
    }
    return std::string_view::npos;
}

std::optional<std::string_view> SvgAttribute(std::string_view source, std::string_view name)
{
    auto rootEnd = source.find('>');
    if (rootEnd == std::string_view::npos) return std::nullopt;
    auto marker = std::string(name) + "=\"";
    auto start = source.find(marker);
    if (start == std::string_view::npos || start >= rootEnd) return std::nullopt;
    start += marker.size();
    auto end = source.find('"', start);
    if (end == std::string_view::npos || end > rootEnd) return std::nullopt;
    return source.substr(start, end - start);
}

JSValue LoadFontModule(JSContext* context, JSValueConst, int count, JSValueConst* arguments)
{
    if (count != 1) return JS_ThrowTypeError(context, "Expected one module");
    auto value = JS_ToCString(context, arguments[0]);
    if (!value) return JS_EXCEPTION;
    std::string name = value;
    JS_FreeCString(context, value);
    auto separator = name.find_last_of('/');
    name = name.substr(separator == std::string::npos ? 0 : separator + 1);
    if (name.empty() || name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-.") != std::string::npos)
    {
        return JS_ThrowReferenceError(context, "Invalid module");
    }
    std::ifstream stream("../../src/app-winui/Assets/mathjax/font/" + name, std::ios::binary);
    std::string source{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    if (source.empty()) return JS_ThrowReferenceError(context, "Missing module");
    return JS_Eval(context, source.data(), source.size(), name.c_str(), JS_EVAL_TYPE_GLOBAL);
}

int main()
{
    std::ifstream stream("../../src/app-winui/Assets/mathjax/mathjax-quickjs.js", std::ios::binary);
    std::string bundle{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    auto runtime = JS_NewRuntime();
    JS_SetMemoryLimit(runtime, 24 * 1024 * 1024);
    JS_SetMaxStackSize(runtime, 8 * 1024 * 1024);
    JS_SetInterruptHandler(runtime, Interrupt, nullptr);
    auto context = JS_NewContext(runtime);
    auto setupGlobal = JS_GetGlobalObject(context);
    JS_SetPropertyStr(context, setupGlobal, "FoliaLoadMathJaxModule", JS_NewCFunction(context, LoadFontModule, "FoliaLoadMathJaxModule", 1));
    JS_FreeValue(context, setupGlobal);
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    auto loaded = JS_Eval(context, bundle.data(), bundle.size(), "mathjax-quickjs.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(loaded))
    {
        auto exception = JS_GetException(context);
        auto message = JS_ToCString(context, exception);
        std::cerr << (message ? message : "load exception") << '\n';
        if (message) JS_FreeCString(context, message);
        JS_FreeValue(context, exception);
        return 1;
    }
    JS_FreeValue(context, loaded);
    auto global = JS_GetGlobalObject(context);
    auto api = JS_GetPropertyStr(context, global, "FoliaMathJax");
    auto render = JS_GetPropertyStr(context, api, "render");
    SvgValidator svgValidator;
    SvgNormalizerRuntime svgNormalizer;
    if (!svgNormalizer.context) return 7;
    std::vector<std::string> formulas{
        R"(\exists\delta>0,s.t. |x'-x_0|<\delta,0<|x''-x_0|<\delta)",
        "y=x",
        R"(\sum_{n=1}^{\infty}\frac{1}{n^2})",
        R"(\left(\frac{a}{b}\right))",
        R"(\mathbb{R}\times\mathfrak{g}\to\mathcal{H})",
        R"(\int\limits_0^\infty e^{-x^2}\,dx)",
        R"(\begin{bmatrix}a&b\\c&d\end{bmatrix})",
        R"(\widehat{f}(\xi)=\int_{-\infty}^{\infty}f(x)e^{-2\pi i x\xi}\,dx)",
        R"(\underbrace{a+b+\cdots+z}_{26\text{ terms}})",
        R"(\xrightarrow[\text{below}]{\text{above}})",
        R"(\boldsymbol{\alpha}+\mathbf{x}+\mathsf{A}+\mathtt{code})",
    };
    bool failed = false;
    for (int index = 0; index < 500; ++index)
    {
        auto tex = formulas[static_cast<std::size_t>(index) % formulas.size()] + "+x_{" + std::to_string(index) + "}";
        if (index == 0) tex = formulas.front();
        JSValue arguments[] = {
            JS_NewString(context, tex.c_str()),
            JS_NewBool(context, index != 0 && index % 3 == 0),
            JS_NewFloat64(context, 18.0),
            JS_NewFloat64(context, 900.0),
        };
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        auto result = JS_Call(context, render, api, 4, arguments);
        for (auto& argument : arguments) JS_FreeValue(context, argument);
        if (JS_IsException(result))
        {
            auto exception = JS_GetException(context);
            auto message = JS_ToCString(context, exception);
            std::cerr << index << ": " << (message ? message : "exception") << '\n';
            if (message) JS_FreeCString(context, message);
            JS_FreeValue(context, exception);
            failed = true;
        }
        else
        {
            auto text = JS_ToCString(context, result);
            if (!text || std::string_view(text).find("<svg") == std::string_view::npos) return 2;
            std::string_view output(text);
            auto first = output.find("<svg");
            if (first == std::string_view::npos) return 3;
            std::size_t cursor = 0;
            std::size_t fragment = 0;
            while ((cursor = output.find("<svg", cursor)) != std::string_view::npos)
            {
                auto end = SvgElementEnd(output, cursor);
                if (end == std::string_view::npos) return 5;
                std::string compatible(output.substr(cursor, end - cursor));
                std::size_t color = 0;
                while ((color = compatible.find("currentColor", color)) != std::string::npos)
                {
                    compatible.replace(color, std::string_view("currentColor").size(), "#000000");
                    color += 7;
                }
                std::string normalized;
                if (!svgNormalizer.Process(compatible, normalized))
                {
                    std::cerr << "SVG normalization failed at formula " << index << ", fragment " << fragment << ": " << normalized << '\n';
                    std::ofstream("quickjs-failed.svg", std::ios::binary) << compatible;
                    return 8;
                }
                if (index == 0 && fragment == 0)
                {
                    auto sourceWidth = SvgAttribute(compatible, "width");
                    auto normalizedWidth = SvgAttribute(normalized, "width");
                    if (!sourceWidth || !sourceWidth->ends_with("ex") || !normalizedWidth) return 9;
                    auto expected = std::stof(std::string(sourceWidth->substr(0, sourceWidth->size() - 2))) * 18.0f * 0.5f;
                    auto actual = std::stof(std::string(*normalizedWidth));
                    if (std::abs(expected - actual) > 0.02f)
                    {
                        std::cerr << "SVG font-size conversion failed: expected " << expected << ", got " << actual << '\n';
                        return 10;
                    }
                }
                auto validation = svgValidator.Validate(normalized);
                if (FAILED(validation))
                {
                    std::cerr << "D2D SVG fragment " << fragment << " failed: 0x" << std::hex << static_cast<unsigned long>(validation) << '\n';
                    std::ofstream("quickjs-failed.svg", std::ios::binary) << normalized;
                    return 6;
                }
                cursor = end;
                ++fragment;
            }
            JS_FreeCString(context, text);
        }
        JS_FreeValue(context, result);
    }
    {
        JSValue arguments[] = {
            JS_NewString(context, R"(\frac{)"),
            JS_NewBool(context, true),
            JS_NewFloat64(context, 18.0),
            JS_NewFloat64(context, 900.0),
        };
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        auto result = JS_Call(context, render, api, 4, arguments);
        for (auto& argument : arguments) JS_FreeValue(context, argument);
        if (JS_IsException(result)) return 11;
        auto text = JS_ToCString(context, result);
        if (!text) return 12;
        auto output = std::string_view(text);
        auto hasError = output.find("data-mjx-error=") != std::string_view::npos;
        auto hasRedError = output.find("fill=\"red\"") != std::string_view::npos
            || output.find("stroke=\"red\"") != std::string_view::npos;
        if (!hasError || !hasRedError)
        {
            std::cerr << "MathJax did not emit a visible red merror: " << output << '\n';
            JS_FreeCString(context, text);
            JS_FreeValue(context, result);
            return 13;
        }
        auto svgStart = output.find("<svg");
        auto svgEnd = svgStart == std::string_view::npos
            ? std::string_view::npos
            : SvgElementEnd(output, svgStart);
        if (svgEnd == std::string_view::npos) return 14;
        std::string compatible(output.substr(svgStart, svgEnd - svgStart));
        std::size_t color = 0;
        while ((color = compatible.find("currentColor", color)) != std::string::npos)
        {
            compatible.replace(color, std::string_view("currentColor").size(), "#000000");
            color += 7;
        }
        std::string normalized;
        if (!svgNormalizer.Process(compatible, normalized)
            || FAILED(svgValidator.Validate(normalized)))
        {
            std::cerr << "MathJax merror SVG is not compatible with the native renderer\n";
            JS_FreeCString(context, text);
            JS_FreeValue(context, result);
            return 15;
        }
        JS_FreeCString(context, text);
        JS_FreeValue(context, result);
    }
    JS_FreeValue(context, render);
    JS_FreeValue(context, api);
    JS_FreeValue(context, global);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
    if (failed) return 4;
    std::cout << "ok\n";
}
