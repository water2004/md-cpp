#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
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
    JS_SetPropertyStr(context, setupGlobal, "ElMdLoadMathJaxModule", JS_NewCFunction(context, LoadFontModule, "ElMdLoadMathJaxModule", 1));
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
    auto api = JS_GetPropertyStr(context, global, "ElMdMathJax");
    auto render = JS_GetPropertyStr(context, api, "render");
    SvgValidator svgValidator;
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
            if (index == 0)
            {
                std::string_view output(text);
                auto first = output.find("<svg");
                if (first == std::string_view::npos || output.find("<svg", first + 4) == std::string_view::npos) return 3;
                std::size_t cursor = 0;
                std::size_t fragment = 0;
                while ((cursor = output.find("<svg", cursor)) != std::string_view::npos)
                {
                    auto end = output.find("</svg>", cursor);
                    if (end == std::string_view::npos) return 5;
                    end += std::string_view("</svg>").size();
                    auto source = output.substr(cursor, end - cursor);
                    std::string compatible(source);
                    std::size_t color = 0;
                    while ((color = compatible.find("currentColor", color)) != std::string::npos)
                    {
                        compatible.replace(color, std::string_view("currentColor").size(), "#000000");
                        color += 7;
                    }
                    auto style = compatible.find(" style=\"");
                    if (style != std::string::npos)
                    {
                        auto endStyle = compatible.find('"', style + 8);
                        if (endStyle != std::string::npos) compatible.erase(style, endStyle - style + 1);
                    }
                    auto removeAttribute = [&](std::string_view name)
                    {
                        auto marker = " " + std::string(name) + "=\"";
                        std::size_t position = 0;
                        while ((position = compatible.find(marker, position)) != std::string::npos)
                        {
                            auto endAttribute = compatible.find('"', position + marker.size());
                            if (endAttribute == std::string::npos) break;
                            compatible.erase(position, endAttribute - position + 1);
                        }
                    };
                    removeAttribute("role");
                    removeAttribute("focusable");
                    removeAttribute("data-mml-node");
                    removeAttribute("data-latex");
                    removeAttribute("data-c");
                    auto validation = svgValidator.Validate(compatible);
                    if (FAILED(validation))
                    {
                        std::cerr << "D2D SVG fragment " << fragment << " failed: 0x" << std::hex << static_cast<unsigned long>(validation) << '\n';
                        std::ofstream("quickjs-failed.svg", std::ios::binary) << source;
                        return 6;
                    }
                    cursor = end;
                    ++fragment;
                }
            }
            JS_FreeCString(context, text);
        }
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
