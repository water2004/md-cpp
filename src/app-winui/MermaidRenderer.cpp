#include "pch.h"
#include "MermaidRenderer.h"

namespace
{
    std::wstring AssetFolder()
    {
        std::wstring path(32768, L'\0');
        auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        path.resize(length);
        auto separator = path.find_last_of(L"\\/");
        if (separator == std::wstring::npos) return L"Assets\\mermaid";
        return path.substr(0, separator + 1) + L"Assets\\mermaid";
    }

    std::string Base64(std::string_view input)
    {
        constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string output;
        output.reserve((input.size() + 2) / 3 * 4);
        std::uint32_t value = 0;
        int bits = -6;
        for (unsigned char byte : input)
        {
            value = (value << 8) | byte;
            bits += 8;
            while (bits >= 0)
            {
                output.push_back(alphabet[(value >> bits) & 0x3f]);
                bits -= 6;
            }
        }
        if (bits > -6) output.push_back(alphabet[((value << 8) >> (bits + 8)) & 0x3f]);
        while (output.size() % 4) output.push_back('=');
        return output;
    }
}

namespace winrt::ElMd
{
    struct MermaidRenderer::State : std::enable_shared_from_this<State>
    {
        struct Request
        {
            std::string key;
            std::string source;
            bool dark = false;
            std::uint64_t id = 0;
            std::uint64_t generation = 0;
        };

        winrt::Microsoft::UI::Xaml::Controls::WebView2 webView{ nullptr };
        winrt::Microsoft::Web::WebView2::Core::CoreWebView2 core{ nullptr };
        winrt::event_token messageToken{};
        bool hasMessageHandler = false;
        bool initializing = false;
        bool ready = false;
        std::deque<Request> pending;
        std::optional<Request> current;
        std::unordered_set<std::string> queued;
        std::unordered_map<std::string, MermaidSvg> cache;
        std::deque<std::string> cacheOrder;
        std::size_t cacheBytes = 0;
        std::uint64_t nextId = 1;
        std::uint64_t generation = 0;
        std::function<void()> completion;

        ~State()
        {
            if (core && hasMessageHandler) core.WebMessageReceived(messageToken);
        }

        void EnsureInitialized()
        {
            if (ready || initializing || !webView) return;
            initializing = true;
            InitializeAsync();
        }

        winrt::fire_and_forget InitializeAsync()
        {
            auto lifetime = shared_from_this();
            try
            {
                co_await webView.EnsureCoreWebView2Async();
                core = webView.CoreWebView2();
                auto settings = core.Settings();
                settings.IsScriptEnabled(true);
                settings.IsWebMessageEnabled(true);
                settings.AreDevToolsEnabled(false);
                settings.AreDefaultContextMenusEnabled(false);
                settings.AreHostObjectsAllowed(false);
                settings.IsZoomControlEnabled(false);
                core.SetVirtualHostNameToFolderMapping(
                    L"elmd.local",
                    AssetFolder(),
                    winrt::Microsoft::Web::WebView2::Core::CoreWebView2HostResourceAccessKind::DenyCors);
                std::weak_ptr<State> weak = lifetime;
                messageToken = core.WebMessageReceived([weak](auto const&, auto const& args)
                {
                    if (auto self = weak.lock()) self->OnMessage(args);
                });
                hasMessageHandler = true;
                webView.Source(winrt::Windows::Foundation::Uri(L"https://elmd.local/renderer.html"));
            }
            catch (...)
            {
                initializing = false;
                FailCurrent("WebView2 could not be initialized");
            }
        }

        void OnMessage(winrt::Microsoft::Web::WebView2::Core::CoreWebView2WebMessageReceivedEventArgs const& args)
        {
            winrt::Windows::Data::Json::JsonObject message;
            if (!winrt::Windows::Data::Json::JsonObject::TryParse(args.WebMessageAsJson(), message)) return;
            if (message.HasKey(L"ready"))
            {
                ready = true;
                initializing = false;
                StartNext();
                return;
            }
            if (!current || !message.HasKey(L"id")) return;
            auto id = static_cast<std::uint64_t>(message.GetNamedNumber(L"id", 0));
            if (id != current->id) return;
            MermaidSvg result;
            result.svg = winrt::to_string(message.GetNamedString(L"svg", L""));
            result.error = winrt::to_string(message.GetNamedString(L"error", L""));
            result.width = static_cast<float>(message.GetNamedNumber(L"width", 0));
            result.height = static_cast<float>(message.GetNamedNumber(L"height", 0));
            if (!result && result.error.empty()) result.error = "Mermaid returned an invalid SVG";
            Complete(std::move(result));
        }

        void StartNext()
        {
            if (!ready || current || pending.empty()) return;
            current = std::move(pending.front());
            pending.pop_front();
            current->id = nextId++;
            auto script = L"void window.elmdRender(" + std::to_wstring(current->id) + L",\""
                + winrt::to_hstring(Base64(current->source)) + L"\"," + (current->dark ? L"true" : L"false") + L");";
            InvokeAsync(std::move(script));
        }

        winrt::fire_and_forget InvokeAsync(winrt::hstring script)
        {
            auto lifetime = shared_from_this();
            try
            {
                co_await webView.ExecuteScriptAsync(script);
            }
            catch (...)
            {
                FailCurrent("Mermaid script execution failed");
            }
        }

        static std::size_t ResultBytes(MermaidSvg const& result)
        {
            return sizeof(result) + result.svg.size() + result.error.size();
        }

        void Store(Request const& request, MermaidSvg result)
        {
            constexpr std::size_t budget = 16 * 1024 * 1024;
            auto bytes = ResultBytes(result);
            while (!cacheOrder.empty() && (cacheBytes + bytes > budget || cache.size() >= 64))
            {
                auto oldest = std::move(cacheOrder.front());
                cacheOrder.pop_front();
                auto found = cache.find(oldest);
                if (found == cache.end()) continue;
                cacheBytes -= ResultBytes(found->second);
                cache.erase(found);
            }
            if (bytes <= budget)
            {
                cacheBytes += bytes;
                cacheOrder.push_back(request.key);
                cache.emplace(request.key, std::move(result));
            }
        }

        void Complete(MermaidSvg result)
        {
            if (!current) return;
            auto request = std::move(*current);
            current.reset();
            queued.erase(request.key);
            if (request.generation == generation)
            {
                Store(request, std::move(result));
                if (completion) completion();
            }
            StartNext();
        }

        void FailCurrent(std::string error)
        {
            if (!current)
            {
                if (completion) completion();
                return;
            }
            MermaidSvg result;
            result.error = std::move(error);
            Complete(std::move(result));
        }
    };

    MermaidRenderer::MermaidRenderer() : state(std::make_shared<State>()) {}
    MermaidRenderer::~MermaidRenderer() = default;

    void MermaidRenderer::Initialize(winrt::Microsoft::UI::Xaml::Controls::WebView2 const& webView, std::function<void()> completion)
    {
        state->webView = webView;
        state->completion = std::move(completion);
    }

    void MermaidRenderer::SetCompletionCallback(std::function<void()> completion)
    {
        state->completion = std::move(completion);
    }

    std::optional<MermaidSvg> MermaidRenderer::GetOrQueue(std::string_view source, bool dark, bool allowQueue)
    {
        auto key = std::string(source) + '\x1f' + (dark ? "1" : "0");
        if (auto found = state->cache.find(key); found != state->cache.end()) return found->second;
        if (!allowQueue) return std::nullopt;
        if (state->queued.insert(key).second)
        {
            state->pending.push_back(State::Request{ key, std::string(source), dark, 0, state->generation });
        }
        state->EnsureInitialized();
        state->StartNext();
        return std::nullopt;
    }

    void MermaidRenderer::Clear()
    {
        ++state->generation;
        state->pending.clear();
        state->queued.clear();
        state->cache.clear();
        state->cacheOrder.clear();
        state->cacheBytes = 0;
    }
}
