#include "pch.h"
#include "editor/rendering/EditorSvgDocumentCache.h"

namespace
{
    ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> CreateSvgDocument(
        ID2D1DeviceContext5* context,
        std::string const& source,
        float width,
        float height)
    {
        if (!context || source.empty() || width <= 0.0f || height <= 0.0f) return {};
        auto allocation = GlobalAlloc(GMEM_MOVEABLE, source.size());
        if (!allocation) return {};
        auto bytes = static_cast<char*>(GlobalLock(allocation));
        if (!bytes)
        {
            GlobalFree(allocation);
            return {};
        }
        std::memcpy(bytes, source.data(), source.size());
        GlobalUnlock(allocation);
        ::Microsoft::WRL::ComPtr<IStream> stream;
        if (FAILED(CreateStreamOnHGlobal(allocation, TRUE, stream.GetAddressOf())) || !stream)
        {
            GlobalFree(allocation);
            return {};
        }
        ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> document;
        if (FAILED(context->CreateSvgDocument(
                stream.Get(),
                D2D1::SizeF(width, height),
                document.GetAddressOf()))) return {};
        return document;
    }
}

namespace winrt::Folia
{
    struct EditorSvgDocumentCache::State
    {
        struct Request
        {
            std::uint64_t renderId = 0;
            std::string source;
            float width = 0.0f;
            float height = 0.0f;
            std::uint64_t generation = 0;
            bool highPriority = false;
        };

        struct Entry
        {
            ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> document;
            std::size_t bytes = 0;
            std::list<std::uint64_t>::iterator order;
        };

        void ClearLocked()
        {
            ++generation;
            pending.clear();
            queued.clear();
            documents.clear();
            order.clear();
            bytes = 0;
        }

        void StoreLocked(
            std::uint64_t renderId,
            ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> const& document,
            std::size_t resourceCost)
        {
            if (!document || renderId == 0 || documents.contains(renderId)) return;
            constexpr std::size_t budget = 24 * 1024 * 1024;
            constexpr std::size_t limit = 512;
            while (!order.empty() && (bytes + resourceCost > budget || documents.size() >= limit))
            {
                auto oldest = order.front();
                order.pop_front();
                auto found = documents.find(oldest);
                if (found == documents.end()) continue;
                bytes -= found->second.bytes;
                documents.erase(found);
            }
            if (resourceCost > budget) return;
            bytes += resourceCost;
            order.push_back(renderId);
            documents.emplace(renderId, Entry{
                document,
                resourceCost,
                std::prev(order.end()),
            });
        }

        std::mutex mutex;
        ::Microsoft::WRL::ComPtr<ID2D1DeviceContext5> context;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher{nullptr};
        std::function<void()> invalidate;
        std::deque<Request> pending;
        std::unordered_set<std::uint64_t> queued;
        std::unordered_map<std::uint64_t, Entry> documents;
        std::list<std::uint64_t> order;
        std::size_t bytes = 0;
        std::uint64_t generation = 1;
        bool active = false;
        bool pumpScheduled = false;
    };

    EditorSvgDocumentCache::EditorSvgDocumentCache()
        : state(std::make_shared<State>())
    {
    }

    EditorSvgDocumentCache::~EditorSvgDocumentCache()
    {
        Detach();
    }

    void EditorSvgDocumentCache::Attach(
        winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher,
        std::function<void()> invalidate)
    {
        {
            std::scoped_lock lock(state->mutex);
            state->dispatcher = dispatcher;
            state->invalidate = std::move(invalidate);
            state->active = true;
        }
        Schedule(state);
    }

    void EditorSvgDocumentCache::Detach()
    {
        std::scoped_lock lock(state->mutex);
        state->active = false;
        state->pumpScheduled = false;
        state->dispatcher = nullptr;
        state->invalidate = {};
        state->context = nullptr;
        state->ClearLocked();
    }

    void EditorSvgDocumentCache::Configure(ID2D1DeviceContext5* context)
    {
        {
            std::scoped_lock lock(state->mutex);
            if (state->context.Get() == context) return;
            state->context = context;
            state->ClearLocked();
        }
        Schedule(state);
    }

    void EditorSvgDocumentCache::Clear()
    {
        std::scoped_lock lock(state->mutex);
        state->ClearLocked();
    }

    ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> EditorSvgDocumentCache::Find(
        std::uint64_t renderId)
    {
        std::scoped_lock lock(state->mutex);
        auto found = state->documents.find(renderId);
        if (found == state->documents.end()) return {};
        state->order.splice(state->order.end(), state->order, found->second.order);
        return found->second.document;
    }

    bool EditorSvgDocumentCache::Queue(
        std::uint64_t renderId,
        std::string const& source,
        float width,
        float height,
        bool highPriority)
    {
        if (renderId == 0 || source.empty() || width <= 0.0f || height <= 0.0f) return false;
        auto inserted = false;
        {
            std::scoped_lock lock(state->mutex);
            if (!state->context || state->documents.contains(renderId)) return false;
            if (state->queued.contains(renderId))
            {
                if (highPriority)
                {
                    auto found = std::find_if(
                        state->pending.begin(),
                        state->pending.end(),
                        [renderId](State::Request const& request) { return request.renderId == renderId; });
                    if (found != state->pending.end() && !found->highPriority)
                    {
                        auto request = std::move(*found);
                        state->pending.erase(found);
                        request.highPriority = true;
                        auto insertion = std::find_if(
                            state->pending.begin(),
                            state->pending.end(),
                            [](State::Request const& pending) { return !pending.highPriority; });
                        state->pending.insert(insertion, std::move(request));
                    }
                }
                return false;
            }
            constexpr std::size_t pendingLimit = 512;
            while (state->pending.size() >= pendingLimit)
            {
                state->queued.erase(state->pending.back().renderId);
                state->pending.pop_back();
            }
            auto request = State::Request{
                renderId,
                source,
                width,
                height,
                state->generation,
                highPriority,
            };
            state->queued.insert(renderId);
            if (highPriority)
            {
                auto insertion = std::find_if(
                    state->pending.begin(),
                    state->pending.end(),
                    [](State::Request const& pending) { return !pending.highPriority; });
                state->pending.insert(insertion, std::move(request));
            }
            else state->pending.push_back(std::move(request));
            inserted = true;
        }
        Schedule(state);
        return inserted;
    }

    ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> EditorSvgDocumentCache::FindOrCreate(
        ID2D1DeviceContext5* context,
        std::uint64_t renderId,
        std::string const& source,
        float width,
        float height)
    {
        if (auto document = Find(renderId)) return document;
        auto document = CreateSvgDocument(context, source, width, height);
        if (!document) return {};
        auto resourceCost = (std::max)(std::size_t{16 * 1024}, source.size() * 8);
        std::scoped_lock lock(state->mutex);
        state->queued.erase(renderId);
        state->StoreLocked(renderId, document, resourceCost);
        return document;
    }

    void EditorSvgDocumentCache::Schedule(std::shared_ptr<State> const& state)
    {
        winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher{nullptr};
        {
            std::scoped_lock lock(state->mutex);
            if (!state->active || state->pumpScheduled || state->pending.empty()
                || !state->context || !state->dispatcher) return;
            state->pumpScheduled = true;
            dispatcher = state->dispatcher;
        }
        std::weak_ptr<State> weak = state;
        if (!dispatcher.TryEnqueue(
                winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
                [weak]
                {
                    if (auto current = weak.lock()) ProcessOne(current);
                }))
        {
            std::scoped_lock lock(state->mutex);
            state->pumpScheduled = false;
        }
    }

    void EditorSvgDocumentCache::ProcessOne(std::shared_ptr<State> const& state)
    {
        State::Request request;
        ::Microsoft::WRL::ComPtr<ID2D1DeviceContext5> context;
        {
            std::scoped_lock lock(state->mutex);
            state->pumpScheduled = false;
            if (!state->active || state->pending.empty() || !state->context) return;
            request = std::move(state->pending.front());
            state->pending.pop_front();
            context = state->context;
        }

        auto document = CreateSvgDocument(
            context.Get(),
            request.source,
            request.width,
            request.height);
        auto resourceCost = (std::max)(std::size_t{16 * 1024}, request.source.size() * 8);
        std::function<void()> invalidate;
        {
            std::scoped_lock lock(state->mutex);
            state->queued.erase(request.renderId);
            if (request.generation == state->generation && document)
            {
                state->StoreLocked(request.renderId, document, resourceCost);
                if (state->active) invalidate = state->invalidate;
            }
        }
        if (invalidate) invalidate();
        Schedule(state);
    }
}
