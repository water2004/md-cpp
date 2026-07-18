#include "pch.h"
#include "media/SvgNormalizer.h"

namespace
{
    std::wstring ModuleDirectory()
    {
        std::wstring path(32768, L'\0');
        auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        path.resize(length);
        auto separator = path.find_last_of(L"\\/");
        return separator == std::wstring::npos ? std::wstring{} : path.substr(0, separator + 1);
    }

    class Runtime
    {
    public:
        Runtime()
        {
            module = LoadLibraryExW((ModuleDirectory() + L"folia_svg_normalizer.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!module)
            {
                error = "SVG normalizer library could not be loaded";
                return;
            }
            create = reinterpret_cast<Create>(GetProcAddress(module, "folia_svg_normalizer_create"));
            destroy = reinterpret_cast<Destroy>(GetProcAddress(module, "folia_svg_normalizer_destroy"));
            normalize = reinterpret_cast<Normalize>(GetProcAddress(module, "folia_svg_normalize"));
            freeBuffer = reinterpret_cast<FreeBuffer>(GetProcAddress(module, "folia_svg_buffer_destroy"));
            if (!create || !destroy || !normalize || !freeBuffer)
            {
                error = "SVG normalizer library has an incompatible ABI";
                return;
            }
            context = create();
            if (!context) error = "SVG normalizer could not initialize its font database";
        }

        ~Runtime()
        {
            if (context && destroy) destroy(context);
            if (module) FreeLibrary(module);
        }

        winrt::Folia::NormalizedSvg Process(std::string const& source, float fontSize)
        {
            winrt::Folia::NormalizedSvg result;
            if (!error.empty())
            {
                result.error = error;
                return result;
            }
            std::uint8_t* output = nullptr;
            std::size_t outputLength = 0;
            auto status = normalize(
                context,
                reinterpret_cast<std::uint8_t const*>(source.data()),
                source.size(),
                fontSize,
                &output,
                &outputLength,
                &result.width,
                &result.height);
            std::string value;
            if (output && outputLength > 0) value.assign(reinterpret_cast<char const*>(output), outputLength);
            if (output) freeBuffer(output, outputLength);
            if (status == 0) result.svg = std::make_shared<std::string const>(std::move(value));
            else result.error = value.empty() ? "SVG normalization failed" : std::move(value);
            return result;
        }

    private:
        using Create = void* (*)();
        using Destroy = void (*)(void*);
        using Normalize = std::int32_t (*)(void*, std::uint8_t const*, std::size_t, float, std::uint8_t**, std::size_t*, float*, float*);
        using FreeBuffer = void (*)(std::uint8_t*, std::size_t);

        HMODULE module = nullptr;
        void* context = nullptr;
        Create create = nullptr;
        Destroy destroy = nullptr;
        Normalize normalize = nullptr;
        FreeBuffer freeBuffer = nullptr;
        std::string error;
    };
}

namespace winrt::Folia
{
    struct SvgNormalizer::State
    {
        struct Request
        {
            std::string key;
            std::string source;
            float fontSize = 0.0f;
        };

        enum class RequestState
        {
            BackgroundQueued,
            HighPriorityQueued,
            InFlight,
        };

        State() : worker([this] { Run(); }) {}

        ~State()
        {
            {
                std::scoped_lock lock(mutex);
                stopping = true;
                completion = {};
            }
            wake.notify_one();
            if (worker.joinable()) worker.join();
        }

        void Run()
        {
            std::unique_ptr<Runtime> runtime;
            for (;;)
            {
                Request request;
                auto highPriority = false;
                {
                    std::unique_lock lock(mutex);
                    wake.wait(lock, [this] {
                        return stopping || !highPriorityPending.empty() || !pending.empty();
                    });
                    if (stopping) return;
                    if (!highPriorityPending.empty())
                    {
                        highPriority = true;
                        request = std::move(highPriorityPending.front());
                        highPriorityPending.pop_front();
                    }
                    else
                    {
                        request = std::move(pending.front());
                        pending.pop_front();
                    }
                    if (auto found = requestStates.find(request.key);
                        found != requestStates.end())
                        found->second = RequestState::InFlight;
                }
                SetThreadPriority(
                    GetCurrentThread(),
                    highPriority
                        ? THREAD_PRIORITY_NORMAL
                        : THREAD_PRIORITY_BELOW_NORMAL);
                if (!runtime) runtime = std::make_unique<Runtime>();
                auto result = runtime->Process(request.source, request.fontSize);
                std::function<void()> callback;
                {
                    std::scoped_lock lock(mutex);
                    requestStates.erase(request.key);
                    Store(request.key, std::move(result));
                    if (!stopping) callback = completion;
                }
                if (callback) callback();
            }
        }

        void Store(std::string const& key, NormalizedSvg result)
        {
            constexpr std::size_t budget = 32 * 1024 * 1024;
            constexpr std::size_t limit = 8192;
            auto bytes = key.size() + (result.svg ? result.svg->size() : 0) + result.error.size();
            while (!cacheOrder.empty() && (cacheBytes + bytes > budget || cache.size() >= limit))
            {
                auto oldest = std::move(cacheOrder.front());
                cacheOrder.pop_front();
                auto found = cache.find(oldest);
                if (found == cache.end()) continue;
                cacheBytes -= oldest.size() + (found->second.svg ? found->second.svg->size() : 0) + found->second.error.size();
                cache.erase(found);
            }
            if (bytes <= budget)
            {
                result.id = nextId++;
                cacheBytes += bytes;
                cacheOrder.push_back(key);
                cache.emplace(key, std::move(result));
            }
        }

        std::mutex mutex;
        std::condition_variable wake;
        bool stopping = false;
        std::thread worker;
        std::deque<Request> highPriorityPending;
        std::deque<Request> pending;
        std::unordered_map<std::string, RequestState> requestStates;
        std::unordered_map<std::string, NormalizedSvg> cache;
        std::deque<std::string> cacheOrder;
        std::size_t cacheBytes = 0;
        std::uint64_t nextId = 1;
        std::function<void()> completion;
    };

    SvgNormalizer::SvgNormalizer() : state(std::make_unique<State>()) {}
    SvgNormalizer::~SvgNormalizer() = default;

    std::optional<NormalizedSvg> SvgNormalizer::GetOrQueue(
        std::string_view source,
        float fontSize,
        bool allowQueue,
        bool highPriority)
    {
        if (source.empty()) return std::nullopt;
        std::scoped_lock lock(state->mutex);
        auto key = std::string(source);
        key.push_back('\0');
        key.append(reinterpret_cast<char const*>(&fontSize), sizeof(fontSize));
        if (auto found = state->cache.find(key); found != state->cache.end()) return found->second;
        if (!allowQueue) return std::nullopt;
        auto [requestState, inserted] = state->requestStates.try_emplace(
            key,
            highPriority
                ? State::RequestState::HighPriorityQueued
                : State::RequestState::BackgroundQueued);
        if (!inserted)
        {
            if (highPriority
                && requestState->second == State::RequestState::BackgroundQueued)
            {
                auto found = std::ranges::find(state->pending, key, &State::Request::key);
                if (found != state->pending.end())
                {
                    state->highPriorityPending.push_back(std::move(*found));
                    state->pending.erase(found);
                    requestState->second = State::RequestState::HighPriorityQueued;
                    state->wake.notify_one();
                }
            }
            return std::nullopt;
        }

        while (state->highPriorityPending.size() + state->pending.size() >= 512)
        {
            auto* queue = !state->pending.empty()
                ? &state->pending
                : &state->highPriorityPending;
            state->requestStates.erase(queue->front().key);
            queue->pop_front();
        }
        auto request = State::Request{key, std::string(source), fontSize};
        if (highPriority)
        {
            state->highPriorityPending.push_back(std::move(request));
        }
        else
        {
            state->pending.push_back(std::move(request));
        }
        state->wake.notify_one();
        return std::nullopt;
    }

    void SvgNormalizer::SetCompletionCallback(std::function<void()> callback)
    {
        std::scoped_lock lock(state->mutex);
        state->completion = std::move(callback);
    }
}
