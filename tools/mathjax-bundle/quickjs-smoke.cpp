#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

extern "C"
{
#include "../../src/app-winui/third_party/quickjs/quickjs.h"
}

int main()
{
    std::ifstream stream("../../src/app-winui/Assets/mathjax/mathjax-quickjs.js", std::ios::binary);
    std::string bundle{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    auto runtime = JS_NewRuntime();
    JS_SetMemoryLimit(runtime, 128 * 1024 * 1024);
    JS_SetMaxStackSize(runtime, 8 * 1024 * 1024);
    auto context = JS_NewContext(runtime);
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
    for (int index = 0; index < 500; ++index)
    {
        auto tex = index % 2 == 0 ? std::string{} : "x_{" + std::to_string(index) + "}^2 + y^2";
        JSValue arguments[] = {
            JS_NewString(context, tex.c_str()),
            JS_NewBool(context, index % 3 == 0),
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
        }
        else
        {
            auto text = JS_ToCString(context, result);
            if (!text || std::string_view(text).find("<svg") == std::string_view::npos) return 2;
            JS_FreeCString(context, text);
        }
        JS_FreeValue(context, result);
    }
    JS_FreeValue(context, render);
    JS_FreeValue(context, api);
    JS_FreeValue(context, global);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
    std::cout << "ok\n";
}
