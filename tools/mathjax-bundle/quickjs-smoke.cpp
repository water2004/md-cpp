#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

extern "C"
{
#include "../../src/app-winui/third_party/quickjs/quickjs.h"
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
    auto context = JS_NewContext(runtime);
    auto setupGlobal = JS_GetGlobalObject(context);
    JS_SetPropertyStr(context, setupGlobal, "ElMdLoadMathJaxModule", JS_NewCFunction(context, LoadFontModule, "ElMdLoadMathJaxModule", 1));
    JS_FreeValue(context, setupGlobal);
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
    std::vector<std::string> formulas{
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
