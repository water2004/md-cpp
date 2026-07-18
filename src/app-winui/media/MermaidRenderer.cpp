#include "pch.h"
#include "media/MermaidRenderer.h"

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
                error = "Native Mermaid library could not be loaded";
                return;
            }
            create = reinterpret_cast<Create>(GetProcAddress(module, "folia_svg_normalizer_create"));
            destroy = reinterpret_cast<Destroy>(GetProcAddress(module, "folia_svg_normalizer_destroy"));
            render = reinterpret_cast<Render>(GetProcAddress(module, "folia_mermaid_render"));
            freeBuffer = reinterpret_cast<FreeBuffer>(GetProcAddress(module, "folia_svg_buffer_destroy"));
            if (!create || !destroy || !render || !freeBuffer)
            {
                error = "Native Mermaid library has an incompatible ABI";
                return;
            }
            context = create();
            if (!context) error = "Native Mermaid renderer could not initialize";
        }

        ~Runtime()
        {
            if (context && destroy) destroy(context);
            if (module) FreeLibrary(module);
        }

        winrt::Folia::MermaidSvg Process(std::string const& source, bool dark, std::uint64_t id)
        {
            winrt::Folia::MermaidSvg result;
            result.renderId = id;
            if (!error.empty())
            {
                result.error = error;
                return result;
            }
            std::uint8_t* output = nullptr;
            std::size_t outputLength = 0;
            auto status = render(
                context,
                reinterpret_cast<std::uint8_t const*>(source.data()),
                source.size(),
                dark,
                &output,
                &outputLength,
                &result.width,
                &result.height);
            std::string value;
            if (output && outputLength > 0) value.assign(reinterpret_cast<char const*>(output), outputLength);
            if (output) freeBuffer(output, outputLength);
            if (status == 0) result.svg = std::move(value);
            else result.error = value.empty() ? "Native Mermaid rendering failed" : std::move(value);
            return result;
        }

    private:
        using Create = void* (*)();
        using Destroy = void (*)(void*);
        using Render = std::int32_t (*)(void*, std::uint8_t const*, std::size_t, bool, std::uint8_t**, std::size_t*, float*, float*);
        using FreeBuffer = void (*)(std::uint8_t*, std::size_t);

        HMODULE module = nullptr;
        void* context = nullptr;
        Create create = nullptr;
        Destroy destroy = nullptr;
        Render render = nullptr;
        FreeBuffer freeBuffer = nullptr;
        std::string error;
    };
}

namespace winrt::Folia
{
    struct MermaidRenderer::State
    {
        struct Request
        {
            std::string key;
            std::string source;
            bool dark = false;
            std::uint64_t id = 0;
            std::uint64_t generation = 0;
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
            Runtime runtime;
            for (;;)
            {
                Request request;
                {
                    std::unique_lock lock(mutex);
                    wake.wait(lock, [this] { return stopping || !pending.empty(); });
                    if (stopping) return;
                    request = std::move(pending.front());
                    pending.pop_front();
                }
                auto result = runtime.Process(request.source, request.dark, request.id);
                std::function<void()> callback;
                {
                    std::scoped_lock lock(mutex);
                    queued.erase(request.key);
                    if (request.generation == generation)
                    {
                        Store(request.key, std::move(result));
                        if (!stopping) callback = completion;
                    }
                }
                if (callback) callback();
            }
        }

        static std::size_t ResultBytes(MermaidSvg const& result)
        {
            return sizeof(result) + result.svg.size() + result.error.size();
        }

        void Store(std::string const& key, MermaidSvg result)
        {
            constexpr std::size_t budget = 16 * 1024 * 1024;
            constexpr std::size_t limit = 64;
            auto bytes = key.size() + ResultBytes(result);
            while (!cacheOrder.empty() && (cacheBytes + bytes > budget || cache.size() >= limit))
            {
                auto oldest = std::move(cacheOrder.front());
                cacheOrder.pop_front();
                auto found = cache.find(oldest);
                if (found == cache.end()) continue;
                cacheBytes -= oldest.size() + ResultBytes(found->second);
                cache.erase(found);
            }
            if (bytes <= budget)
            {
                cacheBytes += bytes;
                cacheOrder.push_back(key);
                cache.emplace(key, std::move(result));
            }
        }

        std::mutex mutex;
        std::condition_variable wake;
        bool stopping = false;
        std::thread worker;
        std::deque<Request> pending;
        std::unordered_set<std::string> queued;
        std::unordered_map<std::string, MermaidSvg> cache;
        std::deque<std::string> cacheOrder;
        std::size_t cacheBytes = 0;
        std::uint64_t nextId = 1;
        std::uint64_t generation = 0;
        std::function<void()> completion;
    };

    MermaidRenderer::MermaidRenderer() : state(std::make_unique<State>()) {}
    MermaidRenderer::~MermaidRenderer() = default;

    void MermaidRenderer::SetCompletionCallback(std::function<void()> completion)
    {
        std::scoped_lock lock(state->mutex);
        state->completion = std::move(completion);
    }

    std::optional<MermaidSvg> MermaidRenderer::GetOrQueue(std::string_view source, bool dark, bool allowQueue)
    {
        if (source.empty()) return std::nullopt;
        auto key = std::string(source) + '\x1f' + (dark ? "1" : "0");
        std::scoped_lock lock(state->mutex);
        if (auto found = state->cache.find(key); found != state->cache.end()) return found->second;
        if (!allowQueue) return std::nullopt;
        if (state->queued.insert(key).second)
        {
            while (state->pending.size() >= 128)
            {
                state->queued.erase(state->pending.front().key);
                state->pending.pop_front();
            }
            state->pending.push_back(State::Request{ key, std::string(source), dark, state->nextId++, state->generation });
            state->wake.notify_one();
        }
        return std::nullopt;
    }

    void MermaidRenderer::Clear()
    {
        std::scoped_lock lock(state->mutex);
        ++state->generation;
        state->pending.clear();
        state->queued.clear();
        state->cache.clear();
        state->cacheOrder.clear();
        state->cacheBytes = 0;
    }
}
