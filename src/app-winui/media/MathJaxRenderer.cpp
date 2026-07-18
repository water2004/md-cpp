#include "pch.h"
#include "media/MathJaxRenderer.h"
#include "storage/AssetPaths.h"

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
        return winrt::Folia::AssetPath(std::filesystem::path(L"mathjax") / L"mathjax-quickjs.js").wstring();
    }

    std::wstring MathJaxFontPath(std::string_view module)
    {
        auto separator = module.find_last_of('/');
        auto name = module.substr(separator == std::string_view::npos ? 0 : separator + 1);
        if (name.empty() || name.size() > 64) return {};
        for (auto value : name)
        {
            if (!(std::isalnum(static_cast<unsigned char>(value)) || value == '-' || value == '.')) return {};
        }
        auto bundle = MathJaxBundlePath();
        auto directory = bundle.substr(0, bundle.find_last_of(L"\\/"));
        return directory + L"\\font\\" + std::wstring(name.begin(), name.end());
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

    std::optional<std::string> AttributeText(std::string_view markup, std::string_view attribute)
    {
        auto marker = std::string(attribute) + "=\"";
        auto start = markup.find(marker);
        if (start == std::string_view::npos) return std::nullopt;
        start += marker.size();
        auto end = markup.find('"', start);
        if (end == std::string_view::npos) return std::nullopt;
        return std::string(markup.substr(start, end - start));
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

    float BreakSpacing(std::string_view markup, float em)
    {
        auto sizeStart = markup.find("size=\"");
        if (sizeStart != std::string_view::npos)
        {
            sizeStart += std::string_view("size=\"").size();
            auto sizeEnd = markup.find('"', sizeStart);
            if (sizeEnd != std::string_view::npos)
            {
                auto value = markup.substr(sizeStart, sizeEnd - sizeStart);
                constexpr std::array<float, 6> spaces{ 0.0f, 2.0f / 18.0f, 3.0f / 18.0f, 4.0f / 18.0f, 5.0f / 18.0f, 6.0f / 18.0f };
                if (value.size() == 1 && value.front() >= '0' && value.front() <= '5')
                {
                    return spaces[static_cast<std::size_t>(value.front() - '0')] * em;
                }
            }
        }
        auto spacingStart = markup.find("letter-spacing:");
        if (spacingStart == std::string_view::npos)
        {
            return 0.0f;
        }
        spacingStart += std::string_view("letter-spacing:").size();
        while (spacingStart < markup.size() && markup[spacingStart] == ' ') ++spacingStart;
        auto spacingEnd = markup.find_first_of(";\"", spacingStart);
        if (spacingEnd == std::string_view::npos) spacingEnd = markup.size();
        return (std::max)(0.0f, ParseLength(markup.substr(spacingStart, spacingEnd - spacingStart), em) + em);
    }

    std::optional<std::size_t> SvgElementEnd(std::string_view markup, std::size_t start)
    {
        std::size_t depth = 0;
        auto cursor = start;
        while (cursor < markup.size())
        {
            auto open = markup.find("<svg", cursor);
            auto close = markup.find("</svg>", cursor);
            if (close == std::string_view::npos) return std::nullopt;
            if (open != std::string_view::npos && open < close)
            {
                auto tagEnd = markup.find('>', open + 4);
                if (tagEnd == std::string_view::npos) return std::nullopt;
                if (tagEnd == open || markup[tagEnd - 1] != '/') ++depth;
                cursor = tagEnd + 1;
                continue;
            }
            if (depth == 0) return std::nullopt;
            --depth;
            cursor = close + std::string_view("</svg>").size();
            if (depth == 0) return cursor;
        }
        return std::nullopt;
    }
}

namespace winrt::Folia
{
    struct MathJaxRenderer::State
    {
        struct Request
        {
            std::string key;
            std::string tex;
            bool display = false;
            float em = 0.0f;
            float containerWidth = 0.0f;
            std::uint64_t generation = 0;
        };

        std::mutex mutex;
        std::condition_variable_any ready;
        std::deque<Request> requests;
        std::unordered_set<std::string> queued;
        std::unordered_map<std::string, std::shared_ptr<MathJaxSvg const>> cache;
        std::unordered_map<std::string, std::size_t> transientFailures;
        std::deque<std::string> cacheOrder;
        std::size_t cacheBytes = 0;
        std::uint64_t generation = 0;
        std::function<void()> completion;
        JSRuntime* runtime = nullptr;
        JSContext* context = nullptr;
        std::chrono::steady_clock::time_point deadline{};
        bool initializationAttempted = false;
        bool enabled = false;
        std::atomic_bool interruptRequested = false;
        std::jthread worker;

        static bool RetryableFailure(std::string_view error)
        {
            return error.find("interrupted") != std::string_view::npos
                || error.find("out of memory") != std::string_view::npos
                || error.find("stack overflow") != std::string_view::npos
                || error.find("runtime could not be initialized") != std::string_view::npos
                || error.find("font module is unavailable") != std::string_view::npos;
        }

        static int Interrupt(JSRuntime*, void* opaque)
        {
            auto self = static_cast<State*>(opaque);
            return self->interruptRequested.load(std::memory_order_relaxed)
                || std::chrono::steady_clock::now() >= self->deadline;
        }

        static JSValue LoadFontModule(JSContext* context, JSValueConst, int count, JSValueConst* arguments)
        {
            if (count != 1) return JS_ThrowTypeError(context, "Expected one MathJax font module name");
            auto name = JS_ToCString(context, arguments[0]);
            if (!name) return JS_EXCEPTION;
            auto path = MathJaxFontPath(name);
            JS_FreeCString(context, name);
            auto source = path.empty() ? std::string{} : ReadUtf8File(path);
            if (source.empty()) return JS_ThrowReferenceError(context, "MathJax font module is unavailable");
            auto result = JS_Eval(context, source.data(), source.size(), "mathjax-font.js", JS_EVAL_TYPE_GLOBAL);
            if (JS_IsException(result)) return result;
            JS_FreeValue(context, result);
            return JS_UNDEFINED;
        }

        std::string ExceptionText()
        {
            auto exception = JS_GetException(context);
            auto message = JS_ToCString(context, exception);
            std::string text = message ? message : "MathJax evaluation failed";
            if (message) JS_FreeCString(context, message);
            JS_FreeValue(context, exception);
            return text;
        }

        bool InitializeRuntime()
        {
            if (context) return true;
            if (initializationAttempted) return false;
            initializationAttempted = true;
            auto bundle = ReadUtf8File(MathJaxBundlePath());
            if (bundle.empty())
            {
                initializationAttempted = false;
                return false;
            }
            runtime = JS_NewRuntime();
            if (!runtime)
            {
                initializationAttempted = false;
                return false;
            }
            JS_SetMemoryLimit(runtime, 24 * 1024 * 1024);
            JS_SetMaxStackSize(runtime, 8 * 1024 * 1024);
            JS_SetInterruptHandler(runtime, Interrupt, this);
            context = JS_NewContext(runtime);
            if (!context)
            {
                ShutdownRuntime();
                initializationAttempted = false;
                return false;
            }
            auto global = JS_GetGlobalObject(context);
            JS_SetPropertyStr(context, global, "FoliaLoadMathJaxModule", JS_NewCFunction(context, LoadFontModule, "FoliaLoadMathJaxModule", 1));
            JS_FreeValue(context, global);
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            auto result = JS_Eval(context, bundle.data(), bundle.size(), "mathjax-quickjs.js", JS_EVAL_TYPE_GLOBAL);
            if (JS_IsException(result))
            {
                JS_FreeValue(context, result);
                ShutdownRuntime();
                initializationAttempted = false;
                return false;
            }
            JS_FreeValue(context, result);
            return true;
        }

        void ShutdownRuntime()
        {
            if (context)
            {
                JS_FreeContext(context);
                context = nullptr;
            }
            if (runtime)
            {
                JS_FreeRuntime(runtime);
                runtime = nullptr;
            }
            initializationAttempted = false;
        }

        MathJaxSvg RenderNow(Request const& request)
        {
            MathJaxSvg rendered;
            rendered.display = request.display;
            if (!InitializeRuntime())
            {
                rendered.error = "MathJax runtime could not be initialized";
                rendered.errorKind = MathJaxErrorKind::Infrastructure;
                return rendered;
            }
            auto global = JS_GetGlobalObject(context);
            auto api = JS_GetPropertyStr(context, global, "FoliaMathJax");
            auto function = JS_GetPropertyStr(context, api, "render");
            JSValue arguments[] = {
                JS_NewStringLen(context, request.tex.data(), request.tex.size()),
                JS_NewBool(context, request.display),
                JS_NewFloat64(context, request.em),
                JS_NewFloat64(context, request.containerWidth),
            };
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            auto result = JS_Call(context, function, api, static_cast<int>(std::size(arguments)), arguments);
            for (auto& argument : arguments) JS_FreeValue(context, argument);
            JS_FreeValue(context, function);
            JS_FreeValue(context, api);
            JS_FreeValue(context, global);
            if (JS_IsException(result))
            {
                rendered.error = ExceptionText();
                rendered.errorKind = MathJaxErrorKind::Infrastructure;
                JS_FreeValue(context, result);
                if (RetryableFailure(rendered.error))
                {
                    ShutdownRuntime();
                    initializationAttempted = false;
                }
                return rendered;
            }
            std::string output;
            auto svg = JS_ToCString(context, result);
            if (svg)
            {
                output = svg;
                JS_FreeCString(context, svg);
            }
            JS_FreeValue(context, result);

            if (auto diagnostic = AttributeText(output, "data-mjx-error"))
            {
                rendered.error = std::move(*diagnostic);
                rendered.errorKind = MathJaxErrorKind::Formula;
            }

            std::size_t cursor = 0;
            std::size_t previousEnd = 0;
            float ascent = 0.0f;
            float descent = 0.0f;
            while (true)
            {
                auto svgStart = output.find("<svg", cursor);
                if (svgStart == std::string::npos) break;
                auto svgEnd = SvgElementEnd(output, svgStart);
                if (!svgEnd) break;
                MathJaxSvgFragment fragment;
                fragment.svg = std::make_shared<std::string const>(output.substr(svgStart, *svgEnd - svgStart));
                fragment.width = AttributeLength(*fragment.svg, "width", request.em);
                fragment.height = AttributeLength(*fragment.svg, "height", request.em);
                fragment.verticalAlign = VerticalAlignment(*fragment.svg, request.em);
                if (!rendered.fragments.empty())
                {
                    auto between = std::string_view(output).substr(previousEnd, svgStart - previousEnd);
                    fragment.breakBefore = between.find("<mjx-break") != std::string_view::npos;
                    fragment.breakSpace = BreakSpacing(between, request.em);
                }
                if (fragment.width > 0.0f && fragment.height > 0.0f)
                {
                    rendered.width += fragment.breakSpace + fragment.width;
                    auto baseline = (std::clamp)(fragment.height + fragment.verticalAlign, 0.0f, fragment.height);
                    ascent = (std::max)(ascent, baseline);
                    descent = (std::max)(descent, fragment.height - baseline);
                    rendered.fragments.push_back(std::move(fragment));
                }
                previousEnd = *svgEnd;
                cursor = *svgEnd;
            }
            rendered.height = ascent + descent;
            rendered.verticalAlign = -descent;
            if (!rendered)
            {
                rendered.error = "MathJax returned an invalid SVG";
                rendered.errorKind = MathJaxErrorKind::Infrastructure;
            }
            return rendered;
        }

        static std::size_t ResultBytes(MathJaxSvg const& result)
        {
            std::size_t bytes = sizeof(result);
            for (auto const& fragment : result.fragments) bytes += sizeof(fragment) + (fragment.svg ? fragment.svg->size() : 0);
            return bytes + result.error.size();
        }

        void Store(Request const& request, MathJaxSvg result)
        {
            constexpr std::size_t budget = 16 * 1024 * 1024;
            auto shared = std::make_shared<MathJaxSvg const>(std::move(result));
            auto bytes = request.key.size() + ResultBytes(*shared);
            while ((!cacheOrder.empty()) && (cacheBytes + bytes > budget || cache.size() >= 4096))
            {
                auto oldest = std::move(cacheOrder.front());
                cacheOrder.pop_front();
                auto found = cache.find(oldest);
                if (found == cache.end()) continue;
                cacheBytes -= found->first.size() + ResultBytes(*found->second);
                cache.erase(found);
            }
            if (bytes <= budget)
            {
                cacheBytes += bytes;
                cacheOrder.push_back(request.key);
                cache.emplace(request.key, std::move(shared));
            }
        }

        void Run(std::stop_token stop)
        {
            while (!stop.stop_requested())
            {
                Request request;
                {
                    std::unique_lock lock(mutex);
                    if (!ready.wait(lock, stop, [&] { return !requests.empty(); })) break;
                    request = std::move(requests.front());
                    requests.pop_front();
                }
                auto result = RenderNow(request);
                std::function<void()> notify;
                {
                    std::scoped_lock lock(mutex);
                    queued.erase(request.key);
                    if (request.generation == generation)
                    {
                        auto retryable = RetryableFailure(result.error);
                        bool store = true;
                        if (retryable)
                        {
                            auto [failure, first] = transientFailures.try_emplace(request.key, 1);
                            if (first) store = false;
                            else transientFailures.erase(failure);
                        }
                        else
                        {
                            transientFailures.erase(request.key);
                        }
                        if (store) Store(request, std::move(result));
                        notify = completion;
                    }
                }
                if (notify) notify();
            }
            ShutdownRuntime();
        }
    };

    MathJaxRenderer::MathJaxRenderer() : state(std::make_unique<State>()) {}

    MathJaxRenderer::~MathJaxRenderer()
    {
        if (!state) return;
        SetCompletionCallback({});
        SetEnabled(false);
    }

    void MathJaxRenderer::SetEnabled(bool enabled)
    {
        if (!state) return;
        if (enabled)
        {
            std::scoped_lock lock(state->mutex);
            if (state->enabled) return;
            state->enabled = true;
            state->interruptRequested = false;
            auto current = state.get();
            state->worker = std::jthread([current](std::stop_token stop) { current->Run(stop); });
            return;
        }

        std::jthread worker;
        {
            std::scoped_lock lock(state->mutex);
            if (!state->enabled && !state->worker.joinable()) return;
            state->enabled = false;
            ++state->generation;
            state->requests.clear();
            state->queued.clear();
            state->cache.clear();
            state->cacheOrder.clear();
            state->transientFailures.clear();
            state->cacheBytes = 0;
            state->interruptRequested = true;
            state->worker.request_stop();
            state->ready.notify_all();
            worker = std::move(state->worker);
        }
        if (worker.joinable()) worker.join();
        state->interruptRequested = false;
    }

    bool MathJaxRenderer::Enabled() const
    {
        if (!state) return false;
        std::scoped_lock lock(state->mutex);
        return state->enabled;
    }

    void MathJaxRenderer::Clear()
    {
        if (!state) return;
        std::scoped_lock lock(state->mutex);
        ++state->generation;
        state->requests.clear();
        state->queued.clear();
        state->cache.clear();
        state->cacheOrder.clear();
        state->transientFailures.clear();
        state->cacheBytes = 0;
    }

    void MathJaxRenderer::SetCompletionCallback(std::function<void()> callback)
    {
        std::scoped_lock lock(state->mutex);
        state->completion = std::move(callback);
    }

    std::shared_ptr<MathJaxSvg const> MathJaxRenderer::GetOrQueue(std::string_view tex, bool display, float em, float containerWidth, bool allowQueue)
    {
        auto key = std::string(tex) + '\x1f' + (display ? "1" : "0") + '\x1f' + std::to_string(em) + '\x1f' + std::to_string(containerWidth);
        std::scoped_lock lock(state->mutex);
        if (!state->enabled) return {};
        if (auto found = state->cache.find(key); found != state->cache.end()) return found->second;
        if (!allowQueue) return {};
        if (state->queued.insert(key).second)
        {
            while (state->requests.size() >= 256)
            {
                state->queued.erase(state->requests.front().key);
                state->requests.pop_front();
            }
            state->requests.push_back(State::Request{ key, std::string(tex), display, em, containerWidth, state->generation });
            state->ready.notify_one();
        }
        return {};
    }
}
